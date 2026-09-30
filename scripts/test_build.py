#!/usr/bin/env python3
"""A18: real incremental builds in an isolated checkout, including pipe jobserver."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[1]
checks = 0


def check(ok, message):
    global checks
    assert ok, message
    checks += 1
    print(f"ok build {checks} - {message}", flush=True)


with tempfile.TemporaryDirectory(prefix="cgra-build-") as temp:
    root = Path(temp)
    shutil.copy2(REPO / "Makefile", root)
    for part in ("lib", "sw", "scripts"):
        shutil.copytree(REPO / part, root / part,
                        ignore=shutil.ignore_patterns("build", "__pycache__"))
    env = dict(os.environ)
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "PROFILE", "pkgdatadir",
                "CPPFLAGS", "CFLAGS", "LDFLAGS", "LDLIBS"):
        env.pop(key, None)

    def run(*args, expected=0, extra=None):
        p = subprocess.run(["make", "-j4", *args], cwd=root,
                           env=dict(env, **(extra or {})), capture_output=True,
                           text=True, timeout=120)
        if (p.returncode == 0) != (expected == 0):
            raise AssertionError(p.stdout + p.stderr)
        check("jobserver unavailable" not in p.stderr, "recursive make retains the jobserver")
        return p

    def stamps():
        return {str(p.relative_to(root)): p.stat().st_mtime_ns
                for folder in (root / "lib/build", root / "sw/build")
                for p in folder.iterdir()
                if p.suffix in (".o", ".a", ".json") or p.name in ("cgra", "libcgra.so.0.1.0")}

    run()
    before = stamps()
    run()
    check(stamps() == before, "unchanged build preserves all object/archive/binary/signature mtimes")
    run("-C", "lib", "-f", "lib.mk", "CC=/nonexistent/cgra-review-cc", "static", expected=1)
    check((root / "lib/build/libcgra.a").stat().st_mtime_ns == before["lib/build/libcgra.a"],
          "missing new compiler fails without replacing the previous library")
    run()
    check(stamps()["lib/build/cgra.o"] > before["lib/build/cgra.o"], "restoring compiler rebuilds after a failed configuration")
    for flag, value in (("CPPFLAGS", "-DCGRA_CONFIG_TEST=1"),
                        ("CFLAGS", "-DCGRA_CONFIG_TEST=2"),
                        ("LDFLAGS", "-Wl,--as-needed")):
        before = stamps()
        run(extra={flag: value})
        check(stamps()["sw/build/cgra"] > before["sw/build/cgra"], f"{flag} invalidates the same build profile")
        before = stamps()
        run(extra={flag: value})
        check(stamps() == before, f"unchanged {flag} causes no rebuild")
    before = stamps()
    run("HAVE_READLINE=0")
    check(stamps()["sw/build/main.o"] > before["sw/build/main.o"], "optional dependency selection invalidates CLI objects")
    before = stamps()
    run("HAVE_READLINE=0", "pkgdatadir=/tmp/cgra build ' paths")
    check(stamps()["sw/build/main.o"] > before["sw/build/main.o"], "changed generated runtime path rebuilds CLI")
    before = stamps()
    run("HAVE_READLINE=0", "pkgdatadir=/tmp/cgra build ' paths")
    check(stamps() == before, "generated path and signatures are stable on repeat")
    # GNU Make >= 4.4 offers explicit pipe mode; older Make uses pipes by default.
    help_text = subprocess.check_output(["make", "--help"], text=True)
    pipe = ["--jobserver-style=pipe"] if "--jobserver-style" in help_text else []
    run(*pipe, "test-compdb")
    run(*pipe, "test-install")

print(f"# {checks} incremental build checks passed")
