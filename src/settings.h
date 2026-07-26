#pragma once
#include <stdint.h>
#include <stddef.h> // offsetof, for the layout asserts below
// User settings + advanced settings. State owned by settings.c; access via
// settings_get() / adv_settings_get(). Structs are persist-backed.

// Packed-struct-on-flash contract — applies to BOTH `persist` and
// `persist_adv_settings` below. Both are written to / read from flash verbatim
// via persist_write_data()/persist_read_data(), so their byte layout is a
// compatibility contract with the settings blob already stored on a watch that
// ran an earlier build. The layout MAY be changed deliberately while the fork
// has no users whose settings are worth preserving, but every such change OTHER
// THAN APPENDING A FIELD AT THE END MUST be made safe against the stale blob,
// and what that takes differs per struct. For persist it means a bump of
// PERSIST_SETTINGS_VERSION below, so that a stale blob is rejected and replaced
// by the compiled-in defaults instead of being misread field-by-field.
// persist_adv_settings has NO version field, so a bump does nothing for it and
// there is no way to reject its stale blob: appending at the end is the only
// change it may legally receive (see rule 2 and its asserts).
// Appending is the single exception and needs no bump: persist_read_data
// tolerates a shorter stored blob, so an appended field simply keeps its
// settings.c default when an older blob is read (see rule 2).
//
// The guards below exist so that a layout change is hard to make silently,
// without that decision being taken. They are not airtight: a size-neutral edit
// — reordering fields of equal width, or substituting a field of the same width
// — passes every _Static_assert UNLESS it disturbs a field an offset assert
// names (track_battery, theme_mode, weather_icons); such a move does trip the
// build. Anywhere else in the structs a size-neutral edit is caught only by the
// offsets tests/test_settings_layout.c pins, and only for the fields listed
// there; every other field is covered by review alone.
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
//  2. Do NOT insert or remove a field in the middle: that shifts every later
//     field, so a blob persisted by an earlier build is read field-by-field into
//     the wrong members. New fields go at the END only — persist_read_data
//     tolerates a shorter stored blob, leaving appended fields at their
//     settings.c default when reading an older blob, which is why appending
//     needs no version bump. If a mid-struct change is made anyway, it is only
//     safe together with a bump of PERSIST_SETTINGS_VERSION: init() in Timely.c
//     compares the stored version against it and throws the whole blob away on a
//     mismatch. persist_adv_settings carries no version field at all, so for it
//     append-at-the-end is the only option.
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

// On-flash layout version of `persist`, and of `persist` only. Bump it in the
// SAME commit as any change to the `persist` field layout below other than
// appending at the end (bumping it does nothing for `persist_adv_settings`,
// which stores no version byte to check against): it is the
// single value that settings.c stores in `.version` and that init() in Timely.c
// requires a stored blob to carry, discarding the blob in favour of the
// compiled-in defaults otherwise. 13 = strftime_format_reserved dropped (12 was
// the last layout that still had it).
#define PERSIST_SETTINGS_VERSION 13

typedef struct persist { // 21 bytes: 21 x uint8
  uint8_t version;                // layout version, see PERSIST_SETTINGS_VERSION
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
  uint8_t track_battery;          // track battery information
  uint8_t theme;                  // color theme id (see theme.h)
  uint8_t theme_mode;             // 0 light, 1 dark, 2 auto (dark at night)
} __attribute__((__packed__)) persist;

// Layout guards for the append-only rule: they catch an insertion, a removal or
// a resize of a field, because those change sizeof and/or shift the trailing
// fields. Their job is to make such a change visible at compile time rather than
// silent: unless PERSIST_SETTINGS_VERSION is bumped along with it, the change
// makes the app misread the settings blob persisted by an earlier build.
//
// They do NOT catch most size-neutral edits: reordering fields of equal width
// (e.g. swapping inverted and day_invert), or substituting a field of the same
// width, passes every assert as long as it leaves the fields the offset asserts
// name where they are. Against a size-neutral move the only covered fields are
// the ones an offset assert names — track_battery and the last field — plus
// theme, whose offset tests/test_settings_layout.c pins; the remaining fields
// still need review by eye.
//
// Appending a new field at the end IS allowed by the contract above and
// legitimately trips the sizeof assert — bump the size constant here (and the
// struct's size comment) in the same commit, and move the trailing offset assert
// onto the new last field (the convention in both structs: the last field always
// has an offset assert, so nothing at the tail is left unpinned). No version
// bump is needed for that case.
_Static_assert(sizeof(persist) == 21,
               "persist grew or shrank: a blob persisted by an earlier build is now read with the wrong length. "
               "Appending a field at the end is the safe case - then update this constant. Anything else must "
               "also bump PERSIST_SETTINGS_VERSION so the stale blob is discarded instead of misread");
_Static_assert(offsetof(persist, track_battery) == 18,
               "persist field moved: a blob persisted by an earlier build would be read field-by-field into the "
               "wrong members. Do not just re-point this offset - either keep the layout, or bump "
               "PERSIST_SETTINGS_VERSION so the stale blob is discarded instead of misread");
_Static_assert(offsetof(persist, theme_mode) == 20,
               "persist trailing field moved: a blob persisted by an earlier build would be read field-by-field "
               "into the wrong members. Re-pointing this offset onto a field just appended at the end is the "
               "correct fix - that append is allowed and needs no version bump. For any other move, keep the "
               "layout or bump PERSIST_SETTINGS_VERSION so the stale blob is discarded instead of misread");

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
// insertion, a removal or a resize of a field — which would misread the settings
// blob persisted by an earlier build, and this struct has no version field to
// reject such a blob with — but not a size-neutral reordering or substitution of
// equal-width fields, except when it moves the field the offset assert names.
//
// Covered against that are the field the offset assert names — the last one,
// weather_icons — plus custom_date_fmt and slots (offset and size), which
// tests/test_settings_layout.c pins; the remaining fields still need review by
// eye.
//
// Appending a new field at the end IS allowed by the contract above — and, with
// no version field here, it is the ONLY layout change that can be made safe. It
// legitimately trips the sizeof assert: bump the size constant here (and the
// struct's size comment) in the same commit, and move the trailing offset assert
// onto the new last field (the convention in both structs: the last field always
// has an offset assert, so nothing at the tail is left unpinned).
_Static_assert(sizeof(persist_adv_settings) == 244,
               "persist_adv_settings grew or shrank: a blob persisted by an earlier build is now read with the "
               "wrong length. Appending a field at the end is the safe case - then update this constant. This "
               "struct has no version field, so no other layout change can be made safe");
_Static_assert(offsetof(persist_adv_settings, weather_icons) == 243,
               "persist_adv_settings trailing field moved: a blob persisted by an earlier build would be read "
               "field-by-field into the wrong members. Re-pointing this offset onto a field just appended at "
               "the end is the correct fix - appending is this struct's only legal layout change. Any other "
               "move cannot be made safe: there is no version field here to reject a stale blob with");

persist *settings_get(void);
persist_adv_settings *adv_settings_get(void);
