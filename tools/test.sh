#!/usr/bin/env sh
# One command to gate a change the way the cloud would:
#   1. host unit tests (pure logic: layout, calendar, suntimes, timefmt, vibes,
#      clock-font selection)
#   2. pebble build (also generates the per-platform headers)
#   3. reloc-check: no misaligned R_ARM_ABS32 in the built ELFs (an address-valued
#      initializer in a packed struct makes the app unlaunchable)
#   4. strict-check: full compile of every source per platform with the cloud's
#      exact -Werror flag set, failing on any error OR warning
#
# Run inside the dev shell:  nix develop -c sh tools/test.sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

echo "== host unit tests =="
# Single host-test flag set, identical to Makefile / justfile / CI. Test sources
# are one glob (tests/test_*.c); only host-compilable pure modules are linked.
out=$(mktemp)
gcc -std=c11 -Wall -Wextra -Isrc -Itests tests/test_*.c \
    src/layout.c src/calendar.c src/suntimes.c src/timefmt.c src/vibes.c src/math.c \
    src/complications.c src/settings.c src/locale.c \
    -lm -o "$out"
"$out"
rm -f "$out"

echo "== version single-source (package.json <-> src/splash.c) =="
sh tools/check-version.sh

echo "== appkeys bijection (AK_* <-> package.json messageKeys) =="
sh tools/check-appkeys.sh

echo "== JS round-trip tests =="
# Wire-contract round-trip coverage (configpage -> app.js) via the built-in
# node:test runner: T4 codec/coverage + T2 settings-loss regressions driving the
# real webviewclosed handler. No npm deps.
node --test tests/js/

echo "== pebble build =="
pebble build >/dev/null

echo "== reloc-check (packed-struct relocations) =="
sh tools/reloc-check.sh

echo "== strict-check (cloud flags) =="
sh tools/strict-check.sh

echo "ALL CHECKS PASSED"
