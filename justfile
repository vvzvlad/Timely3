# Dev tasks for TimelyNG. Equivalent recipes exist in the Makefile.
#   Host unit tests:        run inside `nix develop .#test`
#   Pebble app + emulator:  run inside `nix develop`

cc     := env_var_or_default("CC", "cc")
cflags := "-I src -I tests -Wall -Wextra -std=c11"
build  := "build"
emu    := env_var_or_default("EMU", "emery")   # aplite|basalt|chalk|diorite|emery|flint
shot   := build / ("screenshot-" + emu + ".png")
test_src := "tests/*.c src/timefmt.c src/layout.c src/calendar.c src/vibes.c src/suntimes.c src/math.c"

# list the available recipes
default:
    @just --list

## ---- host unit tests ----
test: _build-tests
    ./{{build}}/test_suite

test-xml: _build-tests
    ./{{build}}/test_suite --output={{build}}/test-results.xml

_build-tests:
    mkdir -p {{build}}
    {{cc}} {{cflags}} {{test_src}} -o {{build}}/test_suite

test-clean:
    rm -f {{build}}/test_suite {{build}}/test-results.xml

## ---- PebbleKit JS host tests (node:test; no SDK / no npm deps) ----
test-js:
    node --test tests/js/

## ---- Pebble app + emulator ----
# compile the .pbw
build:
    pebble build

# build + (re)install on the emulator (EMU=emery by default)
run: build
    pebble install --emulator {{emu}}

# build + install + grab a screenshot to build/screenshot-<emu>.png
shot: build
    pebble install --emulator {{emu}}
    sleep 4
    pebble screenshot {{shot}} --no-open
    @echo "saved {{shot}}"

# stream app logs from the emulator
logs:
    pebble logs --emulator {{emu}}

# stop the running emulator
kill:
    pebble kill

# pebble clean (forces SDK reconfigure)
app-clean:
    pebble clean

# install the Pebble SDK (first-time setup)
sdk:
    pebble sdk install latest

## ---- guided (interactive; uses gum for input, falls back without it) ----

# print a chosen emulator platform ($EMU wins, else gum picker, else emery)
_pick-emu:
    #!/usr/bin/env bash
    if [ -n "${EMU:-}" ]; then echo "$EMU"
    elif command -v gum >/dev/null 2>&1; then
        gum choose --header "Emulator platform:" emery flint basalt diorite aplite
    else echo emery; fi

# build, install, and open the offline config page to test themes (guided)
config:
    #!/usr/bin/env bash
    set -euo pipefail
    command -v pebble >/dev/null 2>&1 || { echo "Run inside 'nix develop' first."; exit 1; }
    p="$(just _pick-emu)"
    echo "▸ $p: build → install → config"
    pebble build
    pebble install --emulator "$p"
    node tools/gen-config-html.js {{build}}/config.html
    echo "Config page opening for $p — pick a theme/mode and press Save; the watchface updates live."
    # Emulator path: serve the page as a file:// URL. The browser keeps the
    # emulator's ?return_to= in location.href there (a data: URI may drop it), so
    # Save round-trips. On a real watch showConfiguration serves the same page as
    # a data: URI and the page falls back to pebblejs://close#.
    pebble emu-app-config --emulator "$p" --file {{build}}/config.html

# render the config page to build/config.html to open in a desktop browser
preview:
    node tools/gen-config-html.js {{build}}/config.html
    @echo "open {{build}}/config.html"

# one guided command: choose an action (and platform), then run it
menu:
    #!/usr/bin/env bash
    set -euo pipefail
    command -v pebble >/dev/null 2>&1 || { echo "Run inside 'nix develop' first."; exit 1; }
    if ! command -v gum >/dev/null 2>&1; then
        echo "gum not found — use a direct recipe: just run | config | shot | logs | test"; exit 1
    fi
    action=$(gum choose --header "What do you want to do?" \
        "Run on emulator" "Configure themes" "Screenshot" "Tail logs" "Host tests" "Install to watch")
    case "$action" in
      "Host tests") exec just test ;;
      "Install to watch")
        ip=$(gum input --placeholder "phone IP (Pebble app → Developer Connection)")
        pebble build && pebble install --phone "$ip"; exit 0 ;;
    esac
    p=$(gum choose --header "Emulator platform:" emery flint basalt diorite aplite)
    pebble build
    case "$action" in
      "Run on emulator")    pebble install --emulator "$p" ;;
      "Configure themes")   pebble install --emulator "$p"; node tools/gen-config-html.js "{{build}}/config.html"; pebble emu-app-config --emulator "$p" --file "{{build}}/config.html" ;;
      "Screenshot")         pebble install --emulator "$p"; sleep 4; pebble screenshot "{{build}}/screenshot-$p.png" --no-open; echo "saved {{build}}/screenshot-$p.png" ;;
      "Tail logs")          pebble logs --emulator "$p" ;;
    esac
