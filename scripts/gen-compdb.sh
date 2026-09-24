#!/bin/sh
# gen-compdb.sh - generate compile_commands.json for clangd.
#
# Replaces the old static compile_flags.txt: this emits a real compilation
# database whose per-file flags exactly match lib/lib.mk and sw/sw.mk, so
# clangd resolves headers (and the optional readline path) the same way the
# build does. Regenerate with `make compdb`. The file is machine-specific
# (absolute paths) and git-ignored.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# Match the runtime path header selected by sw.mk; no library build required.
make -s -C "$root/sw" -f sw.mk build/install_paths.h

# Match sw.mk's readline auto-detection.
rl=""
if pkg-config --exists readline 2>/dev/null; then
    rl="-DHAVE_READLINE $(pkg-config --cflags readline)"
fi

out="$root/compile_commands.json"
first=1

emit() {   # directory file flags...
    dir=$1; file=$2; shift 2
    [ "$first" = 1 ] && first=0 || printf ',\n'
    printf '  {\n'
    printf '    "directory": "%s",\n' "$dir"
    printf '    "file": "%s",\n'      "$file"
    printf '    "command": "cc %s -c %s"\n' "$*" "$file"
    printf '  }'
}

{
    printf '[\n'
    emit "$root/lib" src/cgra.c        "-std=c11 -Iinclude"
    emit "$root/lib" src/kernels.c     "-std=c11 -Iinclude"
    emit "$root/lib" src/emu.c         "-std=c11 -Iinclude"
    emit "$root/lib" src/serve.c       "-std=c11 -Iinclude"
    emit "$root/lib" test/test_cgra.c  "-std=c11 -Iinclude"
    emit "$root/lib" test/test_kernels.c "-std=c11 -Iinclude"
    emit "$root/lib" test/test_faults.c "-std=c11 -Iinclude"
    emit "$root/lib" test/test_transport.c "-std=c11 -Iinclude"
    emit "$root/sw"  main.c            "-std=c11 -I. -I../lib/include -include build/install_paths.h $rl"
    emit "$root/sw"  dsl.c             "-std=c11 -I. -I../lib/include -include build/install_paths.h"
    emit "$root/sw"  compile.c         "-std=c11 -I. -I../lib/include"
    emit "$root/sw"  bench_ref.c       "-std=c11 -I. -I../lib/include"
    emit "$root/sw"  test/test_sw.c    "-std=c11 -I. -I../lib/include"
    printf '\n]\n'
} > "$out"

echo "wrote $out"
