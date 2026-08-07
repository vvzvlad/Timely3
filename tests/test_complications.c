// Host-side unit tests for src/complications.c (pure complication renderers +
// the id->flags/icon table; no Pebble SDK). The renderer OUTPUTS are the
// host-testable win of the stage-17 table refactor: the device-side glue
// (text_layer_set_text lifetime, GBitmap icons) stays CI-ARM-only.
// Expected strings are pinned against the EN defaults in src/locale.c
// (DaysOfWeek/monthsNames/abbrTime/statuses) and settings.c (date_format 0).
#include "utest.h"
#include "complications.h"
#include "locale.h"
#include "settings.h"
#include "timefmt.h"
#include <string.h>
#include <time.h>

// Fixed reference instant: Fri 2026-08-07 14:00:05.
//   tm_wday=5 (Fri), tm_mon=7 (Aug), tm_year=126 (2026), tm_yday=218 (0-based;
//   %j -> 219), tm_hour=14 (PM), tm_min=0, tm_sec=5, tm_mday=7.
static struct tm ref_tm(void) {
  struct tm t = {0};
  t.tm_sec  = 5;
  t.tm_min  = 0;
  t.tm_hour = 14;
  t.tm_mday = 7;
  t.tm_mon  = 7;
  t.tm_year = 126;
  t.tm_wday = 5;
  t.tm_yday = 218;
  t.tm_isdst = 0;
  return t;
}

// Build a ComplicationCtx around a caller-owned struct tm plus explicit field
// values. tz_offset = -4 formats as "UTC+1" (see tests/test_timefmt.c).
static ComplicationCtx ref_ctx(const struct tm *now, int phone, uint8_t watch, bool bt) {
  ComplicationCtx c = {0};
  c.now           = now;
  c.tz_offset     = -4;
  c.watch_battery = watch;
  c.phone_battery = phone;
  c.bluetooth_up  = bt;
  c.weather_city  = "Berlin";
  return c;
}

// ---- date/calendar renderers ----

UTEST(complications, day_month_ampm) {
  struct tm t = ref_tm();
  ComplicationCtx c = ref_ctx(&t, 77, 42, true);
  ASSERT_STREQ("Friday", complication_render_text(1, &c)); // DaysOfWeek[5]
  ASSERT_STREQ("August", complication_render_text(2, &c)); // monthsNames[7]
  ASSERT_STREQ("PM",     complication_render_text(5, &c)); // abbrTime[1] at hour 14
}

UTEST(complications, seconds_and_doy) {
  struct tm t = ref_tm();
  ComplicationCtx c = ref_ctx(&t, 77, 42, true);
  ASSERT_STREQ("05",   complication_render_text(9, &c)); // %S, zero-padded
  ASSERT_STREQ("D219", complication_render_text(6, &c)); // %j is 1-based (yday 218 -> 219)
}

UTEST(complications, timezone) {
  struct tm t = ref_tm();
  ComplicationCtx c = ref_ctx(&t, 77, 42, true); // tz_offset = -4
  char expected[16];
  format_timezone_offset(c.tz_offset, expected, sizeof(expected));
  ASSERT_STREQ("UTC+1", expected); // sanity-pin the fixture
  ASSERT_STREQ(expected, complication_render_text(4, &c));
}

UTEST(complications, date_default_format) {
  struct tm t = ref_tm();
  ComplicationCtx c = ref_ctx(&t, 77, 42, true);
  // settings.c default date_format = 0 -> "MMMM DD, YYYY" localized.
  ASSERT_STREQ("August 07, 2026", complication_render_text(18, &c));
}

// ---- system-info renderers (battery / connection / location) ----

UTEST(complications, watch_battery) {
  struct tm t = ref_tm();
  ComplicationCtx c = ref_ctx(&t, 77, 42, true);
  ASSERT_STREQ("42%", complication_render_text(15, &c));
}

UTEST(complications, phone_battery_known_and_unknown) {
  struct tm t = ref_tm();
  ComplicationCtx known = ref_ctx(&t, 77, 42, true);
  ASSERT_STREQ("77%", complication_render_text(16, &known));
  ComplicationCtx unknown = ref_ctx(&t, -1, 42, true); // -1 = phone hasn't reported
  ASSERT_STREQ("--", complication_render_text(16, &unknown));
}

UTEST(complications, connection_status) {
  struct tm t = ref_tm();
  ComplicationCtx up   = ref_ctx(&t, 77, 42, true);
  ComplicationCtx down = ref_ctx(&t, 77, 42, false);
  ASSERT_STREQ("Linked", complication_render_text(17, &up));   // statuses[0]
  ASSERT_STREQ("NOLINK", complication_render_text(17, &down)); // statuses[1]
}

UTEST(complications, location_from_ctx) {
  struct tm t = ref_tm();
  ComplicationCtx c = ref_ctx(&t, 77, 42, true);
  ASSERT_STREQ("Berlin", complication_render_text(10, &c)); // ctx->weather_city
}

// ---- hidden / out-of-range (old `default: break` == leave text unchanged) ----

UTEST(complications, hidden_and_out_of_range_return_null) {
  struct tm t = ref_tm();
  ComplicationCtx c = ref_ctx(&t, 77, 42, true);
  ASSERT_TRUE(complication_render_text(0, &c) == NULL);   // id 0 hidden
  ASSERT_TRUE(complication_render_text(19, &c) == NULL);  // just past the table
  ASSERT_TRUE(complication_render_text(200, &c) == NULL); // far out of range
}

// ---- flags fence (H2 non-regression): the id list that used to live in
// Timely.c's complication_flags() now lives only in complications.c ----

UTEST(complications, flags_fence) {
  ASSERT_TRUE((complication_flags(9)  & COMP_NEEDS_SECOND_TICK) != 0); // Seconds
  ASSERT_TRUE((complication_flags(15) & COMP_IS_BATTERY) != 0);        // watch battery
  ASSERT_TRUE((complication_flags(16) & COMP_IS_BATTERY) != 0);        // phone battery
  // A representative non-special id carries no flags.
  ASSERT_EQ(0, complication_flags(1));
  // Seconds is not a battery; batteries do not need a second tick.
  ASSERT_TRUE((complication_flags(9)  & COMP_IS_BATTERY) == 0);
  ASSERT_TRUE((complication_flags(15) & COMP_NEEDS_SECOND_TICK) == 0);
  // Out-of-range ids carry no flags.
  ASSERT_EQ(0, complication_flags(19));
  ASSERT_EQ(0, complication_flags(200));
}

// ---- icon slots (enum, not GBitmap) ----

UTEST(complications, icon_slots) {
  ASSERT_EQ(ICON_WATCH, complication_icon_slot(15));
  ASSERT_EQ(ICON_PHONE, complication_icon_slot(16));
  ASSERT_EQ(ICON_BT,    complication_icon_slot(17));
  ASSERT_EQ(ICON_NONE,  complication_icon_slot(1));  // text-only
  ASSERT_EQ(ICON_NONE,  complication_icon_slot(0));  // hidden
  ASSERT_EQ(ICON_NONE,  complication_icon_slot(19)); // out of range
}
