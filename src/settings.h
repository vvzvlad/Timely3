#pragma once
#include <stdint.h>
#include <stddef.h> // offsetof, for the layout _Static_assert guards below
// User settings + advanced settings. State owned by settings.c; access via
// settings_get() / adv_settings_get(). Structs are persist-backed.

typedef struct persist { // 18 bytes
  uint8_t version;                // version key
  uint8_t inverted;               // Invert display
  uint8_t day_invert;             // Invert colors on today's date
  uint8_t grid;                   // Show the grid
  uint8_t vibe_hour;              // vibrate at the top of the hour?
  uint8_t dayOfWeekOffset;        // first day of our week
  uint8_t date_format;            // date format
  uint8_t show_am_pm;             // Above-calendar slot, right (complication content)
  uint8_t show_day;               // (retired middle slot)
  uint8_t show_week;              // Above-calendar slot, left (complication content)
  uint8_t slot_stat_l;            // Status bar slot, left (complication content)
  uint8_t slot_stat_r;            // Status bar slot, right (complication content)
  uint8_t slot_ctr_l;             // Center row (above time), left
  uint8_t slot_ctr_r;             // Center row (above time), right
  uint8_t batt_style;             // battery complication style: 0 bar, 1 text, 2 icon+text
  uint8_t week_format;            // week format (calculation, e.g. ISO 8601)
  uint8_t vibe_pat_disconnect;    // vibration pattern for disconnect
  uint8_t vibe_pat_connect;       // vibration pattern for connect
  char *strftime_format;          // custom date_format string (date_format = 255)
  uint8_t track_battery;          // track battery information
  uint8_t theme;                  // color theme id (see theme.h)
  uint8_t theme_mode;             // 0 light, 1 dark, 2 auto (dark at night)
} __attribute__((__packed__)) persist;

// FROZEN LAYOUT. persist_adv_settings sits at 244/256 bytes of Pebble's
// PERSIST_DATA_MAX_LENGTH — only 12 bytes of headroom remain, past which
// persist_write_data(PK_ADV_SETTINGS, ...) silently fails. Do NOT append new
// fields here: every NEW advanced setting goes into persist_settings_ext
// (PK_SETTINGS_EXT) below. The _Static_assert after the typedef enforces this.
typedef struct persist_adv_settings { // 244 bytes
  uint8_t week_pattern;    //  1 byte
  uint8_t invertStatBar;   //  1 byte
  uint8_t invertTopSlot;   //  1 byte
  uint8_t invertBotSlot;   //  1 byte
  uint8_t showStatus;      //  1 byte
  uint8_t showStatusBat;   //  1 byte
  uint8_t showDate;        //  1 byte
  uint8_t DND_start;       //  1 byte
  uint8_t DND_stop;        //  1 byte
  uint8_t dnd_mode;        //  1 byte: 0 off, 1 follow watch Quiet Time, 2 app window
  uint8_t vibe_hour_start; //  1 byte
  uint8_t vibe_hour_stop;  //  1 byte
  uint8_t vibe_hour_days;  //  1 byte
  uint8_t idle_reminder;   //  1 byte
  uint8_t idle_pattern;    //  1 byte
  char custom_date_fmt[32]; // 32 bytes: custom strftime date format (date_format == 255)
  uint8_t idle_start;      //  1 byte
  uint8_t idle_stop;       //  1 byte
  int8_t clock2_tz;        //  1 byte
  char clock2_desc[32];    // 32 bytes
  uint8_t weather_format;  //  1 byte
  uint8_t weather_update;  //  1 byte
  char weather_lat[8];     //  8 bytes
  char weather_lon[8];     //  8 bytes
  uint8_t clock_font;      //  1 byte
  uint8_t token_type[2];   //  2 bytes
  char token_code[2][65];  //130 bytes
  uint8_t slots[10];       // 10 bytes
  uint8_t weather_icons;   //  1 byte: 0 default (colour where supported), 1 force colour, 2 force B&W
} __attribute__((__packed__)) persist_adv_settings;
// Frozen size + layout guards: the whole struct is written to flash as one blob,
// so its size and the offset of the last field are a user-data compat contract.
_Static_assert(sizeof(persist_adv_settings) == 244,
               "PK_ADV_SETTINGS is FROZEN at 244 bytes — add new settings to persist_settings_ext (PK_SETTINGS_EXT), never here");
_Static_assert(offsetof(persist_adv_settings, weather_icons) == 243,
               "persist_adv_settings layout drifted");

// Extended settings (PK_SETTINGS_EXT). PK_ADV_SETTINGS hit its practical ceiling
// (244/256), so every NEW advanced setting goes here. `version` is first so the
// struct can be migrated; a missing key reads as all-defaults (see settings.c).
// New fields go at the END (persist_read_data tolerates a shorter stored blob).
#define SETTINGS_EXT_VERSION 1
typedef struct persist_settings_ext { // version-led, currently minimal
  uint8_t version;   // SETTINGS_EXT_VERSION
  // (future advanced settings appended here)
} __attribute__((__packed__)) persist_settings_ext;

persist *settings_get(void);
persist_adv_settings *adv_settings_get(void);
persist_settings_ext *settings_ext_get(void);
