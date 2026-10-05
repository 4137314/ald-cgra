#!/usr/bin/env python3
"""Provenance/publication regressions; synthetic tool, no FPGA timing claim."""
import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location("artifacts", Path(__file__).resolve().parents[1] / "scr/artifacts.py")
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message
    print(f"ok artifacts {checks} - {message}")


with tempfile.TemporaryDirectory(prefix="cgra-artifacts-") as temporary:
    root = Path(temporary)
    hw = root / "hw"
    for name in ("rtl", "scr", "con"):
        (hw / name).mkdir(parents=True)
        (hw / name / "test.txt").write_text("fixture, not production RTL\n")
    real_hw = a.HW
    a.HW = hw
    tool = root / "fake-vivado"
    tool.write_text(f"#!{sys.executable}\n" + '''
from pathlib import Path
import json,os,sys
if '-version' in sys.argv:
    print('synthetic artifact test tool; NOT Vivado')
    sys.exit(0)
mode=os.environ.get('ARTIFACT_TEST_FAULT', '')
if mode == 'exit': sys.exit(7)
out=Path('build/vivado'); out.mkdir(parents=True)
(out/'design.bit').write_bytes(b'synthetic bitstream; not programmable')
if mode == 'no-report': sys.exit(0)
gates={k:'PASS' for k in ('synthesis','synchronizers','endpoint_coverage','control_timing','cdc','constraints','drc','setup','hold')}
if mode == 'missing-gate': del gates['cdc']
if mode == 'failed-gate': gates['setup']='FAIL'
(out/'gates.json').write_text(json.dumps(dict(schema_version=1,period_ns=10,gates=gates,metrics=dict(wns_ns=1,whs_ns=0.1))))
''')
    tool.chmod(0o755)
    args = SimpleNamespace(out=str(root / "runs"), mode="bit", vivado=str(tool), board="nexys_a7",
                           rows=4, cols=4, step_div=2, period=10, floorplan=False, timeout=20)
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        status = a.run_build(args)
    check(status == 0, "successful synthetic run publishes")
    manifest_path = next((root / "runs/runs").glob("*/manifest.json"))
    m = json.loads(manifest_path.read_text())
    bit, _ = a.validate_artifact(manifest_path, "nexys_a7", 4, 4, 2)
    check(bit.read_bytes().startswith(b"synthetic"), "valid manifest selects its exact hashed bitstream")
    pointer = next((root / "runs/current").glob("*.json"))
    original_pointer = pointer.read_bytes()
    check(json.loads(original_pointer)["manifest_sha256"] == a.digest(manifest_path), "current pointer hashes completed manifest")
    # Nix unpacked sources need not have Git installed or a .git directory.
    original_path = os.environ.get("PATH", "")
    os.environ["PATH"] = str(root / "no-tools")
    no_git_args = copy.copy(args); no_git_args.out = str(root / "no-git-runs")
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        status = a.run_build(no_git_args)
    os.environ["PATH"] = original_path
    no_git_manifest = json.loads(next((root / "no-git-runs/runs").glob("*/manifest.json")).read_text())
    check(status == 0 and no_git_manifest['commit'] is None and no_git_manifest['source_sha256'], "no Git executable retains source hashes and explicit unknown commit")
    for fault in ("exit", "no-report", "missing-gate", "failed-gate"):
        os.environ["ARTIFACT_TEST_FAULT"] = fault
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = a.run_build(args)
        check(status != 0, f"reject {fault}")
        check(pointer.read_bytes() == original_pointer, f"{fault} preserves last valid pointer")
    del os.environ["ARTIFACT_TEST_FAULT"]
    check(len(list((root / "runs/runs").glob("*/manifest.json"))) == 5, "failed and successful runs all retain isolated manifests")
    original_manifest = manifest_path.read_bytes()
    cases = [("status", "FAIL"), ("mode", "sta"), ("schema_version", 999), ("bitstream", "../outside.bit")]
    for key, value in cases:
        changed = copy.deepcopy(m); changed[key] = value
        a.write_json(manifest_path, changed)
        try:
            a.validate_artifact(manifest_path, "nexys_a7", 4, 4, 2)
            accepted = True
        except ValueError:
            accepted = False
        check(not accepted, f"reject invalid manifest {key}")
    manifest_path.write_bytes(original_manifest)
    for target in (("basys3",4,4,2), ("nexys_a7",2,8,2), ("nexys_a7",4,4,3)):
        try:
            a.validate_artifact(manifest_path, *target)
            accepted = True
        except ValueError:
            accepted = False
        check(not accepted, f"reject target mismatch {target}")
    for field, value in (("period_ns", 20), ("period_ns", float('nan'))):
        changed = copy.deepcopy(m); changed['parameters'][field] = value
        manifest_path.write_text(json.dumps(changed))
        try:
            a.validate_artifact(manifest_path, "nexys_a7", 4,4,2)
            accepted = True
        except ValueError:
            accepted = False
        check(not accepted, "reject constraint incompatible with physical clock")
    for field, value in (("wns_ns", -0.1), ("whs_ns", None), ("whs_ns", float('inf'))):
        changed = copy.deepcopy(m); changed['metrics'][field] = value
        manifest_path.write_text(json.dumps(changed))
        try:
            a.validate_artifact(manifest_path, "nexys_a7", 4,4,2)
            accepted = True
        except ValueError:
            accepted = False
        check(not accepted, f"reject invalid routed slack {field}={value}")
    manifest_path.write_bytes(original_manifest)
    for relative in (next(iter(m['source_sha256'])), m['bitstream']):
        file = manifest_path.parent / relative
        original = file.read_bytes(); file.write_bytes(original + b"tampered")
        try:
            a.validate_artifact(manifest_path, "nexys_a7",4,4,2)
            accepted = True
        except ValueError:
            accepted = False
        check(not accepted, f"reject changed file {relative}")
        file.write_bytes(original)
    changed = copy.deepcopy(m); changed['source_sha256'] = {'../../outside': '0'*64}
    a.write_json(manifest_path, changed)
    try:
        a.validate_artifact(manifest_path, "nexys_a7",4,4,2)
        accepted = True
    except ValueError:
        accepted = False
    check(not accepted, "reject source path outside run")
    # A rejected artifact must fail before launching either hardware tool.
    marker = root / "hardware-opened"
    probe = root / "hardware-probe"
    probe.write_text(f"#!{sys.executable}\nfrom pathlib import Path\nPath({str(marker)!r}).touch()\n")
    probe.chmod(0o755)
    for mode in ("prog", "prog-ofl"):
        r = subprocess.run([sys.executable,str(real_hw/'scr/artifacts.py'),mode,'--artifact',str(manifest_path),'--vivado',str(probe),'--ofl',str(probe)], capture_output=True)
        check(r.returncode != 0 and not marker.exists(), f"{mode} rejects before hardware side effects")
    manifest_path.write_bytes(original_manifest)
    r = subprocess.run([sys.executable,str(real_hw/'scr/artifacts.py'),'check-artifact','--artifact',str(manifest_path)],capture_output=True,text=True)
    check(r.returncode == 0 and Path(r.stdout.strip()) == bit and not marker.exists(), "check-artifact validates without hardware access")

print(f"# {checks} artifact checks passed (synthetic tool; no Vivado result)")
