#include <pebble.h>
#include "Timely.h"
#include "effect_layer.h"
#include "timefmt.h"
#include "layout.h"
#include "calendar.h"
#include "calendar_view.h"
#include "vibes.h"
#include "ui.h"
#include "theme.h"
#include "splash.h"
#include "suntimes.h"
#include "complications.h"
#define DEBUGLOG 0
#define TRANSLOG 0
#define CONFIG_VERSION "3.0" // informational version tag sent to the JS bundle for logging only; NOT enforced (no protocol guard was ever wired)
/*
 * If you fork this code and release the resulting app, please be considerate and change all the appropriate values in appinfo.json 
 *
 * DESCRIPTION
 *  This watchface shows the current date and current time in the top 'half',
 *    and then a small calendar w/ 3 weeks: last, current, and next week, in the bottom 'half'
 *  The statusbar at the top shows the connection status, charging, and battery level - and it will vibrate on link lost.
 *  The settings for the face are configurable using the new PebbleKit JS configuration page
 * END DESCRIPTION Section
 *
 */

Window *window;

static Layer *battery_layer;
static Layer *datetime_layer;
static TextLayer *date_layer;
static TextLayer *time_layer;
static TextLayer *week_layer;
static TextLayer *ampm_layer;
static TextLayer *ctr_r_layer; // center row (above time), right slot; date_layer is the left
static Layer *statusbar;
static Layer *slot_status;
static Layer *slot_top;
static Layer *slot_bot;
static GFont unifont_16;
static GFont unifont_16_bold;
// Whether unifont_16/_bold currently hold a CUSTOM font (vs. the stage-12
// system-font fallback used when the custom load failed). Only custom handles
// may be passed to fonts_unload_custom_font at window_unload; unloading a
// system handle is a bug. Set true only inside the custom-load success branch.
static bool unifont_16_is_custom = false;
static bool unifont_16_bold_is_custom = false;
GFont cal_normal;
GFont cal_bold;
GFont climacons;
static int s_climacons_size = 0; // loaded climacons px; 0 = not loaded yet

// (Re)load the climacons font when the band-driven size class changes. A
// single handle stays live so RAM never holds two copies; reloads only happen
// on settings toggles that resize the time band.
static void ensure_climacons(int size) {
  if (size == s_climacons_size) { return; }
  // Load into a temp first; only unload the old handle and adopt the new size
  // when the load succeeds. On OOM (real at 48px) fonts_load_custom_font returns
  // NULL, so keep the currently-loaded font rather than drawing through NULL.
  GFont loaded = fonts_load_custom_font(resource_get_handle(
    size == 48 ? RESOURCE_ID_FONT_CLIMACONS_48 :
    size == 40 ? RESOURCE_ID_FONT_CLIMACONS_40 :
                 RESOURCE_ID_FONT_CLIMACONS_28));
  if (loaded == NULL) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "ensure_climacons: font load failed for size %d, keeping old", size);
    return;
  }
  if (climacons) { fonts_unload_custom_font(climacons); }
  climacons = loaded;
  s_climacons_size = size;
  weather_set_glyph_size(size);
}

static BitmapLayer *bmp_charging_layer;
// Wide screens: charging/DND/hourvibe shown side by side under the clock
// (narrow screens keep the single statusbar icon in bmp_charging_layer).
static BitmapLayer *tray_layers[3];
static GBitmap *image_charging_icon;
static GBitmap *image_hourvibe_icon;
static GBitmap *image_dnd_icon;
static BitmapLayer *bmp_phone_layer;   // phone icon next to the phone-battery readout
static GBitmap *image_phone_icon;
static BitmapLayer *bmp_watch_layer;   // watch icon next to the watch battery
static GBitmap *image_watch_icon;
static GBitmap *image_bt16_icon;       // 16px Bluetooth glyph for the status-bar slots
static TextLayer *text_connection_layer; // status-bar slot, left (text)
static TextLayer *text_battery_layer;

static EffectLayer *inverter_layer;
static EffectLayer *battery_meter_layer; // proportional fill for the LEFT status battery (bar style)
static EffectLayer *batt_fill_r;         // proportional fill for the RIGHT status battery (bar style)

// battery info, instantiate to 'worst scenario' to prevent false hopes
static uint8_t battery_percent = 10;
static bool battery_charging = false;
static bool battery_plugged = false;
static int phone_battery_percent = -1; // -1 = unknown (phone hasn't reported / API unavailable)
static AppTimer *battery_sending = NULL;
static AppTimer *timezone_request = NULL;
static AppTimer *weather_request = NULL;
static AppTimer *bottom_toggle = NULL;
// connected info
static bool bluetooth_connected = false;
// suppress vibration
static bool vibe_suppression = true;
int8_t timezone_offset = TIMEZONE_UNINITIALIZED;
// Own backing store for "now" so currentTime never aliases libc's static
// localtime()/gmtime() buffer (which any future date call would overwrite).
// currentTime points at our own storage (was libc's static localtime buffer, which
// any future localtime/gmtime call would silently overwrite). It is therefore never
// NULL now — the old `if (!currentTime)` fallbacks downstream are vestigial but harmless.
static struct tm s_now;
struct tm *currentTime = &s_now;
static int8_t seconds_shown = 0;
static bool dnd_period_active = false;
static bool vibe_period_active = false;
static bool showing_statusbar = true;

// define the persistent storage key(s)
#define PK_SETTINGS      0
#define PK_LANG_GEN      1 // updated    v10->v11 (utf8)
#define PK_LANG_DATETIME 2 // deprecated v10->v11 (utf8)
#define PK_LANG_MONTHS   3
#define PK_LANG_DAYS     4
#define PK_DEBUGGING     5
#define PK_ADV_SETTINGS  6
#define PK_SETTINGS_EXT  7 // versioned home for all NEW advanced settings (PK_ADV_SETTINGS is frozen at 244B)

// define the appkeys used for appMessages
#define AK_STYLE_INV     0
#define AK_STYLE_DAY_INV 1
#define AK_STYLE_GRID    2
#define AK_VIBE_HOUR     3
#define AK_INTL_DOWO     4
#define AK_INTL_FMT_DATE 5 // INCOMPLETE
#define AK_STYLE_AM_PM   6
// 7 (AK_STYLE_DAY) retired — the middle "day" slot was removed; wire number reserved.
#define AK_STYLE_WEEK    8
#define AK_INTL_FMT_WEEK 9
#define AK_DEBUGGING_ON          10
#define AK_VIBE_PAT_DISCONNECT   11
#define AK_VIBE_PAT_CONNECT      12
#define AK_STRFTIME_FORMAT       13 // custom strftime format string, used when date_format == 255
#define AK_TRACK_BATTERY         14 // enable battery-history tracking (gates battery_status_send)
#define AK_LANGUAGE              15
#define AK_DEBUGLANG_ON          16
#define AK_CAL_WEEK_PATTERN      17
// 18/19/20 (AK_INV_SLOT_STAT/TOP/BOT) retired — write-only handlers removed; wire numbers reserved.
#define AK_SHOW_STAT_BAR         21
#define AK_SHOW_STAT_BATT        22
// 23 (AK_SHOW_DATE) retired — write-only handler removed; wire number reserved.
#define AK_DND_START             24
#define AK_DND_STOP              25
#define AK_DND_NOACCEL           26 // DND mode: 0 off, 1 follow watch Quiet Time, 2 app window
#define AK_VIBE_START            27
#define AK_VIBE_STOP             28
#define AK_VIBE_DAYS             29 // days-of-week mask for hourly vibration
// 30..34 (AK_IDLE_REMINDER/VIBE_PATT/MESSAGE/START/STOP) retired — handlers removed; wire numbers reserved.
#define AK_WEATHER_FMT           35
#define AK_WEATHER_UPDATE        36
#define AK_THEME                 37
#define AK_THEME_MODE            38

#define AK_MESSAGE_TYPE          99
#define AK_SEND_BATT_PERCENT    100
#define AK_SEND_BATT_CHARGING   101
#define AK_SEND_BATT_PLUGGED    102
#define AK_TIMEZONE_OFFSET      103
#define AK_SEND_WATCH_VERSION   104
#define AK_SEND_CONFIG_VERSION  105
#define AK_REQUEST_WEATHER      106
#define AK_WEATHER_TEMP         107
#define AK_WEATHER_COND         108
#define AK_WEATHER_CITY         109
#define AK_PHONE_BATTERY        110
#define AK_WEATHER_LAT          111
#define AK_WEATHER_LON          112
#define AK_CLOCK2_TZ            113
#define AK_SLOT_STAT_L         114
#define AK_SLOT_STAT_R         115
#define AK_SLOT_CTR_L          116
#define AK_SLOT_CTR_R          117
#define AK_BATT_STYLE          118
#define AK_WEATHER_ICONS       119

#define AK_TRANS_ABBR_SUNDAY    500
#define AK_TRANS_ABBR_MONDAY    501
#define AK_TRANS_ABBR_TUESDAY   502
#define AK_TRANS_ABBR_WEDSDAY   503
#define AK_TRANS_ABBR_THURSDAY  504
#define AK_TRANS_ABBR_FRIDAY    505
#define AK_TRANS_ABBR_SATURDAY  506
#define AK_TRANS_JANUARY    507
#define AK_TRANS_FEBRUARY   508
#define AK_TRANS_MARCH      509
#define AK_TRANS_APRIL      510
#define AK_TRANS_MAY        511
#define AK_TRANS_JUNE       512
#define AK_TRANS_JULY       513
#define AK_TRANS_AUGUST     514
#define AK_TRANS_SEPTEMBER  515
#define AK_TRANS_OCTOBER    516
#define AK_TRANS_NOVEMBER   517
#define AK_TRANS_DECEMBER   518
#define AK_TRANS_ALARM      519 // UNUSED
#define AK_TRANS_SUNDAY     520
#define AK_TRANS_MONDAY     521
#define AK_TRANS_TUESDAY    522
#define AK_TRANS_WEDSDAY    523
#define AK_TRANS_THURSDAY   524
#define AK_TRANS_FRIDAY     525
#define AK_TRANS_SATURDAY   526
#define AK_TRANS_CONNECTED     527
#define AK_TRANS_DISCONNECTED  528
#define AK_TRANS_TIME_AM    529
#define AK_TRANS_TIME_PM    530
#define AK_TRANS_ABBR_JANUARY    531
#define AK_TRANS_ABBR_FEBRUARY   532
#define AK_TRANS_ABBR_MARCH      533
#define AK_TRANS_ABBR_APRIL      534
#define AK_TRANS_ABBR_MAY        535
#define AK_TRANS_ABBR_JUNE       536
#define AK_TRANS_ABBR_JULY       537
#define AK_TRANS_ABBR_AUGUST     538
#define AK_TRANS_ABBR_SEPTEMBER  539
#define AK_TRANS_ABBR_OCTOBER    540
#define AK_TRANS_ABBR_NOVEMBER   541
#define AK_TRANS_ABBR_DECEMBER   542

// primary coordinates
static int DEVICE_WIDTH  = 144; // recomputed per-screen by compute_layout()
static int DEVICE_HEIGHT = 168;
static int LAYOUT_STAT = 0;
static int LAYOUT_SLOT_TOP = 24;
static int LAYOUT_SLOT_BOT = 96;
static int LAYOUT_SLOT_HEIGHT = 72;     // top slot (time/date) height
static int LAYOUT_SLOT_BOT_HEIGHT = 72; // bottom slot (calendar) height — differs from the top once capped
static int STAT_BATT_LEFT = 96; // right-aligned at runtime
static int STAT_CHRG_ICON_LEFT = 76; // right-aligned at runtime
#define STAT_CHRG_ICON_TOP    2

void apply_center(void); // center row (above time): 1 slot full / 2 slots halves
void apply_bottom(void); // bottom row (above calendar): 1 slot full / 2 slots halves

// relative coordinates (relative to SLOTs)
static int REL_CLOCK_DATE_LEFT = 2;
static int REL_CLOCK_DATE_TOP = 0;
static int REL_CLOCK_DATE_HEIGHT = 30;
static int REL_CLOCK_DATE_WIDTH = 140;
static int REL_CLOCK_TIME_LEFT = 0;
static int REL_CLOCK_TIME_TOP = 7;
static int REL_CLOCK_TIME_HEIGHT = 60;
static int REL_CLOCK_SUBTEXT_TOP = 56;

// Recompute the runtime layout for the actual screen. Bands are adaptive: a row
// only reserves height when it is in use, so disabling rows grows the rest.
static void compute_layout(int w, int h) {
  int has_top    = adv_settings_get()->showStatus != 0;
  int has_center = settings_get()->slot_ctr_l || settings_get()->slot_ctr_r;
  int has_bottom = settings_get()->show_week  || settings_get()->show_am_pm;
  TimelyLayout L = layout_compute_rows(w, h, has_top, has_center, has_bottom);
  DEVICE_WIDTH = w; DEVICE_HEIGHT = h;
  LAYOUT_STAT = L.statusbar.y;
  LAYOUT_SLOT_TOP = L.slot_top.y;
  LAYOUT_SLOT_BOT = L.slot_bot.y;
  LAYOUT_SLOT_HEIGHT = L.slot_top.h;
  LAYOUT_SLOT_BOT_HEIGHT = L.slot_bot.h;
  STAT_BATT_LEFT = L.battery.x;
  STAT_CHRG_ICON_LEFT = L.chrg_icon_x;
  REL_CLOCK_DATE_LEFT = L.clock_date.x;
  REL_CLOCK_DATE_TOP = L.clock_date.y;
  REL_CLOCK_DATE_HEIGHT = L.clock_date.h;
  REL_CLOCK_DATE_WIDTH = L.clock_date.w;
  REL_CLOCK_TIME_LEFT = L.clock_time.x;
  REL_CLOCK_TIME_TOP = L.clock_time.y;
  REL_CLOCK_TIME_HEIGHT = L.clock_time.h;
  REL_CLOCK_SUBTEXT_TOP = L.subtext_top;
  layout_store(L);
}

/*
*/






/*
char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    default: return "UNKNOWN ERROR";
  }
}
*/


struct tm *get_time() {
    time_t tt = time(0);
    return localtime(&tt);
}



// Highlight colors for the current day's calendar cell. On color platforms the
// cell gets an accent fill (tune GColorJaegerGreen to taste); on B&W it falls
// back to the classic white-on-black inversion (identical to setInvColors).






// Snapshot the live watch state the pure complication renderers read (see
// src/complications.c). Rebuilt fresh at each use so the values are current; it
// is just field copies, so this is cheap.
static ComplicationCtx comp_ctx(void) {
  return (ComplicationCtx){
    .now           = currentTime,
    .tz_offset     = timezone_offset,
    .watch_battery = battery_percent,
    .phone_battery = phone_battery_percent,
    .bluetooth_up  = bluetooth_connected,
    .weather_city  = weather_state()->city,
  };
}

void update_date_text() {
    apply_center(); // the date lives in the center row now (default left slot)
}

void update_time_text() {
  // Need to be static because used by the system later.
  static char time_text[] = "00:00";

  char *time_format;

  if (clock_is_24h_style()) {
    time_format = "%R";
  } else {
    time_format = "%I:%M";
  }

  strftime(time_text, sizeof(time_text), time_format, currentTime);

  // Kludge to handle lack of non-padded hour format string
  // for twelve hour clock.
  if (!clock_is_24h_style() && (time_text[0] == '0')) {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  }

  // I would love to just use clock_copy_time_string, but it refuses to center properly in 12-hour time (see Kludge above).
  //clock_copy_time_string(time_text, sizeof(time_text));
  text_layer_set_text(time_layer, time_text);

}

// All six configurable slots ("complications") share the same content menu.
// The text renderers + the id->flags/icon table now live in the pure,
// host-testable src/complications.c; this thin glue snapshots the live watch
// state and asks the table for the string. A NULL return means hidden/unknown
// (id 0 or out of range): leave the layer's text unchanged, matching the old
// `default: break`. Keep the id values in sync with src/js/config.js.
void update_slot_text(TextLayer *layer, uint8_t content) {
  ComplicationCtx ctx = comp_ctx();
  const char *t = complication_render_text(content, &ctx);
  if (t) { text_layer_set_text(layer, t); }
}

// Lay out a two-slot complication row at (top,h): both set -> halves; one set ->
// full-width centered in the left layer; none -> both hidden.
static void layout_two_slots(TextLayer *l, TextLayer *r, uint8_t cl, uint8_t cr, int top, int h) {
  Layer *ll = text_layer_get_layer(l), *rl = text_layer_get_layer(r);
  if (cl && cr) {
    SlotPair p = layout_slot_pair(DEVICE_WIDTH);
    layer_set_frame(ll, GRect(p.left.x,  top, p.left.w,  h)); text_layer_set_text_alignment(l, GTextAlignmentLeft);
    layer_set_frame(rl, GRect(p.right.x, top, p.right.w, h)); text_layer_set_text_alignment(r, GTextAlignmentRight);
    layer_set_hidden(ll, false); layer_set_hidden(rl, false);
    update_slot_text(l, cl); update_slot_text(r, cr);
  } else if (cl || cr) {
    SlotSpan f = layout_slot_full(DEVICE_WIDTH);
    layer_set_frame(ll, GRect(f.x, top, f.w, h)); text_layer_set_text_alignment(l, GTextAlignmentCenter);
    layer_set_hidden(ll, false); layer_set_hidden(rl, true);
    update_slot_text(l, cl ? cl : cr);
  } else {
    layer_set_hidden(ll, true); layer_set_hidden(rl, true);
  }
}

void apply_center(void) {
  if (!date_layer || !ctr_r_layer) { return; } // not built yet
  int voff = (strcmp(lang_gen_get()->language, "RU") == 0)
             ? (showing_statusbar ? -4 : 0) : (showing_statusbar ? -9 : -5);
  layout_two_slots(date_layer, ctr_r_layer, settings_get()->slot_ctr_l, settings_get()->slot_ctr_r,
                   REL_CLOCK_DATE_TOP + voff, REL_CLOCK_DATE_HEIGHT);
}

void apply_bottom(void) {
  if (!week_layer || !ampm_layer) { return; }
  int voff = (strcmp(lang_gen_get()->language, "RU") == 0) ? -2 : 0;
  layout_two_slots(week_layer, ampm_layer, settings_get()->show_week, settings_get()->show_am_pm,
                   REL_CLOCK_SUBTEXT_TOP + voff, 22);
}

// The two status-bar slots also draw from the unified menu; battery/connection
// content additionally shows a 16px icon (the only contents with a glyph).
static GBitmap *stat_slot_icon(uint8_t content) {
  switch (complication_icon_slot(content)) {
  case ICON_WATCH: return image_watch_icon;  // Watch battery
  case ICON_PHONE: return image_phone_icon;  // Phone battery
  case ICON_BT:    return image_bt16_icon;   // Bluetooth
  case ICON_NONE:  return NULL;              // text-only complication
  }
  return NULL;
}

// complication_flags() / COMP_* flags now live in src/complications.c (the
// single source of truth for the per-second-tick and battery ids).
static bool is_battery_content(uint8_t c) { return (complication_flags(c) & COMP_IS_BATTERY) != 0; }
static bool batt_style_is_bar(uint8_t s) { return s == 0 || s == 3; } // 0 bar, 3 bar+icon

// Whether the charging/DND/hourvibe icon is currently visible; the right
// battery bar yields 22px to it (see batt_box_geom).
static bool s_status_icon_shown = false;

// Battery-bar box geometry for one side. with_icon (bar+icon style) reserves a
// leading 18px for the glyph on the outer edge and narrows the box.
static void batt_box_geom(bool is_right, bool with_icon, int *bx, int *bw) {
  layout_batt_box(DEVICE_WIDTH, is_right, with_icon, s_status_icon_shown, bx, bw);
}

// Render one status-bar slot. Battery content honours batt_style: bar (outline
// drawn by battery_layer with the % centred inside), text-only, or icon+text.
// Other content shows its icon (if any) on the outer edge with the value beside.
static void apply_stat_slot(TextLayer *txt, BitmapLayer *icon, uint8_t content, bool is_right) {
  int half = DEVICE_WIDTH / 2;
  Layer *il = bitmap_layer_get_layer(icon);
  Layer *tl = text_layer_get_layer(txt);
  uint8_t bstyle = settings_get()->batt_style;

  if (is_battery_content(content) && batt_style_is_bar(bstyle)) { // BAR (+ optional icon)
    bool with_icon = (bstyle == 3);
    int bx, bw; batt_box_geom(is_right, with_icon, &bx, &bw);
    if (with_icon) {
      bitmap_layer_set_bitmap(icon, stat_slot_icon(content));
      layer_set_hidden(il, false);
      layer_set_frame(il, GRect(is_right ? DEVICE_WIDTH - 18 : 2, 4, 16, 16));
    } else {
      layer_set_hidden(il, true);
    }
    text_layer_set_text_alignment(txt, GTextAlignmentCenter);
    layer_set_frame(tl, GRect(bx, 0, bw, 20)); // % in the box, nudged up (was clipping low)
    update_slot_text(txt, content);
    return;
  }

  GBitmap *bmp = (is_battery_content(content) && bstyle == 1) ? NULL : stat_slot_icon(content);
  if (bmp) {                                               // ICON + value
    bitmap_layer_set_bitmap(icon, bmp);
    layer_set_hidden(il, false);
    if (is_right) {
      layer_set_frame(il, GRect(DEVICE_WIDTH - 18, 4, 16, 16));
      text_layer_set_text_alignment(txt, GTextAlignmentRight);
      layer_set_frame(tl, GRect(half, 2, DEVICE_WIDTH - 20 - half, 22));
    } else {
      layer_set_frame(il, GRect(2, 4, 16, 16));
      text_layer_set_text_alignment(txt, GTextAlignmentLeft);
      layer_set_frame(tl, GRect(20, 2, half - 22, 22));
    }
  } else {                                                 // TEXT only
    layer_set_hidden(il, true);
    if (is_right) {
      text_layer_set_text_alignment(txt, GTextAlignmentRight);
      layer_set_frame(tl, GRect(half, 2, DEVICE_WIDTH - 2 - half, 22));
    } else {
      text_layer_set_text_alignment(txt, GTextAlignmentLeft);
      layer_set_frame(tl, GRect(2, 2, half - 4, 22));
    }
  }
  update_slot_text(txt, content);
}

// A lone status complication is centred across the whole bar (the row is
// dynamic like the others: one slot = centre, two = halves). Rendered into the
// left layer/icon; the right slot is hidden by the caller. Bar style keeps its
// side box, so it is handled by the normal two-slot path instead.
static void apply_stat_single(uint8_t content) {
  TextLayer *txt = text_connection_layer;
  Layer *il = bitmap_layer_get_layer(bmp_phone_layer);
  Layer *tl = text_layer_get_layer(txt);
  GBitmap *bmp = (is_battery_content(content) && settings_get()->batt_style == 1) ? NULL : stat_slot_icon(content);
  if (bmp) {                                  // icon + value, group centred
    int gx = DEVICE_WIDTH / 2 - 27;           // ~half the icon+value group width
    bitmap_layer_set_bitmap(bmp_phone_layer, bmp);
    layer_set_hidden(il, false);
    layer_set_frame(il, GRect(gx, 4, 16, 16));
    text_layer_set_text_alignment(txt, GTextAlignmentLeft);
    layer_set_frame(tl, GRect(gx + 18, 2, DEVICE_WIDTH - (gx + 18) - 2, 22));
  } else {                                    // text only, centred full width
    layer_set_hidden(il, true);
    text_layer_set_text_alignment(txt, GTextAlignmentCenter);
    layer_set_frame(tl, GRect(2, 2, DEVICE_WIDTH - 4, 22));
  }
  update_slot_text(txt, content);
}

// Position the proportional fill (an invert effect, raised above the % text so
// the digits stay readable over the filled part) inside one battery box.
static void set_batt_fill(EffectLayer *fl, int box_x, int box_w, int pct, bool show) {
  if (!fl) { return; }
  Layer *l = effect_layer_get_layer(fl);
  if (!show || pct < 0) { layer_set_hidden(l, true); return; }
  if (pct > 100) { pct = 100; }
  int fillw = (box_w - 2) * pct / 100;
  if (fillw < 1 && pct > 0) { fillw = 1; }
  // Full interior height (box is y4..20) so the fill has no empty top/bottom
  // strip and covers the whole % text, keeping it readable when inverted.
  layer_set_frame(l, GRect(box_x + 1, 5, fillw, 14));
  layer_set_hidden(l, false);
  layer_add_child(statusbar, l); // raise to top so it inverts the box + % text
}

// Re-apply both status-bar slots (content + icon + position). Cheap; call it
// whenever the underlying data (battery, connection) or the config changes.
void refresh_stat_slots(void) {
  if (!text_connection_layer || !text_battery_layer) { return; } // not built yet
  uint8_t cl = settings_get()->slot_stat_l, cr = settings_get()->slot_stat_r;
  bool bar = batt_style_is_bar(settings_get()->batt_style);
  bool with_icon = settings_get()->batt_style == 3;
  // One slot set (and not the side-drawn bar style) -> centre it; else two halves.
  bool single = (cl && !cr) || (!cl && cr);
  if (single && !bar) {
    apply_stat_single(cl ? cl : cr);
    layer_set_hidden(text_layer_get_layer(text_battery_layer), true);
    layer_set_hidden(bitmap_layer_get_layer(bmp_watch_layer), true);
  } else {
    apply_stat_slot(text_connection_layer, bmp_phone_layer, cl, false);
    apply_stat_slot(text_battery_layer,    bmp_watch_layer, cr, true);
  }

  bool bars = bar && (is_battery_content(cl) || is_battery_content(cr));
  if (battery_layer) { // battery_layer draws the bar outlines
    layer_set_hidden(battery_layer, !bars);
    if (bars) { layer_mark_dirty(battery_layer); }
  }
  if (bmp_charging_layer) { // status icon dodges the right-slot battery bar
    bool right_bar = bar && is_battery_content(cr);
    layer_set_frame(bitmap_layer_get_layer(bmp_charging_layer),
                    GRect(chrg_icon_x_for(DEVICE_WIDTH, right_bar), STAT_CHRG_ICON_TOP, 20, 20));
  }
  int lx, lw, rx, rw;
  batt_box_geom(false, with_icon, &lx, &lw);
  batt_box_geom(true,  with_icon, &rx, &rw);
  set_batt_fill(battery_meter_layer, lx, lw,
    is_battery_content(cl) ? (cl == 15 ? battery_percent : phone_battery_percent) : -1,
    bar && is_battery_content(cl));
  set_batt_fill(batt_fill_r, rx, rw,
    is_battery_content(cr) ? (cr == 15 ? battery_percent : phone_battery_percent) : -1,
    bar && is_battery_content(cr));
}

void position_connection_layer() {
  // Status-bar slots own their own geometry now; re-apply them (also picks up
  // the current language/font via the value text).
  refresh_stat_slots();
}

void position_date_layer() {
  apply_center(); // center row owns the date band now
}

// Clock font scales to the time band height; Roboto (wide) only on wide screens
// where it won't collide with the weather to its left. Returns a non-const
// FONT_KEY string literal so it can pass to set_layer_attr_sfont(char*).
char *time_font_key(void) {
  switch (clock_font_for(DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT)) {
  case CLOCK_FONT_ROBOTO_49: return FONT_KEY_ROBOTO_BOLD_SUBSET_49;
  case CLOCK_FONT_LECO_42:   return FONT_KEY_LECO_42_NUMBERS;
  case CLOCK_FONT_LECO_38:   return FONT_KEY_LECO_38_BOLD_NUMBERS;
  case CLOCK_FONT_LECO_32:   return FONT_KEY_LECO_32_BOLD_NUMBERS;
  default:                   return FONT_KEY_LECO_28_LIGHT_NUMBERS;
  }
}

// Wide screens: pack the active status icons (fixed order charging/DND/
// hourvibe) from the right edge, seated on the bottom of the time band.
static void refresh_status_tray(void) {
  if (!tray_layers[0]) { return; } // narrow screens / not built yet
  bool active[3] = {
    battery_charging,
    dnd_period_active,
    !battery_plugged && settings_get()->vibe_hour && vibe_period_active,
  };
  int y = REL_CLOCK_TIME_TOP + REL_CLOCK_TIME_HEIGHT - 15;
  int idx = 0;
  for (int i = 0; i < 3; i++) {
    Layer *l = bitmap_layer_get_layer(tray_layers[i]);
    layer_set_hidden(l, !active[i]);
    if (active[i]) {
      layer_set_frame(l, GRect(status_tray_x(DEVICE_WIDTH, idx), y, 20, 20));
      idx++;
    }
  }
}

void position_time_layer() {
  // The clock and the weather both live in the time band; seat them on it so the
  // weather is vertically centred against the time instead of floating above.
  ensure_climacons(weather_glyph_size_for(DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT));
  layer_set_frame( text_layer_get_layer(time_layer), GRect(REL_CLOCK_TIME_LEFT, REL_CLOCK_TIME_TOP, DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT) );
  weather_set_frame( GRect(REL_CLOCK_TIME_LEFT, REL_CLOCK_TIME_TOP, DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT) );
  refresh_status_tray(); // the tray sits on the band's bottom edge
}

void update_datetime_subtext() {
    apply_bottom();
    position_time_layer();
}

void datetime_layer_update_callback(Layer *me, GContext* ctx) {
    // No-op: layout/content is now driven from the tick + event handlers
    // (apply_center/apply_bottom/update_time_text), not the draw callback.
    // This proc issues no graphics_* calls, so drawing nothing is safe.
    (void)me; (void)ctx;
}

void statusbar_visible() {
  if (adv_settings_get()->showStatus == 0) {
    showing_statusbar = false;
  } else if (adv_settings_get()->showStatus == 1) {
    showing_statusbar = true;
  } else if (battery_percent <= adv_settings_get()->showStatusBat) {
    showing_statusbar = true;
  } else {
    showing_statusbar = false;
  }
}

void toggle_weather() {
  if (adv_settings_get()->weather_update) {
    //if (!showing_statusbar) { text_layer_set_text_alignment(date_layer, GTextAlignmentRight); }
    text_layer_set_text_alignment(time_layer, GTextAlignmentRight);
    weather_set_hidden(false);
  } else {
    weather_set_hidden(true);
    text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
    //text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
  }
}

void toggle_statusbar() {
  if (showing_statusbar) {
    // status
    layer_set_hidden(statusbar, false);
    // date / center-left (alignment owned by apply_center)
    layer_add_child(datetime_layer, text_layer_get_layer(date_layer));
    // icon(s) — NULL on wide screens (not created there); guard the re-parent.
    if (bmp_charging_layer) { layer_add_child(statusbar, bitmap_layer_get_layer(bmp_charging_layer)); }
    layer_add_child(statusbar, battery_layer);
    // Keep the slot value text above battery_layer (which draws the bar-style
    // outline) so re-parenting here doesn't bury the percentage under the box.
    // The battery fills are (re)stacked on top by refresh_stat_slots() afterwards.
    if (text_connection_layer) { layer_add_child(statusbar, text_layer_get_layer(text_connection_layer)); }
    if (text_battery_layer)    { layer_add_child(statusbar, text_layer_get_layer(text_battery_layer)); }
  } else {
    // status
    layer_set_hidden(statusbar, true);
    // When the bar is only auto-hidden (when-low) the 24px strip is still
    // reserved, so move the date up to use it; when the bar is fully disabled
    // (showStatus 0) there is no strip, so keep the date in the center row to
    // avoid clipping it to a zero-height parent.
    if (adv_settings_get()->showStatus != 0) {
      layer_add_child(slot_status, text_layer_get_layer(date_layer));
    } else {
      layer_add_child(datetime_layer, text_layer_get_layer(date_layer));
    }
    // icon(s) — NULL on wide screens (not created there); guard the re-parent.
    if (bmp_charging_layer) { layer_add_child(datetime_layer, bitmap_layer_get_layer(bmp_charging_layer)); }
    layer_add_child(datetime_layer, battery_layer);
  }
  position_date_layer();
}


// Draw a battery outline + nib for the "bar with %" style; the percentage text
// is the slot's own TextLayer, centred inside this box.
static void draw_batt_box(GContext *ctx, int x, int w, bool low, bool nib_left) {
  graphics_context_set_stroke_color(ctx, low ? theme_palette().warn : theme_palette().fg);
  graphics_draw_rect(ctx, GRect(x, 4, w, 16));
  // The left slot's nib faces left (toward its icon) so the two read as one unit.
  graphics_draw_rect(ctx, GRect(nib_left ? x - 2 : x + w, 4 + 5, 2, 6));
}

void battery_layer_update_callback(Layer *me, GContext* ctx) {
  (void)me;
  if (!batt_style_is_bar(settings_get()->batt_style)) { return; } // only bar styles draw a box
  setColors(ctx);
  bool with_icon = settings_get()->batt_style == 3;
  uint8_t cl = settings_get()->slot_stat_l, cr = settings_get()->slot_stat_r;
  int bx, bw;
  if (is_battery_content(cl)) {
    int pct = (cl == 15) ? battery_percent : phone_battery_percent;
    batt_box_geom(false, with_icon, &bx, &bw);
    draw_batt_box(ctx, bx, bw, pct >= 0 && pct <= 20, true);  // left: nib toward the icon
  }
  if (is_battery_content(cr)) {
    int pct = (cr == 15) ? battery_percent : phone_battery_percent;
    batt_box_geom(true, with_icon, &bx, &bw);
    draw_batt_box(ctx, bx, bw, pct >= 0 && pct <= 20, false); // right: nib on the outer edge
  }
}

// Return a NUL-terminated C string from a dictionary tuple, or NULL if the
// tuple is missing / not a cstring / empty. Forces a terminator at the last
// byte of the tuple payload so a maxed multibyte string can never be read
// past its declared length. (audit M5)
static const char *tuple_str(Tuple *t) {
  if (t == NULL || t->type != TUPLE_CSTRING || t->length == 0) { return NULL; }
  // Index through a plain char* rather than the char[0] flexible-array member
  // directly, so -Wzero-length-bounds (strict-check -Werror) stays quiet; the
  // runtime length is a real, payload-bounded value the compiler cannot see.
  char *cs = (char *)t->value->cstring;
  cs[t->length - 1] = '\0';
  return cs;
}

static void request_weather(void *data) {
  weather_request = NULL; // the AppTimer has already fired; clear the handle up front so early returns cannot leave it stale (audit H4)
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Requesting Weather [%d/%d]", weather_state()->failures, weather_state()->requests); }
  weather_state()->condition[0] = 'h'; weather_state()->condition[1] = '\0'; // h = updating 'cloud' icon
  weather_mark_dirty(); // update UI element to indicate we're fetching weather...
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (iter == NULL) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "iterator is null: %d", result); }
    return;
  }
  if (dict_write_uint8(iter, AK_MESSAGE_TYPE, AK_REQUEST_WEATHER) != DICT_OK) {
    return;
  }
  if (dict_write_uint8(iter, AK_WEATHER_FMT, adv_settings_get()->weather_format) != DICT_OK) {
    return;
  }
  app_message_outbox_send();
  weather_state()->requests++;
}

static void request_timezone(void *data) {
  timezone_request = NULL; // the AppTimer has already fired; clear the handle up front so early returns cannot leave it stale (audit H4)
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (iter == NULL) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "iterator is null: %d", result); }
    return;
  }
  if (dict_write_uint8(iter, AK_MESSAGE_TYPE, AK_TIMEZONE_OFFSET) != DICT_OK) {
    return;
  }
  app_message_outbox_send();
}

static void watch_version_send(void *data) {
  DictionaryIterator *iter;

  AppMessageResult result = app_message_outbox_begin(&iter);

  if (iter == NULL) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "iterator is null: %d", result); }
    return;
  }

  if (result != APP_MSG_OK) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Dict write failed to open outbox: %d", (AppMessageResult) result); }
    return;
  }

  if (dict_write_uint8(iter, AK_MESSAGE_TYPE, AK_SEND_WATCH_VERSION) != DICT_OK) {
    return;
  }
  if (dict_write_uint8(iter, AK_SEND_WATCH_VERSION, settings_get()->version) != DICT_OK) {
    return;
  }
  if (dict_write_cstring(iter, AK_SEND_CONFIG_VERSION, CONFIG_VERSION) != DICT_OK) {
    return;
  }
  app_message_outbox_send();
}

static void battery_status_send(void *data) {
  battery_sending = NULL; // the AppTimer has already fired; clear the handle up front so early returns cannot leave it stale (audit H4)
  static uint8_t sent_battery_percent = 10;
  static bool sent_battery_charging = false;
  static bool sent_battery_plugged = false;
  if (!settings_get()->track_battery) {
    return; // if user has chosen not to track battery (saves power w/ appmessages)
  }
  if ( (battery_percent  == sent_battery_percent  )
     & (battery_charging == sent_battery_charging )
     & (battery_plugged  == sent_battery_plugged  ) ) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "repeat battery reading"); }
    return; // no need to resend the same value
  }
  DictionaryIterator *iter;

  AppMessageResult result = app_message_outbox_begin(&iter);

  if (iter == NULL) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "iterator is null: %d", result); }
    return;
  }

  if (result != APP_MSG_OK) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Dict write failed to open outbox: %d", (AppMessageResult) result); }
    return;
  }

  if (dict_write_uint8(iter, AK_MESSAGE_TYPE, AK_SEND_BATT_PERCENT) != DICT_OK) {
    return;
  }
  if (dict_write_uint8(iter, AK_SEND_BATT_PERCENT, battery_percent) != DICT_OK) {
    return;
  }
  if (dict_write_uint8(iter, AK_SEND_BATT_CHARGING, battery_charging ? 1: 0) != DICT_OK) {
    return;
  }
  if (dict_write_uint8(iter, AK_SEND_BATT_PLUGGED, battery_plugged ? 1: 0) != DICT_OK) {
    return;
  }
  app_message_outbox_send();
  sent_battery_percent  = battery_percent;
  sent_battery_charging = battery_charging;
  sent_battery_plugged  = battery_plugged;
}

void set_status_charging_icon() {
  if (layout_is_wide(DEVICE_WIDTH)) { refresh_status_tray(); return; } // wide: tray under the clock
  // this icon shows either DND, hourly vibration, or charging...
  bool chrg_shown = true;
  if (battery_charging) { // charging
    bitmap_layer_set_bitmap(bmp_charging_layer, image_charging_icon);
  } else if (dnd_period_active) {
    bitmap_layer_set_bitmap(bmp_charging_layer, image_dnd_icon);
  } else if (!battery_plugged && settings_get()->vibe_hour && vibe_period_active) {
    bitmap_layer_set_bitmap(bmp_charging_layer, image_hourvibe_icon);
  } else { // plugged-but-full, or normal wear: nothing to show
    chrg_shown = false;
  }
  layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), !chrg_shown);
  if (chrg_shown != s_status_icon_shown) { // bars re-geom around the icon
    s_status_icon_shown = chrg_shown;
    refresh_stat_slots();
  }
}

static void toggle_slot_bottom(void *data) {
  watch_version_send(NULL); // no guarantee the JS is there to receive me...
  static int shown = -1; // 0 = splash, 1 = calendar
  int which = (int)(intptr_t)data;
  if (shown == 0) { splash_set_hidden(true); } else if (shown == 1) { calendar_set_hidden(true); }
  shown = which;
  if (shown == 0) { splash_set_hidden(false); } else if (shown == 1) { calendar_set_hidden(false); }
  bottom_toggle = NULL;
}

static void handle_battery(BatteryChargeState charge_state) {
  battery_percent = charge_state.charge_percent;
  battery_charging = charge_state.is_charging;
  battery_plugged = charge_state.is_plugged;

  //if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "battery reading"); }
  if (battery_sending == NULL) {
    // multiple battery events can fire in rapid succession, we'll let it settle down before logging it
    battery_sending = app_timer_register(5000, &battery_status_send, NULL);
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "battery timer queued"); }
  }

  set_status_charging_icon();

  statusbar_visible();
  toggle_statusbar();
  refresh_stat_slots(); // after toggle so the battery fills end up on top
  // Immediacy parity: a battery% complication in a center/bottom slot must
  // refresh now (the draw callback no longer does it).
  apply_center();
  apply_bottom();
  handle_vibe_suppression();
}

void generate_vibe(uint32_t vibe_pattern_number) {
  if (vibe_suppression) { return; }
  vibes_cancel();
  switch ( vibe_pattern_number ) {
  case 0: // No Vibration
    return;
  case 1: // Single short
    vibes_short_pulse();
    break;
  case 2: // Double short
    vibes_double_pulse();
    break;
  case 3: // Triple
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {200, 100, 200, 100, 200},
      .num_segments = 5
    } );
    break;
  case 4: // Long
    vibes_long_pulse();
    break;
  case 5: // Subtle
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {50, 200, 50, 200, 50, 200, 50},
      .num_segments = 7
    } );
    break;
  case 6: // Less Subtle
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {100, 200, 100, 200, 100, 200, 100},
      .num_segments = 7
    } );
    break;
  case 7: // Not Subtle
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {500, 250, 500, 250, 500, 250, 500},
      .num_segments = 7
    } );
    break;
  default: // No Vibration
    return;
  }
}

// Connection/battery now live in the configurable status-bar slots; refreshing
// them re-renders whatever the user put there.
void set_connection_text(void) {
  refresh_stat_slots();
}

void update_connection() {
  generate_vibe(bluetooth_connected ? settings_get()->vibe_pat_connect
                                     : settings_get()->vibe_pat_disconnect);
  refresh_stat_slots();
  // Immediacy parity: a BT-status complication in a center/bottom slot must
  // refresh now (the draw callback no longer does it).
  apply_center();
  apply_bottom();
}

static void handle_bluetooth(bool connected) {
  if (bluetooth_connected != connected) {
    bluetooth_connected = connected;
    update_connection();
    if (bluetooth_connected == true) {
      if ( (timezone_request == NULL) & (timezone_offset == TIMEZONE_UNINITIALIZED) ) {
        timezone_request = app_timer_register(5000, &request_timezone, NULL); // give it time to settle...
        if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "timezone request timer queued"); }
      }
    }
  }
}

#ifdef PBL_COLOR
// Recolor an alpha icon's opaque pixels to `color` (keeps the antialiased edges),
// so status icons follow the theme like the text. Color platforms only.
static void tint_icon(GBitmap *bmp, GColor color) {
  if (!bmp || gbitmap_get_format(bmp) != GBitmapFormat8Bit) { return; }
  GRect b = gbitmap_get_bounds(bmp);
  for (int y = b.origin.y; y < b.origin.y + b.size.h; y++) {
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(bmp, y);
    for (int x = ri.min_x; x <= ri.max_x; x++) {
      GColor8 *px = (GColor8 *)&ri.data[x];
      if (px->a != 0) { px->r = color.r; px->g = color.g; px->b = color.b; }
    }
  }
}
#endif

static void apply_palette(void) {
  // Own the window background here (moved out of theme.c's setColors/setInv/
  // setToday). Deterministic per relayout/tick, and fixes a latent flicker
  // where a calendar cell's setInvColors could leave bg == fg.
  window_set_background_color(window, theme_palette().bg);
  GColor fg = theme_palette().fg;
  text_layer_set_text_color(time_layer, fg);
  text_layer_set_text_color(date_layer, fg);
  if (ctr_r_layer) { text_layer_set_text_color(ctr_r_layer, fg); }
  text_layer_set_text_color(week_layer, fg);
  text_layer_set_text_color(ampm_layer, fg);
  text_layer_set_text_color(text_connection_layer, fg);
  text_layer_set_text_color(text_battery_layer, fg);
#ifdef PBL_COLOR
  tint_icon(image_charging_icon, fg);
  tint_icon(image_hourvibe_icon, fg);
  tint_icon(image_dnd_icon, fg);
  tint_icon(image_phone_icon, fg);
  tint_icon(image_watch_icon, fg);
  tint_icon(image_bt16_icon, fg);
  if (bmp_charging_layer)   { layer_mark_dirty(bitmap_layer_get_layer(bmp_charging_layer)); }
  for (int i = 0; i < 3; i++) {
    if (tray_layers[i]) { layer_mark_dirty(bitmap_layer_get_layer(tray_layers[i])); }
  }
  if (bmp_phone_layer)      { layer_mark_dirty(bitmap_layer_get_layer(bmp_phone_layer)); }
  if (bmp_watch_layer)      { layer_mark_dirty(bitmap_layer_get_layer(bmp_watch_layer)); }
#endif
}

static void set_unifont() {
  if ( strcmp(lang_gen_get()->language,"RU") == 0 ) { // Unicode font w/ Cyrillic characters
    // set fonts...
    text_layer_set_font(text_connection_layer, unifont_16);
    text_layer_set_font(date_layer, unifont_16);
    // set fonts, for calendar
    cal_normal = unifont_16; // fh = 16
    cal_bold   = unifont_16_bold; // fh = 22 // XXX TODO need a bold unicode/unifont option... maybe invert it or box it or something?
  } else { // Standard font
    // set fonts...
    text_layer_set_font(text_connection_layer,fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_font(date_layer,fonts_get_system_font(FONT_KEY_GOTHIC_24));
    // set fonts, for calendar
    cal_normal = fonts_get_system_font(FONT_KEY_GOTHIC_14); // fh = 16
    cal_bold   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); // fh = 22
  }
  // set offsets...
  position_connection_layer();
  position_date_layer();
  position_time_layer();
  apply_bottom(); // above-calendar band (formerly via position_day_layer)
}

bool period_check(uint8_t start_incr, uint8_t stop_incr, bool retval_on_equal) {
  // takes two periods (uint8_t 0-144) in 10 minute increments, and returns whether the current time falls inside them.
  // periods are fully inclusive, presently
  uint8_t current_min_incr = (currentTime->tm_min - (currentTime->tm_min%10))/10;
  uint8_t current_incr = currentTime->tm_hour * 6 + current_min_incr;
  bool inside_period = period_contains(start_incr, stop_incr, current_incr, retval_on_equal);
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Period Check... %d <= %d <= %d == %d*", start_incr, current_incr, stop_incr, (int)inside_period); }
  return inside_period;
}

bool dnd_period_check() {
  // dnd_mode: 0 off, 1 follow the watch's Quiet Time, 2 use the app's own window.
  uint8_t mode = adv_settings_get()->dnd_mode;
  if (mode == 1) {
    dnd_period_active = quiet_time_is_active();
  } else if (mode == 2) {
    dnd_period_active = period_check(adv_settings_get()->DND_start, adv_settings_get()->DND_stop, false);
  } else {
    dnd_period_active = false;
  }
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Tested DND period... %d", (int)dnd_period_active); }
  return dnd_period_active;
}
bool hourvibe_period_check() {
  // vibe_hour_days is the hourly-vibe mode: 0 off, 1 always, 2 window, 3 follow DND.
  uint8_t mode = adv_settings_get()->vibe_hour_days;
  if (mode == 1) {
    vibe_period_active = true;
  } else if (mode == 2) {
    vibe_period_active = period_check(adv_settings_get()->vibe_hour_start, adv_settings_get()->vibe_hour_stop, true);
  } else if (mode == 3) {
    vibe_period_active = !dnd_period_check(); // active every hour except during Do Not Disturb
  } else {
    vibe_period_active = false;
  }
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Tested vibe period... %d", (int)vibe_period_active); }
  return vibe_period_active;
}

void set_layer_attr(TextLayer *textlayer, GTextAlignment Alignment) {
  text_layer_set_text_alignment(textlayer, Alignment);
  text_layer_set_text_color(textlayer, theme_palette().fg);
  text_layer_set_background_color(textlayer, GColorClear);
}

void set_layer_attr_sfont(TextLayer *textlayer, char *font_key, GTextAlignment Alignment) {
  set_layer_attr(textlayer, Alignment);
  text_layer_set_font(textlayer, fonts_get_system_font(font_key));
}

static void window_load(Window *window) {

  // Check each load: adopt the new handle only on success, otherwise keep the
  // previous one (NULL on first load) and warn, rather than storing a NULL font.
  GFont uni = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UNICODE_16));
  if (uni) { unifont_16 = uni; unifont_16_is_custom = true; } else { APP_LOG(APP_LOG_LEVEL_WARNING, "unifont_16 load failed"); }
  GFont uni_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UNICODE_BOLD_16));
  if (uni_bold) { unifont_16_bold = uni_bold; unifont_16_bold_is_custom = true; } else { APP_LOG(APP_LOG_LEVEL_WARNING, "unifont_16_bold load failed"); }
  // Never leave these NULL: a first-load failure (OOM) would otherwise be handed to
  // graphics_draw_text as a NULL GFont via cal_normal/cal_bold and set_unifont().
  // Fall back to a system font so every consumer has a valid handle (cf. effects.c).
  if (!unifont_16)      { unifont_16      = fonts_get_system_font(FONT_KEY_GOTHIC_14); }
  if (!unifont_16_bold) { unifont_16_bold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); }
  cal_normal = unifont_16;
  cal_bold   = unifont_16_bold;

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  compute_layout(bounds.size.w, bounds.size.h);
  // Glyph size follows the time band (like the clock font); loads the font too.
  ensure_climacons(weather_glyph_size_for(DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT));

  slot_status = layer_create(GRect(0,LAYOUT_STAT,DEVICE_WIDTH,LAYOUT_SLOT_TOP));
  //slot_status = layer_create(GRect(0,0,DEVICE_WIDTH,DEVICE_HEIGHT));
  // No update proc: this is a pure container; its children draw themselves.
  layer_add_child(window_layer, slot_status);

  statusbar = layer_create(GRect(0,LAYOUT_STAT,DEVICE_WIDTH,LAYOUT_SLOT_TOP));
  layer_add_child(slot_status, statusbar);
  GRect stat_bounds = layer_get_bounds(statusbar);

  slot_top = layer_create(GRect(0,LAYOUT_SLOT_TOP,DEVICE_WIDTH,LAYOUT_SLOT_HEIGHT));
  layer_add_child(window_layer, slot_top);
  GRect slot_top_bounds = layer_get_bounds(slot_top);

  slot_bot = layer_create(GRect(0,LAYOUT_SLOT_BOT,DEVICE_WIDTH,LAYOUT_SLOT_BOT_HEIGHT));
  layer_add_child(window_layer, slot_bot);
  GRect slot_bot_bounds = layer_get_bounds(slot_bot);

  // Narrow screens only: wide screens (emery/chalk) use the status tray under the
  // clock and never draw this icon (set_status_charging_icon early-returns), so
  // creating it there is dead. Leave bmp_charging_layer == NULL on wide screens;
  // toggle_statusbar/window_unload guard against that NULL.
  if (!layout_is_wide(DEVICE_WIDTH)) {
    bmp_charging_layer = bitmap_layer_create( GRect(STAT_CHRG_ICON_LEFT, STAT_CHRG_ICON_TOP, 20, 20) );
    bitmap_layer_set_compositing_mode(bmp_charging_layer, GCompOpSet);
    layer_add_child(statusbar, bitmap_layer_get_layer(bmp_charging_layer));
  }
  image_charging_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHARGING_ICON);
  image_hourvibe_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HOURVIBE_ICON);
  image_dnd_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DONOTDISTURB_ICON);

  // Status-bar slot icons (16px): one per slot, bitmap chosen by slot content.
  image_phone_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PHONE_ICON);
  image_watch_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WATCH_ICON);
  image_bt16_icon  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT_16_ICON);

  bmp_phone_layer = bitmap_layer_create( GRect(2, 4, 16, 16) ); // left slot icon
  bitmap_layer_set_compositing_mode(bmp_phone_layer, GCompOpSet);
  layer_add_child(statusbar, bitmap_layer_get_layer(bmp_phone_layer));

  bmp_watch_layer = bitmap_layer_create( GRect(DEVICE_WIDTH - 18, 4, 16, 16) ); // right slot icon
  bitmap_layer_set_compositing_mode(bmp_watch_layer, GCompOpSet);
  layer_add_child(statusbar, bitmap_layer_get_layer(bmp_watch_layer));

  dnd_period_check();
  hourvibe_period_check();
  set_status_charging_icon();

  // Graphical battery meter retired: battery now shows as a slot (text + icon).
  battery_layer = layer_create(stat_bounds);
  layer_set_update_proc(battery_layer, battery_layer_update_callback);
  layer_add_child(statusbar, battery_layer);
  layer_set_hidden(battery_layer, true);

  datetime_layer = layer_create(slot_top_bounds);
  layer_set_update_proc(datetime_layer, datetime_layer_update_callback);
  layer_add_child(slot_top, datetime_layer);

  if (layout_is_wide(DEVICE_WIDTH)) { // status tray under the clock (see refresh_status_tray)
    GBitmap *tray_bmps[3] = { image_charging_icon, image_dnd_icon, image_hourvibe_icon };
    for (int i = 0; i < 3; i++) {
      tray_layers[i] = bitmap_layer_create(GRect(0, 0, 20, 20));
      bitmap_layer_set_compositing_mode(tray_layers[i], GCompOpSet);
      bitmap_layer_set_bitmap(tray_layers[i], tray_bmps[i]);
      layer_set_hidden(bitmap_layer_get_layer(tray_layers[i]), true);
      layer_add_child(datetime_layer, bitmap_layer_get_layer(tray_layers[i]));
    }
    refresh_status_tray();
  }

  calendar_create(slot_bot, slot_bot_bounds);

  splash_create(slot_bot, slot_bot_bounds);

  toggle_slot_bottom((void*)(intptr_t)0);  // show @ start...
  bottom_toggle = app_timer_register(2000, &toggle_slot_bottom, (void*)(intptr_t)1); // queue calendar to reappear in 2 seconds

  // Center row (above time): date_layer is the left slot, ctr_r_layer the right.
  date_layer = text_layer_create( GRect(REL_CLOCK_DATE_LEFT, REL_CLOCK_DATE_TOP, REL_CLOCK_DATE_WIDTH, REL_CLOCK_DATE_HEIGHT) );
  set_layer_attr_sfont(date_layer, FONT_KEY_GOTHIC_24, GTextAlignmentCenter);
  layer_add_child(datetime_layer, text_layer_get_layer(date_layer));
  ctr_r_layer = text_layer_create( GRect(DEVICE_WIDTH/2 + 2, REL_CLOCK_DATE_TOP, DEVICE_WIDTH/2 - 4, REL_CLOCK_DATE_HEIGHT) );
  set_layer_attr_sfont(ctr_r_layer, FONT_KEY_GOTHIC_24, GTextAlignmentRight);
  layer_add_child(datetime_layer, text_layer_get_layer(ctr_r_layer));
  apply_center(); // position + fill both center slots

  weather_create(datetime_layer, slot_top_bounds);

  time_layer = text_layer_create( GRect(REL_CLOCK_TIME_LEFT, REL_CLOCK_TIME_TOP, DEVICE_WIDTH - 2, REL_CLOCK_TIME_HEIGHT) ); // see position_time_layer()
  set_layer_attr_sfont(time_layer, time_font_key(), GTextAlignmentCenter);
  toggle_weather();
  position_time_layer(); // make use of our whitespace, if we have it...
  update_time_text();
  layer_add_child(datetime_layer, text_layer_get_layer(time_layer));

  week_layer = text_layer_create( GRect(2, REL_CLOCK_SUBTEXT_TOP, DEVICE_WIDTH / 2 - 4, 22) ); // left half
  set_layer_attr_sfont(week_layer, FONT_KEY_GOTHIC_18, GTextAlignmentLeft);
  layer_add_child(datetime_layer, text_layer_get_layer(week_layer));
  if ( settings_get()->show_week == 0 ) {
    layer_set_hidden(text_layer_get_layer(week_layer), true);
  }

  // Middle slot retired: only two complications above the calendar (left/right).
  ampm_layer = text_layer_create( GRect(DEVICE_WIDTH / 2 + 2, REL_CLOCK_SUBTEXT_TOP, DEVICE_WIDTH / 2 - 4, 22) ); // right half
  set_layer_attr_sfont(ampm_layer, FONT_KEY_GOTHIC_18, GTextAlignmentRight);
  layer_add_child(datetime_layer, text_layer_get_layer(ampm_layer));
  if ( settings_get()->show_am_pm == 0 ) {
    layer_set_hidden(text_layer_get_layer(ampm_layer), true);
  }

  update_datetime_subtext();

  // Status-bar slots: left (value, left-aligned) and right (value, right-aligned);
  // apply_stat_slot() sets their frames based on whether the content has an icon.
  text_connection_layer = text_layer_create( GRect(20, 2, DEVICE_WIDTH/2 - 22, 22) ); // left slot
  set_layer_attr_sfont(text_connection_layer, FONT_KEY_GOTHIC_18, GTextAlignmentLeft);
  layer_add_child(statusbar, text_layer_get_layer(text_connection_layer));

  text_battery_layer = text_layer_create( GRect(DEVICE_WIDTH/2, 2, DEVICE_WIDTH/2 - 20, 22) ); // right slot
  set_layer_attr_sfont(text_battery_layer, FONT_KEY_GOTHIC_18, GTextAlignmentRight);
  layer_add_child(statusbar, text_layer_get_layer(text_battery_layer));

  refresh_stat_slots(); // fill both slots (content + icon + position)

  set_unifont();
  apply_palette();

  // NOTE: No more adding layers below here - the inverter layers NEED to be the last to be on top!

  // Battery bar fills (invert effect): one per status battery slot, positioned
  // proportionally by refresh_stat_slots(); hidden unless the bar style is used.
  battery_meter_layer = effect_layer_create(stat_bounds);
  effect_layer_add_effect(battery_meter_layer, effect_invert, NULL);
  layer_set_hidden(effect_layer_get_layer(battery_meter_layer), true);
  layer_add_child(statusbar, effect_layer_get_layer(battery_meter_layer));
  batt_fill_r = effect_layer_create(stat_bounds);
  effect_layer_add_effect(batt_fill_r, effect_invert, NULL);
  layer_set_hidden(effect_layer_get_layer(batt_fill_r), true);
  layer_add_child(statusbar, effect_layer_get_layer(batt_fill_r));

  statusbar_visible();
  toggle_statusbar();
  refresh_stat_slots(); // place/raise the battery fills now the layers exist

  // topmost inverter layer, determines dark or light...
  inverter_layer = effect_layer_create(bounds);
  effect_layer_add_effect(inverter_layer, effect_invert, NULL);
  if (settings_get()->inverted==0) {
    layer_set_hidden(effect_layer_get_layer(inverter_layer), true);
  }
  layer_add_child(window_layer, effect_layer_get_layer(inverter_layer));

}

static void window_unload(Window *window) {
  // unload anything we loaded, destroy anything we created, remove anything we added
  layer_destroy(effect_layer_get_layer(inverter_layer));
  layer_destroy(effect_layer_get_layer(battery_meter_layer));
  layer_destroy(effect_layer_get_layer(batt_fill_r));
  layer_destroy(text_layer_get_layer(text_battery_layer));
  layer_destroy(text_layer_get_layer(text_connection_layer));
  layer_destroy(text_layer_get_layer(ampm_layer));
  layer_destroy(text_layer_get_layer(week_layer));
  layer_destroy(text_layer_get_layer(time_layer));
  layer_destroy(text_layer_get_layer(date_layer));
  layer_destroy(text_layer_get_layer(ctr_r_layer));
  weather_destroy();
  splash_destroy();
  calendar_destroy();
  layer_destroy(datetime_layer);
  layer_destroy(battery_layer);
  // Custom fonts are now explicitly unloaded below (see the font block). The
  // stage-12 system-font fallback (unifont_16/_bold) must NEVER be unloaded, so
  // *_is_custom flags gate those unloads; climacons is never system-fallback.
  if (bmp_charging_layer) { // NULL on wide screens (never created there)
    layer_remove_from_parent(bitmap_layer_get_layer(bmp_charging_layer));
    bitmap_layer_destroy(bmp_charging_layer);
    bmp_charging_layer = NULL;
  }
  for (int i = 0; i < 3; i++) {
    if (tray_layers[i]) { bitmap_layer_destroy(tray_layers[i]); tray_layers[i] = NULL; }
  }
  gbitmap_destroy(image_charging_icon);
  gbitmap_destroy(image_hourvibe_icon);
  gbitmap_destroy(image_dnd_icon);
  bitmap_layer_destroy(bmp_phone_layer);
  bitmap_layer_destroy(bmp_watch_layer);
  gbitmap_destroy(image_phone_icon);
  gbitmap_destroy(image_watch_icon);
  gbitmap_destroy(image_bt16_icon);
  layer_destroy(slot_bot);
  layer_destroy(slot_top);
  layer_destroy(statusbar);
  // slot_status is the parent of statusbar, so destroy it last (leaf-first).
  layer_destroy(slot_status);
  slot_status = NULL;

  // Explicitly unload the custom fonts. Only unload a handle we know is CUSTOM:
  // if the custom load failed, unifont_16/_bold hold a system font (stage-12
  // fallback) and fonts_unload_custom_font on a system handle is a bug.
  if (unifont_16_is_custom)      { fonts_unload_custom_font(unifont_16); }
  if (unifont_16_bold_is_custom) { fonts_unload_custom_font(unifont_16_bold); }
  if (climacons)                 { fonts_unload_custom_font(climacons); } // never system-fallback
  unifont_16 = NULL; unifont_16_bold = NULL; climacons = NULL;
  cal_normal = NULL; cal_bold = NULL; // alias the unifont handles — null only, no separate unload (double-free)
  s_climacons_size = 0;               // force a reload if the window is re-created
  unifont_16_is_custom = unifont_16_bold_is_custom = false;
}

static void deinit(void) {
  // deinit anything we init
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(window);
}

void handle_vibe_suppression() {
  // control vibe_suppression events - we should never set vibe_suppression to false outside of this function
  // it is useful to set it true directly (briefly), to ensure suppression, and then call this function afterwards
  // Suppress only inside a DND period or while charging; the hourly-vibe window
  // never suppresses (both non-DND branches previously set false).
  vibe_suppression = (dnd_period_active || battery_plugged);
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
  *currentTime = *tick_time;
  apply_palette();
  update_time_text();
  // Refresh center/bottom slot CONTENT every minute (was previously done only by
  // the per-minute draw callback). A clock2 (2nd-timezone) or date/AM-PM
  // complication in a center/bottom slot changes each minute; without this it
  // goes stale. Geometry-only helpers (position_time_layer/ensure_climacons) are
  // intentionally NOT re-run here.
  apply_center();
  apply_bottom();
  refresh_stat_slots(); // keep time-based status-bar slots current
  if ( currentTime->tm_min % 10 == 0) {
    dnd_period_check();
    hourvibe_period_check();
    set_status_charging_icon();
    handle_vibe_suppression();
  }
  if (bluetooth_connected && adv_settings_get()->weather_update) {
    if (adv_settings_get()->weather_update && (currentTime->tm_min + 60) % adv_settings_get()->weather_update == 0) {
      if (weather_request == NULL) { weather_request = app_timer_register(1000, &request_weather, NULL); } // don't overwrite a live pending timer (audit H4)
    } else if (weather_state()->current == 999 && weather_state()->requests < 5) {
      // ANDROIIIIIDRAGE  (or, someone who's got weather enabled but location services disabled)
      if (weather_request == NULL) { weather_request = app_timer_register(1000, &request_weather, NULL); } // for Android's slow JS...
    } 
  } 

//  if (units_changed & MONTH_UNIT) {
//  }

  if (units_changed & HOUR_UNIT) {
    request_timezone(NULL);
    update_datetime_subtext();
    if (settings_get()->vibe_hour && vibe_period_active) {
      generate_vibe(settings_get()->vibe_hour); // will be suppressed if within DND
    }
  }

  if (units_changed & DAY_UNIT) {
    layer_mark_dirty(datetime_layer);
    calendar_mark_dirty();
  }

  // calendar gets redrawn every time because time_layer is changed and all layers are redrawn together.
}

// Refresh a single slot's TextLayer iff it currently holds a per-second
// complication (id 9 = Seconds), routing the decision through complication_flags
// so the id list lives in one place. Null-safe: a layout that did not create a
// given layer leaves it NULL, so we must never dereference it (mirrors the null
// guards in refresh_stat_slots()/apply_center()).
static void refresh_second_slot(TextLayer *layer, uint8_t content) {
  if (layer && (complication_flags(content) & COMP_NEEDS_SECOND_TICK)) {
    update_slot_text(layer, content); // re-renders the per-second complication (Seconds)
  }
}

void handle_second_tick(struct tm *tick_time, TimeUnits units_changed)
{
  *currentTime = *tick_time;
  // Every second, refresh each live slot that holds a per-second complication,
  // using that slot's own TextLayer. Slot->layer mapping mirrors the normal
  // redraw paths so the second-tick and full-redraw agree.
  persist *s = settings_get();

  // Above-calendar row: apply_bottom draws it via layout_two_slots(week_layer,
  // ampm_layer, show_week, show_am_pm, ...) — left slot into week_layer, right into
  // ampm_layer, but a single set slot is centred into the LEFT layer (week_layer).
  {
    uint8_t bl = s->show_week, br = s->show_am_pm;
    if (bl && br) {
      refresh_second_slot(week_layer, bl);
      refresh_second_slot(ampm_layer, br);
    } else if (bl || br) {
      refresh_second_slot(week_layer, bl ? bl : br);
    }
  }

  // Center row (above time): layout_two_slots(date_layer, ctr_r_layer, ...) draws
  // the left slot into date_layer and the right into ctr_r_layer; when only one
  // center slot is set it is centred into the LEFT layer (date_layer).
  {
    uint8_t cl = s->slot_ctr_l, cr = s->slot_ctr_r;
    if (cl && cr) {
      refresh_second_slot(date_layer,  cl);
      refresh_second_slot(ctr_r_layer, cr);
    } else if (cl || cr) {
      refresh_second_slot(date_layer, cl ? cl : cr);
    }
  }

  // Status bar: refresh_stat_slots() draws two halves as left->text_connection_layer,
  // right->text_battery_layer, but centres a single non-bar slot into the left
  // (connection) layer. Mirror that same slot->layer choice here.
  {
    uint8_t cl = s->slot_stat_l, cr = s->slot_stat_r;
    bool single = (cl && !cr) || (!cl && cr);
    if (single && !batt_style_is_bar(s->batt_style)) {
      refresh_second_slot(text_connection_layer, cl ? cl : cr);
    } else {
      refresh_second_slot(text_connection_layer, cl);
      refresh_second_slot(text_battery_layer,    cr);
    }
  }

  // redraw everything else if the minute changes...
  if (units_changed & MINUTE_UNIT) {
    handle_minute_tick(tick_time, units_changed);
  }
}

static int need_second_tick_handler(void) {
  // A per-second tick is needed if ANY of the six live slots holds a
  // needs-second-tick complication. The id list lives only in complication_flags().
  persist *s = settings_get();
  uint8_t slots[] = {
    s->show_week, s->show_am_pm,     // above-calendar row
    s->slot_stat_l, s->slot_stat_r,  // status bar
    s->slot_ctr_l,  s->slot_ctr_r,   // center row
  };
  for (unsigned i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
    if (complication_flags(slots[i]) & COMP_NEEDS_SECOND_TICK) { return 1; }
  }
  return 0;
}

static void switch_tick_handler(void) {
  tick_timer_service_unsubscribe(); // safe to call even before we've subscribed
  seconds_shown = need_second_tick_handler();
  if (seconds_shown) {
    tick_timer_service_subscribe(SECOND_UNIT, &handle_second_tick);
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Seconds handler enabled"); }
  } else {
    tick_timer_service_subscribe(MINUTE_UNIT, &handle_minute_tick);
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Seconds handler disabled"); }
  }
}

void my_out_sent_handler(DictionaryIterator *sent, void *context) {
// outgoing message was delivered
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "AppMessage Delivered"); }
}
void my_out_fail_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
// outgoing message failed
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "AppMessage Failed to Send: %d", reason); }
}

void in_js_ready_handler(DictionaryIterator *received, void *context) {
    watch_version_send(NULL);
    if (weather_request == NULL) { weather_request = app_timer_register(3000, &request_weather, NULL); } // for Android's slow JS...
}

void in_weather_handler(DictionaryIterator *received, void *context) {
    Tuple *appkey     = dict_find(received, AK_WEATHER_TEMP);
    if (appkey != NULL)     { weather_state()->current = appkey->value->int16; }
    appkey = dict_find(received, AK_WEATHER_COND);
    { const char *s = tuple_str(appkey);
      if (s != NULL) {
        strncpy(weather_state()->condition, s, sizeof(weather_state()->condition)-1);
        weather_state()->condition[sizeof(weather_state()->condition)-1] = '\0';
      } }
    appkey = dict_find(received, AK_WEATHER_CITY);
    { const char *s = tuple_str(appkey);
      if (s != NULL) {
        strncpy(weather_state()->city, s, sizeof(weather_state()->city)-1);
        weather_state()->city[sizeof(weather_state()->city)-1] = '\0';
      } }
    // Coordinates feed the sunrise/sunset complications and the Auto theme.
    Tuple *lat = dict_find(received, AK_WEATHER_LAT);
    Tuple *lon = dict_find(received, AK_WEATHER_LON);
    const char *slat = tuple_str(lat), *slon = tuple_str(lon);
    if (slat != NULL && slon != NULL &&
        (strcmp(slat, adv_settings_get()->weather_lat) != 0 ||
         strcmp(slon, adv_settings_get()->weather_lon) != 0)) {
      // Only copy + persist + repaint when the location actually changed, to
      // avoid a full 244-byte flash write on every weather response (audit M6).
      strncpy(adv_settings_get()->weather_lat, slat, sizeof(adv_settings_get()->weather_lat)-1);
      adv_settings_get()->weather_lat[sizeof(adv_settings_get()->weather_lat)-1] = '\0';
      strncpy(adv_settings_get()->weather_lon, slon, sizeof(adv_settings_get()->weather_lon)-1);
      adv_settings_get()->weather_lon[sizeof(adv_settings_get()->weather_lon)-1] = '\0';
      int wrote = persist_write_data(PK_ADV_SETTINGS, adv_settings_get(), sizeof(persist_adv_settings));
      if (wrote < (int)sizeof(persist_adv_settings)) {
        app_log(APP_LOG_LEVEL_WARNING, __FILE__, __LINE__, "persist_write_data(PK_ADV_SETTINGS) failed: wrote %d of %d bytes", wrote, (int)sizeof(persist_adv_settings));
      }
      apply_palette(); // Auto theme may flip only when the location actually changes
    }
    weather_mark_dirty();
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Weather received [%d/%d]: %d, %s", weather_state()->failures, weather_state()->requests, weather_state()->current, weather_state()->condition); }
    if (weather_state()->current == 999) {
      weather_state()->failures++;
    } else {
      weather_state()->requests = 0;
      weather_state()->failures = 0;
    }
}

void in_timezone_handler(DictionaryIterator *received, void *context) {
    Tuple *tz_offset = dict_find(received, AK_TIMEZONE_OFFSET);
    if (tz_offset != NULL) {
      timezone_offset = tz_offset->value->int8;
      // Refresh both rows immediately: a Timezone/clock2 complication may sit in a
      // center slot (apply_center) or the bottom row (update_datetime_subtext).
      // Before the draw-callback refactor, apply_bottom's dirty cascade re-ran the
      // callback which refreshed center too; now the callback is a no-op, so do it here.
      apply_center();
      update_datetime_subtext();
    }
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Timezone received: %d", timezone_offset); }
}

// Recompute the adaptive layout from the current settings and reposition every
// band live (no watchface reload). Called after config so enabling/disabling a
// row redistributes the freed space immediately.
static void relayout(void) {
  if (!slot_top) { return; } // not built yet
  compute_layout(DEVICE_WIDTH, DEVICE_HEIGHT);
  GRect stat = GRect(0, LAYOUT_STAT, DEVICE_WIDTH, LAYOUT_SLOT_TOP);
  layer_set_frame(statusbar, stat);
  layer_set_frame(slot_status, stat);
  layer_set_frame(slot_top, GRect(0, LAYOUT_SLOT_TOP, DEVICE_WIDTH, LAYOUT_SLOT_HEIGHT));
  layer_set_frame(slot_bot, GRect(0, LAYOUT_SLOT_BOT, DEVICE_WIDTH, LAYOUT_SLOT_BOT_HEIGHT));
  layer_set_frame(datetime_layer, GRect(0, 0, DEVICE_WIDTH, LAYOUT_SLOT_HEIGHT));
  layer_set_frame(battery_layer, GRect(0, 0, DEVICE_WIDTH, LAYOUT_SLOT_TOP));
  calendar_set_frame(GRect(0, 0, DEVICE_WIDTH, LAYOUT_SLOT_BOT_HEIGHT));
  splash_set_frame(GRect(0, 0, DEVICE_WIDTH, LAYOUT_SLOT_BOT_HEIGHT));
  text_layer_set_font(time_layer, fonts_get_system_font(time_font_key())); // band height may have changed
  position_time_layer();   // time + weather frames within the new time band
  statusbar_visible();
  toggle_statusbar();
  apply_center();
  apply_bottom();
  refresh_stat_slots();    // after toggle so the battery fills stay on top
  calendar_mark_dirty();
  layer_mark_dirty(datetime_layer);
}

void in_configuration_handler(DictionaryIterator *received, void *context) {
    // debugging first (so we can catch this message)

    // AK_DEBUGGING_ON == general debugging
    Tuple *debugging = dict_find(received, AK_DEBUGGING_ON);
    if (debugging != NULL) {
      if (debugging->value->uint8 != 0) {
        debug_get()->general = true;
        app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Debugging enabled.");
      } else {
        if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Debugging disabled."); }
        debug_get()->general = false;
      } 
    }

    // AK_DEBUGLANG_ON == language / translation debugging
    Tuple *debuglang = dict_find(received, AK_DEBUGLANG_ON);
    if (debuglang != NULL) {
      if (debuglang->value->uint8 != 0) {
        debug_get()->language = true;
        if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Language debugging enabled."); }
      } else {
        debug_get()->language = false;
        if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Language debugging disabled."); }
      } 
    }

    // style_inv == inverted
    Tuple *style_inv = dict_find(received, AK_STYLE_INV);
    if (style_inv != NULL) {
      settings_get()->inverted = style_inv->value->uint8;
      if (style_inv->value->uint8==0) {
        layer_set_hidden(effect_layer_get_layer(inverter_layer), true); // hide inversion = dark
      } else {
        layer_set_hidden(effect_layer_get_layer(inverter_layer), false); // show inversion = light
      }
    }

    // style_day_inv == day_invert
    Tuple *style_day_inv = dict_find(received, AK_STYLE_DAY_INV);
    if (style_day_inv != NULL) {
      settings_get()->day_invert = style_day_inv->value->uint8;
    }

    // style_grid == grid
    Tuple *style_grid = dict_find(received, AK_STYLE_GRID);
    if (style_grid != NULL) {
      settings_get()->grid = style_grid->value->uint8;
    }

    // AK_VIBE_HOUR == vibe_hour - vibration patterns for hourly vibration
    Tuple *vibe_hour = dict_find(received, AK_VIBE_HOUR);
    if (vibe_hour != NULL) {
      settings_get()->vibe_hour = vibe_hour->value->uint8;
      set_status_charging_icon();
    }

    // INTL_DOWO == dayOfWeekOffset
    Tuple *INTL_DOWO = dict_find(received, AK_INTL_DOWO);
    if (INTL_DOWO != NULL) {
      uint8_t dowo = INTL_DOWO->value->uint8;
      if (dowo <= 6) { settings_get()->dayOfWeekOffset = dowo; } // reject out-of-range; keep previous
    }

    // AK_INTL_FMT_DATE == date format (strftime + manual localization)
    Tuple *FMT_DATE = dict_find(received, AK_INTL_FMT_DATE);
    if (FMT_DATE != NULL) {
      settings_get()->date_format = datefmt_clamp(FMT_DATE->value->uint8); // validate on receive
      update_date_text();
    }

    // AK_STRFTIME_FORMAT == custom strftime string used when date_format == 255
    Tuple *sfmt = dict_find(received, AK_STRFTIME_FORMAT);
    if (sfmt != NULL) {
      const char *s = tuple_str(sfmt);
      if (s != NULL) {
        strncpy(adv_settings_get()->custom_date_fmt, s, sizeof(adv_settings_get()->custom_date_fmt)-1);
        adv_settings_get()->custom_date_fmt[sizeof(adv_settings_get()->custom_date_fmt)-1] = '\0';
      }
      update_date_text();
    }

    // AK_STYLE_WEEK
    Tuple *style_week = dict_find(received, AK_STYLE_WEEK);
    if (style_week != NULL) {
      settings_get()->show_week = style_week->value->uint8;
      if ( settings_get()->show_week ) {
        layer_set_hidden(text_layer_get_layer(week_layer), false);
      }  else {
        layer_set_hidden(text_layer_get_layer(week_layer), true);
      }
    }

    // AK_INTL_FMT_WEEK == week format (strftime)
    Tuple *FMT_WEEK = dict_find(received, AK_INTL_FMT_WEEK);
    if (FMT_WEEK != NULL) {
      settings_get()->week_format = FMT_WEEK->value->uint8;
    }

    // AK_STYLE_AM_PM
    Tuple *style_am_pm = dict_find(received, AK_STYLE_AM_PM);
    if (style_am_pm != NULL) {
      settings_get()->show_am_pm = style_am_pm->value->uint8;
      if ( settings_get()->show_am_pm ) {
        layer_set_hidden(text_layer_get_layer(ampm_layer), false);
      }  else {
        layer_set_hidden(text_layer_get_layer(ampm_layer), true);
      }
    }

    // Status-bar slots (left/right) — configurable like the rest.
    Tuple *slot_stat_l = dict_find(received, AK_SLOT_STAT_L);
    if (slot_stat_l != NULL) { settings_get()->slot_stat_l = slot_stat_l->value->uint8; }
    Tuple *slot_stat_r = dict_find(received, AK_SLOT_STAT_R);
    if (slot_stat_r != NULL) { settings_get()->slot_stat_r = slot_stat_r->value->uint8; }
    if (slot_stat_l != NULL || slot_stat_r != NULL) { refresh_stat_slots(); }

    // Center-row slots (above the time).
    Tuple *slot_ctr_l = dict_find(received, AK_SLOT_CTR_L);
    if (slot_ctr_l != NULL) { settings_get()->slot_ctr_l = slot_ctr_l->value->uint8; }
    Tuple *slot_ctr_r = dict_find(received, AK_SLOT_CTR_R);
    if (slot_ctr_r != NULL) { settings_get()->slot_ctr_r = slot_ctr_r->value->uint8; }
    if (slot_ctr_l != NULL || slot_ctr_r != NULL) { apply_center(); }

    Tuple *batt_style = dict_find(received, AK_BATT_STYLE);
    if (batt_style != NULL) { settings_get()->batt_style = batt_style->value->uint8; refresh_stat_slots(); }

    if (need_second_tick_handler() != seconds_shown) {
      switch_tick_handler();
    }

    // now that we've received any changes, redraw the subtext (which processes week, day, and AM/PM)
    update_datetime_subtext();

    // AK_VIBE_PAT_DISCONNECT / AK_VIBE_PAT_CONNECT == vibration patterns for connect and disconnect
    Tuple *VIBE_PAT_D = dict_find(received, AK_VIBE_PAT_DISCONNECT);
    if (VIBE_PAT_D != NULL) {
      settings_get()->vibe_pat_disconnect = VIBE_PAT_D->value->uint8;
    }
    Tuple *VIBE_PAT_C = dict_find(received, AK_VIBE_PAT_CONNECT);
    if (VIBE_PAT_C != NULL) {
      settings_get()->vibe_pat_connect = VIBE_PAT_C->value->uint8;
    }

    // AK_TRACK_BATTERY == whether or not to do battery tracking
    Tuple *track_battery = dict_find(received, AK_TRACK_BATTERY);
    if (track_battery != NULL) {
      settings_get()->track_battery = track_battery->value->uint8;
      if (settings_get()->track_battery) {
        battery_status_send(NULL); // either it was just turned on, or we'll get a bonus datapoint from running config.
      }
    }

    // AK_CAL_WEEK_PATTERN == which weeks are shown in calendar (last,current,next - etc.)
    Tuple *week_pattern = dict_find(received, AK_CAL_WEEK_PATTERN);
    if (week_pattern != NULL) {
      adv_settings_get()->week_pattern = week_pattern->value->uint8;
    }

    Tuple *appkey;

    // AK_SHOW_STAT_BAR == show statusbar
    appkey = dict_find(received, AK_SHOW_STAT_BAR);
    if (appkey != NULL) { adv_settings_get()->showStatus = appkey->value->uint8; }

    // AK_SHOW_STAT_BATT == statusbar battery limit
    appkey = dict_find(received, AK_SHOW_STAT_BATT);
    if (appkey != NULL) { adv_settings_get()->showStatusBat = appkey->value->uint8; }

    // AK_CLOCK2_TZ == second time zone UTC offset (whole hours, signed)
    appkey = dict_find(received, AK_CLOCK2_TZ);
    if (appkey != NULL) { adv_settings_get()->clock2_tz = appkey->value->int8; }

    // AK_DND_START == period start, DND
    appkey = dict_find(received, AK_DND_START);
    if (appkey != NULL) { adv_settings_get()->DND_start = appkey->value->uint8; }

    // AK_DND_STOP == period stop, DND
    appkey = dict_find(received, AK_DND_STOP);
    if (appkey != NULL) { adv_settings_get()->DND_stop = appkey->value->uint8; }

    // AK_DND_NOACCEL == DND mode (0 off, 1 follow watch Quiet Time, 2 app window)
    appkey = dict_find(received, AK_DND_NOACCEL);
    if (appkey != NULL) { adv_settings_get()->dnd_mode = appkey->value->uint8; }

    // AK_VIBE_START == period start, VIBE
    appkey = dict_find(received, AK_VIBE_START);
    if (appkey != NULL) { adv_settings_get()->vibe_hour_start = appkey->value->uint8; }

    // AK_VIBE_STOP == period stop, VIBE
    appkey = dict_find(received, AK_VIBE_STOP);
    if (appkey != NULL) { adv_settings_get()->vibe_hour_stop = appkey->value->uint8; }

    // AK_VIBE_DAYS == days-of-week mask for hourly vibration (read by hourvibe_period_check)
    appkey = dict_find(received, AK_VIBE_DAYS);
    if (appkey != NULL) { adv_settings_get()->vibe_hour_days = appkey->value->uint8; }

    // AK_WEATHER_FMT == weather format (0:C / 1:F)
    appkey = dict_find(received, AK_WEATHER_FMT);
    if (appkey != NULL) {
      adv_settings_get()->weather_format = appkey->value->uint8;
      if (weather_request == NULL) { weather_request = app_timer_register(1000, &request_weather, NULL); }
    }

    // AK_WEATHER_UPDATE == weather update frequency
    appkey = dict_find(received, AK_WEATHER_UPDATE);
    if (appkey != NULL) {
      if (appkey->value->uint8 < adv_settings_get()->weather_update && weather_request == NULL) {
        weather_request = app_timer_register(1000, &request_weather, NULL);
      }
      adv_settings_get()->weather_update = appkey->value->uint8;
    }

    // AK_WEATHER_ICONS == weather icon style (0 default / 1 colour / 2 B&W)
    appkey = dict_find(received, AK_WEATHER_ICONS);
    if (appkey != NULL) {
      adv_settings_get()->weather_icons = appkey->value->uint8;
      weather_mark_dirty();
    }

    // AK_THEME == color theme id; AK_THEME_MODE == 0 light / 1 dark / 2 auto
    appkey = dict_find(received, AK_THEME);
    if (appkey != NULL && appkey->value->uint8 < THEME_COUNT) {
      settings_get()->theme = appkey->value->uint8;
    }
    appkey = dict_find(received, AK_THEME_MODE);
    if (appkey != NULL && appkey->value->uint8 <= THEME_MODE_AUTO) {
      settings_get()->theme_mode = appkey->value->uint8;
    }

    statusbar_visible();
    toggle_weather();
    toggle_statusbar();

    // begin translations...
    Tuple *translation;

    // AK_LANGUAGE == language, e.g. EN
    Tuple *chosen_language = dict_find(received, AK_LANGUAGE);
    if (chosen_language != NULL) {
      const char *s = tuple_str(chosen_language);
      if (s != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Language is set to %s", s); }
        strncpy(lang_gen_get()->language, s, sizeof(lang_gen_get()->language)-1);
        lang_gen_get()->language[sizeof(lang_gen_get()->language)-1] = '\0';
        set_unifont();
      }
    }

    // AK_TRANS_ABBR_*DAY == abbrDaysOfWeek // localized Su Mo Tu We Th Fr Sa, max 2 characters
    for (int i = AK_TRANS_ABBR_SUNDAY; i <= AK_TRANS_ABBR_SATURDAY; i++ ) {
      translation = dict_find(received, i);
      const char *s = tuple_str(translation);
      if (s != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, s); }
        strncpy(lang_gen_get()->abbrDaysOfWeek[i - AK_TRANS_ABBR_SUNDAY], s, sizeof(lang_gen_get()->abbrDaysOfWeek[i - AK_TRANS_ABBR_SUNDAY])-1);
        lang_gen_get()->abbrDaysOfWeek[i - AK_TRANS_ABBR_SUNDAY][sizeof(lang_gen_get()->abbrDaysOfWeek[i - AK_TRANS_ABBR_SUNDAY])-1] = '\0'; // strncpy leaves a maxed multibyte buffer unterminated (H9)
      }
    }

    // AK_TRANS_*DAY == daysOfWeek // localized Sunday through Saturday, max 12 characters
    for (int i = AK_TRANS_SUNDAY; i <= AK_TRANS_SATURDAY; i++ ) {
      translation = dict_find(received, i);
      const char *s = tuple_str(translation);
      if (s != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, s); }
        strncpy(lang_days_get()->DaysOfWeek[i - AK_TRANS_SUNDAY], s, sizeof(lang_days_get()->DaysOfWeek[i - AK_TRANS_SUNDAY])-1);
        lang_days_get()->DaysOfWeek[i - AK_TRANS_SUNDAY][sizeof(lang_days_get()->DaysOfWeek[i - AK_TRANS_SUNDAY])-1] = '\0'; // strncpy leaves a maxed multibyte buffer unterminated (H9)
      }
    }

    // AK_TRANS_ABBR_*MONTH == monthsOfYear // localized month name abbreviations, max 3 characters
    for (int i = AK_TRANS_ABBR_JANUARY; i <= AK_TRANS_ABBR_DECEMBER; i++ ) {
      translation = dict_find(received, i);
      const char *s = tuple_str(translation);
      if (s != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, s); }
        strncpy(lang_gen_get()->abbrMonthsNames[i - AK_TRANS_ABBR_JANUARY], s, sizeof(lang_gen_get()->abbrMonthsNames[i - AK_TRANS_ABBR_JANUARY])-1);
        lang_gen_get()->abbrMonthsNames[i - AK_TRANS_ABBR_JANUARY][sizeof(lang_gen_get()->abbrMonthsNames[i - AK_TRANS_ABBR_JANUARY])-1] = '\0'; // strncpy leaves a maxed multibyte buffer unterminated (H9)
      }
    }

    // AK_TRANS_*MONTH == monthsOfYear // localized month names, max 12 characters
    for (int i = AK_TRANS_JANUARY; i <= AK_TRANS_DECEMBER; i++ ) {
      translation = dict_find(received, i);
      const char *s = tuple_str(translation);
      if (s != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, s); }
        strncpy(lang_months_get()->monthsNames[i - AK_TRANS_JANUARY], s, sizeof(lang_months_get()->monthsNames[i - AK_TRANS_JANUARY])-1);
        // strncpy does not NUL-terminate when the source fills the buffer. A maxed
        // multibyte (Cyrillic) name is exactly sizeof-1 bytes, so terminate the
        // last byte explicitly to avoid an over-read (H9).
        lang_months_get()->monthsNames[i - AK_TRANS_JANUARY][sizeof(lang_months_get()->monthsNames[i - AK_TRANS_JANUARY])-1] = '\0';
      }
    }

    // AK_TRANS_CONNECTED / AK_TRANS_DISCONNECTED == status text, e.g. "Linked" "NOLINK", max 9 characters
    for (int i = AK_TRANS_CONNECTED; i <= AK_TRANS_DISCONNECTED; i++ ) {
      translation = dict_find(received, i);
      const char *s = tuple_str(translation);
      if (s != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, s); }
        strncpy(lang_gen_get()->statuses[i - AK_TRANS_CONNECTED], s, sizeof(lang_gen_get()->statuses[i - AK_TRANS_CONNECTED])-1);
        lang_gen_get()->statuses[i - AK_TRANS_CONNECTED][sizeof(lang_gen_get()->statuses[i - AK_TRANS_CONNECTED])-1] = '\0'; // strncpy leaves a maxed multibyte buffer unterminated (H9)
      }
    }
    vibe_suppression = true;
    update_connection();
    handle_vibe_suppression();

    // AK_TRANS_TIME_AM / AK_TRANS_TIME_PM == AM / PM text, e.g. "AM" "PM" :), max 5 characters
    for (int i = AK_TRANS_TIME_AM; i <= AK_TRANS_TIME_PM; i++ ) {
      translation = dict_find(received, i);
      const char *s = tuple_str(translation);
      if (s != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, s); }
        strncpy(lang_gen_get()->abbrTime[i - AK_TRANS_TIME_AM], s, sizeof(lang_gen_get()->abbrTime[i - AK_TRANS_TIME_AM])-1);
        // strncpy leaves the buffer unterminated when the source fills it; a maxed
        // multibyte (Cyrillic) AM/PM string is sizeof-1 bytes, so NUL the last byte
        // explicitly to avoid an over-read (H9).
        lang_gen_get()->abbrTime[i - AK_TRANS_TIME_AM][sizeof(lang_gen_get()->abbrTime[i - AK_TRANS_TIME_AM])-1] = '\0';
      }
    }
    
    // end translations...

    int result = 0;
    result = persist_write_data(PK_SETTINGS, settings_get(), sizeof(persist) );
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into settings", result); }
    result = persist_write_data(PK_LANG_GEN, lang_gen_get(), sizeof(persist_general_lang) );
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into lang_gen", result); }
    result = persist_write_data(PK_LANG_MONTHS, lang_months_get(), sizeof(persist_months_lang) );
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into lang_months", result); }
    result = persist_write_data(PK_LANG_DAYS, lang_days_get(), sizeof(persist_days_lang) );
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into lang_days", result); }
    result = persist_write_data(PK_DEBUGGING, debug_get(), sizeof(persist_debug) );
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into debug", result); }
    result = persist_write_data(PK_ADV_SETTINGS, adv_settings_get(), sizeof(persist_adv_settings) );
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into adv_settings", result); }
    // Explicit failure log (audit M1): PK_ADV_SETTINGS sits at the 244/256 ceiling,
    // so a short/negative write means advanced settings silently stopped saving.
    if (result < (int)sizeof(persist_adv_settings)) { app_log(APP_LOG_LEVEL_WARNING, __FILE__, __LINE__, "persist_write_data(PK_ADV_SETTINGS) failed: wrote %d of %d bytes", result, (int)sizeof(persist_adv_settings)); }
    result = persist_write_data(PK_SETTINGS_EXT, settings_ext_get(), sizeof(persist_settings_ext) );
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into settings_ext", result); }
    if (result < (int)sizeof(persist_settings_ext)) { app_log(APP_LOG_LEVEL_WARNING, __FILE__, __LINE__, "persist_write_data(PK_SETTINGS_EXT) failed: wrote %d of %d bytes", result, (int)sizeof(persist_settings_ext)); }

    // ==== Implemented SDK ====
    // Battery
    // Connected
    // Persistent Storage
    // Screenshot Operation
    // ==== Available in SDK ====
    // Accelerometer
    // App Focus ( does this apply to Timely? )
    // PebbleKit JS - more accurate location data: enableHighAccuracy (takes a while on IOS)
    // ==== Waiting on / SDK gaps ====
    // Magnetometer
    // ==== Interesting SDK possibilities ====
    // PebbleKit JS - more information from phone
    // ==== Future improvements ====
    // Positioning - top, bottom, etc.
  apply_palette(); // re-tint text + icons for the (possibly new) theme
  relayout();      // recompute + reposition the bands live for the new settings
}

void my_in_rcv_handler(DictionaryIterator *received, void *context) {
// incoming message received
  Tuple *message_type = dict_find(received, AK_MESSAGE_TYPE);
  if (message_type != NULL) {
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Message type %d received", message_type->value->uint8); }
    switch ( message_type->value->uint8 ) {
    case AK_SEND_WATCH_VERSION:
      in_js_ready_handler(received, context);
      return;
    case AK_TIMEZONE_OFFSET:
      in_timezone_handler(received, context);
      return;
    case AK_REQUEST_WEATHER:
      in_weather_handler(received, context);
      return;
    case AK_PHONE_BATTERY: {
      Tuple *pb = dict_find(received, AK_PHONE_BATTERY);
      if (pb != NULL) { phone_battery_percent = pb->value->uint8; set_connection_text(); }
      return;
    }
    }
  } else {
    // default to configuration, which may not send the message type...
    in_configuration_handler(received, context);
  }
}

void my_in_drp_handler(AppMessageResult reason, void *context) {
// incoming message dropped
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "AppMessage Dropped: %d", reason); }
}

static void app_message_init(void) {
  // Register message handlers
  app_message_register_inbox_received(my_in_rcv_handler);
  app_message_register_inbox_dropped(my_in_drp_handler);
  app_message_register_outbox_sent(my_out_sent_handler);
  app_message_register_outbox_failed(my_out_fail_handler);
  // Init buffers
//  app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "AM Inbox max %lu", app_message_inbox_size_maximum());
//[INFO    ] D Timely.c:2079 AM Inbox 2044 received
//  app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "AM Outbox max %lu", app_message_outbox_size_maximum());
//[INFO    ] D Timely.c:2080 AM Outbox 636 received
  // Open the inbox at the platform maximum instead of a hardcoded 1280 bytes.
  // The bulky part of a settings save is the translation strings, which travel
  // by NAME (trans_* tuples): switching language to Italiano builds a worst-case
  // AppMessage that overflowed the old 1280-byte inbox, so the whole batch was
  // silently dropped (issue #4 / audit C4). app_message_inbox_size_maximum()
  // returns the real ceiling the firmware grants (~2044 bytes here), which holds
  // the Italiano worst case. The outbox (watch->phone: weather/battery, ~60 bytes)
  // stays at the original 512 — maxing it too would just hold extra heap on the
  // tighter platforms (diorite) for no gain; only the inbox overflowed.
  app_message_open(app_message_inbox_size_maximum(), 512);
}


static void init(void) {

  if (DEBUGLOG == 1) { debug_get()->general = true; }
  if (TRANSLOG == 1) { debug_get()->language = true; }
  // Seed our own store from libc localtime once; currentTime already points at
  // s_now, and the minute/second ticks write through it (never re-aliasing).
  struct tm *now = get_time();
  if (now) { s_now = *now; }

  app_message_init();

  if (persist_exists(PK_SETTINGS)) {
    persist_read_data(PK_SETTINGS, settings_get(), sizeof(persist) );
    if (settings_get()->version == 11) { // v11 -> v12 bugfix
      settings_get()->date_format = datefmt_migrate_v11_to_v12(settings_get()->date_format);
      settings_get()->version = 12;
    }
    // Clamp whatever we loaded/migrated to the renderer's safe set.
    settings_get()->date_format = datefmt_clamp(settings_get()->date_format);
    settings_get()->dayOfWeekOffset %= 7; // keep 0..6 so calendar indexing stays in bounds
    if (persist_exists(PK_LANG_GEN)) {
      persist_read_data(PK_LANG_GEN, lang_gen_get(), sizeof(persist_general_lang) );
    }
    if (persist_exists(PK_LANG_MONTHS)) {
      persist_read_data(PK_LANG_MONTHS, lang_months_get(), sizeof(persist_months_lang) );
    }
    if (persist_exists(PK_LANG_DAYS)) {
      persist_read_data(PK_LANG_DAYS, lang_days_get(), sizeof(persist_days_lang) );
    }
    if (persist_exists(PK_DEBUGGING)) {
      persist_read_data(PK_DEBUGGING, debug_get(), sizeof(persist_debug) );
    }
    //persist_write_data(PK_ADV_SETTINGS, adv_settings_get(), sizeof(persist_adv_settings) ); // XXX TODO reset to defaults, for testing...
    if (persist_exists(PK_ADV_SETTINGS)) {
      persist_read_data(PK_ADV_SETTINGS, adv_settings_get(), sizeof(persist_adv_settings) );
    }
    // Extended settings: a missing key leaves the settings.c defaults in place;
    // a shorter stored blob (older version) leaves appended fields at default too.
    if (persist_exists(PK_SETTINGS_EXT)) {
      persist_read_data(PK_SETTINGS_EXT, settings_ext_get(), sizeof(persist_settings_ext) );
    }
  }
  // re-initialize this, if it was set, since we're storing those values persistently as well...
  if (DEBUGLOG == 1) { debug_get()->general = true; }
  if (TRANSLOG == 1) { debug_get()->language = true; }

  if (adv_settings_get()->weather_update) {
    weather_request = app_timer_register(1250, &request_weather, NULL); // one-shot init registration: runs once at startup where the handle is guaranteed NULL (audit H4)
    //request_weather(NULL);
  }

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
  window_set_background_color(window, GColorBlack);
  window_stack_push(window, false);

  //update_time_text();

  switch_tick_handler();
  bluetooth_connection_service_subscribe(&handle_bluetooth);
  handle_bluetooth(bluetooth_connection_service_peek()); // initialize
  battery_state_service_subscribe(&handle_battery);
  handle_battery(battery_state_service_peek()); // initialize
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
