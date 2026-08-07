# Changelog

All notable changes to TimelyNG. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/). The entry below is also the
text to paste into the **Release notes** field when uploading the `.pbw` to the
[Rebble developer portal](https://dev-portal.rebble.io/) (it is not embedded in
the build, so the portal does not auto-fill it). The store **Description** lives
in [`PUBLISHING.md`](PUBLISHING.md).

## 0.0.7

- Status icons (charging, Do Not Disturb, hourly vibration) move out of the
  status bar into a tray under the clock on large screens: they no longer
  overlap the battery bar, and multiple icons now show side by side instead
  of a single priority-picked one. Small screens keep the status-bar icon,
  now repositioned clear of the battery bar.

  > **Known issue:** the published v0.0.7 artifact fails to start on emery
  > (invalid app relocation). A fix is prepared and awaits a re-release.

## 0.0.6

- Fix the phone-battery complication always showing empty on a real watch:
  0.0.5's emulator mock accidentally replaced the real Battery Status API path
  instead of complementing it, so the phone level was never sent.

## 0.0.5

- Bigger weather icon on large screens (Pebble Time 2 / Core Time 2): the
  condition glyph grows from 40px to 48px and now scales with the time band,
  like the clock font already did. Requested by a user; narrow screens are
  unchanged.

## 0.0.4

- Fix settings never saving on a real watch — the actual root cause. The config
  handler re-ran `decodeURIComponent` on a response the phone app had already
  decoded; the literal `%` in the default `strftime_format` (`"%Y-%m-%d"`) made
  it throw "URI malformed", so every save was silently dropped. Decode
  defensively like Clay — only when the response is still percent-encoded. (The
  0.0.2 WYSIWYG change exposed this by sending `strftime_format` on every save;
  0.0.3's payload-size change did not address the real fault.)
- Serve the config page as UTF-8 so accented translation strings survive.

## 0.0.3

- Fix settings being completely ignored on a real watch. 0.0.2 sent every
  setting by name in the `pebblejs://close` payload; on a real watch that
  payload is length-limited, so it was truncated, the JSON failed to parse, and
  the save was silently dropped (it worked on the emulator, which uses a
  different return path). Numeric settings now travel positionally (values only,
  no key names), shrinking the payload ~3.6× so it always fits — while still
  applying every shown value (WYSIWYG preserved).
- The loading splash now shows the real app version instead of a hard-coded
  "3.0".

## 0.0.2

- Settings are now WYSIWYG: saving the config page applies **every** value it
  shows, not only the ones you touched. Previously the page sent just the keys
  that changed against a phone-side baseline; when that baseline had drifted from
  the watch (reinstall, a different phone, or defaults that never matched), a
  setting shown in the page but left untouched was silently never applied — so
  e.g. a complication could read "Location" in the settings yet stay "Date" on
  the watch. Every non-translation setting is now always sent. Translation
  strings stay delta-only (they are bulk and must fit the watch's message inbox);
  the language picker resends them when it changes them.
- Config page defaults now match the watch's firmware defaults (the
  above-calendar Week and AM/PM slots, low-battery threshold), so a fresh install
  shows the watch's real out-of-box state before the first save.
- Coloured weather icons on colour watches (basalt, emery): the condition glyph
  is tinted by meaning — sun amber, rain/snow blue, lightning amber, moon pale
  yellow — while the temperature keeps the theme colour. Black-and-white watches
  (diorite, flint) and the Mono theme are unchanged.
- New **Weather → Icon style** setting: *Default* (colour on colour watches, B&W
  on the rest), *Colour* (force the tinted icons, overriding the Mono theme), or
  *Black & White* (force the classic monochrome glyph on colour watches too).

## 0.0.1 — first public release

- Configurable complication rows (status bar, above the time, above the
  calendar) — one centred or two side by side per row.
- One shared, alphabetical menu for every slot, including watch/phone battery,
  Bluetooth and the date itself.
- Adaptive layout that reflows live on save; the clock font auto-scales.
- Battery styles: filling bar with the % inside, text, icon + text, or bar +
  icon.
- Open-Meteo weather (no API key); fully offline configuration.
- Builds for Pebble Time 2 (emery), Pebble 2 Duo (flint), basalt and diorite.

Freely inspired by, and an homage to, the original Timely / PebbleTimely and the
Timely-2 and PebbleTimely community forks.
