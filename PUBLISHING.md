# Publishing TimelyNG to the Pebble / Rebble appstore

Listing copy and asset specs, ready to paste into the
[Rebble developer portal](https://dev-portal.rebble.io/). Field set per the
Rebble guides:
[Publishing an App](https://developer.rebble.io/guides/appstore-publishing/publishing-an-app/),
[Submit an app](https://help.rebble.io/appstore-submission/?viewall=true),
[Appstore assets](https://developer.rebble.io/guides/appstore-publishing/appstore-assets/).

## Basic details

- **Title:** TimelyNG
- **Type:** Watchface (no large/small icon step required for watchfaces)
- **Category:** Watchfaces
- **UUID:** `de253577-14c6-4ab2-9db6-65bda1c7c1d2`
- **Source code:** https://github.com/eldios/TimelyNG
- **Website:** https://github.com/eldios/TimelyNG
- **Support:** https://github.com/eldios/TimelyNG/issues  (or a support email)

## Description

> A calendar watchface with complications you arrange yourself.
>
> TimelyNG shows the date, time and weather up top and a compact three‑week
> calendar (previous · current · next) below. Around them are three configurable
> rows — a status bar, a row above the time, and a row above the calendar — and
> each can hold one complication (centred) or two side by side.
>
> Pick from one shared menu for every slot: date, day, month, ISO week number,
> day‑of‑year / days‑left, seconds, AM/PM, timezone, a second time zone,
> sunrise, sunset, moon phase, weather location, watch battery, phone battery
> and Bluetooth. Battery can be a filling bar with the percentage inside, plain
> text, an icon with text, or a bar with an icon.
>
> The layout is adaptive: leave a row empty and the clock and calendar grow to
> use the space, with the clock font scaling to fit — and everything updates the
> moment you save your settings.
>
> Weather comes from Open‑Meteo over HTTPS (no API key). Configuration is 100 %
> offline — no server, no account. Vibration (hourly, connect/disconnect),
> Do‑Not‑Disturb, themes and full localization are all built in.
>
> Freely inspired by, and an homage to, the original Timely / PebbleTimely and
> the Timely‑2 and PebbleTimely community forks.

**Keywords / tags:** calendar, weather, complications, date, week number,
battery, moon phase, world time, minimal, localizable

**External services (for the app's privacy/data disclosure):** weather comes
from Open‑Meteo over HTTPS with no API key. When the "weather location"
complication is used, the app also sends the coordinates — rounded to
**2 decimal places** — to `api.bigdatacloud.net` to resolve a city name for the
weather line. No other network access; configuration is fully offline.

## Release notes — v0.0.7

**Known issue:** the currently published v0.0.7 artifact fails to start on
emery (invalid app relocation). A fix is prepared and awaits a re-release;
publish the corrected build before relying on these notes.

> • Status icons (charging, Do Not Disturb, hourly vibe) now sit in a tray
>   under the clock on large screens, side by side — no more overlap with the
>   battery bar in the status bar.
> • Fix the phone-battery complication always showing empty on a real watch
>   (0.0.5 regression).
> • Bigger weather icon on large screens (Pebble Time 2 / Core Time 2): the
>   condition glyph grows from 40px to 48px and scales with the time band.
> • Settings are WYSIWYG and reliable: saving applies exactly what the
>   configuration page shows (an earlier build could silently drop a save on a
>   real watch).
> • Coloured weather icons on colour watches (basalt, emery) — sun amber,
>   rain/snow blue, lightning amber, moon pale yellow — with a Default / Colour /
>   Black & White choice under Weather → Icon style.
> • The loading splash shows the real app version.
> • Configurable complication rows (status bar, above the time, above the
>   calendar) — one centred or two side by side per row.
> • One shared, alphabetical menu for every slot, incl. watch/phone battery,
>   Bluetooth and the date.
> • Adaptive layout that reflows live on save; clock font auto‑scales.
> • Battery as a filling bar (% inside), text, icon + text, or bar + icon.
> • Open‑Meteo weather (no API key); fully offline configuration.
> • Builds for Pebble Time 2 (emery), Pebble 2 Duo (flint), basalt and diorite.

## Assets

Create an asset collection per supported platform (`emery`, `basalt`,
`diorite`, `flint`). Per the Rebble guides:

- **Screenshots:** up to 5 per platform, at the watch's native resolution,
  **unframed** (the listing must not use framed screenshots):
  - `emery` — 200×228
  - `basalt` / `diorite` / `flint` — 144×168
  - Capture with `pebble screenshot --emulator <platform>`.
- **Marketing banner:** optional for watchfaces (required only for apps).
  Templates: see the Rebble appstore‑assets guide.
- **Icons:** not required for watchfaces.

## Publish flow

1. `pebble build` → upload `build/TimelyNG.pbw` with the release notes above.
2. Add an asset collection (description + screenshots) for each platform.
3. Click **Publish** (or **Publish Privately**), then reload for the public
   appstore link.
