#!/bin/sh
# Compatible entry point. Knobs: DEV, SIZES, REPEAT, CGRA, STDLIB and OUT.
# Each size is executed once; OUT indexes every report in an isolated run folder.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$here/cgra-bench.py"
