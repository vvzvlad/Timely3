# Dev tasks for Timely3. Equivalent recipes exist in the justfile.
#   Host unit tests:        run inside `nix develop .#test`   (needs a host cc)
#   Pebble app + emulator:  run inside `nix develop`          (needs pebble CLI)

# Single host-test flag set, shared verbatim with justfile / tools/test.sh /
# tools/strict-check.sh consumers and the CI workflows.
CC       ?= cc
CFLAGS   ?= -std=c11 -Wall -Wextra -Isrc -Itests
BUILD    := build
TEST_BIN := $(BUILD)/test_suite
# Test sources are a single glob (tests/test_*.c) so a new test file is picked up
# everywhere; only the host-compilable pure modules from src/ are linked.
TEST_SRC := $(wildcard tests/test_*.c) src/timefmt.c src/layout.c src/calendar.c src/vibes.c src/suntimes.c src/math.c src/complications.c src/settings.c src/locale.c

# emulator platform: basalt | diorite | emery | flint
EMU  ?= emery
SHOT ?= $(BUILD)/screenshot-$(EMU).png

.PHONY: help test test-js test-xml test-clean build install run shot config preview logs kill app-clean sdk

help:
	@echo "Host tests (nix develop .#test):  test  test-xml  test-clean"
	@echo "JS round-trip test (node):        test-js"
	@echo "Pebble (nix develop):             build  run  config  preview  shot  logs  kill  app-clean  sdk"
	@echo "Guided picker (just):             just menu   (or: just config)"
	@echo "Vars: EMU=$(EMU)  SHOT=$(SHOT)"

## ---- host unit tests ----
test: $(TEST_BIN)
	$(TEST_BIN)

test-xml: $(TEST_BIN)
	$(TEST_BIN) --output=$(BUILD)/test-results.xml

$(TEST_BIN): $(TEST_SRC) $(wildcard src/*.h tests/*.h)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_BIN)

## ---- PebbleKit JS host tests (node:test; no SDK / no npm deps) ----
test-js:
	node --test tests/js/

test-clean:
	rm -f $(TEST_BIN) $(BUILD)/test-results.xml

## ---- Pebble app + emulator ----
build:
	pebble build

run install: build          ## build + (re)install on the emulator
	pebble install --emulator $(EMU)

config: build               ## build + install + open the offline config page (EMU=$(EMU); use `just menu` for a picker)
	pebble install --emulator $(EMU)
	node tools/gen-config-html.js $(BUILD)/config.html
	pebble emu-app-config --emulator $(EMU) --file $(BUILD)/config.html

preview:                    ## render the config page to build/config.html to open in a browser
	node tools/gen-config-html.js $(BUILD)/config.html
	@echo "open $(BUILD)/config.html"

shot: build                 ## build + install + grab a screenshot to $(SHOT)
	pebble install --emulator $(EMU)
	sleep 4
	pebble screenshot $(SHOT) --no-open
	@echo "saved $(SHOT)"

logs:                       ## stream app logs from the emulator
	pebble logs --emulator $(EMU)

kill:                       ## stop the running emulator
	pebble kill

app-clean:                  ## pebble clean (forces SDK reconfigure)
	pebble clean

sdk:                        ## install the Pebble SDK (first-time setup)
	pebble sdk install latest
