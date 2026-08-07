#!/usr/bin/env sh
# Compile every C source with the SAME flags the repebble cloud build uses, so
# build errors AND warnings are caught locally instead of in the cloud. The local
# `pebble build` relaxes these via pbl_suppress_newer_gcc_warnings(); this does not.
#
# Fails on any compiler error (-Werror class, e.g. discarded-qualifiers) OR any
# remaining warning (full codegen compile, so format-truncation etc. surface).
#
# Requires a prior `pebble build` (reuses the generated headers under build/).
# Run inside the dev shell:  nix develop -c sh tools/strict-check.sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

CC="arm-none-eabi-gcc"

# Locate the SDK's per-platform headers instead of hardcoding one path. `pebble
# sdk install latest` puts them under ~/.pebble-sdk (a symlink to
# ~/.local/share/pebble-sdk); PEBBLE_SDK overrides for a non-standard install.
# Fail with a clear, actionable message if none is found.
SDK=""
for cand in \
  "${PEBBLE_SDK:-}" \
  "${HOME}/.pebble-sdk/SDKs/current/sdk-core/pebble" \
  "${HOME}/.local/share/pebble-sdk/SDKs/current/sdk-core/pebble"
do
  if [ -n "$cand" ] && [ -d "$cand" ]; then SDK="$cand"; break; fi
done
if [ -z "$SDK" ]; then
  echo "strict-check: Pebble SDK headers not found." >&2
  echo "  Looked for SDKs/current/sdk-core/pebble under ~/.pebble-sdk and ~/.local/share/pebble-sdk." >&2
  echo "  Install with:  pebble sdk install latest" >&2
  echo "  or point PEBBLE_SDK at an existing .../sdk-core/pebble directory." >&2
  exit 2
fi

# Exact cloud flag set (see a failing cloud log): -Werror with the same
# -Wno-error= relaxations. Full compile to /dev/null so codegen warnings appear.
COMMON="-std=c99 -mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections \
-fcommon -g -fPIE -Os -D_TIME_H_ -Dtime_t=long -Wall -Wextra -Werror \
-Wno-unused-parameter -Wno-error=unused-function -Wno-error=unused-variable \
-Wno-error=builtin-declaration-mismatch -Wno-error=format-truncation \
-Wno-error=expansion-to-defined -Wno-error=zero-length-bounds \
-Wno-error=cast-function-type -Wno-error=unused-value -DRELEASE -DPBL_SDK_3"

# platform | UPPER | COLOR|BW | width | height
PLATFORMS="basalt:BASALT:PBL_COLOR:144:168
diorite:DIORITE:PBL_BW:144:168
emery:EMERY:PBL_COLOR:200:228
flint:FLINT:PBL_BW:144:168"

log=$(mktemp)
fail=0
for entry in $PLATFORMS; do
  plat=${entry%%:*}; rest=${entry#*:}
  upper=${rest%%:*}; rest=${rest#*:}
  depth=${rest%%:*}; rest=${rest#*:}
  w=${rest%%:*}; h=${rest##*:}

  [ -d "build/$plat" ] || { echo "strict-check: build/$plat missing — run 'pebble build' first" >&2; exit 2; }

  inc="-I$SDK/$plat/include -Ibuild/$plat -Ibuild/include -Ibuild/src -Isrc -Iinclude"
  defs="-DPBL_PLATFORM_$upper -D$depth -DPBL_RECT -DPBL_DISPLAY_WIDTH=$w -DPBL_DISPLAY_HEIGHT=$h"

  echo "== strict-check: $plat =="
  for f in src/*.c; do
    if ! $CC $COMMON $inc $defs -c "$f" -o /dev/null 2>"$log"; then
      cat "$log" >&2; echo "strict-check: ERROR in $f ($plat)" >&2; fail=1
    elif grep -q "warning:" "$log"; then
      cat "$log" >&2; echo "strict-check: WARNING in $f ($plat)" >&2; fail=1
    fi
  done
done
rm -f "$log"

if [ "$fail" -ne 0 ]; then
  echo "strict-check: FAILED — fix the errors/warnings above before pushing to cloud." >&2
  exit 1
fi
echo "strict-check: OK (all sources compile clean, no warnings, under cloud flags)"
