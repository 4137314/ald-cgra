#!/usr/bin/env python3
"""Run one actual gate and retain command, status, log and source provenance."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def sources():
    paths = set(git("ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0"))
    result = {}
    for raw in sorted(paths):
        if not raw:
            continue
        name = os.fsdecode(raw)
        if name.startswith("doc/verification/"):
            continue  # append-only evidence is not an input to build/test
        path = ROOT / name
        if path.is_file():
            result[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", type=Path, help="new evidence directory")
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        ap.error("a command is required after --")
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = dict(schema_version=1, command=command, cwd=str(ROOT),
                    started=datetime.now(timezone.utc).isoformat(),
                    commit=git("rev-parse", "HEAD").decode().strip(),
                    dirty=bool(git("status", "--porcelain")), platform=platform.platform(),
                    sources_sha256=sources(), log="output.txt", timeout_seconds=args.timeout,
                    environment={k: os.environ.get(k) for k in
                        ("CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "LDLIBS", "PROFILE",
                         "ASAN_OPTIONS", "LSAN_OPTIONS", "UBSAN_OPTIONS")})
    start = time.monotonic()
    with (args.output / "output.txt").open("wb") as log:
        try:
            p = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                 start_new_session=True)
            try:
                p.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                # Recursive make and solver children must not outlive the gate.
                os.killpg(p.pid, signal.SIGTERM)
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                p.wait()
                raise
            manifest.update(exit_code=p.returncode, status="PASS" if p.returncode == 0 else "FAIL")
        except subprocess.TimeoutExpired:
            manifest.update(exit_code=None, status="TIMEOUT")
        except OSError as e:
            manifest.update(exit_code=None, status="ERROR", error=str(e))
    manifest["duration_seconds"] = time.monotonic() - start
    after = sources()
    manifest["sources_changed"] = sorted(k for k in set(after) | set(manifest["sources_sha256"])
                                         if after.get(k) != manifest["sources_sha256"].get(k))
    if manifest["sources_changed"]:
        manifest["status"] = "ERROR"
        manifest["error"] = "source inputs changed during the gate"
    manifest["log_sha256"] = hashlib.sha256((args.output / "output.txt").read_bytes()).hexdigest()
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{manifest['status']}: {args.output / 'manifest.json'}", flush=True)
    return 0 if manifest["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
