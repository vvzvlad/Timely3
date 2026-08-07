#include "theme.h"
#include "ui.h"        // window, currentTime
#include "settings.h"  // settings_get()->theme / theme_mode
#include "suntimes.h"
#include "timefmt.h"  // TIMEZONE_UNINITIALIZED

// Palettes: [theme][0=light, 1=dark]. Tune these via emulator screenshots.
static const Palette PALETTES[THEME_COUNT][2] = {
  // THEME_MONO — pure black & white (the Pebble 2 Duo look, even on color)
  { { GColorWhite, GColorBlack, GColorBlack, GColorWhite, GColorBlack, GColorBlack, GColorWhite },
    { GColorBlack, GColorWhite, GColorWhite, GColorBlack, GColorWhite, GColorWhite, GColorBlack } },
  // THEME_FUNCTIONAL — color carries meaning (today / weekend / warn)
  { { GColorWhite, GColorBlack, GColorJaegerGreen, GColorWhite, GColorRed,  GColorRed, GColorLightGray },
    { GColorBlack, GColorWhite, GColorJaegerGreen, GColorWhite, GColorMelon, GColorRed, GColorDarkGray } },
  // THEME_MINIMAL — monochrome + a single accent
  { { GColorWhite, GColorBlack, GColorBlue,          GColorWhite, GColorBlack, GColorBlack, GColorWhite },
    { GColorBlack, GColorWhite, GColorVividCerulean, GColorBlack, GColorWhite, GColorWhite, GColorBlack } },
  // THEME_VIBRANT — bold flat colors
  { { GColorWhite,      GColorBlack, GColorOrange,       GColorWhite, GColorMagenta,       GColorRed, GColorPastelYellow },
    { GColorOxfordBlue, GColorWhite, GColorChromeYellow, GColorBlack, GColorBrilliantRose, GColorRed, GColorDukeBlue } },
};

#ifndef PBL_PLATFORM_APLITE
// Minimal "[-]int[.frac]" coordinate parser (Pebble libc lacks atof).
static bool parse_coord(const char *s, float *out) {
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
#endif

static bool is_night(void) {
  if (!currentTime) return true;
#ifndef PBL_PLATFORM_APLITE
  // Sunset/sunrise from the configured weather location. Skipped on aplite,
  // whose 24KB app budget can't afford the float trig; it uses the window below.
  float lat, lon;
  if (timezone_offset != TIMEZONE_UNINITIALIZED &&
      parse_coord(adv_settings_get()->weather_lat, &lat) &&
      parse_coord(adv_settings_get()->weather_lon, &lon)) {
    float now = currentTime->tm_hour + currentTime->tm_min / 60.0f;
    float tz = -timezone_offset / 4.0f; // stored as quarter-hours, inverted sign
    return sun_is_night(lat, lon, currentTime->tm_yday, tz, now);
  }
#endif
  // No location (or aplite): simple 19:00-06:59 night window.
  return (currentTime->tm_hour >= 19 || currentTime->tm_hour < 7);
}

Palette theme_palette(void) {
  uint8_t theme = settings_get()->theme;
  if (theme >= THEME_COUNT) theme = THEME_FUNCTIONAL;
  uint8_t mode = settings_get()->theme_mode;
  int dark = (mode == THEME_MODE_DARK) || (mode == THEME_MODE_AUTO && is_night());
  return PALETTES[theme][dark ? 1 : 0];
}

void setColors(GContext* ctx) {
  Palette p = theme_palette();
  // Window background is set once per relayout/tick in apply_palette(), not here;
  // these setters only touch ctx colors so a draw callback can stay a no-op.
  graphics_context_set_stroke_color(ctx, p.fg);
  graphics_context_set_fill_color(ctx, p.bg);
  graphics_context_set_text_color(ctx, p.fg);
}

void setInvColors(GContext* ctx) {
  Palette p = theme_palette();
  // Ctx colors only; window bg is owned by apply_palette() (see setColors).
  graphics_context_set_stroke_color(ctx, p.bg);
  graphics_context_set_fill_color(ctx, p.fg);
  graphics_context_set_text_color(ctx, p.bg);
}

void setTodayColors(GContext* ctx) {
  Palette p = theme_palette();
  // Ctx colors only; window bg is owned by apply_palette() (see setColors).
  graphics_context_set_stroke_color(ctx, p.accent_fg);
  graphics_context_set_fill_color(ctx, p.accent);
  graphics_context_set_text_color(ctx, p.accent_fg);
}

GColor weather_glyph_color(char glyph) {
  Palette p = theme_palette();
#ifdef PBL_COLOR
  uint8_t style = adv_settings_get()->weather_icons;
  if (style == WEATHER_ICONS_BW) { return p.fg; } // forced monochrome
  // Default follows the Mono theme's single-colour look; Colour overrides it.
  if (style == WEATHER_ICONS_DEFAULT && settings_get()->theme == THEME_MONO) { return p.fg; }
  // Saturated tints chosen to read on both light and dark backgrounds. Clouds,
  // fog, tornado and anything unmapped stay the theme fg (which reads anywhere).
  switch (glyph) {
    case 'I':                                                    // sun
      return GColorChromeYellow;
    case 'J': case 'K':                                          // sunset / sunrise
      return GColorOrange;
    case 'N': case 'O': case 'P': case 'Q': case 'R':            // moon phases
    case 'S': case 'T': case 'U': case 'V':
      return GColorPastelYellow;
    case '$': case '%': case '&':                                // rain
    case '\'': case '(': case ')':                               // showers
    case '*': case '+': case ',':                                // downpour
    case '-': case '.': case '/':                                // drizzle
      return GColorVividCerulean;
    case '0': case '1': case '2':                                // sleet
    case '3': case '4': case '5':                                // hail
      return GColorBlue;
    case '6': case '7': case '8':                                // flurries
    case '9': case ':': case ';':                                // snow
    case 'W':                                                    // snowflake
      return GColorVividCerulean;
    case 'B': case 'C': case 'D': case 'E':                      // wind
      return GColorJaegerGreen;
    case 'F': case 'G': case 'H':                                // lightning
      return GColorChromeYellow;
    default:                                                     // clouds, fog, haze, …
      return p.fg;
  }
#else
  (void)glyph;
  return p.fg;
#endif
}
