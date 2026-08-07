#include "calendar.h"
#include "timefmt.h" // daysInMonth

CalGrid calendar_build(int year, int mon, int mday, int wday,
                       int dow_offset, int week_pattern) {
  CalGrid g;
  // Defensive: keep wday in 0..6 so grid indexing never runs off the ends even
  // if a corrupted tm_wday (or offset) reaches us (audit M2).
  wday = ((wday % 7) + 7) % 7;
  dow_offset = ((dow_offset % 7) + 7) % 7;
  int daysThisMonth = daysInMonth(mon, year);
  int specialDay = wday - dow_offset;

  int show_last = 1, show_next = 1;
  switch (week_pattern) {
    case 1: show_last = 2; show_next = 0; break;
    case 2: show_last = 0; show_next = 2; break;
    default: break;
  }

  int cellNum = 0;
  int daysVisPrevMonth = 0, daysVisNextMonth = 0;
  int daysPriorToToday = specialDay;
  int daysAfterToday   = (6 - specialDay) % 7;

  // tm_wday is Sunday-based; the configured start-of-week may differ.
  if (wday < dow_offset) {
    daysPriorToToday += 7 * (show_last + 1);
    specialDay += 7;
  } else {
    daysPriorToToday += 7 * show_last;
  }
  daysAfterToday += 7 * show_next;

  if (daysPriorToToday >= mday) {
    int daysInPrevMonth = daysInMonth(mon - 1, year);
    daysVisPrevMonth = daysPriorToToday - mday + 1;
    for (int i = 0; i < daysVisPrevMonth; i++, cellNum++)
      g.days[cellNum] = daysInPrevMonth + i - daysVisPrevMonth + 1;
  }

  int firstDayShownThisMonth = daysVisPrevMonth + mday - daysPriorToToday;
  for (int i = firstDayShownThisMonth; i < mday; i++, cellNum++)
    g.days[cellNum] = i;

  g.days[cellNum] = mday;
  cellNum++;

  if (mday + daysAfterToday > daysThisMonth)
    daysVisNextMonth = mday + daysAfterToday - daysThisMonth;

  int daysLeftThisMonth = daysAfterToday - daysVisNextMonth;
  for (int i = 0; i < daysLeftThisMonth; i++, cellNum++)
    g.days[cellNum] = i + mday + 1;

  for (int i = 0; i < daysVisNextMonth; i++, cellNum++)
    g.days[cellNum] = i + 1;

  g.special_col = specialDay;
  g.special_row = show_last + 1;
  return g;
}
