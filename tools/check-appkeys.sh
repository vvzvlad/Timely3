#!/usr/bin/env sh
# Verify the AK_* message-key #defines in src/Timely.c are a bijection with
# package.json's "messageKeys" (same names -> same numbers). The three config
# layers must agree (AGENTS.md); a silent drift here breaks settings on the wire.
#
# Name mapping: AK_STYLE_INV -> style_inv (drop the AK_ prefix, lowercase).
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

akf=$(mktemp)
mkf=$(mktemp)
trap 'rm -f "$akf" "$mkf"' EXIT

# name=number from the C #defines, lowercased.
grep -oE '#define AK_[A-Z0-9_]+ +[0-9]+' src/Timely.c \
  | awk '{ gsub("AK_", "", $2); print tolower($2) "=" $3 }' \
  | sort > "$akf"

# name=number from package.json messageKeys.
node -e 'const m = require("./package.json").pebble.messageKeys;
  for (const [k, v] of Object.entries(m)) console.log(k + "=" + v);' \
  | sort > "$mkf"

if ! diff "$akf" "$mkf" >/dev/null 2>&1; then
  echo "check-appkeys: MISMATCH between src/Timely.c AK_* and package.json messageKeys:" >&2
  echo "  (< = only in src/Timely.c AK_*, > = only in package.json messageKeys)" >&2
  diff "$akf" "$mkf" >&2 || true
  exit 1
fi
echo "check-appkeys: OK ($(wc -l < "$akf" | tr -d ' ') keys, AK_* <-> messageKeys bijection)"
