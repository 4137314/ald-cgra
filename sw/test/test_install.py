#!/usr/bin/env python3
"""Install the working sources in isolation; exercise the result off-tree."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
checks = 0


def check(condition, message):
    global checks
    assert condition, message
    checks += 1
    print(f"ok install {checks} - {message}", flush=True)


with tempfile.TemporaryDirectory(prefix="cgra-install-") as temp:
    root = Path(temp)
    source = root / "source"
    source.mkdir()
    shutil.copy2(repo / "Makefile", source / "Makefile")
    for part in ("sw", "lib"):
        shutil.copytree(repo / part, source / part,
                        ignore=shutil.ignore_patterns("build", "__pycache__", "*.o"))
    env = dict(os.environ)
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "pkgdatadir",
                "CGRA_PATH", "CGRA_PORT", "CGRA_CONFIG", "CGRA_STDLIB", "CGRA_DEVICE", "CGRA_BAUD"):
        env.pop(key, None)
    outside = root / "outside"
    outside.mkdir()
    env.update(HOME=str(root / "home"), XDG_CONFIG_HOME=str(root / "settings"),
               CGRA_CONFIG=str(outside / "empty.cgra"))
    Path(env["CGRA_CONFIG"]).write_text("")

    def invoke(args, cwd=outside, expected=0, custom_env=None):
        result = subprocess.run(list(map(str, args)), cwd=cwd, env=custom_env or env,
                                text=True, capture_output=True, timeout=120)
        if result.returncode != expected:
            print(result.stdout)
            print(result.stderr)
            raise AssertionError(f"{args}: exit {result.returncode}, expected {expected}")
        return result

    first = root / "prefix 'one&"
    invoke(["make", "install", "PROFILE=asan", f"PREFIX={first}"], cwd=source)
    binary = first / "bin/cgra"
    check(binary.is_file(), "install uses release artifacts despite PROFILE=asan; prefix has spaces/quote/ampersand")
    config = outside / "includes.cgra"
    config.write_text('include "arithmetic.cgra"\ninclude "linalg.cgra"\n')
    result = invoke([binary, "-c", config, "run", "neg", "-d", "sim:", "--a", "-2 0 3"])
    check(result.stdout.strip() == "2 0 -3", "installed include lookup works before init, outside checkout")
    result = invoke([binary, "init"])
    user_stdlib = Path(env["XDG_CONFIG_HOME"]) / "cgra/stdlib"
    check(len(list(user_stdlib.glob("*.cgra"))) == 4 and "installed 4" in result.stdout,
          "init copies every bundled stdlib file from the installed prefix")
    original = {file.name: file.read_bytes() for file in (source / "sw/config/stdlib").glob("*.cgra")}
    check(all((user_stdlib / name).read_bytes() == data for name, data in original.items()),
          "installed stdlib is byte-identical to sources")

    # pkg-config and a real external C client must work without source includes.
    pc_env = dict(env, PKG_CONFIG_PATH=str(first / "lib/pkgconfig"))
    flags = invoke(["pkg-config", "--cflags", "--libs", "cgra"], custom_env=pc_env).stdout
    client = outside / "client.c"
    client.write_text('''#include <cgra.h>
int main(void) {
    const char *devices[] = {"sim:2x3", "sim:2x3:v2"};
    for (unsigned i = 0; i < 2; i++) {
        cgra_t *d = cgra_open(devices[i], 115200);
        if (!d) return 1;
        const int16_t A[] = {1,2,3,4,5,6}, x[] = {1,2,3}, h[] = {2,-1};
        int16_t out[4];
        if (cgra_matvec(d, A, 2, 3, x, out, 4) || out[0]!=14 || out[1]!=32) return 2;
        if (cgra_scan(d, CGRA_OP_ADD, x, 3, out, 4) || out[0]!=1 || out[1]!=3 || out[2]!=6) return 3;
        if (cgra_conv(d, h, 2, x, 3, out, 4) || out[0]!=2 || out[1]!=3 || out[2]!=4 || out[3]!=-3) return 4;
        cgra_close(d);
    }
    return 0;
}
''')
    executable = outside / "client"
    invoke(["cc", client, *shlex.split(flags), "-o", executable])
    invoke([executable], custom_env=dict(env, LD_LIBRARY_PATH=str(first / "lib")))
    check(True, "installed API links/runs external C matrix, scan and convolution kernels on v2/v3")

    # Rebuild the SAME object directory for a different PREFIX with DESTDIR.
    # Deploy the staging tree afterwards: the binary must not embed DESTDIR.
    second = root / "second 'two&"
    second_lib = second / "lib64 custom"
    second_include = second / "include custom"
    stage = root / "staging"
    invoke(["make", "install", f"PREFIX={second}", f"DESTDIR={stage}",
            f"libdir={second_lib}", f"includedir={second_include}"], cwd=source)
    header = (source / "sw/build/install_paths.h").read_text()
    check(str(second / "share/cgra/stdlib") in header and str(stage) not in header,
          "changing PREFIX refreshes runtime paths; DESTDIR stays out of the binary configuration")
    shutil.copytree(stage / second.relative_to("/"), second, symlinks=True)
    pc = second_lib / "pkgconfig/cgra.pc"
    check((second_lib / "libcgra.so").is_file() and
          (second_include / "cgra.h").is_file() and str(stage) not in pc.read_text(),
          "custom library/header directories install through DESTDIR without embedding it")
    pc_env = dict(env, PKG_CONFIG_PATH=str(pc.parent))
    flags = shlex.split(invoke(["pkg-config", "--cflags", "--libs", "cgra"], custom_env=pc_env).stdout)
    check(f"-I{second_include}" in flags and f"-L{second_lib}" in flags,
          "pkg-config describes the actual custom directories, including spaces/quote/ampersand")
    invoke(["cc", client, *flags, "-o", executable])
    invoke([executable], custom_env=dict(env, LD_LIBRARY_PATH=str(second_lib)))
    check(True, "external C client links/runs using only the custom installation's pkg-config flags")
    shutil.rmtree(first)
    shutil.rmtree(user_stdlib)
    # Remove source fallbacks too; execution is still outside the source tree.
    shutil.rmtree(source / "sw/config/stdlib")
    binary = second / "bin/cgra"
    result = invoke([binary, "-c", config, "run", "square", "-d", "sim:2x3", "--a", "2 -3 4"])
    check(result.stdout.strip() == "4 9 16", "rebuilt staged binary resolves only its new installed prefix")
    invoke([binary, "init", "--force"])
    check(len(list(user_stdlib.glob("*.cgra"))) == 4, "staged/deployed installation supports init without source fallback")

    # A write failure must not be reported as a successful stdlib installation.
    shutil.rmtree(user_stdlib)
    user_stdlib.write_text("not a directory")
    result = invoke([binary, "init", "--force"], expected=1)
    check("cannot" in result.stderr or "failed" in result.stderr,
          "init propagates stdlib installation failure")

print(f"# {checks} isolated installation checks passed")
