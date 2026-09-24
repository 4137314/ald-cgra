#!/usr/bin/env python3
"""CLI contracts checked with a real JSON parser and a corrupt local PTY peer."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import threading

binary = str(Path(sys.argv[1]).resolve())
checks = 0


def check(condition, message):
    global checks
    assert condition, message
    checks += 1
    print(f"ok benchmark {checks} - {message}")


with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    empty = root / "empty.cgra"
    empty.write_text("")
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"),
               CGRA_CONFIG=str(empty), CGRA_PATH=str(Path("config/stdlib").resolve()))
    for name in ("CGRA_PORT", "CGRA_BAUD", "CGRA_DEVICE"):
        env.pop(name, None)

    def run(*args):
        return subprocess.run([binary, *args], env=env, capture_output=True, timeout=15)

    def report(result):
        # Reject NaN/Infinity too: Python's default parser otherwise accepts them.
        return json.loads(result.stdout.decode("utf-8"),
                          parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))

    for geometry in ("1x16", "16x1", "2x3", "3x2", "4x4"):
        for version in ("", ":v2"):
            result = run("benchall", "-d", f"sim:{geometry}{version}",
                         "--size", "17", "--repeat", "2", "--json")
            data = report(result)
            rows, cols = map(int, geometry.split("x"))
            check(data["device"]["rows"] == rows and data["device"]["cols"] == cols and
                  data["device"]["protocol_version"] == (2 if version else 3) and
                  data["device"]["backend"] == "emulator" and data["workload"] == "deterministic-edges-v1",
                  "benchmark metadata describes the actual backend and geometry")
            invalid_custom = geometry != "4x4"
            failures = [row for row in data["results"] if row["status"] == "failed"]
            check(bool(result.returncode) == invalid_custom and data["ok"] != invalid_custom and
                  data["failed"] == int(invalid_custom) and
                  (not failures or failures[0]["mode"] == "custom_example"),
                  f"stdlib scalar checks pass; fixed custom template is diagnosed on {geometry}{version}: {result.stderr!r}")
            passed = [row for row in data["results"] if row["status"] == "passed"]
            check(len(passed) == data["modes"] and all(row["verification"] == "scalar" for row in passed)
                  and data["skipped"] > 0 and
                  len(data["results"]) == data["modes"] + data["failed"] + data["skipped"],
                  "report accounts for every mode and distinguishes scalar checks from skips")

    for option, values in (("--size", ("0", "-1", "4097", "2junk")),
                           ("--repeat", ("0", "-1", "2junk", "999999999999999999999")),
                           ("--cols", ("0", "4097", "2junk"))):
        for value in values:
            result = run("bench", "-d", "sim:", option, value)
            check(result.returncode != 0 and result.stderr, f"reject {option}={value}")
    for value in ("32768junk", "65536", "-32769", "1-2", "999999999999999999999999999"):
        result = run("run", "addi", "-d", "sim:", "--a", value)
        check(result.returncode != 0, f"reject malformed/out-of-range vector {value}")
    for args in (("--dtype", "u8"), ("--io", "missing")):
        check(run("run", "addi", "-d", "sim:", "--a", "1", *args).returncode != 0,
              f"reject unknown formatting {args}")

    # Quotes, backslashes and UTF-8 in names must round-trip through JSON.
    name = 'méthode"\\😀'
    config = root / "bench.cgra"
    config.write_text(f"[mode {name}]\nop=add\n[mode broken]\nop=oops\n[mode unknown_pattern]\npattern=typo\n")
    destination = root / "result.json"
    destination.write_text("previous")
    result = run("-c", str(config), "benchall", "-d", "sim:", "--size", "8",
                 "--repeat", "1", "--json", "-o", str(destination))
    data = json.loads(destination.read_text())
    rows = {row["mode"]: row for row in data["results"]}
    check(result.returncode != 0 and not data["ok"] and data["failed"] == 2,
          "failed sweep returns nonzero and publishes its complete JSON artifact")
    check(rows[name]["status"] == "passed" and rows["broken"]["status"] == "failed"
          and rows["unknown_pattern"]["status"] == "failed" and rows["broken"]["reason"],
          "escaped names round-trip; invalid modes and patterns are never omitted")
    result = run("bench", "custom_example", "-d", "sim:", "--size", "8", "--repeat", "1", "--json")
    check(result.returncode != 0 and report(result)["results"][0]["status"] == "skipped",
          "a single unsupported benchmark does not claim success")
    config.write_bytes(b"[mode invalid\xff]\nop=add\n")
    result = run("-c", str(config), "bench", "-d", "sim:", "--size", "8", "--repeat", "1", "--json")
    check(result.returncode == 0 and report(result)["ok"], "valid single benchmark JSON")
    result = run("-c", str(config), "benchall", "-d", "sim:", "--size", "8", "--repeat", "1", "--json")
    check(result.returncode == 0 and report(result)["ok"], "invalid UTF-8 bytes cannot invalidate JSON")
    config.write_bytes(b"[mode add]\nop=mul\0bogus=1\n")
    check(run("-c", str(config), "check").returncode != 0, "embedded NUL in configuration rejected")
    vector = root / "vector"
    vector.write_bytes(b"1\0garbage")
    check(run("run", "addi", "-d", "sim:", "--a", f"@{vector}").returncode != 0,
          "embedded NUL in vector input rejected")
    old_paths = env["CGRA_PATH"]
    roots = [root / f"config_root_{i}" for i in range(8)]
    for directory in roots:
        directory.mkdir()
    (roots[-1] / "mode.cgra").write_text("[mode tail]\nb=const(2)\n")
    env["CGRA_PATH"] = ":".join(map(str, roots))
    result = run("run", "tail", "-d", "sim:", "--a", "0")
    check(result.returncode == 0 and result.stdout.strip() == b"2",
          "CGRA_PATH longer than the old fixed buffer is fully searched")
    env["CGRA_PATH"] = old_paths
    env["CGRA_BAUD"] = "115200junk"
    check(run("ping", "-d", "sim:").returncode != 0, "CGRA_BAUD requires a complete integer")
    del env["CGRA_BAUD"]
    long_config = root / ("x" * 190)
    long_config.write_text("[mode add]\nop=sub\n")
    check(run("-c", str(long_config), "check").returncode != 0,
          "oversized configuration path is rejected instead of truncated")
    config.write_text("".join(f'include "child{i}.cgra"\n' for i in range(33)))
    for i in range(33):
        (root / f"child{i}.cgra").write_text("[mode add]\nop=sub\n")
    check(run("-c", str(config), "check").returncode != 0,
          "configuration file capacity overflow is explicit")
    # The sweep wrapper must retain a failing report and propagate its status.
    broken_root = root / "broken_root"
    broken_root.mkdir()
    (broken_root / "bad.cgra").write_text("[mode invalid]\nop=oops\n")
    sweep_env = dict(env, CGRA=binary, DEV="sim:", SIZES="8 17", REPEAT="1",
                     STDLIB=str(broken_root), OUT=str(destination))
    result = subprocess.run(["sh", "scripts/cgra-bench.sh"], env=sweep_env,
                            capture_output=True, timeout=15)
    check(result.returncode != 0 and json.loads(destination.read_text())["failed"] == 1,
          "sweep script preserves failing JSON and propagates nonzero status")

    # A fake 1x1 device ACKs configuration and returns a checksum-valid but
    # numerically wrong EXEC reply. This must fail the scalar comparison.
    master, slave = os.openpty()
    errors = []

    def peer():
        def read_exact(length):
            data = b""
            while len(data) < length:
                if not select.select([master], [], [], 5)[0]:
                    raise TimeoutError("peer did not receive the expected request")
                data += os.read(master, length - len(data))
            return data
        try:
            assert read_exact(1) == b"\x01"
            os.write(master, bytes([0xCA, 3, 1, 1, 16]))
            assert read_exact(6)[0] == 2
            os.write(master, b"\x79")
            assert read_exact(10)[0] == 7
            os.write(master, bytes([0, 0, 0, 0x79]))
        except Exception as error:
            errors.append(error)

    thread = threading.Thread(target=peer, daemon=True)
    thread.start()
    try:
        result = run("bench", "add", "-d", os.ttyname(slave), "--size", "1", "--repeat", "1", "--json")
        thread.join(timeout=6)
        data = report(result)
        check(not errors and not thread.is_alive() and result.returncode != 0 and
              data["failed"] == 1 and "expected" in data["results"][0]["reason"],
              f"checksum-valid wrong hardware output fails scalar validation: {errors}")
    finally:
        os.close(master)
        os.close(slave)

print(f"# {checks} benchmark/CLI checks passed")
