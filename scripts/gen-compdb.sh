#!/bin/sh
# Compatibility entry point; make owns the compiler/profile/include settings.
# Arguments are forwarded to make, e.g. PROFILE=asan CC=clang.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec make -C "$root" compdb "$@"
