// Host-side unit tests for src/timefmt.c (pure logic, no Pebble SDK).
// Build/run via `make test`, `just test`, or `nix flake check`.
#include "utest.h"
#include "timefmt.h"
#include <string.h>
#include <time.h>

UTEST(daysInMonth, february_leap_rules) {
  ASSERT_EQ(29, daysInMonth(1, 2000)); // divisible by 400 -> leap
  ASSERT_EQ(28, daysInMonth(1, 1900)); // divisible by 100, not 400 -> common
  ASSERT_EQ(29, daysInMonth(1, 2024)); // divisible by 4 -> leap
  ASSERT_EQ(28, daysInMonth(1, 2026)); // common year
}

UTEST(daysInMonth, month_lengths) {
  ASSERT_EQ(31, daysInMonth(0, 2026));  // January
  ASSERT_EQ(30, daysInMonth(3, 2026));  // April
  ASSERT_EQ(31, daysInMonth(11, 2026)); // December
}

UTEST(days_left_in_year, boundaries) {
  ASSERT_EQ(364, days_left_in_year(2026, 0));    // common year, Jan 1
  ASSERT_EQ(0,   days_left_in_year(2026, 364));  // common year, Dec 31
  ASSERT_EQ(365, days_left_in_year(2024, 0));    // leap year, Jan 1
  ASSERT_EQ(0,   days_left_in_year(2024, 365));  // leap year, Dec 31
}

UTEST(format_days_left_in_year, typical_and_clamp) {
  char b[5];
  format_days_left_in_year(364, b, sizeof(b));  ASSERT_STREQ("R364", b);
  format_days_left_in_year(0, b, sizeof(b));    ASSERT_STREQ("R000", b);
  format_days_left_in_year(5, b, sizeof(b));    ASSERT_STREQ("R005", b);
  format_days_left_in_year(-3, b, sizeof(b));   ASSERT_STREQ("R000", b); // clamp low
  format_days_left_in_year(9999, b, sizeof(b)); ASSERT_STREQ("R365", b); // clamp high
}

// Preserves the watchface's existing (inverted-sign) convention:
// positive offset -> "UTC-", non-positive -> "UTC+".
UTEST(format_timezone_offset, formats) {
  char b[16];
  format_timezone_offset(TIMEZONE_UNINITIALIZED, b, sizeof(b)); ASSERT_STREQ("UTC ?", b);
  format_timezone_offset(0, b, sizeof(b));   ASSERT_STREQ("UTC+0", b);
  format_timezone_offset(4, b, sizeof(b));   ASSERT_STREQ("UTC-1", b);     // +1h east
  format_timezone_offset(5, b, sizeof(b));   ASSERT_STREQ("UTC-1:15", b);  // +1h15
  format_timezone_offset(-4, b, sizeof(b));  ASSERT_STREQ("UTC+1", b);     // -1h west
  format_timezone_offset(-5, b, sizeof(b));  ASSERT_STREQ("UTC+1:15", b);  // -1h15
}

// A fixed, unambiguous date (2026-01-15) used by the date-format tests. Day 15
// keeps %e (space-padded day) two digits so there are no padding surprises.
static struct tm fixed_tm(void) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = 2026 - 1900; // 126
  t.tm_mon  = 0;           // January
  t.tm_mday = 15;
  t.tm_hour = 9;
  t.tm_min  = 41;
  t.tm_wday = 4;           // Thursday
  t.tm_yday = 14;
  return t;
}

// ---- T3: table lookup + render selection (C1 fix) ----
// datefmt_table_entry bounds-checks 195..254; everything else (incl. 194 and
// the custom sentinel 255) is NULL. Reddens if datefmt_table_entry drops its
// `date_format < 195 || date_format > 254` guard (original OOB tautology).
UTEST(datefmt_table_entry, boundaries) {
  ASSERT_TRUE(datefmt_table_entry(194) == NULL);          // below table -> NULL
  ASSERT_STREQ("%m.%d.%Y", datefmt_table_entry(195));     // first entry
  ASSERT_STREQ("%d.%m.%y", datefmt_table_entry(220));     // a middle entry
  ASSERT_STREQ("%y%m%e",   datefmt_table_entry(254));     // last entry
  ASSERT_TRUE(datefmt_table_entry(255) == NULL);          // sentinel is NOT in the table
}

// datefmt_render resolves all three regions safely for a fixed date.
// The 255 case pins the C1 fix from the caller's side: custom_date_fmt is now
// actually applied (the original dead-code branch never reached it). OOB safety
// itself is guarded by datefmt_table_entry's bounds — pinned in
// datefmt_table_entry.boundaries (entry(255)==NULL), which reddens if the upper
// bound is dropped. (Branch ORDER here is not load-bearing: entry(255) is NULL
// regardless, so the sentinel-first arm is for clarity, not correctness.)
UTEST(datefmt_render, all_branches) {
  struct tm t = fixed_tm();
  char out[32];

  // 194: localized region from this unit's view -> empty string, no indexing.
  strcpy(out, "sentinel");
  datefmt_render(194, "IGNORED", &t, out, sizeof(out));
  ASSERT_STREQ("", out);

  // 195: first table entry "%m.%d.%Y".
  datefmt_render(195, NULL, &t, out, sizeof(out));
  ASSERT_STREQ("01.15.2026", out);

  // 254: last table entry "%y%m%e".
  datefmt_render(254, NULL, &t, out, sizeof(out));
  ASSERT_STREQ("260115", out);

  // 220: a middle table entry "%d.%m.%y".
  datefmt_render(220, NULL, &t, out, sizeof(out));
  ASSERT_STREQ("15.01.26", out);

  // 255: custom format actually applied (C1 fix). A literal custom string with
  // no % directives is copied verbatim, proving the table is NOT consulted.
  datefmt_render(255, "CUSTOM", &t, out, sizeof(out));
  ASSERT_STREQ("CUSTOM", out);

  // 255 with a real strftime directive uses the custom format, not the table.
  datefmt_render(255, "%Y-%m-%d", &t, out, sizeof(out));
  ASSERT_STREQ("2026-01-15", out);

  // 255 with NULL custom format -> empty, no crash.
  strcpy(out, "sentinel");
  datefmt_render(255, NULL, &t, out, sizeof(out));
  ASSERT_STREQ("", out);
}

// H1: strftime return checked -> a format that cannot fit yields "" (terminated),
// never an unterminated buffer. Reddens if datefmt_render drops the r==0 check.
UTEST(datefmt_render, overflow_is_empty) {
  struct tm t = fixed_tm();
  char out[6]; // too small for "%Y-%m-%d" ("2026-01-15" needs 11)
  strcpy(out, "junk");
  datefmt_render(255, "%Y-%m-%d", &t, out, sizeof(out));
  ASSERT_STREQ("", out); // strftime returned 0 -> forced empty, no OOB read
}

// ---- T5: v11 -> v12 migration (C2 fix) ----
// Saturates at 254 and never reaches the sentinel 255. Reddens if the guard
// loses `&& old < 254` (original: 254 -> 255) or `> 234` (original wrap 255->0
// with the raw `+1`).
UTEST(datefmt_migrate_v11_to_v12, boundaries) {
  ASSERT_EQ(234, datefmt_migrate_v11_to_v12(234)); // at/below cutoff: unchanged
  ASSERT_EQ(236, datefmt_migrate_v11_to_v12(235)); // shifts up by one
  ASSERT_EQ(254, datefmt_migrate_v11_to_v12(253)); // shifts up to the last table code
  ASSERT_EQ(254, datefmt_migrate_v11_to_v12(254)); // saturates, NOT 255 (C2 fix)
  ASSERT_EQ(255, datefmt_migrate_v11_to_v12(255)); // sentinel preserved, NOT 0
}

// ---- L10: coordinate parser (deduped from Timely.c/theme.c into timefmt.c) ----
// "[-]int[.frac]" with optional sign; rejects empty/NULL/no-digit input and
// leaves *out untouched on rejection.
UTEST(parse_coord, valid_and_edge) {
  float v;

  v = 123.0f; ASSERT_TRUE(parse_coord("0", &v));        ASSERT_EQ(0, (int)(v * 1000));
  v = 0.0f;   ASSERT_TRUE(parse_coord("55.75", &v));    ASSERT_EQ(55750, (int)(v * 1000 + 0.5f));
  v = 0.0f;   ASSERT_TRUE(parse_coord("-37.62", &v));   ASSERT_EQ(-37620, (int)(v * 1000 - 0.5f)); // negative
  v = 0.0f;   ASSERT_TRUE(parse_coord("+12", &v));      ASSERT_EQ(12000, (int)(v * 1000 + 0.5f));  // explicit plus
  v = 0.0f;   ASSERT_TRUE(parse_coord("-0.5", &v));     ASSERT_EQ(-500, (int)(v * 1000 - 0.5f));
  v = 0.0f;   ASSERT_TRUE(parse_coord("90", &v));       ASSERT_EQ(90000, (int)(v * 1000 + 0.5f));
  v = 0.0f;   ASSERT_TRUE(parse_coord("7.", &v));       ASSERT_EQ(7000, (int)(v * 1000 + 0.5f));   // trailing dot, integer part only
}

UTEST(parse_coord, rejects_invalid) {
  float v = 42.0f;
  ASSERT_FALSE(parse_coord(NULL, &v));   ASSERT_EQ(42, (int)v); // NULL -> untouched
  ASSERT_FALSE(parse_coord("", &v));     ASSERT_EQ(42, (int)v); // empty
  ASSERT_FALSE(parse_coord("-", &v));    ASSERT_EQ(42, (int)v); // sign, no digits
  ASSERT_FALSE(parse_coord(".", &v));    ASSERT_EQ(42, (int)v); // dot, no digits
  ASSERT_FALSE(parse_coord("abc", &v));  ASSERT_EQ(42, (int)v); // non-numeric
}

// ---- clamp policy ----
// Valid-set = localized (<195) | table (195..254) | sentinel 255 == the whole
// uint8_t domain, so clamp is an identity pass-through today (documented
// defensive no-op); it never remaps a localized code.
UTEST(datefmt_clamp, passes_whole_domain) {
  ASSERT_EQ(0,   datefmt_clamp(0));    // localized default
  ASSERT_EQ(14,  datefmt_clamp(14));   // localized (handled switch case)
  ASSERT_EQ(194, datefmt_clamp(194));  // localized, just below the table
  ASSERT_EQ(195, datefmt_clamp(195));  // first table code
  ASSERT_EQ(254, datefmt_clamp(254));  // last table code
  ASSERT_EQ(255, datefmt_clamp(255));  // custom sentinel
}

