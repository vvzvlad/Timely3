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
  // MUST stay a null pointer (0). This packed field sits at an unaligned offset,
  // so a string literal here emits a misaligned R_ARM_ABS32 relocation that the
  // app loader rejects ("Invalid app relocation target" -> app never launches).
  // The custom date format lives in adv_settings.custom_date_fmt; unused in C.
  .strftime_format = 0,
  .track_battery = 0,
  .theme = 1,       // Functional
  .theme_mode = 1,  // Dark (matches the pre-theme look)
};

static persist_adv_settings s_adv = {
  .week_pattern = 0,
  .reserved_invertStatBar = 0,
  .reserved_invertTopSlot = 0,
  .reserved_invertBotSlot = 0,
  .showStatus = 1,
  .showStatusBat = 20, // matches the config page default; threshold for "when battery low"
  .reserved_showDate = 1,
  .DND_start = 0,
  .DND_stop  = 0,
  .dnd_mode = 0,
  .vibe_hour_start = 0,
  .vibe_hour_stop  = 0,
  .vibe_hour_days  = 0,
  .reserved_idle_reminder = 0,
  .reserved_idle_pattern = 0,
  .custom_date_fmt = { "%Y-%m-%d" },
  .reserved_idle_start = 0,
  .reserved_idle_stop  = 0,
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

// Extended settings (PK_SETTINGS_EXT). Holds the defaults for a fresh install or
// a missing key; a shorter/absent stored blob leaves later fields at these values.
static persist_settings_ext s_ext = {
  .version = SETTINGS_EXT_VERSION,
  // (future advanced settings default here)
};

persist *settings_get(void) { return &s_settings; }
persist_adv_settings *adv_settings_get(void) { return &s_adv; }
persist_settings_ext *settings_ext_get(void) { return &s_ext; }
