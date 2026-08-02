'use strict';

// Stage 6 (issue #6): round-trip regression tests that drive the ACTUAL app.js
// `webviewclosed` config-save handler through the node harness, plus the T4
// 32-key wire-coverage check. Each T2 test names the historical commit whose fix
// it guards and reddens if that fix is reverted (proven by scratch-revert in the
// stage report). Dependency-free; run via `node --test tests/js/`.
//
// OUT OF SCOPE (honest note per the audit): two of the seven historical
// settings-loss fixes are DEVICE-TRANSPORT failures a host node test cannot
// reproduce, and this suite deliberately does NOT fake them:
//   - 1c2ecfe: on a REAL watch the phone TRUNCATES an oversized pebblejs://close
//     URL mid-JSON (the emulator's file:// return path was unaffected, which is
//     why the regression slipped through). There is no phone URL transport in
//     node, so the truncation event cannot occur here. The compact positional
//     wire format that fixes it IS exercised indirectly (encodeN/decodeN in
//     roundtrip.test.js and the full-array application test below), but the
//     truncation itself is not host-reproducible.
//   - ff78b94: reading return_to from location.href because a data:/file: URI
//     does not populate location.search. That is browser-navigation behavior of
//     the generated page inside the emulator webview; node has no data: URI
//     location object, so it is out of scope.

var test = require('node:test');
var assert = require('node:assert');

var makeHarness = require('./harness');
var WIRE_KEYS = require('../../src/js/wirekeys');
var CONFIG_SPEC = require('../../src/js/config');
var pkg = require('../../package.json');

var h = makeHarness();

// A full positional array: each wire slot gets a distinct in-range value.
function fullN() { return WIRE_KEYS.map(function (k, i) { return i + 1; }); }

// ---------------------------------------------------------------------------
// T4 — 32-key coverage: the three layers converge.
// ---------------------------------------------------------------------------
// Every wire key must be (a) a real messageKey in package.json (the C/wire
// layer) AND (b) reachable from a config-page control — either a config.js
// field `key` or a key DERIVED from a composite control (vibe-sched / dnd-sched)
// or the language selector. The derived list mirrors buildConfigPage() in
// configpage.js. Non-vacuous: dropping a wire key from messageKeys, or adding a
// wire key with no page control, reddens this test.
test('T4: every wire key is a real messageKey AND has a config-page control (config ∪ derived)', function () {
  var messageKeys = Object.keys(pkg.pebble.messageKeys);
  var derived = ['vibe_days', 'vibe_hour', 'vibe_start', 'vibe_stop',
                 'dnd_noaccel', 'dnd_start', 'dnd_stop', 'language'];
  var configKeys = {};
  for (var s = 0; s < CONFIG_SPEC.length; s++) {
    var fields = CONFIG_SPEC[s].fields || [];
    for (var i = 0; i < fields.length; i++) {
      if (fields[i].key) { configKeys[fields[i].key] = true; }
    }
  }

  assert.strictEqual(WIRE_KEYS.length, 32, 'the wire contract is 32 numeric keys');
  WIRE_KEYS.forEach(function (k) {
    assert.ok(messageKeys.indexOf(k) !== -1,
      k + ' must be declared in package.json messageKeys (C/wire layer)');
    assert.ok(configKeys[k] === true || derived.indexOf(k) !== -1,
      k + ' must have a config-page control (config.js field ∪ derived composite)');
  });
});

// ---------------------------------------------------------------------------
// T2 — 4e16eff: an already-decoded payload with a literal % must still save.
// ---------------------------------------------------------------------------
// The phone hands e.response back ALREADY DECODED: it starts with "{" and holds
// a literal % from the default strftime_format "%Y-%m-%d". The pre-fix handler
// ran decodeURIComponent unconditionally -> URIError "URI malformed" -> the catch
// swallowed it -> the whole save was silently dropped. REDDENS if app.js drops
// the `raw.match(/^\{/) ? raw : decodeURIComponent(raw)` guard (unconditional
// decode): sent.length becomes 0.
test('4e16eff: an already-decoded payload with a literal % (strftime) still saves', function () {
  h.reset();
  var payload = { n: fullN(), strftime_format: '%Y-%m-%d', language: 'EN' };
  h.handlers.webviewclosed({ response: JSON.stringify(payload) });

  assert.strictEqual(h.sent.length, 1,
    'exactly one watch message must be sent (handler must not die on decodeURIComponent)');
  var msg = h.sent[0].msg;
  assert.strictEqual(msg.strftime_format, '%Y-%m-%d', 'the literal-% string setting survives');
  assert.strictEqual(msg.language, 'EN');
  WIRE_KEYS.forEach(function (k, i) {
    assert.strictEqual(msg[k], i + 1, k + ' must reach the watch');
  });
});

// ---------------------------------------------------------------------------
// T2 — length-mismatch refusal: dict.n.length !== WIRE_KEYS.length -> not applied.
// ---------------------------------------------------------------------------
// A short n[] leaves an undefined tail; a long one misaligns every following key.
// decodeN returns null and the handler must send NOTHING. REDDENS if the length
// check in wirec.decodeN is removed: a mismatched payload then gets applied and a
// watch message is sent.
test('length-mismatch: a wrong-length n[] is refused, not applied misaligned', function () {
  h.reset();
  h.handlers.webviewclosed({ response: JSON.stringify({ n: [1, 2, 3] }) });
  assert.strictEqual(h.sent.length, 0, 'a short n[] must be refused (no watch message)');

  h.reset();
  var long = new Array(WIRE_KEYS.length + 5).fill(1);
  h.handlers.webviewclosed({ response: JSON.stringify({ n: long }) });
  assert.strictEqual(h.sent.length, 0, 'a long n[] must be refused (no watch message)');

  h.reset();
  h.handlers.webviewclosed({ response: JSON.stringify({ n: fullN() }) });
  assert.strictEqual(h.sent.length, 1, 'an exact-length n[] is accepted');
});

// ---------------------------------------------------------------------------
// T2 — 46a5f52 (drifted baseline): every SHOWN setting reaches the watch.
// ---------------------------------------------------------------------------
// 46a5f52 fixed the ENCODE side (configpage.js) so the page always emits every
// numeric setting positionally instead of only baseline-changed keys — a drifted
// phone baseline must never leave a shown value unapplied. That inlined browser
// script is not host-executable, so this test guards the RECEIVE-side half in
// app.js: given a full positional array, the handler expands it and forwards
// EVERY wire key to the watch (and strips the local-only lang_mode). REDDENS if
// app.js stops expanding n[] to keys (the pre-1c2ecfe passthrough) — the wire
// keys never reach the watch payload.
test('46a5f52: a full positional save applies every shown setting despite a drifted baseline', function () {
  h.reset();
  // Phone-side localStorage is STALE / drifted from the watch (wrong theme, etc.).
  h.store['timely_settings'] = JSON.stringify({ theme: 0, weather_fmt: 0, show_stat_batt: 5 });
  h.handlers.webviewclosed({
    response: JSON.stringify({ n: fullN(), language: 'RU', lang_mode: 'custom' })
  });

  assert.strictEqual(h.sent.length, 1);
  var msg = h.sent[0].msg;
  WIRE_KEYS.forEach(function (k, i) {
    assert.strictEqual(msg[k], i + 1,
      k + ' (shown value) must reach the watch even though the phone baseline drifted');
  });
  assert.strictEqual(msg.language, 'RU', 'the string setting is applied too');
  assert.ok(!('lang_mode' in msg), 'lang_mode is local-only and must never be sent to the watch');
});

// ---------------------------------------------------------------------------
// T2 — c2a0665: a save MERGES into stored settings (persist only on ACK).
// ---------------------------------------------------------------------------
// c2a0665 fixed localStorage being OVERWRITTEN with just the delta, which wiped
// every untouched setting so the page reopened showing defaults. The handler now
// merges the save into the existing blob, and only from the success callback (H5)
// so a NACK leaves the full delta to recompute. REDDENS if app.js reverts to
// overwriting localStorage with only the current payload — the pre-existing
// trans_january disappears.
test('c2a0665: a save merges into stored settings on ACK, keeping unrelated keys', function () {
  h.reset();
  // A previously-saved translation string this save's payload does NOT carry.
  h.store['timely_settings'] = JSON.stringify({ trans_january: 'Январь', language: 'RU' });
  h.handlers.webviewclosed({ response: JSON.stringify({ n: fullN() }) });
  assert.strictEqual(h.sent.length, 1);

  // H5: nothing persists until the watch ACKs — the blob is untouched pre-callback.
  assert.strictEqual(JSON.parse(h.store['timely_settings']).theme, undefined,
    'must NOT persist before the watch ACKs');

  // Fire the success callback the handler passed to sendAppMessage.
  h.sent[0].ok({ data: { transactionId: 7 } });

  var stored = JSON.parse(h.store['timely_settings']);
  assert.strictEqual(stored.trans_january, 'Январь',
    'a pre-existing key must survive the merge (not be overwritten by the delta)');
  assert.strictEqual(stored.theme, 1, 'the newly saved wire keys are merged in');
});
