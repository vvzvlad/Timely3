#!/usr/bin/env sh
# One command to gate a change the way the cloud would:
#   1. host unit tests (pure logic: layout, calendar, suntimes, timefmt, vibes,
#      clock-font selection, and the persisted settings layout)
#   2. pebble build (also generates the per-platform headers)
#   3. reloc-check: no misaligned R_ARM_ABS32 relocation in the built ELFs (an
#      address-valued initializer in a packed struct makes the app unlaunchable)
#   4. strict-check: full compile of every source per platform with the cloud's
#      exact -Werror flag set, failing on any error OR warning
#
# Run inside the dev shell:  nix develop -c sh tools/test.sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

echo "== host unit tests =="
out=$(mktemp)
gcc -std=c11 -DUNIT_TEST -Isrc -Itests tests/test_*.c \
    src/layout.c src/calendar.c src/suntimes.c src/timefmt.c src/vibes.c src/math.c \
    -lm -o "$out"
"$out"
rm -f "$out"

echo "== pebble build =="
pebble build >/dev/null

echo "== reloc-check (packed-struct relocations) =="
sh tools/reloc-check.sh

echo "== strict-check (cloud flags) =="
sh tools/strict-check.sh

echo "ALL CHECKS PASSED"
