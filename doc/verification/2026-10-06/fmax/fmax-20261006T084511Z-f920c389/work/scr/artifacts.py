#!/usr/bin/env python3
"""Isolated Vivado runs and explicit, validated programming artifacts.

The manifest is provenance/integrity metadata, not a cryptographic signature.
It is meaningful for trusted locally produced artifacts. No hardware access
occurs for build modes or check-artifact.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
import uuid

HW = Path(__file__).resolve().parents[1]
PARTS = {"nexys_a7": "xc7a100tcsg324-1", "basys3": "xc7a35tcpg236-1"}
SCRIPTS = {"synth": "synth.tcl", "sta": "timing.tcl", "bit": "build.tcl", "fmax": "fmax.tcl"}
BASE_GATES = ("synthesis", "synchronizers", "endpoint_coverage", "control_timing", "cdc", "constraints", "drc")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, value):
    temporary = path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def within(root, relative):
    path = (root / relative).resolve()
    if Path(relative).is_absolute() or not path.is_relative_to(root.resolve()):
        raise ValueError(f"artifact path escapes run: {relative}")
    return path


def verify_files(root, files):
    if not files:
        raise ValueError("missing file hashes")
    for relative, expected in files.items():
        path = within(root, relative)
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f"missing or changed artifact: {relative}")


def validate_artifact(manifest_path, board, rows, cols, step_div):
    """Validate before either programming tool can open a device."""
    manifest_path = Path(manifest_path).resolve(strict=True)
    m = json.loads(manifest_path.read_text())
    if m.get("schema_version") != 1 or m.get("status") != "PASS" or m.get("mode") != "bit":
        raise ValueError("programming requires a successful bit run manifest")
    expected = {"board": board, "part": PARTS[board], "rows": rows, "cols": cols, "step_div": step_div}
    if any(m.get("parameters", {}).get(k) != v for k, v in expected.items()):
        raise ValueError("artifact target/geometry/STEP_DIV mismatch")
    gates = m.get("gates", {})
    for name in (*BASE_GATES, "setup", "hold"):
        if gates.get(name) != "PASS":
            raise ValueError(f"artifact gate not passed: {name}")
    period = m["parameters"].get("period_ns", 0)
    # Both supported boards have a 100 MHz oscillator; relaxing the constraint
    # to >10 ns does not make that oscillator run more slowly.
    if not isinstance(period, (int, float)) or not math.isfinite(period) or not 0 < period <= 10:
        raise ValueError("artifact does not meet the physical 100 MHz board clock constraint")
    for name in ("wns_ns", "whs_ns"):
        slack = m.get("metrics", {}).get(name)
        if not isinstance(slack, (int, float)) or not math.isfinite(slack) or slack < 0:
            raise ValueError(f"invalid routed slack: {name}")
    root = manifest_path.parent
    verify_files(root, m.get("source_sha256", {}))
    verify_files(root, m.get("artifact_sha256", {}))
    bit = m.get("bitstream")
    if not bit or bit not in m["artifact_sha256"] or not bit.endswith(".bit"):
        raise ValueError("bitstream is not part of this checked run")
    return within(root, bit), m


def read_gates(work, mode):
    paths = list((work / "build/vivado").rglob("gates.json"))
    if len(paths) != 1:
        raise ValueError("Vivado did not produce exactly one completed gate report")
    report = json.loads(paths[0].read_text())
    if report.get("schema_version") != 1:
        raise ValueError("unknown Vivado gate report schema")
    required = BASE_GATES + (() if mode == "synth" else ("setup", "hold"))
    if any(report.get("gates", {}).get(k) != "PASS" for k in required):
        raise ValueError("Vivado gate report contains an unsuccessful gate")
    if mode != "synth":
        for key in ("wns_ns", "whs_ns"):
            slack = report.get("metrics", {}).get(key)
            if not isinstance(slack, (float, int)) or not math.isfinite(slack) or slack < 0:
                raise ValueError(f"invalid routed metric {key}")
    return report


def resources(work):
    """Keep synth and implemented counts separate, as reported by Vivado."""
    result = {}
    for path in (work / "build/vivado").rglob("utilization*.rpt"):
        report = path.read_text()
        counts = {}
        for label, pattern in (("lut", r"(?:Slice|CLB) LUTs\*?"),
                               ("registers", r"(?:Slice|CLB) Registers"),
                               ("dsp", "DSPs"), ("bram_tiles", "Block RAM Tile")):
            match = re.search(r"\|\s*" + pattern + r"\s*\|\s*([0-9.]+)\s*\|", report)
            if match:
                counts[label] = float(match.group(1))
        result[str(path.relative_to(work))] = counts
    return result


def publish(run, manifest, base):
    """Atomic last-valid pointer; failed runs never touch this directory."""
    if manifest["status"] != "PASS":
        return
    p = manifest["parameters"]
    key = f'{p["board"]}-{p["rows"]}x{p["cols"]}-div{p["step_div"]}-{manifest["mode"]}'
    current = base / "current"
    current.mkdir(parents=True, exist_ok=True)
    write_json(current / (key + ".json"), {
        "schema_version": 1, "manifest": os.path.relpath(run / "manifest.json", current),
        "manifest_sha256": digest(run / "manifest.json")})


def run_build(args):
    base = Path(args.out).resolve()
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    run = base / "runs" / (args.mode + "-" + run_id)
    work = run / "work"
    work.mkdir(parents=True)
    for name in ("rtl", "con", "scr"):
        shutil.copytree(HW / name, work / name, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    source_hashes = {str(p.relative_to(run)): digest(p) for p in work.rglob("*") if p.is_file()}
    tool = shutil.which(args.vivado)
    m = {"schema_version": 1, "run_id": run_id, "mode": args.mode, "status": "ERROR",
         "started_utc": datetime.now(timezone.utc).isoformat(), "source_sha256": source_hashes,
         "parameters": {"board": args.board, "part": PARTS[args.board], "top": "cgra_top",
                        "rows": args.rows, "cols": args.cols, "step_div": args.step_div,
                        "period_ns": args.period, "requested_period_ns": args.period,
                        "floorplan": args.floorplan,
                        "clock_hz": 100000000, "baud": 115200, "timeout_cycles": 100000000},
         "strategy": "synth default; opt/place/phys_opt/route default (synth-only uses hierarchy none)",
         "hardware_test": "NOT_RUN", "gates": {}, "metrics": {}, "artifact_sha256": {},
         "tool": {"requested": args.vivado, "resolved": tool}}
    try:
        m["commit"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=HW, stderr=subprocess.DEVNULL, text=True).strip()
        m["git_status"] = subprocess.check_output(["git", "status", "--porcelain"], cwd=HW, text=True)
        m["dirty"] = bool(m["git_status"])
    except (subprocess.CalledProcessError, OSError):
        m.update(commit=None, dirty=None, git_status=None)
    argv = [args.board, str(args.period), str(args.step_div), str(args.rows), str(args.cols)]
    if args.mode == "fmax":
        argv = [args.board, str(args.fmax_lo), str(args.fmax_hi), str(args.fmax_iters),
                str(args.step_div), str(args.rows), str(args.cols)]
        m["search"] = {"lo_ns": args.fmax_lo, "hi_ns": args.fmax_hi, "iterations": args.fmax_iters}
    command = [tool or args.vivado, "-mode", "batch", "-nolog", "-nojournal", "-source", "scr/" + SCRIPTS[args.mode], "-tclargs", *argv]
    m.update(command=command, cwd=str(work))
    write_json(run / "manifest.json", m)
    started = time.monotonic()
    try:
        if not tool:
            raise FileNotFoundError(f"Vivado executable not found: {args.vivado}")
        version = subprocess.run([tool, "-version"], capture_output=True, text=True, timeout=60)
        m["tool"]["version"] = version.stdout + version.stderr
        if version.returncode:
            raise ValueError("Vivado version query failed")
        env = dict(os.environ, FLOORPLAN=str(int(args.floorplan)))
        with (run / "vivado.log").open("wb") as log:
            proc = subprocess.Popen(command, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                m["exit_code"] = proc.wait(timeout=args.timeout)
            except (subprocess.TimeoutExpired, KeyboardInterrupt):
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()
                raise
        m["status"] = "FAIL"
        if m["exit_code"] != 0:
            raise ValueError(f"Vivado exited with status {m['exit_code']}")
        report = read_gates(work, args.mode)
        m.update(gates=report["gates"], metrics=report["metrics"])
        m["metrics"]["resources_by_report"] = resources(work)
        m["parameters"]["period_ns"] = report["period_ns"]
        m["status"] = "PASS"
    except subprocess.TimeoutExpired:
        m.update(status="TIMEOUT", error=f"tool exceeded timeout ({args.timeout}s for implementation)")
    except (OSError, ValueError, KeyboardInterrupt) as error:
        m["error"] = str(error) or type(error).__name__
    finally:
        m["duration_seconds"] = time.monotonic() - started
        m["finished_utc"] = datetime.now(timezone.utc).isoformat()
        m["artifact_sha256"] = {str(p.relative_to(run)): digest(p) for p in run.rglob("*")
                                 if p.is_file() and (p.suffix in (".rpt", ".tsv", ".dcp", ".bit") or p.name in ("vivado.log", "gates.json"))}
        bits = [p for p in m["artifact_sha256"] if p.endswith(".bit")]
        m["bitstream"] = bits[0] if len(bits) == 1 else None
        try:
            verify_files(run, source_hashes)
            if m["status"] == "PASS" and args.mode == "bit":
                write_json(run / "manifest.json", m)
                validate_artifact(run / "manifest.json", args.board, args.rows, args.cols, args.step_div)
        except (OSError, ValueError) as error:
            m.update(status="ERROR", error=str(error))
        write_json(run / "manifest.json", m)
    publish(run, m, base)
    print(f'{m["status"]}: {run / "manifest.json"}', flush=True)
    if "error" in m:
        print(m["error"], file=sys.stderr)
    return 0 if m["status"] == "PASS" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=[*SCRIPTS, "check-artifact", "prog", "prog-ofl"])
    parser.add_argument("--board", choices=PARTS, default="nexys_a7")
    parser.add_argument("--rows", type=int, default=4)
    parser.add_argument("--cols", type=int, default=4)
    parser.add_argument("--step-div", type=int, default=2)
    parser.add_argument("--period", type=float, default=10)
    parser.add_argument("--floorplan", action="store_true")
    parser.add_argument("--vivado", default="vivado")
    parser.add_argument("--ofl", default="openFPGALoader")
    parser.add_argument("--out", default=str(HW / "build/vivado"))
    parser.add_argument("--artifact", help="explicit path to a successful bit run manifest.json")
    parser.add_argument("--timeout", type=float, default=3600)
    parser.add_argument("--fmax-lo", type=float, default=5)
    parser.add_argument("--fmax-hi", type=float, default=10)
    parser.add_argument("--fmax-iters", type=int, default=6)
    args = parser.parse_args()
    if not (1 <= args.rows <= 16 and 1 <= args.cols <= 16 and args.rows * args.cols <= 16 and args.step_div >= 1):
        parser.error("invalid geometry or STEP_DIV")
    if any(not math.isfinite(x) or x <= 0 for x in (args.period, args.timeout, args.fmax_lo, args.fmax_hi)):
        parser.error("periods and timeout must be finite and positive")
    if args.fmax_lo >= args.fmax_hi or args.fmax_iters < 1:
        parser.error("invalid Fmax search interval/iterations")
    if args.mode in SCRIPTS:
        return run_build(args)
    if not args.artifact:
        parser.error("--artifact MANIFEST is required; a loose .bit file is insufficient")
    try:
        bit, m = validate_artifact(args.artifact, args.board, args.rows, args.cols, args.step_div)
        if args.mode == "check-artifact":
            print(bit)
            return 0
        if args.mode == "prog":
            command = [args.vivado, "-mode", "batch", "-nolog", "-nojournal", "-source", str(HW / "scr/program.tcl"), "-tclargs", str(bit), m["parameters"]["part"]]
        else:
            command = [args.ofl, "-b", args.board, str(bit)]
        return subprocess.call(command)
    except (OSError, ValueError, KeyError) as error:
        print(f"artifact rejected: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
