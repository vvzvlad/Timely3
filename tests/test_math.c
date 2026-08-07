// Host-side unit tests for src/math.c (T6). These are the watchface's own
// libc-free float APPROXIMATIONS (a fast-inverse-sqrt sqrt, minimax trig, an
// atan rational, truncating floor). The reference watch has no libm, so the
// contract is "whatever these routines actually compute", NOT the libc-correct
// value. Every expectation below therefore PINS THE MODULE'S REAL OUTPUT
// (measured on the host) with a tight epsilon, and each comment records how far
// that pinned value sits from the mathematically-correct answer so a future
// reader knows the pin is an approximation, not a bug.
//
// Tolerance policy: the pinned numbers are the module's deterministic float
// output, so an exact match is ~1e-6. We use MATH_EPS = 5e-4 uniformly: tight
// enough that any real regression (e.g. dropping my_sqrt's Newton refinement,
// which shifts results by whole percent) reddens the test, yet loose enough to
// absorb benign host-FP rounding differences (x87 vs SSE). Where the
// approximation error itself exceeds MATH_EPS we note it explicitly.
#include "utest.h"
#include "math.h"

#define MATH_EPS 5e-4

// ---- my_sqrt: fast inverse-sqrt seed + ONE Newton step (deliberately coarse) ----
// Pinned to the module output, which is systematically LOW (the single Newton
// step under-corrects). libc-correct values are in the trailing comment.
UTEST(my_sqrt, pins_approximation) {
  ASSERT_NEAR(1.413860f, my_sqrt(2.0f),   MATH_EPS); // libc 1.414214 (err -3.5e-4)
  ASSERT_NEAR(0.499154f, my_sqrt(0.25f),  MATH_EPS); // libc 0.500000 (err -8.5e-4)
  ASSERT_NEAR(0.998307f, my_sqrt(1.0f),   MATH_EPS); // libc 1.000000 (err -1.7e-3): not exact even at 1!
  ASSERT_NEAR(1.996614f, my_sqrt(4.0f),   MATH_EPS); // libc 2.000000 (err -3.4e-3)
  ASSERT_NEAR(9.984488f, my_sqrt(100.0f), MATH_EPS); // libc 10.000000 (err -1.6e-2, ~0.16% low)
}

// ---- my_floor: truncation via (int) cast ----
// CONTRACT: my_floor truncates TOWARD ZERO, it is NOT a real floor(). For
// negatives it disagrees with libc floor: my_floor(-2.5) == -2.0 (libc -3.0).
// This is arguably wrong but SAFE in this codebase because the sole caller
// (my_rint, which pre-applies my_fabs) only ever passes a non-negative argument.
// We pin the ACTUAL truncate-toward-zero behavior; DO NOT "fix" it to round down
// without auditing that caller. (test-only; not a confirmed in-context bug)
UTEST(my_floor, truncates_toward_zero) {
  ASSERT_NEAR( 2.0f, my_floor(2.5f),   1e-6); // positive: matches floor
  ASSERT_NEAR( 3.0f, my_floor(3.99f),  1e-6);
  ASSERT_NEAR(-2.0f, my_floor(-2.5f),  1e-6); // NEGATIVE: toward zero, NOT -3.0
  ASSERT_NEAR( 0.0f, my_floor(-0.5f),  1e-6); // toward zero, NOT -1.0
}

// ---- my_fabs ----
UTEST(my_fabs, magnitude) {
  ASSERT_NEAR(3.5f, my_fabs(-3.5f), 1e-6);
  ASSERT_NEAR(3.5f, my_fabs( 3.5f), 1e-6);
  ASSERT_NEAR(0.0f, my_fabs( 0.0f), 1e-6);
}

// ---- my_atan: Lagrange/rational approximation, odd-symmetric ----
UTEST(my_atan, pins_approximation) {
  ASSERT_NEAR( 0.785398f, my_atan( 1.0f), MATH_EPS); // libc 0.785398: exact by construction at x=1
  ASSERT_NEAR(-0.785398f, my_atan(-1.0f), MATH_EPS); // odd symmetry
  ASSERT_NEAR( 0.466343f, my_atan( 0.5f), MATH_EPS); // libc 0.463648 (err +2.7e-3)
}

// ---- my_rint: my_floor(|x|+0.5) with sign restored ----
// Rounds half AWAY from zero (not round-half-to-even). Pins that policy.
UTEST(my_rint, rounds_half_away_from_zero) {
  ASSERT_NEAR( 3.0f, my_rint( 2.5f), 1e-6);
  ASSERT_NEAR(-3.0f, my_rint(-2.5f), 1e-6);
  ASSERT_NEAR( 2.0f, my_rint( 2.4f), 1e-6);
  ASSERT_NEAR(-3.0f, my_rint(-2.6f), 1e-6);
}

// ---- my_sin: Cody-Waite reduction + minimax core ----
UTEST(my_sin, pins_approximation) {
  ASSERT_NEAR( 0.5f,      my_sin(3.14159265f/6.0f),  MATH_EPS); // libc 0.5
  ASSERT_NEAR( 1.0f,      my_sin(3.14159265f/2.0f),  MATH_EPS); // libc 1.0
  ASSERT_NEAR( 0.0f,      my_sin(3.14159265f),       MATH_EPS); // libc 0.0
  ASSERT_NEAR(-1.0f,      my_sin(-3.14159265f/2.0f), MATH_EPS); // odd
}

// ---- my_cos: my_sin(x + pi/2) ----
UTEST(my_cos, pins_approximation) {
  ASSERT_NEAR(1.0f, my_cos(0.0f),             MATH_EPS);
  ASSERT_NEAR(0.5f, my_cos(3.14159265f/3.0f), MATH_EPS); // libc 0.5
  ASSERT_NEAR(0.0f, my_cos(3.14159265f/2.0f), MATH_EPS); // libc 0.0
}

// ---- my_acos ----
// Two internal branches. |x| <= 0.5625 uses asin_core directly; |x| > 0.5625
// takes the reduction arccos(x) = 2*arcsin(sqrt((1-|x|)/2)) which is THE ONLY
// call site of my_sqrt in the whole module. We exercise BOTH branches, and in
// particular the >0.5625 branch (0.7, 0.9, -0.9) so that a regression in
// my_sqrt's refinement propagates here and reddens the test.
UTEST(my_acos, both_branches_pinned) {
  // |x| <= 0.5625 branch (no my_sqrt):
  ASSERT_NEAR(1.570796f, my_acos(0.0f), MATH_EPS); // libc 1.570796
  ASSERT_NEAR(1.047198f, my_acos(0.5f), MATH_EPS); // libc 1.047198
  ASSERT_NEAR(0.0f,      my_acos(1.0f), MATH_EPS); // libc 0.0 (|x|>0.5625 but arg of sqrt is 0)
  // |x| > 0.5625 branch -> calls my_sqrt:
  ASSERT_NEAR(0.794087f, my_acos( 0.7f), MATH_EPS); // libc 0.795399 (err -1.3e-3)
  ASSERT_NEAR(0.450912f, my_acos( 0.9f), MATH_EPS); // libc 0.451027 (err -1.2e-4)
  ASSERT_NEAR(2.690681f, my_acos(-0.9f), MATH_EPS); // libc 2.690566: pi - t path, negative arg
}

// ---- my_asin: pi/2 - my_acos(x) ----
UTEST(my_asin, pins_approximation) {
  ASSERT_NEAR(0.523599f, my_asin(0.5f), MATH_EPS); // libc 0.523599
  ASSERT_NEAR(0.0f,      my_asin(0.0f), MATH_EPS); // libc 0.0
}

// ---- my_tan: my_sin/my_cos ----
UTEST(my_tan, pins_approximation) {
  ASSERT_NEAR(1.0f, my_tan(3.14159265f/4.0f), MATH_EPS); // libc 1.0
  ASSERT_NEAR(0.0f, my_tan(0.0f),             MATH_EPS); // libc 0.0
}
