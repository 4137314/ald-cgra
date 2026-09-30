#!/usr/bin/env python3
"""Update a build signature only when effective compiler configuration changes.

Make writes the input using $(file), so flags/path text never becomes shell
code here. The first two lines are CC and AR commands; subsequent text is the
component's complete configuration. Missing compilers remain in the signature:
the real compile then fails, instead of accepting stale objects.
"""
import json
import os
from pathlib import Path
import shlex
import shutil
import sys
import tempfile


def executable(command):
    words = shlex.split(command)
    identities = []
    # Account for the compiler and wrappers such as ccache, including changes
    # to an executable behind an unchanged CC spelling or symlink.
    for word in words:
        path = shutil.which(word)
        if path and not word.startswith("-"):
            real = Path(path).resolve()
            st = real.stat()
            identities.append([word, str(real), st.st_size, st.st_mtime_ns])
    return identities


def main():
    source, destination = map(Path, sys.argv[1:])
    config = source.read_text()
    cc, ar = config.splitlines()[:2]
    variables = ("CPATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "COMPILER_PATH",
                 "GCC_EXEC_PREFIX", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET",
                 "NIX_CFLAGS_COMPILE", "NIX_LDFLAGS")
    signature = json.dumps({"schema": 1, "config": config,
                            "tools": [executable(cc), executable(ar)],
                            "environment": {v: os.environ.get(v) for v in variables}},
                           sort_keys=True, indent=2).encode() + b"\n"
    if not destination.exists() or destination.read_bytes() != signature:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as tmp:
            tmp.write(signature)
            name = tmp.name
        os.replace(name, destination)
    source.unlink()


if __name__ == "__main__":
    main()
