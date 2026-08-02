#include "utest.h"
#include "calendar.h"

// Thu June 4 2026, Sunday start, 1 week before + 1 after.
// Matches the emulator screenshot: 24..30 / 31,1..6 / 7..13, today=4.
UTEST(calendar, june_2026_sunday_start) {
  CalGrid g = calendar_build(2026, 5, 4, 4, 0, 0);
  ASSERT_EQ(4, g.special_col);
  ASSERT_EQ(2, g.special_row);
  ASSERT_EQ(24, g.days[0]);
  ASSERT_EQ(30, g.days[6]);
  ASSERT_EQ(31, g.days[7]);
  ASSERT_EQ(4,  g.days[11]);
  ASSERT_EQ(6,  g.days[13]);
  ASSERT_EQ(7,  g.days[14]);
  ASSERT_EQ(13, g.days[20]);
}

UTEST(calendar, monday_start_offset) {
  CalGrid g = calendar_build(2026, 5, 4, 4, 1, 0);
  ASSERT_EQ(3, g.special_col);
  ASSERT_EQ(1, g.days[7]);   // Monday Jun 1 starts the week row
  ASSERT_EQ(4, g.days[10]);  // today
}

UTEST(calendar, sunday_with_monday_start_wraps_to_last_col) {
  CalGrid g = calendar_build(2026, 5, 7, 0, 1, 0);
  ASSERT_EQ(6, g.special_col); // Sunday is the last column under Monday start
}

// T8: week_pattern (config.js `cal_week_pattern`: 0=prev+next, 1=last two,
// 2=next two). The audit pinned special_row = 2 / 3 / 1 for patterns 0 / 1 / 2
// (special_row = show_last + 1). Same anchor date as above: Thu 4 Jun 2026,
// wday=4, Sunday start. today always lands at column 4.

// Pattern 1 "Last two weeks": both extra weeks are BEFORE today, so today sits
// on the third (bottom) row. Grid runs 17 May .. 6 Jun.
UTEST(calendar, week_pattern1_last_two_weeks) {
  CalGrid g = calendar_build(2026, 5, 4, 4, 0, 1);
  ASSERT_EQ(4, g.special_col);
  ASSERT_EQ(3, g.special_row);      // audit: pattern 1 -> row 3
  ASSERT_EQ(17, g.days[0]);         // two full weeks before today's week
  ASSERT_EQ(4,  g.days[18]);        // today at row 3 (index (3-1)*7+4)
  ASSERT_EQ(6,  g.days[20]);        // last cell = Sat 6 Jun
}

// Pattern 2 "Next two weeks": both extra weeks are AFTER today, so today sits on
// the first (top) row. Grid runs 31 May .. 20 Jun.
UTEST(calendar, week_pattern2_next_two_weeks) {
  CalGrid g = calendar_build(2026, 5, 4, 4, 0, 2);
  ASSERT_EQ(4, g.special_col);
  ASSERT_EQ(1, g.special_row);      // audit: pattern 2 -> row 1
  ASSERT_EQ(31, g.days[0]);         // 31 May leads the top row
  ASSERT_EQ(4,  g.days[4]);         // today at row 1 (index (1-1)*7+4)
  ASSERT_EQ(20, g.days[20]);        // last cell = Sat 20 Jun
}

// January boundary: the previous-month length comes from daysInMonth(mon-1),
// which at mon=0 evaluates daysInMonth(-1) -> the Dec-of-previous-year wrap.
// daysInMonth(-1) returns 31 ("works by accident": -1 hits neither the 30-day
// nor the February arm), which happens to be December's real length. This test
// pins that behavior so a daysInMonth refactor can't silently break the wrap.
// Anchor: Fri 2 Jan 2026 (wday=5), Sunday start -> the leading cells must be the
// last days of December 2026-1 = Dec 2025 (21..31), then 1, 2 Jan.
UTEST(calendar, january_prev_december_wrap) {
  CalGrid g = calendar_build(2026, 0, 2, 5, 0, 0);
  ASSERT_EQ(5, g.special_col);
  ASSERT_EQ(2, g.special_row);
  ASSERT_EQ(21, g.days[0]);   // Dec 21 (needs daysInMonth(-1)==31: 31-11+1)
  ASSERT_EQ(31, g.days[10]);  // Dec 31 -> proves the wrap used 31 days
  ASSERT_EQ(1,  g.days[11]);  // Jan 1
  ASSERT_EQ(2,  g.days[12]);  // today, Jan 2
}

// December boundary: the trailing cells must roll over into January of the next
// year (day numbers restart at 1), driven by daysThisMonth = daysInMonth(11).
// Anchor: Wed 30 Dec 2026 (wday=3), Sunday start.
UTEST(calendar, december_next_january_wrap) {
  CalGrid g = calendar_build(2026, 11, 30, 3, 0, 0);
  ASSERT_EQ(3, g.special_col);
  ASSERT_EQ(2, g.special_row);
  ASSERT_EQ(30, g.days[10]);  // today at row 2 (index (2-1)*7+3)
  ASSERT_EQ(31, g.days[11]);  // Dec 31 (December has 31 days)
  ASSERT_EQ(1,  g.days[12]);  // Jan 1 next year -> rolled over
  ASSERT_EQ(9,  g.days[20]);  // Jan 9
}
