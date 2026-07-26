#include "utest.h"
#include <stddef.h> // offsetof
#include "settings.h"

// Layout guard for the persist-backed structs. The real enforcement is the five
// _Static_asserts inside settings.h; the point of this translation unit is that
// including the header at all makes the host build (and therefore CI, which has
// no Pebble SDK) evaluate them. src/settings.c is deliberately NOT part of the
// test build — only the header is needed.
//
// Both structs are byte-granular and packed, so their size is identical on a
// 64-bit host and on the watch. Division of labour: the header's _Static_asserts
// pin the two sizes and the boundary offsets (track_battery, theme_mode,
// weather_icons); this file pins WHICH FIELD OWNS WHICH BYTE, for the three
// fields pinned below only — the remaining fields are not covered against a
// size-neutral reordering and still need review by eye. Swapping two equal-width fields keeps
// every size intact — and every asserted offset too, unless one of the two is a field an
// offset assert names — while handing a byte of the
// persisted blob to a different field — silently, and once a release has
// shipped that byte belongs to a setting already stored on a watch. That applies
// to a field sitting directly before an asserted one too:
// offsetof(theme_mode) == 20 says nothing about which field owns byte 19 — it
// only implies theme sits there while theme is the field declared immediately
// before theme_mode, which is exactly the property the theme pin guards. Same
// for slots against the weather_icons assert.
//
// The sizes (21 / 244) and the offsets the header already asserts are NOT
// restated here: a second copy of those constants would just have to be edited
// in lockstep when a field is appended.

UTEST(settings_layout, persist_interior_fields_pinned) {
  ASSERT_EQ(19u, (unsigned)offsetof(persist, theme));
}

UTEST(settings_layout, persist_adv_settings_interior_fields_pinned) {
  ASSERT_EQ(15u, (unsigned)offsetof(persist_adv_settings, custom_date_fmt));
  ASSERT_EQ(32u, (unsigned)sizeof(((persist_adv_settings *)0)->custom_date_fmt));
  ASSERT_EQ(233u, (unsigned)offsetof(persist_adv_settings, slots));
  ASSERT_EQ(10u, (unsigned)sizeof(((persist_adv_settings *)0)->slots));
}
