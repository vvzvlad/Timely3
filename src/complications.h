#pragma once
// Pure, host-testable complication text renderers + behaviour flags. Free of any
// Pebble SDK dependency (no pebble.h / ui.h / weather.h): the watch's live state
// is passed in through ComplicationCtx, and settings/locale come from the
// host-linkable settings.c / locale.c accessors. See tests/test_complications.c.
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Number of defined complication ids (valid ids are 0..COMPLICATION_COUNT-1).
#define COMPLICATION_COUNT 19

// Live watch state a renderer may need, snapshotted by the caller (Timely.c
// rebuilds it fresh at each use). Everything the renderers formerly read from
// Timely.c file-statics or pebble-tainted sources arrives here; settings_get() /
// adv_settings_get() / lang_*_get() are still called directly.
typedef struct {
  const struct tm *now;      // current local time (was currentTime)
  int8_t  tz_offset;         // quarter-hour TZ offset (was timezone_offset)
  uint8_t watch_battery;     // watch battery % (was battery_percent)
  int     phone_battery;     // phone battery % or -1 unknown (was phone_battery_percent)
  bool    bluetooth_up;      // BT link state (was bluetooth_connected)
  const char *weather_city;  // weather provider city (was weather_state()->city)
} ComplicationCtx;

// Per-complication behaviour flags — single source of truth for which slot
// content id needs a per-second tick and which is a battery reading.
#define COMP_NEEDS_SECOND_TICK  (1u << 0)
#define COMP_IS_BATTERY         (1u << 1)

// Icon a status-bar slot shows. An ENUM, not a GBitmap: no Pebble type crosses
// this boundary. Timely.c maps it to the concrete loaded glyph.
typedef enum { ICON_NONE, ICON_WATCH, ICON_PHONE, ICON_BT } IconSlot;

// Render the text for complication `id` using `ctx`. Returns a pointer to
// storage that OUTLIVES the call (a per-id static buffer, or a pointer into a
// lang/weather table) — NEVER a caller-provided stack buffer, because the view's
// text_layer_set_text() does not copy the string. Returns NULL for a hidden or
// out-of-range id (id 0, or id >= COMPLICATION_COUNT), so the caller must leave
// the layer's text unchanged in that case (matching the old `default: break`).
const char *complication_render_text(uint8_t id, const ComplicationCtx *ctx);

// Behaviour flags for `id` (0 for hidden / out-of-range).
uint8_t complication_flags(uint8_t id);

// Icon slot for `id` (ICON_NONE for text-only / out-of-range).
IconSlot complication_icon_slot(uint8_t id);
