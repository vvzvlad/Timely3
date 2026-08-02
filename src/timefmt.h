// Pure date/timezone formatting helpers, free of any Pebble SDK dependency so
// they can be unit-tested on the host. See tests/test_timefmt.c.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Sentinel for an offset that has not been received from the JS config yet.
#define TIMEZONE_UNINITIALIZED 80

// Number of days in a given month. mon is 0-based (0 = January, 11 = December);
// year is the full Gregorian year (e.g. 2026). Leap years handled per the
// Gregorian rules.
int daysInMonth(int mon, int year);

// Days remaining in the year after the given day-of-year. year is the full
// Gregorian year; yday is 0-based (0 = Jan 1), matching struct tm's tm_yday.
// Returns 0..365.
int days_left_in_year(int year, int yday);

// Format days-remaining as "R%03d" into buf (needs >= 5 bytes). days_left is
// clamped to [0, 365] before formatting.
void format_days_left_in_year(int days_left, char *buf, size_t n);

// Format a quarter-hour timezone offset as "UTC", e.g. "UTC-5", "UTC+5:30",
// or "UTC ?" when uninitialized. buf needs >= 11 bytes (16 recommended).
void format_timezone_offset(int tz_offset, char *buf, size_t n);

// ---- Non-localized date format selection (formerly inline in Timely.c) ----
// Date format codes: <195 localized (handled by Timely.c), 195..254 the
// strftime "table" formats below, 255 the "Custom (strftime)" sentinel.

// Return the strftime() format string for a table date format code
// (195..254 inclusive, 60 entries; index = code - 195). Returns NULL for any
// code outside that range, including the custom sentinel 255 and localized
// codes < 195, so a caller must never dereference the result without a check.
const char *datefmt_table_entry(uint8_t date_format);

// Render the non-localized date for date_format into out[0..n-1], always
// NUL-terminating. The sentinel 255 is tested FIRST and uses custom_fmt (may be
// NULL -> empty); 195..254 use the table; anything else yields "". strftime's
// return is checked: on 0 (result did not fit / empty) out becomes "" so a
// caller never reads past a missing terminator. out needs >= 32 bytes to hold
// the widest formats (custom_date_fmt is 31 chars + NUL).
// Forward-declare struct tm: the Pebble target build compiles with -D_TIME_H_,
// which stubs the standard <time.h> (struct tm comes from the SDK headers, not
// libc), so a header relying on <time.h> for the type would declare struct tm
// inside this parameter list and fail under -Werror. A pointer parameter only
// needs the incomplete type; the host build (real <time.h>) is unaffected.
struct tm;
void datefmt_render(uint8_t date_format, const char *custom_fmt,
                    const struct tm *t, char *out, size_t n);

// v11 -> v12 settings migration for date_format. A format was inserted into the
// table ordering at code 235, so stored codes 235..253 shift up by one. It
// saturates at 254 and never produces the custom sentinel 255 (the old code
// did: 254 -> 255, which poisoned the OOB read, and 255 -> 0). Codes <= 234 and
// 254/255 pass through unchanged.
uint8_t datefmt_migrate_v11_to_v12(uint8_t old);

// Clamp a received/persisted date_format to the set the renderer handles
// safely. Localized (<195), table (195..254) and the custom sentinel (255)
// together span the whole uint8_t domain, so today this is a defensive no-op /
// single choke point: any value that ever falls outside the handled set maps to
// the safe default 0. It deliberately does NOT remap localized (<195) codes.
uint8_t datefmt_clamp(uint8_t v);
