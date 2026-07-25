<div align="center">

# ⌚ TimelyNG

**A calendar watchface for Pebble — date, time & weather up top, a 3‑week
calendar below, and fully configurable complication rows you arrange yourself.**

[![CI](https://github.com/eldios/TimelyNG/actions/workflows/ci.yml/badge.svg)](https://github.com/eldios/TimelyNG/actions/workflows/ci.yml)
![Version](https://img.shields.io/badge/version-0.0.7-blue)
![Platforms](https://img.shields.io/badge/Pebble-emery%20·%20flint%20·%20basalt%20·%20diorite-orange)
![Language](https://img.shields.io/badge/C-C99-555?logo=c)
![PebbleKit JS](https://img.shields.io/badge/config-PebbleKit%20JS-f5a623?logo=javascript&logoColor=white)
![SDK](https://img.shields.io/badge/Pebble%20SDK-4.x%20(Core%20Devices)-black)

</div>

---

## 🙏 Homage & inspiration

TimelyNG is **freely inspired by, and an homage to, the original *Timely* /
*PebbleTimely* watchface and its community forks** — in particular
[Andrew129260/Timely‑2](https://github.com/Andrew129260/Timely-2) and
[alan‑johnson/PebbleTimely](https://github.com/alan-johnson/PebbleTimely).
It carries forward their ideas and spirit (the calendar‑centric layout, the
localizable date/time, the complication slots), rebuilt for the modern
**Core Devices Pebble SDK** and the new hardware. All credit for the original
concept goes to those projects; any bugs here are mine.

## ✨ Why TimelyNG (what's different)

Compared to the classic Timely and its forks, TimelyNG adds:

- **Configurable complication *rows*, not fixed slots.** Three rows — status
  bar, above the time, above the calendar — and each holds **one complication
  (centred) or two (side by side)**.
- **One unified, alphabetical menu for every slot**, including things that used
  to be hard‑wired: **watch battery, phone battery, Bluetooth, and the date
  itself** — put any of them anywhere.
- **Adaptive layout that reflows live.** Empty a row and the freed space is
  given back to the clock and calendar; the **clock font auto‑scales** to the
  space available — and it all updates the moment you save the config, no
  reload.
- **Multiple battery styles:** a filling bar with the % *inside*, plain text,
  icon + text, or bar + icon.
- **Open‑Meteo weather over HTTPS, no API key**, sized per screen.
- **100 % offline configuration** — no server, no CDN, no external host.
- **Built for 2026 hardware** (Pebble Time 2, Pebble 2 Duo) on the current SDK,
  with a local test gate that mirrors the cloud's strict build flags.

## 🧩 What it shows

- **Complications** (shared menu): date, day, month, week number,
  day‑of‑year / days‑left, seconds, AM/PM, timezone, a second time zone,
  sunrise, sunset, moon phase, weather location, watch/phone battery, Bluetooth.
- **Weather:** condition icon + temperature (Open‑Meteo).
- **Calendar:** previous / current / next week, today accented on colour
  watches.
- Vibration (hourly, on disconnect / reconnect), Do‑Not‑Disturb, themes and
  full localization — all from the config screen.

## ⌚ Platforms

| Platform  | Watch                      | Screen          | Status |
| --------- | -------------------------- | --------------- | ------ |
| `emery`   | Pebble Time 2              | colour 200×228  | ✅ Supported |
| `flint`   | Pebble 2 Duo / Core 2 Duo  | B&W 144×168     | ✅ Supported |
| `basalt`  | Pebble Time / Time Steel   | colour 144×168  | ✅ Supported |
| `diorite` | Pebble 2                   | B&W 144×168     | ✅ Supported |
| `aplite`  | Pebble (2013) / Steel      | B&W 144×168     | ⏳ Dropped — future (24 KB RAM, needs a code diet) |
| `chalk`   | Pebble Time Round          | colour 180×180 round | 🔜 Not supported — future (round layout) |
| `gabbro`  | Core Time 2                | colour 260×260 round | 🔜 Not supported — future (round layout) |

The round platforms (`chalk` / `gabbro`) need a dedicated round layout — the
current design is rectangle‑centric. `aplite` was dropped for RAM. See
[`TODO.md`](TODO.md).

## 🔨 Building (Nix + official Pebble CLI)

```sh
nix develop                      # pebble CLI + ARM toolchain + emulator
just build                       # -> build/TimelyNG.pbw   (or: make build)
pebble install --emulator emery  # colour emulator (or basalt / diorite / flint)
```

`just build` / `make build` run `pebble build` and then `tools/reloc-check.sh`,
which fails the build if a packed struct emitted a misaligned relocation (that
bug made 0.0.7 unlaunchable). A bare `pebble build` skips that gate.

Build/test exactly as the cloud does (catches the `-Werror` / warnings the
local build relaxes via `pbl_suppress_newer_gcc_warnings`):

```sh
nix develop -c sh tools/test.sh  # host unit tests + build + reloc-check + strict per‑platform compile
```

## ⚙️ Configuration

The settings screen is fully offline. On `showConfiguration` the watch's
PebbleKit JS builds the page from `src/js/config.js` (the field spec) via
`src/js/configpage.js` and hands it to the phone as a `data:text/html` URI; the
page returns the chosen values via `pebblejs://close#`. Edit
`src/js/config.js` to change the options.

## 📦 Publishing

Store listing copy (description, release notes, categories, asset specs) lives
in [`PUBLISHING.md`](PUBLISHING.md), ready to paste into the
[Rebble developer portal](https://dev-portal.rebble.io/).
