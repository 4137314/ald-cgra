#!/usr/bin/env python3
"""Public CLI contract: reject unused input before side effects, intact shell lines."""
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
checks = 0


def check(ok, message):
    global checks
    assert ok, message
    checks += 1
    print(f"ok contract {checks} - {message}", flush=True)


with tempfile.TemporaryDirectory(prefix="cgra-contract-") as tmp:
    root = Path(tmp)
    config = root / "empty.cgra"
    config.write_text("")
    env = dict(os.environ, CGRA_CONFIG=str(config), CGRA_PATH="", XDG_CONFIG_HOME=tmp)
    for var in ("CGRA_PORT", "CGRA_DEVICE", "CGRA_BAUD"):
        env.pop(var, None)

    def run(args, data=None):
        return subprocess.run([binary, *args], input=data, capture_output=True, env=env, timeout=10)

    for args in (["ping", "-d", "sim", "unexpected"],
                 ["run", "add", "--a", "1", "--b", "2", "-d", "sim", "--cols", "4"],
                 ["show", "add", "extra"], ["bench", "add", "extra"],
                 ["scan", "--a", "1", "2"], ["run", "relu", "--a", "1", "--b", "2"],
                 ["conv", "--a", "1", "-m", "2", "--x", "3"],
                 ["matvec", "-m", "1", "--x", "2", "--b", "3"]):
        p = run(args)
        check(p.returncode != 0 and p.stderr and not p.stdout, f"reject unused input: {' '.join(args)}")

    # All commands reject irrelevant options before opening a device or file,
    # loading an invalid config, or entering a blocking interactive service.
    for command in ("ping", "reset", "dump", "run", "pipe", "conv", "scan", "bench", "benchall",
                    "show", "probe", "modes", "devices", "pipelines", "io", "config", "check",
                    "init", "emulate", "selftest", "shell", "block-dot"):
        p = run([command, "--cols", "4", "-c", str(root / "missing")])
        check(p.returncode != 0 and b"does not support --" in p.stderr and b"config error" not in p.stderr,
              f"{command}: validate option before configuration or external effects")
    for args in (["--help"], ["run", "--help"], ["--version"]):
        p = run(args)
        check(p.returncode == 0 and p.stdout and not p.stderr, f"stdout/success for {' '.join(args)}")
    p = run(["run", "relu", "-d", "sim", "--", "-3", "-1", "0", "5"])
    check(p.returncode == 0 and p.stdout.strip() == b"0 0 0 5", "negative positional operands remain valid")

    p = run(["block-dot", "-d", "sim:blocks", "--a", "1 -2 32767", "--b", "3 4 2", "--repeat", "3", "--json"])
    check(p.returncode == 0 and b'[-7, -7, -7]' in p.stdout and not p.stderr,
          "explicit simulator resident workload with modular signed output")
    p = run(["block-dot", "-d", "sim", "--a", "1", "--b", "2"])
    check(p.returncode != 0 and b'unavailable' in p.stderr and not p.stdout,
          "old device reports unsupported block capability")
    for args in (["--a", "1 2", "--b", "3"], ["--a", "1", "--b", "2", "--repeat", "4097"],
                 ["--a", " ".join(["1"]*1025), "--b", "2"]):
        p = run(["block-dot", "-d", "/nonexistent/cgra-test", *args])
        check(p.returncode != 0 and b'/nonexistent' not in p.stderr and not p.stdout,
              "block input limits fail before device open")

    marker = root / "must-not-exist"
    prefix = f'ping -d sim -o "{marker}" '
    bad_lines = [(prefix + '"unclosed', b"unclosed"),
                 (prefix + "x " * 128, b"argument limit"),
                 (prefix + '"' + "x" * 32769 + '"', b"token too long"),
                 (prefix + " " * 65537, b"line too long"),
                 (prefix + "\0", b"contains NUL")]
    for line, diagnostic in bad_lines:
        p = run(["shell"], (line + "\nquit\n").encode())
        check(p.returncode != 0 and diagnostic in p.stderr and not marker.exists(),
              f"reject whole shell line: {diagnostic.decode()}")
    p = run(["shell"], b'run add -d sim --a "1 2" --b \'3 4\'\nrun relu -d sim -- -2 5\nquit\n')
    check(p.returncode == 0 and b"4 6" in p.stdout and b"0 5" in p.stdout,
          "shell quotes and negative numbers round-trip")
    # Prove draining: the next complete command is still parsed normally.
    p = run(["shell"], (" " * 65537 + "\n--version\nquit\n").encode())
    check(p.returncode == 0 and b"cgra 0.1.0" in p.stdout and b"line too long" in p.stderr,
          "oversized line is drained before the next command")
    inherited = root / "inherited.cgra"
    inherited.write_text("[mode add]\nop=sub\n")
    p = run(["shell", "-c", str(inherited)], b'run add -d sim --a 5 --b 2\nquit\n')
    check(p.returncode == 0 and b"3\n" in p.stdout, "shell retains inherited configuration")
    p = run(["shell", "-c", str(inherited)],
            f'run add -c "{config}" -d sim --a 5 --b 2\nquit\n'.encode())
    check(p.returncode == 0 and b"7\n" in p.stdout, "per-line configuration overrides inherited configuration")

    bash = next((candidate for candidate in (shutil.which("bash"), "/bin/bash", "/usr/bin/bash")
                 if candidate and Path(candidate).exists() and
                 subprocess.run([candidate, "-c", "type compgen"], capture_output=True).returncode == 0), None)
    if bash:
        completion = Path(__file__).resolve().parents[1] / "completions/cgra.bash"
        script = '''source "$1"
shift
COMP_WORDS=("$@")
COMP_CWORD=$((${#COMP_WORDS[@]} - 1))
_cgra
printf '%s\\n' "${COMPREPLY[@]}"
'''
        def complete(words):
            return subprocess.check_output([bash, "-c", script, "completion-test", str(completion),
                                            "cgra", *words], env=env, text=True).splitlines()
        values = complete([""])
        check("shell" in values and "benchall" in values, "completion lists shell and benchall")
        values = complete(["-d", "sim", "run", "--"])
        check("--steps" in values and "--cols" not in values and "--force" not in values,
              "completion follows run options after a global device option")
        check("--cols" in complete(["matvec", "--"]), "completion exposes matvec dimensions")
        check("--version" in complete(["--"]), "completion exposes version")
    else:
        print("# SKIP completion: Bash with programmable completion unavailable")

    # Exercise the terminal branch too (readline when enabled), without
    # mistaking successful stream/fallback checks for terminal coverage.
    import pty
    master, slave = pty.openpty()
    p = subprocess.Popen([binary, "shell"], stdin=slave, stdout=slave, stderr=subprocess.PIPE, env=env)
    os.close(slave)
    os.write(master, (prefix + '"unclosed\nquit\n').encode())
    _, diagnostic = p.communicate(timeout=10)
    os.close(master)
    check(p.returncode != 0 and b"unclosed" in diagnostic and not marker.exists(),
          "terminal shell rejects an unclosed quote before dispatch")

    # Configurable service and clean signal shutdown through the CLI.
    import select
    for version, blocks in ((2, False), (3, False), (3, True)):
        server = subprocess.Popen([binary, "emulate", "--rows", "2", "--cols", "3",
                                   "--protocol", str(version), *(["--blocks"] if blocks else [])], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, env=env)
        try:
            check(bool(select.select([server.stdout], [], [], 5)[0]), "PTY service publishes readiness")
            line = server.stdout.readline().decode()
            check("serving on /dev/pts/" in line, "emulate accepts selected geometry/protocol")
            path = line.split()[-1]
            p = run(["ping", "-d", path])
            check(p.returncode == 0 and not p.stderr, "CLI reaches configurable server over real serial transport")
            if blocks:
                p = run(["block-dot", "-d", path, "--a", "1 -2 32767", "--b", "3 4 2", "--repeat", "3", "--json"])
                check(p.returncode == 0 and b'[-7, -7, -7]' in p.stdout and not p.stderr,
                      "resident CLI workload through real PTY and explicit blocks selection")
            server.terminate()
            _, err = server.communicate(timeout=5)
            check(server.returncode == 0 and not err, "SIGTERM shuts down service with success and no error")
        finally:
            if server.poll() is None: server.kill(); server.wait()
    for args in (["emulate", "--rows", "16", "--cols", "2"],
                 ["emulate", "--protocol", "4"], ["ping", "--rows", "2"]):
        p = run(args)
        check(p.returncode != 0 and p.stderr and not p.stdout, "invalid service geometry/options rejected")

print(f"# {checks} command/shell contract checks passed")
