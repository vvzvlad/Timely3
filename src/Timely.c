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
#define DEBUGLOG 0
#define TRANSLOG 0
#define CONFIG_VERSION "3.0" // config-protocol version (own sequence); major bump: the save wire format changed
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
static TextLayer *day_layer;
static TextLayer *ctr_r_layer; // center row (above time), right slot; date_layer is the left
static Layer *statusbar;
static Layer *slot_status;
static Layer *slot_top;
static Layer *slot_bot;
static GFont unifont_16;
static GFont unifont_16_bold;
GFont cal_normal;
GFont cal_bold;
GFont climacons;
static int s_climacons_size = 0; // loaded climacons px; 0 = not loaded yet

// (Re)load the climacons font when the band-driven size class changes. A
// single handle stays live so RAM never holds two copies; reloads only happen
// on settings toggles that resize the time band.
static void ensure_climacons(int size) {
  if (size == s_climacons_size) { return; }
  if (climacons) { fonts_unload_custom_font(climacons); }
  climacons = fonts_load_custom_font(resource_get_handle(
    size == 48 ? RESOURCE_ID_FONT_CLIMACONS_48 :
    size == 40 ? RESOURCE_ID_FONT_CLIMACONS_40 :
                 RESOURCE_ID_FONT_CLIMACONS_28));
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
AppTimer *battery_sending = NULL;
AppTimer *timezone_request = NULL;
AppTimer *weather_request = NULL;
AppTimer *bottom_toggle = NULL;
// connected info
static bool bluetooth_connected = false;
// suppress vibration
static bool vibe_suppression = true;
int8_t timezone_offset = TIMEZONE_UNINITIALIZED;
struct tm *currentTime;
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

// define the appkeys used for appMessages
#define AK_STYLE_INV     0
#define AK_STYLE_DAY_INV 1
#define AK_STYLE_GRID    2
#define AK_VIBE_HOUR     3
#define AK_INTL_DOWO     4
#define AK_INTL_FMT_DATE 5 // INCOMPLETE
#define AK_STYLE_AM_PM   6
#define AK_STYLE_DAY     7
#define AK_STYLE_WEEK    8
#define AK_INTL_FMT_WEEK 9
#define AK_DEBUGGING_ON          10
#define AK_VIBE_PAT_DISCONNECT   11
#define AK_VIBE_PAT_CONNECT      12
#define AK_STRFTIME_FORMAT       13 // not implemented in config page, yet...
#define AK_TRACK_BATTERY         14 // UNUSED
#define AK_LANGUAGE              15
#define AK_DEBUGLANG_ON          16
#define AK_CAL_WEEK_PATTERN      17
#define AK_INV_SLOT_STAT         18 // UNUSED 
#define AK_INV_SLOT_TOP          19 // UNUSED
#define AK_INV_SLOT_BOT          20 // UNUSED
#define AK_SHOW_STAT_BAR         21
#define AK_SHOW_STAT_BATT        22
#define AK_SHOW_DATE             23 // UNUSED
#define AK_DND_START             24
#define AK_DND_STOP              25
#define AK_DND_NOACCEL           26 // UNUSED
#define AK_VIBE_START            27
#define AK_VIBE_STOP             28
#define AK_VIBE_DAYS             29 // UNUSED
#define AK_IDLE_REMINDER         30 // UNUSED
#define AK_IDLE_VIBE_PATT        31 // UNUSED
#define AK_IDLE_MESSAGE          32 // UNUSED
#define AK_IDLE_START            33 // UNUSED
#define AK_IDLE_STOP             34 // UNUSED
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
#define STAT_BATT_TOP         4
#define STAT_BATT_WIDTH      44 // should be divisible by 10, after subtracting 4 (2 pixels/side for the 'border')
#define STAT_BATT_HEIGHT     15
#define STAT_BATT_NIB_WIDTH   3 // >= 3
#define STAT_BATT_NIB_HEIGHT  5 // >= 3
#define STAT_BT_ICON_LEFT    -2 // 0
#define STAT_BT_ICON_TOP      2
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
static int CAL_WIDTH  = 20; // calendar column width (recomputed at runtime)
static int CAL_HEIGHT = 18; // calendar row height   (recomputed at runtime)

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
  CAL_WIDTH = L.cal_cell_w;
  CAL_HEIGHT = L.cal_cell_h;
  layout_store(L);
}

#define SLOT_ID_CLOCK_1  0
#define SLOT_ID_CALENDAR 1
#define SLOT_ID_WEATHER  2
#define SLOT_ID_CLOCK_2  3

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






// Format the current date per the configured format into a reusable static
// buffer (returned). update_date_text() drives the date layer; the "Date"
// complication uses the same formatter so it can live in any slot.
char *format_current_date(void) {

    //September 11, 2013 => 18 chars, 9 of which could potentially be dual byte utf8 characters
    //123456789012345678
    // The non-localized (195..254 table) and custom (255) strftime formats now
    // live in timefmt.c (datefmt_table_entry / datefmt_render), so they can be
    // host-unit-tested. See tests/test_timefmt.c.
    char date_text[32]; // >= 32: holds the widest custom_date_fmt (31 chars + NUL)
    static char date_string[64]; // localized "%s %s %s" date; sized to avoid truncation under modern gcc
    // http://www.cplusplus.com/reference/ctime/strftime/

    if (settings_get()->date_format < 195) { // localized date formats...
      char date_text_2[24];
      switch ( settings_get()->date_format ) {
      case 0: // MMMM DD, YYYY (localized)
        strftime(date_text, sizeof(date_text), "%d, %Y", currentTime); // DD, YYYY
        snprintf(date_string, sizeof(date_string), "%s %s", lang_months_get()->monthsNames[currentTime->tm_mon], date_text); // prefix Month
        break;
      case 1: // MMMM DD, 'YY (localized)
        strftime(date_text, sizeof(date_text), "%d, '%y", currentTime); // DD, 'YY
        snprintf(date_string, sizeof(date_string), "%s %s", lang_months_get()->monthsNames[currentTime->tm_mon], date_text); // prefix Month
        break;
      case 2: // Mmm DD, YYYY (localized)
        strftime(date_text, sizeof(date_text), "%d, %Y", currentTime); // DD, YYYY
        snprintf(date_string, sizeof(date_string), "%s %s", lang_gen_get()->abbrMonthsNames[currentTime->tm_mon], date_text); // prefix Mon
        break;
      case 3: // Mmm DD, 'YY (localized)
        strftime(date_text, sizeof(date_text), "%d, '%y", currentTime); // DD, 'YY
        snprintf(date_string, sizeof(date_string), "%s %s", lang_gen_get()->abbrMonthsNames[currentTime->tm_mon], date_text); // prefix Mon
        break;
      case 11: // D MMMM YYYY (localized)
        strftime(date_text, sizeof(date_text), "%d", currentTime); // D
        strftime(date_text_2, sizeof(date_text_2), "%Y", currentTime); // YYYY
        snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_months_get()->monthsNames[currentTime->tm_mon], date_text_2); // insert Month
        break;
      case 12: // D MMMM 'YY (localized)
        strftime(date_text, sizeof(date_text), "%d", currentTime); // D
        strftime(date_text_2, sizeof(date_text_2), "'%y", currentTime); // YY
        snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_months_get()->monthsNames[currentTime->tm_mon], date_text_2); // insert Month
        break;
      case 13: // D Mmm YYYY (localized)
        strftime(date_text, sizeof(date_text), "%d", currentTime); // D
        strftime(date_text_2, sizeof(date_text_2), "%Y", currentTime); // YYYY
        snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_gen_get()->abbrMonthsNames[currentTime->tm_mon], date_text_2); // insert Mon
        break;
      case 14: // D Mmm 'YY (localized)
        strftime(date_text, sizeof(date_text), "%d", currentTime); // D
        strftime(date_text_2, sizeof(date_text_2), "'%y", currentTime); // YY
        snprintf(date_string, sizeof(date_string), "%s %s %s", date_text, lang_gen_get()->abbrMonthsNames[currentTime->tm_mon], date_text_2); // insert Mon
        break;
      }
    } else { // non-localized date formats: custom (255) or table (195..254)
      // Sentinel 255 is resolved FIRST (custom format); 195..254 hit the table;
      // any other value yields "" -- no blind indexing, strftime return checked.
      datefmt_render(settings_get()->date_format, adv_settings_get()->custom_date_fmt,
                     currentTime, date_text, sizeof(date_text));
      snprintf(date_string, sizeof(date_string), "%s", date_text); // straight copy
    }

    return date_string;
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

void update_day_text(TextLayer *which_layer) {
  text_layer_set_text(which_layer, lang_days_get()->DaysOfWeek[currentTime->tm_wday]);
}

void update_month_text(TextLayer *which_layer) {
  text_layer_set_text(which_layer, lang_months_get()->monthsNames[currentTime->tm_mon]);
}

void update_week_text(TextLayer *which_layer) {
  static char week_text[] = "W00";
  char week_format[] = "W%V"; // V = ISO 8601 week number (00-53)
  if (settings_get()->week_format == 1) {
    // U = Week number with the first Sunday as the first day of week one (00-53)
    week_format[2] = 'U';
  } else if (settings_get()->week_format == 2) {
    // W = Week number with the first Monday as the first day of week one (00-53)
    week_format[2] = 'W';
  }
  strftime(week_text, sizeof(week_text), week_format, currentTime);
  text_layer_set_text(which_layer, week_text);
}

void update_ampm_text(TextLayer *which_layer) {
  if (currentTime->tm_hour < 12 ) {
    text_layer_set_text(which_layer, lang_gen_get()->abbrTime[0]); //  0-11 AM
  } else {
    text_layer_set_text(which_layer, lang_gen_get()->abbrTime[1]); // 12-23 PM
  }
}

void update_seconds_text(TextLayer *which_layer) {
  static char seconds_text[] = "00"; // 00-61
  strftime(seconds_text, sizeof(seconds_text), "%S", currentTime);
  text_layer_set_text(which_layer, seconds_text);
}

void update_location_text(TextLayer *which_layer) {
  // weather provider's location name (truncated to the slot width)
  text_layer_set_text(which_layer, weather_state()->city);
}

#ifndef PBL_PLATFORM_APLITE
// Minimal "[-]int[.frac]" parser (Pebble libc lacks atof).
static bool tl_parse_coord(const char *s, float *out) {
  if (!s || !s[0]) return false;
  int sign = 1; const char *p = s;
  if (*p == '-') { sign = -1; p++; } else if (*p == '+') { p++; }
  long ip = 0; float frac = 0.0f, scale = 0.1f; bool any = false;
  while (*p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; any = true; }
  if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { frac += (*p - '0') * scale; scale *= 0.1f; p++; any = true; } }
  if (!any) return false;
  *out = sign * (ip + frac);
  return true;
}

static void sun_time_text(TextLayer *layer, char *buf, bool want_sunset) {
  float lat, lon, sr, ss;
  if (currentTime && timezone_offset != TIMEZONE_UNINITIALIZED &&
      tl_parse_coord(adv_settings_get()->weather_lat, &lat) &&
      tl_parse_coord(adv_settings_get()->weather_lon, &lon)) {
    sun_times(lat, lon, currentTime->tm_yday, -timezone_offset / 4.0f, &sr, &ss);
    float h = want_sunset ? ss : sr;
    int hh = (int)h, mm = (int)((h - hh) * 60 + 0.5f);
    if (mm >= 60) { mm -= 60; hh++; }
    hh = ((hh % 24) + 24) % 24; // wrap to 0-23 (also bounds the format width)
    mm = ((mm % 60) + 60) % 60; // wrap to 0-59
    snprintf(buf, 16, "%d:%02d", hh, mm);
    text_layer_set_text(layer, buf);
  } else {
    text_layer_set_text(layer, "--:--"); // no location yet
  }
}
void update_sunrise_text(TextLayer *l) { static char b[16]; sun_time_text(l, b, false); }
void update_sunset_text(TextLayer *l)  { static char b[16]; sun_time_text(l, b, true); }
#else
void update_sunrise_text(TextLayer *l) { text_layer_set_text(l, "--:--"); }
void update_sunset_text(TextLayer *l)  { text_layer_set_text(l, "--:--"); }
#endif

// Moon phase from the date only (Conway's approximation; no coords needed).
void update_moon_text(TextLayer *which_layer) {
  static const char *PHASES[8] = { "New", "Wax cres", "1st qtr", "Wax gib",
                                   "Full", "Wan gib", "Last qtr", "Wan cres" };
  if (!currentTime) { text_layer_set_text(which_layer, "Moon"); return; }
  int y = currentTime->tm_year + 1900, m = currentTime->tm_mon + 1, d = currentTime->tm_mday;
  int r = y % 100; r %= 19; if (r > 9) { r -= 19; }
  r = ((r * 11) % 30) + m + d; if (m < 3) { r += 2; }
  r -= (y < 2000) ? 4 : 8;
  int age = ((r % 30) + 30) % 30; // 0..29 days into the lunation
  int phase = (age < 2 || age >= 28) ? 0 : (age < 6 ? 1 : (age < 9 ? 2 : (age < 13 ? 3 :
              (age < 17 ? 4 : (age < 20 ? 5 : (age < 24 ? 6 : 7))))));
  text_layer_set_text(which_layer, PHASES[phase]);
}

// Second time zone: local time shifted to clock2_tz (UTC offset, whole hours).
void update_clock2_text(TextLayer *which_layer) {
  static char buf[16];
  if (!currentTime || timezone_offset == TIMEZONE_UNINITIALIZED) {
    text_layer_set_text(which_layer, "--:--");
    return;
  }
  int local_tz_min = -timezone_offset * 15; // local UTC offset in minutes
  int utc_min = currentTime->tm_hour * 60 + currentTime->tm_min - local_tz_min;
  int second_min = (((utc_min + adv_settings_get()->clock2_tz * 60) % 1440) + 1440) % 1440;
  snprintf(buf, sizeof(buf), "%d:%02d", second_min / 60, second_min % 60);
  text_layer_set_text(which_layer, buf);
}

// System-info slots: the connection and the two batteries can be shown in any
// complication slot, so they share the same TextLayer renderer as the rest.
void update_wbatt_text(TextLayer *which_layer) {
  static char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", battery_percent);
  text_layer_set_text(which_layer, buf);
}

void update_pbatt_text(TextLayer *which_layer) {
  static char buf[16];
  if (phone_battery_percent >= 0) {
    snprintf(buf, sizeof(buf), "%d%%", phone_battery_percent);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  text_layer_set_text(which_layer, buf);
}

void update_conn_text(TextLayer *which_layer) {
  text_layer_set_text(which_layer, bluetooth_connected
    ? lang_gen_get()->statuses[0] : lang_gen_get()->statuses[1]);
}

char * get_doy_text() {
  static char doy_text[] = "D000";
  strftime(doy_text, sizeof(doy_text), "D%j", currentTime);
  return doy_text;
}

char * get_dliy_text() {
  static char dliy_text[] = "R000";
  int days_left = days_left_in_year(currentTime->tm_year + 1900, currentTime->tm_yday);
  format_days_left_in_year(days_left, dliy_text, sizeof(dliy_text));
  return dliy_text;
}

void update_doy_text(TextLayer *which_layer) {
  text_layer_set_text(which_layer, get_doy_text());
}

void update_dliy_text(TextLayer *which_layer) {
  text_layer_set_text(which_layer, get_dliy_text());
}

void update_doy_dliy_text(TextLayer *which_layer) {
  static char doy_dliy_text[] = "D000/R000";
  snprintf(doy_dliy_text, sizeof(doy_dliy_text), "%s/%s", get_doy_text(), get_dliy_text());
  text_layer_set_text(which_layer, doy_dliy_text);
}

void update_timezone_text(TextLayer *which_layer) {
  static char timezone_text[16];
  format_timezone_offset(timezone_offset, timezone_text, sizeof(timezone_text));
  text_layer_set_text(which_layer, timezone_text);
}

// All three below-time slots ("complications") share the same content menu.
// Keep these values in sync with src/js/config.js.
void update_slot_text(TextLayer *layer, uint8_t content) {
  switch (content) {
  case 1:  update_day_text(layer);       break; // Day name
  case 2:  update_month_text(layer);     break; // Month name
  case 3:  update_week_text(layer);      break; // Week number
  case 4:  update_timezone_text(layer);  break; // Timezone
  case 5:  update_ampm_text(layer);      break; // AM/PM
  case 6:  update_doy_text(layer);       break; // Day of year
  case 7:  update_dliy_text(layer);      break; // Days left in year
  case 8:  update_doy_dliy_text(layer);  break; // Day of year / left (alternating)
  case 9:  update_seconds_text(layer);   break; // Seconds
  case 10: update_location_text(layer);  break; // Weather location
  case 11: update_sunrise_text(layer);   break; // Sunrise
  case 12: update_sunset_text(layer);    break; // Sunset
  case 13: update_moon_text(layer);      break; // Moon phase
  case 14: update_clock2_text(layer);    break; // Second time zone
  case 15: update_wbatt_text(layer);     break; // Watch battery
  case 16: update_pbatt_text(layer);     break; // Phone battery
  case 17: update_conn_text(layer);      break; // Bluetooth connection
  case 18: text_layer_set_text(layer, format_current_date()); break; // Date
  default: break;                                // 0 = hidden
  }
}

// Lay out a two-slot complication row at (top,h): both set -> halves; one set ->
// full-width centered in the left layer; none -> both hidden.
static void layout_two_slots(TextLayer *l, TextLayer *r, uint8_t cl, uint8_t cr, int top, int h) {
  int half = DEVICE_WIDTH / 2;
  Layer *ll = text_layer_get_layer(l), *rl = text_layer_get_layer(r);
  if (cl && cr) {
    layer_set_frame(ll, GRect(2, top, half - 4, h));        text_layer_set_text_alignment(l, GTextAlignmentLeft);
    layer_set_frame(rl, GRect(half + 2, top, half - 4, h)); text_layer_set_text_alignment(r, GTextAlignmentRight);
    layer_set_hidden(ll, false); layer_set_hidden(rl, false);
    update_slot_text(l, cl); update_slot_text(r, cr);
  } else if (cl || cr) {
    layer_set_frame(ll, GRect(2, top, DEVICE_WIDTH - 4, h)); text_layer_set_text_alignment(l, GTextAlignmentCenter);
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
  switch (content) {
  case 15: return image_watch_icon;  // Watch battery
  case 16: return image_phone_icon;  // Phone battery
  case 17: return image_bt16_icon;   // Bluetooth
  default: return NULL;              // text-only complication
  }
}

static bool is_battery_content(uint8_t c) { return c == 15 || c == 16; }
static bool batt_style_is_bar(uint8_t s) { return s == 0 || s == 3; } // 0 bar, 3 bar+icon

// Whether the charging/DND/hourvibe icon is currently visible; the right
// battery bar yields 22px to it (see batt_box_geom).
static bool s_status_icon_shown = false;

// Battery-bar box geometry for one side. with_icon (bar+icon style) reserves a
// leading 18px for the glyph on the outer edge and narrows the box.
static void batt_box_geom(bool is_right, bool with_icon, int *bx, int *bw) {
  int half = DEVICE_WIDTH / 2;
  if (is_right) {
    *bx = half + 2;
    *bw = with_icon ? half - 22 : half - 12;
    if (s_status_icon_shown) { *bx += 22; *bw -= 22; } // status icon docks at half+2
  } else {
    *bx = with_icon ? 20 : 2;
    *bw = with_icon ? half - 28 : half - 10;
  }
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

void position_day_layer() {
  apply_bottom(); // bottom row owns the above-calendar band now
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
    (void)me;
    // TODO: these calls can probably be reduced/optimized, but apparently not removed entirely!
    setColors(ctx);
    update_date_text();
    update_time_text();
    update_datetime_subtext();
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
    // icon(s)
    layer_add_child(statusbar, bitmap_layer_get_layer(bmp_charging_layer));
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
    // icon(s)
    layer_add_child(datetime_layer, bitmap_layer_get_layer(bmp_charging_layer));
    layer_add_child(datetime_layer, battery_layer);
  }
  position_date_layer();
}

void slot_status_layer_update_callback(Layer *me, GContext* ctx) {
}

void statusbar_layer_update_callback(Layer *me, GContext* ctx) {
// XXX positioning tests... only valid if we leave statusbar's frame/bounds set to the whole watch...
/*
    setColors(ctx);
    graphics_draw_rect(ctx, GRect(0,  0, 144, 24)); // statusbar
    graphics_draw_rect(ctx, GRect(0, 24, 144, 72)); // top half
    graphics_draw_rect(ctx, GRect(0, 96, 144, 72)); // bottom half
*
    graphics_draw_rect(ctx, GRect(0, 50, 20, 20)); // linked
    graphics_draw_rect(ctx, GRect(0, 72, 20, 20)); // icon 2
    graphics_draw_rect(ctx, GRect(0, 50, 10, 42)); // battery l
    graphics_draw_rect(ctx, GRect(144-10, 50, 10, 42)); // battery r
    graphics_draw_rect(ctx, GRect(144-20, 50, 20, 20)); // icon 3
    graphics_draw_rect(ctx, GRect(144-20, 72, 20, 20)); // icon 4
    graphics_draw_rect(ctx, GRect(0, 46, 144, 50)); // targeting time
*/
}

void slot_top_layer_update_callback(Layer *me, GContext* ctx) {
// TODO: configurable: draw appropriate slot
}

void slot_bot_layer_update_callback(Layer *me, GContext* ctx) {
// TODO: configurable: draw appropriate slot
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

static void request_weather(void *data) {
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
  weather_request = NULL;
}

static void request_timezone(void *data) {
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
  timezone_request = NULL;
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
    battery_sending = NULL;
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
  battery_sending = NULL;
}

void set_status_charging_icon() {
  if (DEVICE_WIDTH >= 180) { refresh_status_tray(); return; } // wide: tray under the clock
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
  GColor fg = theme_palette().fg;
  text_layer_set_text_color(time_layer, fg);
  text_layer_set_text_color(date_layer, fg);
  if (ctr_r_layer) { text_layer_set_text_color(ctr_r_layer, fg); }
  text_layer_set_text_color(day_layer, fg);
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
    text_layer_set_font(day_layer,unifont_16);
    text_layer_set_font(text_connection_layer, unifont_16);
    text_layer_set_font(date_layer, unifont_16);
    // set fonts, for calendar
    cal_normal = unifont_16; // fh = 16
    cal_bold   = unifont_16_bold; // fh = 22 // XXX TODO need a bold unicode/unifont option... maybe invert it or box it or something?
  } else { // Standard font
    // set fonts...
    text_layer_set_font(day_layer,fonts_get_system_font(FONT_KEY_GOTHIC_14));
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
  position_day_layer();
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

void set_layer_attr_cfont(TextLayer *textlayer, uint32_t FontResHandle, GTextAlignment Alignment) {
  set_layer_attr(textlayer, Alignment);
  text_layer_set_font(textlayer, fonts_load_custom_font(resource_get_handle(FontResHandle)));
}

static void window_load(Window *window) {

  unifont_16 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UNICODE_16));
  unifont_16_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_UNICODE_BOLD_16));
  cal_normal = unifont_16;
  cal_bold   = unifont_16_bold;

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  compute_layout(bounds.size.w, bounds.size.h);
  // Glyph size follows the time band (like the clock font); loads the font too.
  ensure_climacons(weather_glyph_size_for(DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT));

  slot_status = layer_create(GRect(0,LAYOUT_STAT,DEVICE_WIDTH,LAYOUT_SLOT_TOP));
  //slot_status = layer_create(GRect(0,0,DEVICE_WIDTH,DEVICE_HEIGHT));
  layer_set_update_proc(slot_status, slot_status_layer_update_callback);
  layer_add_child(window_layer, slot_status);

  statusbar = layer_create(GRect(0,LAYOUT_STAT,DEVICE_WIDTH,LAYOUT_SLOT_TOP));
  layer_set_update_proc(statusbar, statusbar_layer_update_callback);
  layer_add_child(slot_status, statusbar);
  GRect stat_bounds = layer_get_bounds(statusbar);

  slot_top = layer_create(GRect(0,LAYOUT_SLOT_TOP,DEVICE_WIDTH,LAYOUT_SLOT_HEIGHT));
  layer_set_update_proc(slot_top, slot_top_layer_update_callback);
  layer_add_child(window_layer, slot_top);
  GRect slot_top_bounds = layer_get_bounds(slot_top);

  slot_bot = layer_create(GRect(0,LAYOUT_SLOT_BOT,DEVICE_WIDTH,LAYOUT_SLOT_BOT_HEIGHT));
  layer_set_update_proc(slot_bot, slot_bot_layer_update_callback);
  layer_add_child(window_layer, slot_bot);
  GRect slot_bot_bounds = layer_get_bounds(slot_bot);

  bmp_charging_layer = bitmap_layer_create( GRect(STAT_CHRG_ICON_LEFT, STAT_CHRG_ICON_TOP, 20, 20) );
  bitmap_layer_set_compositing_mode(bmp_charging_layer, GCompOpSet);
  layer_add_child(statusbar, bitmap_layer_get_layer(bmp_charging_layer));
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

  if (DEVICE_WIDTH >= 180) { // status tray under the clock (see refresh_status_tray)
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
  day_layer = text_layer_create( GRect(4, REL_CLOCK_SUBTEXT_TOP, REL_CLOCK_DATE_WIDTH, 22) );
  set_layer_attr_sfont(day_layer, FONT_KEY_GOTHIC_18, GTextAlignmentCenter);
  layer_add_child(datetime_layer, text_layer_get_layer(day_layer));
  layer_set_hidden(text_layer_get_layer(day_layer), true);

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
  layer_destroy(text_layer_get_layer(day_layer));
  layer_destroy(text_layer_get_layer(week_layer));
  layer_destroy(text_layer_get_layer(time_layer));
  layer_destroy(text_layer_get_layer(date_layer));
  layer_destroy(text_layer_get_layer(ctr_r_layer));
  weather_destroy();
  splash_destroy();
  calendar_destroy();
  layer_destroy(datetime_layer);
  layer_destroy(battery_layer);
  // custom fonts are automatically unloaded at exit - http://forums.getpebble.com/discussion/comment/35808/#Comment_35808
  layer_remove_from_parent(bitmap_layer_get_layer(bmp_charging_layer));
  bitmap_layer_destroy(bmp_charging_layer);
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
  if (dnd_period_active || battery_plugged) {
    vibe_suppression = true;
  } else if (settings_get()->vibe_hour && vibe_period_active) {
    vibe_suppression = false;
  } else {
    vibe_suppression = false;
  }
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
  *currentTime = *tick_time;
  apply_palette();
  update_time_text();
  refresh_stat_slots(); // keep time-based status-bar slots current
  if ( currentTime->tm_min % 10 == 0) {
    dnd_period_check();
    hourvibe_period_check();
    set_status_charging_icon();
    handle_vibe_suppression();
  }
  if (bluetooth_connected && adv_settings_get()->weather_update) {
    if (adv_settings_get()->weather_update && (currentTime->tm_min + 60) % adv_settings_get()->weather_update == 0) {
      weather_request = app_timer_register(1000, &request_weather, NULL);
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

void handle_second_tick(struct tm *tick_time, TimeUnits units_changed)
{
  *currentTime = *tick_time;
  // update the seconds layer(s)... (9 == Seconds in the unified slot menu)
  if (settings_get()->show_week == 9) {
    update_seconds_text(week_layer);
  }
  if (settings_get()->show_day == 9) {
    update_seconds_text(day_layer);
  }
  if (settings_get()->show_am_pm == 9) {
    update_seconds_text(ampm_layer);
  }
  // redraw everything else if the minute changes...
  if (units_changed & MINUTE_UNIT) {
    handle_minute_tick(tick_time, units_changed);
  }
}

static int need_second_tick_handler(void) {
  // 9 == Seconds in the unified slot menu
  if ((settings_get()->show_week == 9) || (settings_get()->show_day == 9) || (settings_get()->show_am_pm == 9)) { return 1; }
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
    if (appkey != NULL)     { strncpy(weather_state()->condition, appkey->value->cstring, sizeof(weather_state()->condition)-1); }
    appkey = dict_find(received, AK_WEATHER_CITY);
    if (appkey != NULL)     { strncpy(weather_state()->city, appkey->value->cstring, sizeof(weather_state()->city)-1); }
    // Coordinates feed the sunrise/sunset complications and the Auto theme.
    Tuple *lat = dict_find(received, AK_WEATHER_LAT);
    Tuple *lon = dict_find(received, AK_WEATHER_LON);
    if (lat != NULL && lon != NULL) {
      strncpy(adv_settings_get()->weather_lat, lat->value->cstring, sizeof(adv_settings_get()->weather_lat)-1);
      strncpy(adv_settings_get()->weather_lon, lon->value->cstring, sizeof(adv_settings_get()->weather_lon)-1);
      persist_write_data(PK_ADV_SETTINGS, adv_settings_get(), sizeof(persist_adv_settings));
      apply_palette(); // Auto theme may flip with a known location
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
      settings_get()->dayOfWeekOffset = INTL_DOWO->value->uint8;
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
      strncpy(adv_settings_get()->custom_date_fmt, sfmt->value->cstring, sizeof(adv_settings_get()->custom_date_fmt)-1);
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

    // AK_STYLE_DAY
    Tuple *style_day = dict_find(received, AK_STYLE_DAY);
    if (style_day != NULL) {
      settings_get()->show_day = style_day->value->uint8;
      if ( settings_get()->show_day ) {
        layer_set_hidden(text_layer_get_layer(day_layer), false);
      }  else {
        layer_set_hidden(text_layer_get_layer(day_layer), true);
      }
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

    // AK_INV_SLOT_STAT == invert slot (or not!) // TODO, UNUSED
    appkey = dict_find(received, AK_INV_SLOT_STAT);
    if (appkey != NULL) { adv_settings_get()->invertStatBar = appkey->value->uint8; }

    // AK_INV_SLOT_TOP == invert slot (or not!) // TODO, UNUSED
    appkey = dict_find(received, AK_INV_SLOT_TOP);
    if (appkey != NULL) { adv_settings_get()->invertTopSlot = appkey->value->uint8; }

    // AK_INV_SLOT_BOT == invert slot (or not!) // TODO, UNUSED
    appkey = dict_find(received, AK_INV_SLOT_BOT);
    if (appkey != NULL) { adv_settings_get()->invertBotSlot = appkey->value->uint8; }

    // AK_SHOW_STAT_BAR == show statusbar
    appkey = dict_find(received, AK_SHOW_STAT_BAR);
    if (appkey != NULL) { adv_settings_get()->showStatus = appkey->value->uint8; }

    // AK_SHOW_STAT_BATT == statusbar battery limit
    appkey = dict_find(received, AK_SHOW_STAT_BATT);
    if (appkey != NULL) { adv_settings_get()->showStatusBat = appkey->value->uint8; }

    // AK_CLOCK2_TZ == second time zone UTC offset (whole hours, signed)
    appkey = dict_find(received, AK_CLOCK2_TZ);
    if (appkey != NULL) { adv_settings_get()->clock2_tz = appkey->value->int8; }

    // AK_SHOW_DATE == show date // TODO, UNUSED
    appkey = dict_find(received, AK_SHOW_DATE);
    if (appkey != NULL) { adv_settings_get()->showDate = appkey->value->uint8; }

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

    // AK_VIBE_DAYS == days to do hourly vibration // TODO, UNUSED
    appkey = dict_find(received, AK_VIBE_DAYS);
    if (appkey != NULL) { adv_settings_get()->vibe_hour_days = appkey->value->uint8; }

    // AK_IDLE_REMINDER == period stop, VIBE // TODO, UNUSED
    appkey = dict_find(received, AK_IDLE_REMINDER);
    if (appkey != NULL) { adv_settings_get()->idle_reminder = appkey->value->uint8; }

    // AK_IDLE_VIBE_PATT == idle vibration pattern // TODO, UNUSED
    appkey = dict_find(received, AK_IDLE_VIBE_PATT);
    if (appkey != NULL) { adv_settings_get()->idle_pattern = appkey->value->uint8; }

/* TODO
    // AK_IDLE_MESSAGE == Idle message // TODO, UNUSED
    appkey = dict_find(received, AK_IDLE_MESSAGE);
    if (appkey != NULL) { strncpy(adv_settings_get()->idle_message, appkey->value->cstring, sizeof(adv_settings_get()->idle_message)-1); }
*/

    // AK_IDLE_START == period start, Idle reminder // TODO, UNUSED
    appkey = dict_find(received, AK_IDLE_START);
    if (appkey != NULL) { adv_settings_get()->idle_start = appkey->value->uint8; }

    // AK_IDLE_STOP == period stop, Idle reminder // TODO, UNUSED
    appkey = dict_find(received, AK_IDLE_STOP);
    if (appkey != NULL) { adv_settings_get()->idle_stop = appkey->value->uint8; }

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
      if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Language is set to %s", chosen_language->value->cstring); }
      strncpy(lang_gen_get()->language, chosen_language->value->cstring, sizeof(lang_gen_get()->language)-1);
      set_unifont();
    }

    // AK_TRANS_ABBR_*DAY == abbrDaysOfWeek // localized Su Mo Tu We Th Fr Sa, max 2 characters
    for (int i = AK_TRANS_ABBR_SUNDAY; i <= AK_TRANS_ABBR_SATURDAY; i++ ) {
      translation = dict_find(received, i);
      if (translation != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, translation->value->cstring); }
        strncpy(lang_gen_get()->abbrDaysOfWeek[i - AK_TRANS_ABBR_SUNDAY], translation->value->cstring, sizeof(lang_gen_get()->abbrDaysOfWeek[i - AK_TRANS_ABBR_SUNDAY])-1);
      }
    }

    // AK_TRANS_*DAY == daysOfWeek // localized Sunday through Saturday, max 12 characters
    for (int i = AK_TRANS_SUNDAY; i <= AK_TRANS_SATURDAY; i++ ) {
      translation = dict_find(received, i);
      if (translation != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, translation->value->cstring); }
        strncpy(lang_days_get()->DaysOfWeek[i - AK_TRANS_SUNDAY], translation->value->cstring, sizeof(lang_days_get()->DaysOfWeek[i - AK_TRANS_SUNDAY])-1);
      }
    }

    // AK_TRANS_ABBR_*MONTH == monthsOfYear // localized month name abbreviations, max 3 characters
    for (int i = AK_TRANS_ABBR_JANUARY; i <= AK_TRANS_ABBR_DECEMBER; i++ ) {
      translation = dict_find(received, i);
      if (translation != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, translation->value->cstring); }
        strncpy(lang_gen_get()->abbrMonthsNames[i - AK_TRANS_ABBR_JANUARY], translation->value->cstring, sizeof(lang_gen_get()->abbrMonthsNames[i - AK_TRANS_ABBR_JANUARY])-1);
      }
    }

    // AK_TRANS_*MONTH == monthsOfYear // localized month names, max 12 characters
    for (int i = AK_TRANS_JANUARY; i <= AK_TRANS_DECEMBER; i++ ) {
      translation = dict_find(received, i);
      if (translation != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, translation->value->cstring); }
        strncpy(lang_months_get()->monthsNames[i - AK_TRANS_JANUARY], translation->value->cstring, sizeof(lang_months_get()->monthsNames[i - AK_TRANS_JANUARY])-1);
      }
    }

    // AK_TRANS_CONNECTED / AK_TRANS_DISCONNECTED == status text, e.g. "Linked" "NOLINK", max 9 characters
    for (int i = AK_TRANS_CONNECTED; i <= AK_TRANS_DISCONNECTED; i++ ) {
      translation = dict_find(received, i);
      if (translation != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, translation->value->cstring); }
        strncpy(lang_gen_get()->statuses[i - AK_TRANS_CONNECTED], translation->value->cstring, sizeof(lang_gen_get()->statuses[i - AK_TRANS_CONNECTED])-1);
      }
    }
    vibe_suppression = true;
    update_connection();
    handle_vibe_suppression();

    // AK_TRANS_TIME_AM / AK_TRANS_TIME_PM == AM / PM text, e.g. "AM" "PM" :), max 6 characters
    for (int i = AK_TRANS_TIME_AM; i <= AK_TRANS_TIME_PM; i++ ) {
      translation = dict_find(received, i);
      if (translation != NULL) {
        if (debug_get()->language) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "translation for key %d is %s", i, translation->value->cstring); }
        strncpy(lang_gen_get()->abbrTime[i - AK_TRANS_TIME_AM], translation->value->cstring, sizeof(lang_gen_get()->abbrTime[i - AK_TRANS_TIME_AM])-1);
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
  //app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  app_message_open(1280, 512);
}


static void init(void) {

  if (DEBUGLOG == 1) { debug_get()->general = true; }
  if (TRANSLOG == 1) { debug_get()->language = true; }
  currentTime = get_time();

  app_message_init();

  if (persist_exists(PK_SETTINGS)) {
    persist_read_data(PK_SETTINGS, settings_get(), sizeof(persist) );
    if (settings_get()->version == 11) { // v11 -> v12 bugfix
      settings_get()->date_format = datefmt_migrate_v11_to_v12(settings_get()->date_format);
      settings_get()->version = 12;
    }
    // Clamp whatever we loaded/migrated to the renderer's safe set.
    settings_get()->date_format = datefmt_clamp(settings_get()->date_format);
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
  }
  // re-initialize this, if it was set, since we're storing those values persistently as well...
  if (DEBUGLOG == 1) { debug_get()->general = true; }
  if (TRANSLOG == 1) { debug_get()->language = true; }

  if (adv_settings_get()->weather_update) {
    weather_request = app_timer_register(1250, &request_weather, NULL);
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
