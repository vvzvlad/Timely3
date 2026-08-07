# TODO

Future work for TimelyNG. Roughly ordered by priority.

## Platforms
- **Re-add `aplite`** (original 2013 Pebble, 24 KB app RAM). It was dropped
  because the current feature set no longer fits (≈19 KB of code leaves almost
  no heap, so it faulted on launch). Bringing it back needs an aplite-specific
  code diet behind `#ifdef PBL_PLATFORM_APLITE` — trim optional complications,
  extra icons and large fonts — and a build there that runs without faulting
  before it goes back into `package.json` `targetPlatforms`.
- **Round platforms** (`chalk`, `gabbro`). The layout is rectangle-centric;
  a round variant would need its own geometry in `layout.c`.

## Code cleanup
- **Remove the retired middle `day_layer`** complication slot. It is always
  hidden (the row model is now left/right only) but still created, themed and
  destroyed; also drop the now-unused `show_day` setting + `AK_STYLE_DAY`
  handling and the `STAT_BT_ICON_*` defines left over from the old fixed BT icon.

## Features / polish
- More complications (e.g. step count / health, a configurable text label).
- Per-complication formatting options where it makes sense.
- Revisit weather error/backoff behaviour for flaky phone links.

## Project / infra
- **CI: add the full SDK build** (per-platform `pebble build` + `tools/test.sh`
  strict-check) once a stable Core Devices Pebble SDK container image is
  available. CI currently runs the host unit tests + JS syntax check.

## Known limitations
- The weather location name is only shown when picked as the "Location"
  complication; there is no automatic fallback display.

## Inherited TODO (from the original Timely)

Carried over from `TODO`/`XXX` markers in the upstream code — features that were
stubbed but never finished. Decide per item whether to implement or remove the
dead scaffolding (message keys, settings fields).

- **Idle reminder** — a "you've been still, move" style reminder. The keys exist
  but are unused: `idle_reminder`, `idle_vibe_patt`, `idle_message`,
  `idle_start`, `idle_stop` (all marked `// TODO, UNUSED` in `Timely.c`).
  Implement it or drop the keys + config.
- **Per-slot colour inversion** — `inv_slot_stat` / `inv_slot_top` /
  `inv_slot_bot` are read but do nothing. Wire them up or remove.
- **`show_date` toggle** — the `show_date` key is read but unused (the date is a
  complication now). Remove it, or repurpose as a quick date on/off.
- **Per-weekday hourly vibration** — `vibe_days` was meant to limit the hourly
  buzz to chosen days; today it only encodes the schedule mode. Restore an
  actual day-of-week picker, or document it as mode-only.
- **Real bold Unicode font** — non-Latin (e.g. RU) "today" header is faked with
  a double-strike because there is no bold unicode font; add one for a clean
  bold across all languages.
