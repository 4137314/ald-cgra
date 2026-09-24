"""Diagnostic counterexamples for the audit; run from the repository root.

Only the temporary directory is written. These print observations, rather than
forming a passing regression suite. No board or physical serial port is used.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path.cwd()
fixtures = Path(__file__).resolve().parent
binary = root / "sw/build/cgra"

with tempfile.TemporaryDirectory(prefix="cgra-audit-") as tmp:
    tmp = Path(tmp)
    empty = tmp / "empty.cgra"
    empty.write_text("")
    env = dict(os.environ, XDG_CONFIG_HOME=str(tmp), CGRA_CONFIG=str(empty), CGRA_PATH="")

    def run(label, args, extra_env=None):
        result = subprocess.run(
            [str(binary), *args], cwd=root,
            env=dict(env, **(extra_env or {})), capture_output=True, text=True,
            timeout=10,
        )
        print(json.dumps(dict(case=label, exit=result.returncode,
                              stdout=result.stdout.strip(), stderr=result.stderr.strip())))
        return result

    run("stdlib neg: expected -1 2 -32767 -32768",
        ["run", "neg", "-d", "sim:", "--a", "1 -2 32767 -32768"],
        dict(CGRA_PATH=str(root / "sw/config/stdlib")))
    config = ["-c", str(fixtures / "cases.cgra")]
    run("swapped inputs: expected 11 22 33 44 or explicit rejection",
        config + ["run", "swapped", "-d", "sim:", "--a", "1 2 3 4", "--b", "10 20 30 40"])
    run("different constants: expected 5 or explicit rejection",
        config + ["run", "twoconst", "-d", "sim:", "--a", "1 2 3 4"])
    run("step count overflow: expected rejection",
        ["run", "add", "-d", "sim:", "--a", "1 2", "--b", "3 4", "--steps", "256"])
    run("unknown PE directive: expected rejection",
        config + ["run", "typo", "-d", "sim:", "--a", "1", "--b", "10"])
    run("binary pipeline stage missing b: expected rejection",
        config + ["pipe", "binarylater", "-d", "sim:", "--a", "1 2 3"])
    run("check incorrectly accepts these modes", config + ["check"])
    run("broken automatic configuration: expected rejection",
        ["run", "add", "-d", "sim:", "--a", "2", "--b", "3"],
        dict(CGRA_CONFIG=str(fixtures / "broken.cgra")))
    run("full output device: expected nonzero exit",
        ["run", "add", "-d", "sim:", "--a", "1 2", "--b", "3 4", "-o", "/dev/full"])
    same = tmp / "same.txt"
    same.write_text("1 2 3\n")
    run("input and output alias: expected rejection or 2 3 4",
        ["run", "addi", "-d", "sim:", "--a", "@" + str(same), "--imm", "1", "-o", str(same)])
    print(json.dumps(dict(case="file after aliasing", contents=same.read_text())))
    bench = tmp / "badbench.cgra"
    bench.write_text('[mode invalid]\npattern=diagonal\nop=invalid\n'
                     '[mode quote"here]\npattern=diagonal\nop=add\n')
    result = run("failed benchmarks and JSON escaping",
                 ["-c", str(bench), "benchall", "-d", "sim:", "--size", "4", "--repeat", "1", "--json"])
    try:
        json.loads(result.stdout)
        print("benchmark JSON parses")
    except ValueError as exc:
        print("benchmark JSON does not parse:", exc)
