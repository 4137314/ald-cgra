#!/bin/sh
# Regression tests of CLI validation, discovery and output preservation.
# Invoked from sw/; fixtures and outputs are confined to a fresh temp directory.
set -eu
CGRA=${1:-./build/cgra}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' 0
trap 'exit 1' HUP INT TERM
export XDG_CONFIG_HOME="$test_dir/config"
export CGRA_PATH=""
export CGRA_CONFIG="$test_dir/empty.cgra"
: > "$CGRA_CONFIG"
checks=0
ok() { checks=$((checks + 1)); printf 'ok CLI %s - %s\n' "$checks" "$1"; }
reject() {
    if "$CGRA" "$@" > "$test_dir/stdout" 2> "$test_dir/stderr"; then
        printf 'FAIL: unexpected success: %s\n' "$*" >&2
        exit 1
    fi
    test -s "$test_dir/stderr"
    ok "reject $*"
}

for steps in 256 -1 1junk 999999999999999999999999; do
    reject run add -d sim: --a '1 2' --b '3 4' --steps "$steps"
done
reject run addi -d sim: --a 1 --imm 4junk
test "$("$CGRA" run add -d sim: --a 1 --b 2 --steps 0)" = 0
ok 'explicit zero steps'

cat > "$test_dir/bad.cgra" <<'EOF'
[mode add]
op=mul
bogus=1
EOF
CGRA_CONFIG="$test_dir/bad.cgra" reject run add -d sim: --a 2 --b 3
CGRA_CONFIG="$test_dir/missing.cgra" reject check
mkdir -p "$test_dir/tree/conf.d"
cp "$test_dir/bad.cgra" "$test_dir/tree/conf.d/99_bad.cgra"
CGRA_PATH="$test_dir/tree" reject check

cat > "$test_dir/pipeline.cgra" <<'EOF'
[pipeline invalid]
stage=addi imm=1
stage=mul
EOF
reject -c "$test_dir/pipeline.cgra" pipe invalid -d sim: --a '1 2 3'
if "$CGRA" -c "$test_dir/pipeline.cgra" check > "$test_dir/stdout"; then
    printf 'FAIL: check accepted binary later stage\n' >&2
    exit 1
fi
ok 'check validates pipeline arity'

printf '1 2 3\n' > "$test_dir/data"
"$CGRA" run addi -d sim: --a "@$test_dir/data" --imm 1 -o "$test_dir/data"
test "$(cat "$test_dir/data")" = '2 3 4'
ok 'same input and output file keeps data until read'
ln "$test_dir/data" "$test_dir/alias"
"$CGRA" run addi -d sim: --a "@$test_dir/data" --imm 1 -o "$test_dir/alias"
test "$(cat "$test_dir/data")" = '3 4 5'
ok 'hard-linked input and output'
printf 'preserve\n' > "$test_dir/result"
reject run missing -d sim: --a 1 -o "$test_dir/result"
test "$(cat "$test_dir/result")" = preserve
ok 'failed command preserves existing output'
if [ -e /dev/full ]; then
    reject run addi -d sim: --a 1 -o /dev/full
fi
printf '# %s CLI regression checks passed\n' "$checks"
