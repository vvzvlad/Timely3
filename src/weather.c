#include "weather.h"
#include "theme.h"
#include "debug.h"
#include "ui.h"

// Single owner of the weather state (default: 999 = "N/A", 'h' = updating glyph).
static weather_data s_weather = {
  .current   = 999,
  .condition = {'h'},
  .requests  = 0,
  .failures  = 0,
  .city      = "",
};

weather_data *weather_state(void) { return &s_weather; }

static Layer *s_weather_layer;
static int s_glyph_size = 40; // climacons px: 28 (narrow) / 40 / 48 (tall wide band)

void weather_set_glyph_size(int size) { s_glyph_size = size; }

static void weather_render(Layer *me, GContext *ctx) {
  static char temp_current[12] = "N/A";
  static char cond_current[4] = "0";
  if (weather_state()->current < 900) {
    snprintf(temp_current, sizeof(temp_current), "%d°", weather_state()->current);
  } else {
    snprintf(temp_current, sizeof(temp_current), "N/A");
  }
  cond_current[0] = weather_state()->condition[0]; // single climacons glyph
  cond_current[1] = '\0';

  setColors(ctx);
  // The layer IS the time band. The icon-over-temperature block is centred in
  // the band so it lines up with the clock and fills the column (big glyph on
  // wide screens), without spilling into the calendar below.
  bool compact = (s_glyph_size == 28);
  GFont temp_font = fonts_get_system_font(compact ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_28);
  int icon_w = compact ? 34 : (s_glyph_size == 48 ? 50 : 42); // climacons box
  int gap    = compact ? 18 : (s_glyph_size == 48 ? 36 : 32); // temp baseline below icon top
  int temp_h = compact ? 18 : 28;
  int band_h = layer_get_bounds(me).size.h;
  int block  = gap + temp_h;           // total visual height of icon+temp
  int top = (band_h - block) / 2;
  if (top < 0) { top = 0; }
  // Tint the condition glyph (color platforms, non-Mono theme); the temperature
  // stays the theme color, so restore it afterwards.
  graphics_context_set_text_color(ctx, weather_glyph_color(cond_current[0]));
  // Skip the glyph if the climacons font failed to load (draw-through-NULL guard).
  if (climacons) {
    graphics_draw_text(ctx, cond_current, climacons, GRect(2, top, icon_w, gap + 8), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
  graphics_context_set_text_color(ctx, theme_palette().fg);
  graphics_draw_text(ctx, temp_current, temp_font, GRect(2, top + gap, icon_w + 2, temp_h + 8), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Weather redrawing: %d, %s", weather_state()->current, weather_state()->condition); }
}

void weather_create(Layer *parent, GRect frame) {
  s_weather_layer = layer_create(frame);
  layer_set_update_proc(s_weather_layer, weather_render);
  layer_add_child(parent, s_weather_layer);
}
void weather_destroy(void)            { layer_destroy(s_weather_layer); }
void weather_set_frame(GRect frame)   { layer_set_frame(s_weather_layer, frame); }
void weather_set_hidden(bool hidden)  { layer_set_hidden(s_weather_layer, hidden); }
void weather_mark_dirty(void)         { layer_mark_dirty(s_weather_layer); }
