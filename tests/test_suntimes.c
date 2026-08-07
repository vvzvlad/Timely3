#include "utest.h"
#include "suntimes.h"

// T7: sunrise/sunset approximation (solar declination + hour angle; equation of
// time omitted, so absolute times run up to ~15 min off real almanac data).
//
// Reference model: the SAME analytic formula computed with libc trig (no EoT).
// The module reproduces that reference to <1 min everywhere it was checked, so
// we pin the module output with a TIGHT +-0.05 h (~3 min) tolerance for the
// non-polar cases (was +-45 min, which passed even with badly broken trig). A
// real regression in my_acos/my_tan shifts these by far more than 3 min.
#define SUN_EPS 0.05f // hours (~3 min)

// Milan (45.46N, 9.19E), 4 June (yday 154), CEST = UTC+2.
// Almanac sunrise ~05:33 / sunset ~21:01; the ~11 min gap on sunrise is the
// omitted equation of time, not error against the model (module == model here).
UTEST(suntimes, milan_june) {
  float sr, ss;
  sun_times(45.46f, 9.19f, 154, 2.0f, &sr, &ss);
  ASSERT_NEAR(5.745f,  sr, SUN_EPS);  // tightened from (5.0..6.5)
  ASSERT_NEAR(21.030f, ss, SUN_EPS);  // tightened from (20.0..21.5)
  ASSERT_FALSE(sun_is_night(45.46f, 9.19f, 154, 2.0f, 12.0f)); // noon: day
  ASSERT_TRUE(sun_is_night(45.46f, 9.19f, 154, 2.0f, 2.0f));   // 02:00: night
  ASSERT_TRUE(sun_is_night(45.46f, 9.19f, 154, 2.0f, 23.0f));  // 23:00: night
}

// Equator, any day: lat=0 => my_tan(lat)=0 => cosH=0 exactly, independent of
// declination. Half-day = my_acos(0)*12/pi = 6 h, so with lon=0/tz=0 the sun
// rises at 06:00 and sets at 18:00 year-round. Cleanly pins my_acos(0).
UTEST(suntimes, equator_twelve_hour_day) {
  float sr, ss;
  sun_times(0.0f, 0.0f, 79, 0.0f, &sr, &ss);   // March
  ASSERT_NEAR(6.0f,  sr, SUN_EPS);
  ASSERT_NEAR(18.0f, ss, SUN_EPS);
  sun_times(0.0f, 0.0f, 171, 0.0f, &sr, &ss);  // June: still 12 h at the equator
  ASSERT_NEAR(6.0f,  sr, SUN_EPS);
  ASSERT_NEAR(18.0f, ss, SUN_EPS);
}

// High latitude where the timezone runs well AHEAD of local solar time:
// 60N, 30E (solar noon ~ 10:00 local mean) but tz=+5. The clock offset pushes
// sunset past 24:00 (a valid, un-clamped output of the model), exercising the
// noon = 12 - lon/15 + tz correction. Pinned tight against the module/model.
UTEST(suntimes, high_lat_tz_ahead) {
  float sr, ss;
  sun_times(60.0f, 30.0f, 171, 5.0f, &sr, &ss); // near summer solstice
  ASSERT_NEAR(5.753f,  sr, SUN_EPS);
  ASSERT_NEAR(24.247f, ss, SUN_EPS);
  ASSERT_TRUE(ss > 24.0f); // TZ ahead of solar time pushes sunset past midnight
}

// Polar DAY sentinel: high latitude around the summer solstice -> cosH <= -1 ->
// the function short-circuits to sunrise=0, sunset=24 (sun never sets). The
// sentinel is (0, 24); we assert it exactly and confirm sun_is_night is FALSE
// at every hour (midnight sun).
UTEST(suntimes, polar_day_sentinel) {
  float sr, ss;
  sun_times(78.0f, 15.0f, 171, 1.0f, &sr, &ss); // Svalbard, ~21 June
  ASSERT_NEAR(0.0f,  sr, 1e-6);   // polar-day sentinel: sunrise
  ASSERT_NEAR(24.0f, ss, 1e-6);   // polar-day sentinel: sunset
  ASSERT_FALSE(sun_is_night(78.0f, 15.0f, 171, 1.0f, 0.0f));  // midnight: still day
  ASSERT_FALSE(sun_is_night(78.0f, 15.0f, 171, 1.0f, 3.0f));
  ASSERT_FALSE(sun_is_night(78.0f, 15.0f, 171, 1.0f, 12.0f));
}

// Polar NIGHT sentinel: high latitude around the winter solstice -> cosH >= 1 ->
// short-circuits to sunrise=12, sunset=12 (a zero-length day). The sentinel is
// (12, 12); with sunrise==sunset every hour is night (now < 12 OR now >= 12).
UTEST(suntimes, polar_night_sentinel) {
  float sr, ss;
  sun_times(78.0f, 15.0f, 355, 1.0f, &sr, &ss); // Svalbard, ~21 Dec
  ASSERT_NEAR(12.0f, sr, 1e-6);   // polar-night sentinel: sunrise
  ASSERT_NEAR(12.0f, ss, 1e-6);   // polar-night sentinel: sunset
  ASSERT_TRUE(sun_is_night(78.0f, 15.0f, 355, 1.0f, 6.0f));   // morning: night
  ASSERT_TRUE(sun_is_night(78.0f, 15.0f, 355, 1.0f, 12.0f));  // noon: still night
  ASSERT_TRUE(sun_is_night(78.0f, 15.0f, 355, 1.0f, 18.0f));  // evening: night
}
