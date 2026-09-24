#!/bin/sh
# cgra-bench.sh - stress-benchmark the whole .cgra standard library on a device.
#
# The end-to-end performance pipeline: once the FPGA is programmed with the
# synthesised RTL, run
#
#     make bench DEV=auto              # or: sw/scripts/cgra-bench.sh
#
# and it discovers the board, sanity-checks it, then sweeps EVERY benchable
# standard-library mode at increasing vector sizes, printing a structured table
# per size (throughput + link tx/rx/retries) and writing a JSON artifact. With
# DEV=sim (the default) it dry-runs on the in-process emulator, so the same
# pipeline works with no hardware.
#
# Knobs (environment or `make bench VAR=...`):
#   DEV     device: sim (default) | auto | a profile | /dev/ttyUSB1 | sim:flaky
#   SIZES   vector sizes to sweep     (default "256 1024 4096")
#   REPEAT  iterations per mode       (default 50)
#   CGRA    path to the cgra binary   (default ../build/cgra)
#   STDLIB  .cgra stdlib directory    (default ../config/stdlib)
#   OUT     JSON artifact path        (default ../build/benchmark.json)

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CGRA=${CGRA:-"$here/../build/cgra"}
DEV=${DEV:-sim}
SIZES=${SIZES:-"256 1024 4096"}
REPEAT=${REPEAT:-50}
STDLIB=${STDLIB:-"$here/../config/stdlib"}
OUT=${OUT:-"$here/../build/benchmark.json"}

[ -x "$CGRA" ] || { echo "error: cgra binary not found at $CGRA (run: make -C sw -f sw.mk)" >&2; exit 1; }

# Make the standard library discoverable to `cgra` without an install.
export CGRA_PATH="$STDLIB${CGRA_PATH:+:$CGRA_PATH}"

echo "======================================================================"
echo " cgra standard-library stress benchmark"
echo "   device : $DEV        sizes: $SIZES        repeat: $REPEAT"
echo "   stdlib : $STDLIB"
echo "======================================================================"

# On a real board: autodiscover + run the known-answer correctness suite first,
# so we only benchmark a device we trust.
if [ "$DEV" = auto ]; then
    echo "-- discovering + correctness-checking the FPGA --"
    "$CGRA" probe || { echo "no CGRA found; connect+program the board, then retry" >&2; exit 1; }
    echo
fi
"$CGRA" ping -d "$DEV" || { echo "device did not answer ID" >&2; exit 1; }
echo

failed=0
for sz in $SIZES; do
    echo "---- size $sz x $REPEAT iters ----"
    if ! "$CGRA" benchall -d "$DEV" --size "$sz" --repeat "$REPEAT"; then
        failed=1
    fi
    echo
done

# Machine-readable artifact at the largest size, for CI/plots.
big=$(printf '%s\n' $SIZES | sort -n | tail -1)
mkdir -p "$(dirname "$OUT")"
# A completed failing report is still useful. Stage it separately so an old
# artifact is not accidentally advertised if the command fails before reporting.
artifact_tmp=$(mktemp "${OUT}.XXXXXX")
trap 'rm -f "$artifact_tmp"' 0
trap 'exit 1' HUP INT TERM
if ! "$CGRA" benchall -d "$DEV" --size "$big" --repeat "$REPEAT" --json -o "$artifact_tmp"; then
    failed=1
fi
if [ -s "$artifact_tmp" ]; then
    mv "$artifact_tmp" "$OUT"
    echo "wrote $OUT  (size $big; exit status reflects benchmark failures)"
else
    echo "error: no benchmark report produced; previous artifact preserved" >&2
    failed=1
fi
exit "$failed"
