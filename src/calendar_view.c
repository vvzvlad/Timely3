#include "calendar.h"
#include "calendar_view.h"
#include "layout.h"
#include "theme.h"
#include "locale.h"
#include "settings.h"
#include "debug.h"
#include "ui.h"
#include <pebble.h>

static Layer *s_calendar_layer;

static void calendar_render(Layer *me, GContext* ctx) {
    (void)me;
    TimelyLayout L = layout_get();
    const int CAL_WIDTH = L.cal_cell_w, CAL_HEIGHT = L.cal_cell_h, DEVICE_WIDTH = L.slot_bot.w;
    const int CAL_DAYS = 7, CAL_GAP = 1, CAL_LEFT = 2;

    (void)me;

    CalGrid grid = calendar_build(currentTime->tm_year + 1900,
                                  currentTime->tm_mon,
                                  currentTime->tm_mday,
                                  currentTime->tm_wday,
                                  settings_get()->dayOfWeekOffset,
                                  adv_settings_get()->week_pattern);
    int *calendar = grid.days;
    int specialDay = grid.special_col;
    Palette pal = theme_palette();
    if (debug_get()->general) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Calendar - sCol: %d, sRow: %d", grid.special_col, grid.special_row); }

// ---------------------------
// Now that we've calculated which days go where, we'll move on to the display logic.
// ---------------------------


    int weeks  =  3;  // always display 3 weeks: # previous, current, # next
        
    GFont current = cal_normal;
    int font_vert_offset = -4; // lift the weekday header clear of the grid below it
    if (strcmp(lang_gen_get()->language,"RU") == 0 ) { font_vert_offset = -6; }

    // generate a light background for the calendar grid
    if (settings_get()->grid) {
      setInvColors(ctx);
      graphics_fill_rect(ctx, GRect (CAL_LEFT + CAL_GAP, CAL_HEIGHT - CAL_GAP, DEVICE_WIDTH - 2 * (CAL_LEFT + CAL_GAP), CAL_HEIGHT * weeks), 0, GCornerNone);
      setColors(ctx);
    }
    for (int col = 0; col < CAL_DAYS; col++) {

      // Adjust labels by specified offset
      int weekday = col + settings_get()->dayOfWeekOffset;
      weekday %= 7; // keep index in 0..6 even if the offset is out of range
      bool weekend = (weekday == 0 || weekday == 6); // Sun / Sat
      graphics_context_set_text_color(ctx, weekend ? pal.weekend : pal.fg);

      if (col == specialDay) {
        // Today's header is bold but the SAME size as the others (cal_bold is the
        // big 18px date font, which overflows onto the grid); RU has no Latin
        // bold so it keeps the unicode font + double-strike below.
        if (strcmp(lang_gen_get()->language,"RU") == 0 ) {
          current = cal_bold;
          font_vert_offset = -6;
        } else {
          current = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
          font_vert_offset = -4; // same size as the others -> same offset
        }
      }
      // draw the cell background
    //  graphics_fill_rect(ctx, GRect (CAL_WIDTH * col + CAL_LEFT + CAL_GAP, 0, CAL_WIDTH - CAL_GAP, CAL_HEIGHT - CAL_GAP), 0, GCornerNone);

      // draw the cell text
      graphics_draw_text(ctx, lang_gen_get()->abbrDaysOfWeek[weekday], current, GRect(CAL_WIDTH * col + CAL_LEFT + CAL_GAP, CAL_GAP + font_vert_offset, CAL_WIDTH, CAL_HEIGHT), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); 
      if (col == specialDay) {
        if (strcmp(lang_gen_get()->language,"RU") == 0 ) {
          // we don't actually have a bold font for this, so we'll use font double-striking to simulate bold
          graphics_draw_text(ctx, lang_gen_get()->abbrDaysOfWeek[weekday], current, GRect(CAL_WIDTH * col + CAL_LEFT + CAL_GAP + 1, CAL_GAP + font_vert_offset, CAL_WIDTH, CAL_HEIGHT), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); 
        }
        current = cal_normal;
        font_vert_offset = -4; // restore the header default (not 0) for columns after today
        if (strcmp(lang_gen_get()->language,"RU") == 0 ) { font_vert_offset = -6; }
      }
    }

    GFont normal = fonts_get_system_font(FONT_KEY_GOTHIC_14); // fh = 16
    GFont bold   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); // fh = 22
    current = normal;
    font_vert_offset = 0;

    // draw the individual calendar rows/columns
    int week = 0;
    int specialRow = grid.special_row;
    
    for (int row = 1; row <= 3; row++) {
      week++;
      for (int col = 0; col < CAL_DAYS; col++) {
        if ( row == specialRow && col == specialDay) {
          if (settings_get()->day_invert) {
            setTodayColors(ctx);
          }
          current = bold;
          font_vert_offset = -3;
        }

        // draw the cell background
        graphics_fill_rect(ctx, GRect (CAL_WIDTH * col + CAL_LEFT + CAL_GAP, CAL_HEIGHT * week, CAL_WIDTH - CAL_GAP, CAL_HEIGHT - CAL_GAP), 0, GCornerNone);

        if (!(row == specialRow && col == specialDay)) {
          int wd = col + settings_get()->dayOfWeekOffset;
          wd %= 7; // keep index in 0..6 even if the offset is out of range
          graphics_context_set_text_color(ctx, (wd == 0 || wd == 6) ? pal.weekend : pal.fg);
        }
        // draw the cell text
        char date_text[3];
        snprintf(date_text, sizeof(date_text), "%d", calendar[col + 7 * (row - 1)]);
        // vertically center the number in its cell (Pebble top-aligns text)
        GSize ts = graphics_text_layout_get_content_size(date_text, current,
                     GRect(0, 0, CAL_WIDTH, CAL_HEIGHT), GTextOverflowModeWordWrap, GTextAlignmentCenter);
        // ts.h includes the font's top line-leading, biasing the glyph low; the
        // leading scales with the font, so correct by a fraction of ts.h (~1/6)
        // rather than a fixed px (adapts to normal/bold and per-theme fonts).
        int ty = CAL_HEIGHT * week + (CAL_HEIGHT - CAL_GAP - ts.h) / 2 - ts.h / 6;
        graphics_draw_text(ctx, date_text, current, GRect(CAL_WIDTH * col + CAL_LEFT, ty, CAL_WIDTH, CAL_HEIGHT), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        if ( row == specialRow && col == specialDay) {
          setColors(ctx);
          current = normal;
          font_vert_offset = 0;
        }
      }
    }
}

void calendar_create(Layer *parent, GRect frame) {
  s_calendar_layer = layer_create(frame);
  layer_set_update_proc(s_calendar_layer, calendar_render);
  layer_add_child(parent, s_calendar_layer);
  layer_set_hidden(s_calendar_layer, true);
}
void calendar_destroy(void)           { layer_destroy(s_calendar_layer); }
void calendar_set_hidden(bool hidden) { layer_set_hidden(s_calendar_layer, hidden); }
void calendar_set_frame(GRect frame)  { layer_set_frame(s_calendar_layer, frame); }
void calendar_mark_dirty(void)        { layer_mark_dirty(s_calendar_layer); }
