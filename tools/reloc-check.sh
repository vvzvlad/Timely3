#!/usr/bin/env sh
# Verify that no non-debug R_ARM_ABS32 relocation in a built app image lands on
# a non-word-aligned address. The PebbleOS app loader rejects such targets
# ("Invalid app relocation target") and the watchface then never launches — it
# shipped that way in 0.0.7.
#
# A failure means a pointer or other address-valued initializer ended up inside a
# packed struct: packed structs have alignment 1, so the linker puts them at
# arbitrary (often odd) addresses and base + field offset comes out unaligned.
# See the packed-struct-on-flash contract in src/settings.h.
#
# Every relocation section EXCEPT .rel.debug_* is inspected. Exclusion rather
# than a whitelist of section names, so a loadable section nobody anticipated
# still gets checked; .debug_* relocations are not loaded by the app loader and
# are pure noise here — they dominate the table (measured on build/emery:
# 7640 of the 9639 relocation entries, and 6816 of the 6884 R_ARM_ABS32 ones).
# That leaves .rel.text (no R_ARM_ABS32 in practice) and .rel.data — this project
# has no .rodata output section at all, the linker script merges *(.rodata*)
# into .text.
#
# Requires a prior `pebble build`. CI has no Pebble SDK and does no ARM build, so
# this is a local gate only; it runs from tools/test.sh and from the `build`
# recipe of both the Makefile and the justfile, i.e. from every task-runner build
# path — a bare `pebble build` bypasses it.
# Run inside the dev shell:  nix develop -c sh tools/reloc-check.sh
set -eu
# Both checks below parse binutils' English output ("Machine:", "Relocation
# section ... contains N entries"). binutils ships translations, so on a
# localized system the architecture check would read an empty machine and the
# table parse would recognise no sections — a false failure on correct code.
LC_ALL=C; export LC_ALL
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

READELF="${READELF:-arm-none-eabi-readelf}"
command -v "$READELF" >/dev/null 2>&1 || {
  echo "reloc-check: '$READELF' not found in PATH — run inside 'nix develop', or set READELF to your toolchain's readelf" >&2
  exit 2
}

# The target platforms are READ from package.json -> pebble.targetPlatforms (the
# same field the Pebble SDK builds from), not copied into a constant here: a
# hand-kept copy silently stops covering a platform the moment one is added.
# Coverage is matched by NAME, not by counting build/*/: build/ is gitignored and
# never cleaned, so a leftover directory of a platform we no longer target
# (aplite, say) must neither count towards coverage nor be checked — a stale
# pre-fix ELF in one would fail the gate and blame current, clean code.
fallback_platforms="basalt diorite emery flint"
expected_platforms=""
if command -v node >/dev/null 2>&1; then
  expected_platforms=$(node -p "require('./package.json').pebble.targetPlatforms.join(' ')" 2>/dev/null) || expected_platforms=""
fi
# An empty result must never be taken at face value — "zero platforms to check"
# would turn this gate into a silent no-op. A missing node, a node that fails, a
# missing/empty field: all fall back to the hardcoded list, loudly.
if [ -z "$(printf '%s' "$expected_platforms" | tr -d ' \t\n')" ]; then
  echo "reloc-check: could not read pebble.targetPlatforms from package.json (node missing or failed) — falling back to: $fallback_platforms" >&2
  expected_platforms="$fallback_platforms"
fi
n_expected=0
for p in $expected_platforms; do
  n_expected=$((n_expected + 1))
done

fail=0
n_checked=0
checked_platforms=""
missing_platforms=""
for p in $expected_platforms; do
  elf="build/$p/pebble-app.elf"
  if [ ! -f "$elf" ]; then
    missing_platforms="$missing_platforms $p"
    continue
  fi
  echo "== reloc-check: $elf =="
  # Pin the architecture before parsing anything: every check below keys off ARM
  # relocation type NAMES, so on a foreign-architecture ELF nothing ever matches
  # and the gate would report a serene "0 relocations" instead of admitting it
  # inspected the wrong file.
  ehdr=$("$READELF" -h "$elf") || {
    echo "reloc-check: $READELF -h failed on $elf" >&2
    exit 2
  }
  machine=$(printf '%s\n' "$ehdr" | awk -F: '
    /^[[:space:]]*Machine:/ {
      sub(/^[[:space:]]+/, "", $2); sub(/[[:space:]]+$/, "", $2); print $2; exit
    }
  ')
  if [ "$machine" != "ARM" ]; then
    echo "reloc-check: $elf reports machine '$machine', expected 'ARM' — wrong toolchain or a non-Pebble ELF" >&2
    exit 2
  fi
  # Capture readelf separately from awk: in a pipeline `if ! a | b` reports only
  # b's status, and POSIX sh has no pipefail — a broken readelf would otherwise
  # feed awk empty input and be reported as a clean pass.
  out=$("$READELF" -r "$elf") || {
    echo "reloc-check: $READELF failed on $elf" >&2
    exit 2
  }
  # awk drops the .rel.debug_* sections, then flags every R_ARM_ABS32 whose
  # offset is not a multiple of 4. macOS awk has no strtonum, so alignment is
  # judged by the last hex digit: divisible by 4 iff it is 0, 4, 8 or c.
  #
  # readelf can exit 0 yet print something this parser does not understand
  # (another binutils version, a stripped file), and the caller is tools/test.sh
  # running unattended — an unparsed table must not pass as "0 relocations, all
  # fine". So the parse is self-checking on five levels, each an exit 2:
  #   * not a single relocation-section header recognised;
  #   * a header whose quoted section name cannot be extracted — treated as a
  #     parse error, NOT as "some non-debug section", or a variant that stopped
  #     quoting would silently push all ~6800 debug relocations through the
  #     alignment check and fail the build blaming correct code;
  #   * a non-debug header that carries no "contains N entries" count. That count
  #     is the only independent number the row check below can be measured
  #     against, so a header without one must not be skipped quietly: every row
  #     of that section would go unaccounted for, and a variant that also changed
  #     the row format would then balance at a clean zero;
  #   * a row count that disagrees with the "contains N entries" the header
  #     itself declares. Header recognition alone says nothing about the ROWS:
  #     if a variant printed the Offset column differently (a 0x prefix, say),
  #     every row would fall through the filter and the table would report a
  #     clean zero. Comparing against readelf's own count catches exactly that.
  #   * a counted row whose 3rd field is not an R_ARM_* type name. The row count
  #     above only proves the rows were SEEN; it says nothing about the column
  #     the type is read from. If a variant moved or renamed the type column,
  #     rows would still balance while no row ever matched R_ARM_ABS32 — again a
  #     clean zero. The other way to reach this branch is a readelf older than
  #     the compiler: binutils prints "unrecognized: %-7lx" in the type column
  #     for a relocation type it does not know, so those rows are genuinely
  #     untyped for us and failing is right. On this toolchain every non-debug
  #     row is R_ARM_*.
  # A parsed table that genuinely holds zero relocations is still a pass: with no
  # rows at all, rows, expect and armrows are all zero and every check balances.
  status=0
  printf '%s\n' "$out" | awk -v q="'" -v elf="$elf" '
    /^Relocation section/ {
      sections++;
      s = index($0, q);
      e = (s > 0) ? index(substr($0, s + 1), q) : 0;
      if (s == 0 || e == 0) { parse_error = "section header without a quoted name: " $0; exit 2 }
      sec = substr($0, s + 1, e - 1);
      want = (sec ~ /^\.rela?\.debug_/) ? 0 : 1;
      if (want) {
        if (!match($0, /contains [0-9]+ /)) {
          parse_error = "section header without an entry count: " $0; exit 2
        }
        expect += substr($0, RSTART + 9, RLENGTH - 10) + 0;
      }
      next
    }
    want && $1 ~ /^[0-9a-fA-F]+$/ {
      rows++;
      if ($3 ~ /^R_ARM_/) armrows++;
      if ($3 != "R_ARM_ABS32") next;
      total++;
      last = tolower(substr($1, length($1), 1));
      if (last != "0" && last != "4" && last != "8" && last != "c") {
        printf "reloc-check: MISALIGNED R_ARM_ABS32 at offset 0x%s in section %s of %s\n", $1, sec, elf;
        bad++;
      }
    }
    END {
      if (parse_error != "") { print "reloc-check: " parse_error > "/dev/stderr"; exit 2 }
      if (sections + 0 == 0) exit 2;
      if (rows + 0 != expect + 0) {
        printf("reloc-check: parsed %d relocation row(s) but the section headers declare %d\n", rows + 0, expect + 0) > "/dev/stderr";
        exit 2
      }
      if (armrows + 0 != rows + 0) {
        printf("reloc-check: %d of %d relocation row(s) have no R_ARM_* type in column 3\n", rows - armrows, rows + 0) > "/dev/stderr";
        printf("reloc-check: either the type column is not where this parser reads it, or this readelf is older than the compiler and printed its \"unrecognized: <hex>\" placeholder in that column for a relocation type it does not know — check that the readelf in PATH comes from the same toolchain that built the ELF\n") > "/dev/stderr";
        exit 2
      }
      printf "   %d non-debug R_ARM_ABS32 relocation(s), %d misaligned\n", total + 0, bad + 0;
      if (bad + 0 > 0) exit 1;
    }
  ' || status=$?
  case "$status" in
    0) ;;
    1) fail=1 ;;
    *)
      echo "reloc-check: unexpected readelf output for $elf — could not parse its relocation table" >&2
      echo "reloc-check: '$READELF -r' exited 0 but printed something this script does not understand; check the toolchain." >&2
      exit 2
      ;;
  esac
  n_checked=$((n_checked + 1))
  checked_platforms="$checked_platforms $p"
done

# Directories under build/ that are not target platforms are reported but never
# checked, and never counted towards coverage.
for elf in build/*/pebble-app.elf; do
  [ -f "$elf" ] || continue
  p=$(basename "$(dirname "$elf")")
  case " $expected_platforms " in
    *" $p "*) ;;
    *) echo "reloc-check: build/$p/pebble-app.elf — unknown platform dir, skipped (not in: $expected_platforms)" >&2 ;;
  esac
done

if [ "$n_checked" -eq 0 ]; then
  echo "reloc-check: no build/<platform>/pebble-app.elf found for any of: $expected_platforms — run 'pebble build' first" >&2
  exit 2
fi

if [ -n "$missing_platforms" ]; then
  echo "reloc-check: WARNING — only $n_checked of $n_expected target platforms were built (checked:${checked_platforms}; not built:${missing_platforms})." >&2
  echo "reloc-check: a single-platform local build is fine, but the missing platforms are NOT covered by this run." >&2
fi

if [ "$fail" -ne 0 ]; then
  echo "reloc-check: FAILED — the app would be rejected by the loader at launch." >&2
  echo "reloc-check: an address-valued initializer landed in a packed struct; see src/settings.h." >&2
  exit 1
fi
echo "reloc-check: OK — $n_checked of $n_expected target platform(s) checked (${checked_platforms# }); all non-debug R_ARM_ABS32 relocations are word-aligned"
