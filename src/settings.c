#include "settings.h"

static persist s_settings = {
  .version    = 12,
  .inverted   = 0, // no, dark
  .day_invert = 1, // yes
  .grid       = 1, // yes
  .vibe_hour  = 0, // no
  .dayOfWeekOffset = 0, // 0 - 6, Sun - Sat
  .date_format = 0, // Month DD, YYYY
  .show_am_pm  = 5,  // above calendar, right: AM/PM
  .show_day    = 0,
  .show_week   = 3,  // above calendar, left: Week
  .slot_stat_l = 16, // status bar left: phone battery
  .slot_stat_r = 15, // status bar right: watch battery
  .slot_ctr_l  = 18, // center row left: Date (full width when alone)
  .slot_ctr_r  = 0,  // center row right: empty (Date stays full-width/centered)
  .batt_style  = 2,  // battery: icon + text

  .week_format = 0, // ISO 8601
  .vibe_pat_disconnect = 2, // double vibe
  .vibe_pat_connect = 0, // no vibe
  // Layout padding only, never read in C — must not hold an address of any kind
  // (see the packed-struct-on-flash contract in settings.h).
  .strftime_format_reserved = {0},
  .track_battery = 0,
  .theme = 1,       // Functional
  .theme_mode = 1,  // Dark (matches the pre-theme look)
};

static persist_adv_settings s_adv = {
  .week_pattern = 0,
  .invertStatBar = 0,
  .invertTopSlot = 0,
  .invertBotSlot = 0,
  .showStatus = 1,
  .showStatusBat = 20, // matches the config page default; threshold for "when battery low"
  .showDate = 1,
  .DND_start = 0,
  .DND_stop  = 0,
  .dnd_mode = 0,
  .vibe_hour_start = 0,
  .vibe_hour_stop  = 0,
  .vibe_hour_days  = 0,
  .idle_reminder = 0,
  .idle_pattern = 0,
  .custom_date_fmt = { "%Y-%m-%d" },
  .idle_start = 0,
  .idle_stop  = 0,
  .clock2_tz = 0,
  .clock2_desc = { "Second Clock" },
  .weather_format = 0,
  .weather_update = 15,
  .weather_lat = "",
  .weather_lon = "",
  .clock_font = 1,
  .token_type = { 0, 0 },
  .token_code = { "", "" },
  .slots = { 0, 1, 2, 3, 0, 1, 0, 1, 0, 1 },
  .weather_icons = 0 // Default: colour on colour watches, B&W on the rest
};

persist *settings_get(void) { return &s_settings; }
persist_adv_settings *adv_settings_get(void) { return &s_adv; }
