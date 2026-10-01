#!/usr/bin/env bash
# Usage: tests/compat.sh path/to/mydiff [random_pairs]
set -u
MYDIFF=${1:-./build/mydiff}
RANDOM_PAIRS=${2:-40}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

passed=0
failed=0

note() { printf '%s\n' "$*" >&2; }
check() { if "$@"; then passed=$((passed + 1)); else failed=$((failed + 1)); note "FAIL: $*"; fi; }

# An explicit linear congruential generator keeps cases consistent across awk
# implementations.
LCG='function rnd(){ st=(st*1103515245+12345)%2147483648; return st/2147483648 }'

# A small vocabulary creates ambiguous matches that exercise tie breaking.
generate_file() {
  awk -v n="$1" -v st="$2" "$LCG"' BEGIN{
    split("alpha beta gamma delta epsilon zeta eta theta iota kappa", w, " ");
    for (i = 1; i <= n; i++) printf "%s %d\n", w[int(rnd()*10)+1], int(rnd()*6) }'
}

mutate_file() {
  awk -v st="$3" "$LCG"' {
    r = rnd();
    if (r < 0.10) next;
    if (r < 0.20) print "inserted " int(rnd()*100);
    if (r < 0.30) { print $0 " (edited)"; next }
    print }' "$1" > "$2"
  # Some generated files omit the final newline.
  if [ $(($3 % 7)) -eq 0 ]; then printf '%s' "$(cat "$2")" > "$2.tmp" && mv "$2.tmp" "$2"; fi
}

run_case() {
  local a=$1 b=$2 name=$3

  "$MYDIFF" "$a" "$b" > "$TMP/mine.normal"; local my_status=$?
  diff "$a" "$b" > "$TMP/gnu.normal"; local gnu_status=$?
  check test "$my_status" -eq "$gnu_status"

  if [ "$gnu_status" -ne 1 ]; then return; fi

  "$MYDIFF" -u "$a" "$b" > "$TMP/mine.unified"
  diff -u "$a" "$b" > "$TMP/gnu.unified"

  patch -s -o "$TMP/out1" "$a" "$TMP/mine.unified" 2>/dev/null
  check cmp -s "$TMP/out1" "$b"

  "$MYDIFF" patch "$a" "$TMP/gnu.unified" > "$TMP/out2" 2>/dev/null
  check cmp -s "$TMP/out2" "$b"

  "$MYDIFF" patch "$a" "$TMP/mine.unified" > "$TMP/out3" 2>/dev/null
  check cmp -s "$TMP/out3" "$b"

  "$MYDIFF" patch -R "$b" "$TMP/mine.unified" > "$TMP/out4" 2>/dev/null
  check cmp -s "$TMP/out4" "$a"

  local my_distance gnu_distance
  my_distance=$(grep -c '^[<>]' "$TMP/mine.normal")
  gnu_distance=$(grep -c '^[<>]' "$TMP/gnu.normal")
  check test "$my_distance" -le "$gnu_distance"
}

printf '' > "$TMP/empty"
printf 'x\n' > "$TMP/x"
printf 'x' > "$TMP/x_no_newline"
printf 'a\nb\nc\n' > "$TMP/abc"
printf 'c\nb\na\n' > "$TMP/cba"
printf 'a\nb\nc\nd\ne\n' > "$TMP/abcde"
printf 'b\nc\nd\n' > "$TMP/bcd"
printf 'a\n\n\nb\n' > "$TMP/blank3"
printf 'a\n\nb\n' > "$TMP/blank1"

run_case "$TMP/empty" "$TMP/empty" empty-empty
run_case "$TMP/empty" "$TMP/abc" empty-to-abc
run_case "$TMP/abc" "$TMP/empty" abc-to-empty
run_case "$TMP/abc" "$TMP/abc" identical
run_case "$TMP/abc" "$TMP/cba" reversed
run_case "$TMP/abcde" "$TMP/bcd" trim-both-ends
run_case "$TMP/bcd" "$TMP/abcde" grow-both-ends
run_case "$TMP/x" "$TMP/x_no_newline" missing-final-newline
run_case "$TMP/blank3" "$TMP/blank1" blank-lines

printf 'Hello World\nfoo   bar\n' > "$TMP/i1"
printf 'hello world\nfoo bar\n' > "$TMP/i2"
"$MYDIFF" -i "$TMP/i1" "$TMP/i2" > /dev/null;  check test $? -eq 1
"$MYDIFF" -iw "$TMP/i1" "$TMP/i2" > /dev/null; check test $? -eq 0
"$MYDIFF" -ib "$TMP/i1" "$TMP/i2" > /dev/null; check test $? -eq 0

printf 'one\ntwo\nthree\nfour\nfive\nsix\n' > "$TMP/f1"
printf 'one\ntwo\nTHREE\nfour\nfive\nsix\n' > "$TMP/f2"
"$MYDIFF" -u "$TMP/f1" "$TMP/f2" > "$TMP/f.unified"
{ printf 'zero\nzero\n'; cat "$TMP/f1"; } > "$TMP/f1_shifted"
{ printf 'zero\nzero\n'; cat "$TMP/f2"; } > "$TMP/f2_shifted"
"$MYDIFF" patch "$TMP/f1_shifted" "$TMP/f.unified" > "$TMP/f_out" 2>/dev/null
check cmp -s "$TMP/f_out" "$TMP/f2_shifted"

for i in $(seq 1 "$RANDOM_PAIRS"); do
  lines=$(( (i * 37) % 400 + 1 ))
  generate_file "$lines" "$i" > "$TMP/random_a"
  mutate_file "$TMP/random_a" "$TMP/random_b" "$i"
  run_case "$TMP/random_a" "$TMP/random_b" "random-$i"
done

note "compat.sh: $passed checks passed, $failed failed"
[ "$failed" -eq 0 ]
