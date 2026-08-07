// Complication text renderers + behaviour/icon table (audit A1 variant A).
// Pure and host-testable: NO pebble.h / ui.h / weather.h. The renderers reach
// the watch's live state through ComplicationCtx (passed by pointer) and call
// the host-linkable settings.c / locale.c / timefmt.c / suntimes.c modules
// directly. Each renderer returns a pointer to storage that outlives the call
// (its own `static` buffer, or a pointer into a lang/weather table), because the
// view's text_layer_set_text() does not copy — a stack-buffer return would
// dangle. See tests/test_complications.c.
#include "complications.h"
#include "settings.h"
#include "locale.h"
#include "timefmt.h"
#include "suntimes.h"
#include <stdio.h>
#include <string.h>

// ---- individual renderers (each keeps its OWN static buffer or returns a
// pointer into a lang/weather table; moved verbatim from Timely.c, with the
// former Timely.c globals rerouted to ctx-> fields) ----

static const char *render_day(const ComplicationCtx *ctx) {
  return lang_days_get()->DaysOfWeek[ctx->now->tm_wday];
}

static const char *render_month(const ComplicationCtx *ctx) {
  return lang_months_get()->monthsNames[ctx->now->tm_mon];
}

static const char *render_week(const ComplicationCtx *ctx) {
  static char week_text[] = "W00";
  char week_format[] = "W%V"; // V = ISO 8601 week number (00-53)
  if (settings_get()->week_format == 1) {
    week_format[2] = 'U'; // first Sunday starts week one
  } else if (settings_get()->week_format == 2) {
    week_format[2] = 'W'; // first Monday starts week one
  }
  strftime(week_text, sizeof(week_text), week_format, ctx->now);
  return week_text;
}

static const char *render_timezone(const ComplicationCtx *ctx) {
  static char timezone_text[16];
  format_timezone_offset(ctx->tz_offset, timezone_text, sizeof(timezone_text));
  return timezone_text;
}

static const char *render_ampm(const ComplicationCtx *ctx) {
  return (ctx->now->tm_hour < 12) ? lang_gen_get()->abbrTime[0]  //  0-11 AM
                                  : lang_gen_get()->abbrTime[1];  // 12-23 PM
}

static const char *render_seconds(const ComplicationCtx *ctx) {
  static char seconds_text[] = "00"; // 00-61
  strftime(seconds_text, sizeof(seconds_text), "%S", ctx->now);
  return seconds_text;
}

static const char *render_location(const ComplicationCtx *ctx) {
  // weather provider's location name (truncated to the slot width by the view)
  return ctx->weather_city;
}

// Day-of-year / days-left helpers keep their own statics so the combined
// "D000/R000" renderer can hold both pointers live at once (as the original
// update_doy_dliy_text did).
static const char *doy_text(const ComplicationCtx *ctx) {
  static char buf[] = "D000";
  strftime(buf, sizeof(buf), "D%j", ctx->now);
  return buf;
}

static const char *dliy_text(const ComplicationCtx *ctx) {
  static char buf[] = "R000";
  int days_left = days_left_in_year(ctx->now->tm_year + 1900, ctx->now->tm_yday);
  format_days_left_in_year(days_left, buf, sizeof(buf));
  return buf;
}

static const char *render_doy(const ComplicationCtx *ctx) { return doy_text(ctx); }
static const char *render_dliy(const ComplicationCtx *ctx) { return dliy_text(ctx); }

static const char *render_doy_dliy(const ComplicationCtx *ctx) {
  static char buf[] = "D000/R000";
  snprintf(buf, sizeof(buf), "%s/%s", doy_text(ctx), dliy_text(ctx));
  return buf;
}

#ifdef PBL_PLATFORM_APLITE
// Aplite never computed sun times (matches the old #ifndef PBL_PLATFORM_APLITE
// guard in Timely.c). PBL_PLATFORM_APLITE is a Pebble-SDK compiler define, so it
// is undefined for the host build here -> the computed path below is used, which
// is what the host tests exercise.
static const char *render_sunrise(const ComplicationCtx *ctx) { (void)ctx; return "--:--"; }
static const char *render_sunset(const ComplicationCtx *ctx)  { (void)ctx; return "--:--"; }
#else
static const char *sun_text(const ComplicationCtx *ctx, char *buf, bool want_sunset) {
  float lat, lon, sr, ss;
  if (ctx->now && ctx->tz_offset != TIMEZONE_UNINITIALIZED &&
      parse_coord(adv_settings_get()->weather_lat, &lat) &&
      parse_coord(adv_settings_get()->weather_lon, &lon)) {
    sun_times(lat, lon, ctx->now->tm_yday, -ctx->tz_offset / 4.0f, &sr, &ss);
    float h = want_sunset ? ss : sr;
    // Normalize the fractional hour into [0,24) BEFORE splitting: (int) truncates
    // toward zero, so a negative h (tz running ahead of solar time at high lat)
    // would otherwise give hh/mm the wrong sign and print an hour late.
    while (h < 0.0f)   { h += 24.0f; }
    while (h >= 24.0f) { h -= 24.0f; }
    int hh = (int)h, mm = (int)((h - hh) * 60 + 0.5f);
    if (mm >= 60) { mm -= 60; hh++; } // rounding may carry into the next hour
    hh %= 24;                          // wrap 23:60 -> 00:00
    snprintf(buf, 16, "%d:%02d", hh, mm);
    return buf;
  }
  return "--:--"; // no location yet
}
static const char *render_sunrise(const ComplicationCtx *ctx) { static char b[16]; return sun_text(ctx, b, false); }
static const char *render_sunset(const ComplicationCtx *ctx)  { static char b[16]; return sun_text(ctx, b, true); }
#endif

// Moon phase from the date only (Conway's approximation; no coords needed).
static const char *render_moon(const ComplicationCtx *ctx) {
  static const char *PHASES[8] = { "New", "Wax cres", "1st qtr", "Wax gib",
                                   "Full", "Wan gib", "Last qtr", "Wan cres" };
  if (!ctx->now) { return "Moon"; }
  int y = ctx->now->tm_year + 1900, m = ctx->now->tm_mon + 1, d = ctx->now->tm_mday;
  int r = y % 100; r %= 19; if (r > 9) { r -= 19; }
  r = ((r * 11) % 30) + m + d; if (m < 3) { r += 2; }
  r -= (y < 2000) ? 4 : 8;
  int age = ((r % 30) + 30) % 30; // 0..29 days into the lunation
  int phase = (age < 2 || age >= 28) ? 0 : (age < 6 ? 1 : (age < 9 ? 2 : (age < 13 ? 3 :
              (age < 17 ? 4 : (age < 20 ? 5 : (age < 24 ? 6 : 7))))));
  return PHASES[phase];
}

// Second time zone: local time shifted to clock2_tz (UTC offset, whole hours).
static const char *render_clock2(const ComplicationCtx *ctx) {
  static char buf[16];
  if (!ctx->now || ctx->tz_offset == TIMEZONE_UNINITIALIZED) { return "--:--"; }
  int local_tz_min = -ctx->tz_offset * 15; // local UTC offset in minutes
  int utc_min = ctx->now->tm_hour * 60 + ctx->now->tm_min - local_tz_min;
  int second_min = (((utc_min + adv_settings_get()->clock2_tz * 60) % 1440) + 1440) % 1440;
  snprintf(buf, sizeof(buf), "%d:%02d", second_min / 60, second_min % 60);
  return buf;
}

static const char *render_wbatt(const ComplicationCtx *ctx) {
  static char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", ctx->watch_battery);
  return buf;
}

static const char *render_pbatt(const ComplicationCtx *ctx) {
  static char buf[16];
  if (ctx->phone_battery >= 0) {
    snprintf(buf, sizeof(buf), "%d%%", ctx->phone_battery);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  return buf;
}

static const char *render_conn(const ComplicationCtx *ctx) {
  return ctx->bluetooth_up ? lang_gen_get()->statuses[0] : lang_gen_get()->statuses[1];
}

// Full current date per the configured format (moved from Timely.c's
// format_current_date). The non-localized (195..254) and custom (255) strftime
// formats live in timefmt.c (datefmt_render).
static const char *render_date(const ComplicationCtx *ctx) {
  char date_text[32]; // >= 32: holds the widest custom_date_fmt (31 chars + NUL)
  static char date_string[64]; // localized "%s %s %s" date; sized to avoid truncation

  if (settings_get()->date_format < 195) { // localized date formats...
    char date_text_2[24];
    switch (settings_get()->date_format) {
    case 0: // MMMM DD, YYYY (localized)
      strftime(date_text, sizeof(date_text), "%d, %Y", ctx->now); // DD, YYYY
      snprintf(date_string, sizeof(date_string), "%s %s", lang_months_get()->monthsNames[ctx->now->tm_mon], date_text); // prefix Month
      break;
    case 1: // MMMM DD, 'YY (localized)
      strftime(date_text, sizeof(date_text), "%d, '%y", ctx->now); // DD, 'YY
      snprintf(date_string, sizeof(date_string), "%s %s", lang_months_get()->monthsNames[ctx->now->tm_mon], date_text); // prefix Month
      break;
    case 2: // Mmm DD, YYYY (localized)
      strftime(date_text, sizeof(date_text), "%d, %Y", ctx->now); // DD, YYYY
      snprintf(date_string, sizeof(date_string), "%s %s", lang_gen_get()->abbrMonthsNames[ctx->now->tm_mon], date_text); // prefix Mon
      break;
    case 3: // Mmm DD, 'YY (localized)
      strftime(date_text, sizeof(date_text), "%d, '%y", ctx->now); // DD, 'YY
      snprintf(date_string, sizeof(date_string), "%s %s", lang_gen_get()->abbrMonthsNames[ctx->now->tm_mon], date_text); // prefix Mon
      break;
    case 11: // D MMMM YYYY (localized)
      strftime(date_text, sizeof(date_text), "%d", ctx->now); // D
      strftime(date_text_2, sizeof(date_text_2), "%Y", ctx->now); // YYYY
      snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_months_get()->monthsNames[ctx->now->tm_mon], date_text_2); // insert Month
      break;
    case 12: // D MMMM 'YY (localized)
      strftime(date_text, sizeof(date_text), "%d", ctx->now); // D
      strftime(date_text_2, sizeof(date_text_2), "'%y", ctx->now); // YY
      snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_months_get()->monthsNames[ctx->now->tm_mon], date_text_2); // insert Month
      break;
    case 13: // D Mmm YYYY (localized)
      strftime(date_text, sizeof(date_text), "%d", ctx->now); // D
      strftime(date_text_2, sizeof(date_text_2), "%Y", ctx->now); // YYYY
      snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_gen_get()->abbrMonthsNames[ctx->now->tm_mon], date_text_2); // insert Mon
      break;
    case 14: // D Mmm 'YY (localized)
      strftime(date_text, sizeof(date_text), "%d", ctx->now); // D
      strftime(date_text_2, sizeof(date_text_2), "'%y", ctx->now); // YY
      snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_gen_get()->abbrMonthsNames[ctx->now->tm_mon], date_text_2); // insert Mon
      break;
    }
  } else { // non-localized date formats: custom (255) or table (195..254)
    // Sentinel 255 is resolved FIRST (custom format); 195..254 hit the table;
    // any other value yields "" -- no blind indexing, strftime return checked.
    datefmt_render(settings_get()->date_format, adv_settings_get()->custom_date_fmt,
                   ctx->now, date_text, sizeof(date_text));
    snprintf(date_string, sizeof(date_string), "%s", date_text); // straight copy
  }
  return date_string;
}

// ---- the table (19 rows, id 0..18). id 0 has no renderer (hidden); the view
// leaves the layer's text unchanged for a NULL render, matching the old
// `default: break` in update_slot_text. ----
typedef struct {
  const char *(*render)(const ComplicationCtx *ctx);
  uint8_t  flags;
  IconSlot icon;
} Complication;

static const Complication COMPLICATIONS[COMPLICATION_COUNT] = {
  [0]  = { NULL,            0,                      ICON_NONE  }, // hidden
  [1]  = { render_day,      0,                      ICON_NONE  },
  [2]  = { render_month,    0,                      ICON_NONE  },
  [3]  = { render_week,     0,                      ICON_NONE  },
  [4]  = { render_timezone, 0,                      ICON_NONE  },
  [5]  = { render_ampm,     0,                      ICON_NONE  },
  [6]  = { render_doy,      0,                      ICON_NONE  },
  [7]  = { render_dliy,     0,                      ICON_NONE  },
  [8]  = { render_doy_dliy, 0,                      ICON_NONE  },
  [9]  = { render_seconds,  COMP_NEEDS_SECOND_TICK, ICON_NONE  },
  [10] = { render_location, 0,                      ICON_NONE  },
  [11] = { render_sunrise,  0,                      ICON_NONE  },
  [12] = { render_sunset,   0,                      ICON_NONE  },
  [13] = { render_moon,     0,                      ICON_NONE  },
  [14] = { render_clock2,   0,                      ICON_NONE  },
  [15] = { render_wbatt,    COMP_IS_BATTERY,        ICON_WATCH },
  [16] = { render_pbatt,    COMP_IS_BATTERY,        ICON_PHONE },
  [17] = { render_conn,     0,                      ICON_BT    },
  [18] = { render_date,     0,                      ICON_NONE  },
};

const char *complication_render_text(uint8_t id, const ComplicationCtx *ctx) {
  if (id >= COMPLICATION_COUNT || !COMPLICATIONS[id].render) { return NULL; }
  return COMPLICATIONS[id].render(ctx);
}

uint8_t complication_flags(uint8_t id) {
  return (id < COMPLICATION_COUNT) ? COMPLICATIONS[id].flags : 0;
}

IconSlot complication_icon_slot(uint8_t id) {
  return (id < COMPLICATION_COUNT) ? COMPLICATIONS[id].icon : ICON_NONE;
}
