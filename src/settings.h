#pragma once
#include <stdint.h>
#include <stddef.h> // offsetof, for the layout asserts below
// User settings + advanced settings. State owned by settings.c; access via
// settings_get() / adv_settings_get(). Structs are persist-backed.

// Packed-struct-on-flash contract — applies to BOTH `persist` and
// `persist_adv_settings` below. Both are written to / read from flash verbatim
// via persist_write_data()/persist_read_data(), so their byte layout is a
// compatibility contract with settings already stored on users' watches.
//
//  1. Keep fields byte-granular: uint8_t / int8_t / char[]. NEVER a pointer —
//     a pointer initialized with an address (e.g. a string literal) makes the
//     compiler emit an R_ARM_ABS32 relocation into the struct. Because packed
//     structs have alignment 1, the linker places them at arbitrary (often odd)
//     addresses, so image address = struct base + field offset can be
//     non-word-aligned; the PebbleOS app loader rejects such relocation targets
//     and the app then fails to launch at all. No field offset is inherently
//     safe — the misalignment comes from base address + offset together.
//     Multi-byte scalars (uint16_t/uint32_t) are discouraged for the same
//     alignment-1 reason: access degrades to byte-wise and couples the on-flash
//     format to endianness.
//  2. NEVER insert or remove a field in the middle: that shifts every later
//     field and silently corrupts the persisted settings of existing installs.
//     New fields go at the END only — persist_read_data tolerates a shorter
//     stored blob, leaving appended fields at their settings.c default for
//     pre-existing installs.
//
// tools/reloc-check.sh looks for the SYMPTOM of a broken rule 1 in a built ELF
// (a misaligned relocation), which is not the same as proving the rule: a
// pointer that happens to land word-aligned passes the gate, and may start
// failing after an unrelated change shifts addresses. It is also a LOCAL gate
// only — CI has no Pebble SDK and does no ARM build. Rule 1 itself is enforced
// by review. The _Static_asserts after each struct below guard rule 2 and do
// run everywhere, including in the host test build — tests/test_settings_layout.c
// includes this header so CI evaluates them, and pins some interior offsets and
// sizes the asserts leave open.

typedef struct persist { // 25 bytes: 18 x uint8 + 4 reserved + 3 x uint8
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
  // Unused in C; kept only to preserve the persisted layout of the fields after
  // it. Was formerly `char *strftime_format`, whose string-literal initializer
  // emitted a misaligned relocation that made the app unlaunchable (see the
  // contract above). The live custom date format is
  // persist_adv_settings.custom_date_fmt (wire key "strftime_format", appKey 13).
  uint8_t strftime_format_reserved[4];
  uint8_t track_battery;          // track battery information
  uint8_t theme;                  // color theme id (see theme.h)
  uint8_t theme_mode;             // 0 light, 1 dark, 2 auto (dark at night)
} __attribute__((__packed__)) persist;

// Layout guards for the append-only rule: they catch an insertion, a removal or
// a resize of a field, because those change sizeof and/or shift the trailing
// fields — and such a change would misread the settings blob already persisted
// on existing installs. They do NOT catch size-neutral edits: reordering fields
// of equal width (e.g. swapping inverted and day_invert) or substituting a field
// of the same width passes every assert. Against a size-neutral move, only the
// fields an offset assert names — track_battery and the last field — plus
// strftime_format_reserved (offset and size) and theme (offset), which
// tests/test_settings_layout.c pins, are covered; the remaining fields still
// need review by eye.
// Appending a new field at the end IS allowed by the contract above and
// legitimately trips the sizeof assert — bump the size constant here (and the
// struct's size comment) in the same commit, and move the trailing offset assert
// onto the new last field (the convention in both structs: the last field always
// has an offset assert, so nothing at the tail is left unpinned).
_Static_assert(sizeof(persist) == 25,
               "persist layout changed: stored settings of existing installs would be misread");
_Static_assert(offsetof(persist, track_battery) == 22,
               "persist field shifted: stored settings of existing installs would be misread");
_Static_assert(offsetof(persist, theme_mode) == 24,
               "persist field shifted: stored settings of existing installs would be misread");

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

// Same layout guards for the struct that actually keeps growing: they catch an
// insertion, a removal or a resize of a field — such a change would misread the
// settings blob already persisted on existing installs — but not a size-neutral
// reordering or substitution of equal-width fields. Covered against that are
// the field the offset assert names — the last one, weather_icons — plus
// custom_date_fmt and slots (offset and size), which tests/test_settings_layout.c
// pins; the remaining fields still need review by eye. Appending a new field at the end IS allowed
// by the contract above and legitimately trips the sizeof assert — bump the
// size constant here (and the struct's size comment) in the same commit, and
// move the trailing offset assert onto the new last field (the convention in
// both structs: the last field always has an offset assert, so nothing at the
// tail is left unpinned).
_Static_assert(sizeof(persist_adv_settings) == 244,
               "persist_adv_settings layout changed: stored settings of existing installs would be misread");
_Static_assert(offsetof(persist_adv_settings, weather_icons) == 243,
               "persist_adv_settings field shifted: stored settings of existing installs would be misread");

persist *settings_get(void);
persist_adv_settings *adv_settings_get(void);
