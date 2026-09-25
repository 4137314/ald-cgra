#!/usr/bin/env python3
"""Exercise host compilation databases in an isolated checkout with odd paths."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[1]
checks = 0


def check(condition, message):
    global checks
    assert condition, message
    checks += 1
    print(f"ok compdb {checks} - {message}", flush=True)


with tempfile.TemporaryDirectory(prefix="cgra-compdb-") as temporary:
    root = Path(temporary) / 'checkout with "quotes" and space'
    root.mkdir()
    shutil.copy2(REPO / "Makefile", root)
    for part in ("scripts", "sw", "lib"):
        shutil.copytree(REPO / part, root / part,
                        ignore=shutil.ignore_patterns("build", "__pycache__", "*.o"))
    env = dict(os.environ)
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "PROFILE", "pkgdatadir"):
        env.pop(key, None)
    env["CFLAGS"] = '''-DCGRA_COMPDB_TEST=7 -DCGRA_COMPDB_LABEL='"two words"' '''

    def run(args, cwd=root):
        result = subprocess.run(args, cwd=cwd, env=env, text=True,
                                capture_output=True, timeout=60)
        if result.returncode:
            print(result.stdout, result.stderr)
            raise AssertionError(f"{args}: exit {result.returncode}")
        return result

    for profile, optimization in (("release", "-O3"), ("asan", "-O1")):
        # The shell entry point is kept compatible and forwards make overrides.
        run(["sh", "scripts/gen-compdb.sh", f"PROFILE={profile}", "CC=cc"])
        entries = json.loads((root / "compile_commands.json").read_text())
        check(len(entries) == 13, "all host implementation and C unit-test sources are represented")
        check(len({(row["directory"], row["file"]) for row in entries}) == 13,
              "no duplicate translation units")
        for row in entries:
            directory = Path(row["directory"])
            check(directory.is_dir() and (directory / row["file"]).is_file(),
                  f"source path round-trips: {row['file']}")
            argv = row["arguments"]
            check(argv[0] == "cc" and optimization in argv and "-DCGRA_COMPDB_TEST=7" in argv
                  and '-DCGRA_COMPDB_LABEL="two words"' in argv,
                  f"compiler/profile/quoted flags preserved: {row['file']}")
            is_test = row["file"].startswith("test/")
            check(("-UNDEBUG" in argv) == is_test,
                  f"test assertions follow the build: {row['file']}")
            if directory.name == "sw":
                header = "build/install_paths.h" if profile == "release" else f"build/{profile}/install_paths.h"
                check("-include" in argv and header in argv and (directory / header).is_file(),
                      f"generated header matches the selected profile: {row['file']}")
            check(("-fsanitize=address,undefined" in argv) == (profile == "asan"),
                  f"sanitizer flags follow the profile: {row['file']}")
            # Execute the recorded command in syntax-only mode: includes and
            # quoting must actually work, including in the external test units.
            run([*argv, "-fsyntax-only"], cwd=directory)
        check(True, f"all {profile} commands pass the compiler in syntax-only mode")

    destination = root / "compile_commands.json"
    before = destination.read_bytes()
    malformed = root / "malformed.json"
    malformed.write_text("not JSON")
    result = subprocess.run(["python3", "scripts/gen-compdb.py", "merge", str(destination),
                             str(malformed)], cwd=root, env=env, capture_output=True)
    check(result.returncode != 0 and destination.read_bytes() == before,
          "a failed merge preserves the previous database")

print(f"# {checks} compilation database checks passed")
