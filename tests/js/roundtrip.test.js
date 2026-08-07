'use strict';

// Focused round-trip test for the settings wire codec (issue #3, H8). Plain
// node asserts, no npm deps. Stage 6 builds the full JS harness; this proves the
// four failure modes the audit named, each of which reddened on the ORIGINAL
// (pre-fix) inline encode/decode:
//   - round-trip identity      : encode -> decode == input (valid values)
//   - empty field -> 0 not NaN : Number("") no longer JSON.stringify()s to null
//   - over-max clamped         : 999 clamped to the field's max, not a uint8 wrap
//   - wrong-length rejected     : a mis-sized n[] is refused, not applied misaligned
//
// Run: node tests/js/roundtrip.test.js   (or: make test-js)

var assert = require('assert');
var wirec = require('../../src/js/wirec');
var WIRE_KEYS = require('../../src/js/wirekeys');
var CONFIG_SPEC = require('../../src/js/config');

var CLAMP = wirec.clampFromSpec(CONFIG_SPEC);
var pass = 0;
function test(name, fn) { fn(); pass++; console.log('  ok - ' + name); }

// A fully-populated, in-range settings object: each wire key gets a distinct
// small integer (<= 100, so nothing trips the show_stat_batt 0..100 clamp).
function validSettings() {
  var o = {};
  for (var i = 0; i < WIRE_KEYS.length; i++) { o[WIRE_KEYS[i]] = i + 1; }
  return o;
}

// --- Case 1: round-trip identity (numerics positional + strings by name) ---
test('round-trip identity', function () {
  var settings = validSettings();
  settings.strftime_format = '%Y-%m-%d';
  settings.language = 'EN';

  var n = wirec.encodeN(settings, WIRE_KEYS, CLAMP);
  var payload = { n: n, strftime_format: settings.strftime_format, language: settings.language };
  var decoded = wirec.decodeN(payload, WIRE_KEYS);

  assert.notStrictEqual(decoded, null, 'valid payload must not be rejected');
  for (var i = 0; i < WIRE_KEYS.length; i++) {
    assert.strictEqual(decoded[WIRE_KEYS[i]], settings[WIRE_KEYS[i]],
      'key ' + WIRE_KEYS[i] + ' must round-trip unchanged');
  }
  assert.strictEqual(decoded.strftime_format, '%Y-%m-%d');
  assert.strictEqual(decoded.language, 'EN');
});

// --- Case 2: NaN field -> 0, not NaN/null (the H8 headline) ---
// NOTE ON THE AUDIT'S PREMISE: the audit says Number("") is NaN — it is actually
// 0 (parseInt/parseFloat give NaN for "", Number does not), and the page's val()
// uses Number(), so a literally-cleared input already encodes to 0 even on the
// ORIGINAL code. The genuine hazard the guard removes is any NaN reaching the
// settings object from another path (a drifted localStorage blob, a non-numeric
// coercion). ORIGINAL `n.push(wv==null?0:wv)` let a real NaN through (NaN != null),
// JSON.stringify turned that slot into null, and the watch decode nuked the batch.
// This test feeds an actual NaN so it reddens on the original (a Number("") value
// would NOT, hence would be vacuous).
test('NaN field -> 0 not NaN/null', function () {
  // sanity: confirm the premise correction so the test's rationale stays honest.
  assert.strictEqual(Number(''), 0, "Number('') is 0, not NaN");

  var settings = validSettings();
  settings.show_stat_batt = NaN; // a non-numeric / drifted value reaching the object

  var n = wirec.encodeN(settings, WIRE_KEYS, CLAMP);
  var idx = WIRE_KEYS.indexOf('show_stat_batt');

  assert.strictEqual(n[idx], 0, 'NaN must encode as 0');
  assert.ok(!Number.isNaN(n[idx]), 'no NaN may enter the array');
  // The real corruption path is JSON: NaN serialises to null and nukes the slot.
  assert.strictEqual(JSON.parse(JSON.stringify(n))[idx], 0, 'must not JSON-serialise to null');
});

// --- Case 3: over-max clamped (no uint8 wrap) ---
// ORIGINAL sent 999 verbatim; the watch reads it as uint8 -> 999 & 0xFF = 231.
test('over-max clamped', function () {
  var settings = validSettings();
  settings.show_stat_batt = 999;

  var n = wirec.encodeN(settings, WIRE_KEYS, CLAMP);
  var idx = WIRE_KEYS.indexOf('show_stat_batt');

  assert.strictEqual(n[idx], 100, '999 must clamp to the declared max (100), not wrap to 231');
});

// --- Case 4: wrong-length payload rejected ---
// ORIGINAL iterated WIRE_KEYS.length blindly: a short n[] left an undefined tail,
// a long one silently misaligned every key. decodeN now returns null instead.
test('wrong-length payload rejected', function () {
  var shortPayload = { n: [1, 2, 3] };
  var longPayload = { n: new Array(WIRE_KEYS.length + 5).fill(1) };
  var exactPayload = { n: new Array(WIRE_KEYS.length).fill(1) };

  assert.strictEqual(wirec.decodeN(shortPayload, WIRE_KEYS), null, 'short n[] must be rejected');
  assert.strictEqual(wirec.decodeN(longPayload, WIRE_KEYS), null, 'long n[] must be rejected');
  assert.notStrictEqual(wirec.decodeN(exactPayload, WIRE_KEYS), null, 'exact-length n[] must be accepted');
});

console.log('\nAll ' + pass + ' JS round-trip tests passed.');
