#!/usr/bin/env python3
"""Record review observations; this is not a passing regression test suite.

Run after `make sw`. All CLI destinations and installation probes are temporary.
The JSON captures observed behavior without asserting that defects stay present.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BIN = ROOT / "sw/build/cgra"


def run(argv, **kwargs):
    p = subprocess.run(argv, text=True, capture_output=True, timeout=120, **kwargs)
    return {"argv": [str(x) for x in argv], "exit": p.returncode,
            "stdout": p.stdout, "stderr": p.stderr}


result = {
    "head": run(["git", "rev-parse", "HEAD"], cwd=ROOT)["stdout"].strip(),
    "binary_sha256": hashlib.sha256(BIN.read_bytes()).hexdigest(),
    "cli": [],
}
with tempfile.TemporaryDirectory(prefix="cgra-review-") as temporary:
    work = Path(temporary)
    env = {k: v for k, v in os.environ.items() if not k.startswith("CGRA")}
    env.update(HOME=str(work), XDG_CONFIG_HOME=str(work / "config"))
    config = work / "empty.cgra"
    config.write_text("")
    for args in (["--help"], ["--version"],
                 ["ping", "-d", "sim", "unexpected"],
                 ["run", "add", "--a", "1", "--b", "2", "-d", "sim", "--cols", "4"]):
        result["cli"].append(run([str(BIN), "-c", str(config), *args], env=env))
    for args in (["ping", "-d", "sim"], ["show", "add", "-d", "sim"]):
        destination = work / f"{args[0]}.txt"
        destination.write_text("existing content to preserve or replace with command output\n")
        before = destination.read_text()
        observation = run([str(BIN), "-c", str(config), *args, "-o", str(destination)], env=env)
        observation.update(before=before, after=destination.read_text())
        result["cli"].append(observation)

    # Reproduce JSON escaping independently of a full checkout/build.
    checkout = work / 'checkout-with-"quote'
    (checkout / "scripts").mkdir(parents=True)
    (checkout / "sw").mkdir()
    shutil.copy2(ROOT / "scripts/gen-compdb.sh", checkout / "scripts")
    shutil.copy2(ROOT / "sw/sw.mk", checkout / "sw")
    observation = run(["sh", "scripts/gen-compdb.sh"], cwd=checkout)
    database = checkout / "compile_commands.json"
    if database.exists():
        try:
            json.loads(database.read_text())
            observation["json_valid"] = True
        except json.JSONDecodeError as error:
            observation.update(json_valid=False, parse_error=str(error))
    result["compdb"] = observation

    # Build/install a source copy, preserving the user's build and all paths.
    checkout = work / "install-source"
    checkout.mkdir()
    shutil.copy2(ROOT / "Makefile", checkout)
    for directory in ("lib", "sw"):
        shutil.copytree(ROOT / directory, checkout / directory,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))
    prefix = work / "installed"
    libdir = prefix / "lib64"
    includedir = prefix / "include-custom"
    observation = run(["make", "install", f"PREFIX={prefix}",
                       f"libdir={libdir}", f"includedir={includedir}"], cwd=checkout)
    (HERE / "install-probe.txt").write_text(observation.pop("stdout") + observation.pop("stderr"))
    observation["library_installed"] = (libdir / "libcgra.so").exists()
    observation["header_installed"] = (includedir / "cgra.h").exists()
    pc = libdir / "pkgconfig/cgra.pc"
    if pc.exists():
        observation["pkg_config_file"] = pc.read_text()
        pkg_env = dict(os.environ, PKG_CONFIG_PATH=str(pc.parent))
        observation["pkg_config"] = run(["pkg-config", "--cflags", "--libs", "cgra"], env=pkg_env)
    result["install"] = observation

result["tools"] = {name: run([name, "--version"]) for name in ("codex", "claude")}
(HERE / "probes.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({"cli_cases": len(result["cli"]),
                  "compdb_json_valid": result["compdb"].get("json_valid"),
                  "install_exit": result["install"]["exit"]}, indent=2))
