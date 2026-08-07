#pragma once
#include <stdbool.h>
// Pebble-free layout geometry so it can be unit-tested on the host.
typedef struct { int x, y, w, h; } LayoutRect;

typedef struct {
  LayoutRect statusbar;   // top band (fixed height: BT/charge/battery)
  LayoutRect slot_top;    // upper half: time/date/weather
  LayoutRect slot_bot;    // lower half: calendar
  LayoutRect battery;     // right-aligned battery box (within statusbar)
  int chrg_icon_x;
  LayoutRect clock_time;  // relative to slot_top origin
  LayoutRect clock_date;  // relative to slot_top origin
  int subtext_top;
  int cal_cols;
  int cal_cell_w;
  int cal_cell_h;
} TimelyLayout;

// Compute the layout for a screen of width x height pixels. The _rows variant
// takes which optional rows are enabled (TOP status bar / CENTER above-time /
// BOTTOM above-calendar); disabled rows free their height to the rest.
TimelyLayout layout_compute_rows(int width, int height, int has_top, int has_center, int has_bottom);
TimelyLayout layout_compute(int width, int height); // all rows present (default)

// Wide-screen predicate: the single source of truth for the >= 180px threshold
// that routes emery/chalk to the wide layout (Roboto clock, bigger weather
// glyph, status tray under the clock instead of the statusbar charge icon).
// Behavior-preserving: chalk (round, 180) still classifies as wide here; moving
// chalk's status icons off the round bezel is a known follow-up, out of scope.
bool layout_is_wide(int width);

// Clock font choice as a pure, testable decision (the view maps it to a Pebble
// FONT_KEY). Scales with the time-band height; the big proportional Roboto is
// only for wide screens where it won't collide with the weather column.
typedef enum {
  CLOCK_FONT_ROBOTO_49 = 0, // wide screens, tall band
  CLOCK_FONT_LECO_42,
  CLOCK_FONT_LECO_38,
  CLOCK_FONT_LECO_32,
  CLOCK_FONT_LECO_28,       // smallest (very short band)
} ClockFont;
ClockFont clock_font_for(int width, int band_h);

// Weather glyph size (Climacons px) as a pure, testable decision. Narrow
// screens stay at the compact 28; wide screens take 48 when the band fits
// glyph + temperature (>= 64px), else 40.
int weather_glyph_size_for(int width, int band_h);

// Status icon (charging/DND/hourvibe) x position. The legacy spot sits left of
// the 44px battery box; when the right status slot draws a battery bar (box
// from width/2+2) the icon moves clear of it. Narrow screens only — wide
// screens use the status tray below the clock instead.
int chrg_icon_x_for(int width, int right_slot_is_bar);

// Status-tray slot x (wide screens): 20px icons pack from the right edge
// (width-4) at a 22px pitch; idx 0 is the rightmost.
int status_tray_x(int width, int idx);

// Slot-rectangle geometry for the two dynamic complication rows (center /
// above-calendar) and the battery boxes. Pure integer math extracted verbatim
// from the view so it can be host-tested; every consumer that laid these out
// inline now routes through here (behavior-preserving, zero-pixel).
typedef struct { int x, w; } SlotSpan;
typedef struct { SlotSpan left, right; } SlotPair;

// Two-slot split for a row of `width`: half = width/2; left {2, half-4},
// right {half+2, half-4}. Used when both slots have content.
SlotPair layout_slot_pair(int width);
// Single centered slot spanning the row: {2, width-4}. Used when one slot set.
SlotSpan layout_slot_full(int width);

// Battery-bar box geometry for one side (right/left), matching the status-bar
// battery layout. with_icon (bar+icon style) reserves a leading glyph on the
// outer edge and narrows the box; status_icon_shown yields the right box 22px
// so the charging/DND/hourvibe icon can dock at half+2. Writes bx/bw.
void layout_batt_box(int width, bool is_right, bool with_icon,
                     bool status_icon_shown, int *bx, int *bw);

// The most recently computed layout (set by the view at window_load); read by
// components that render proportionally (calendar, etc.).
void layout_store(TimelyLayout l);
TimelyLayout layout_get(void);
