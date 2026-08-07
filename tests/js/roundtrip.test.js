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
// Run: node --test tests/js/   (or: make test-js)
//
// Migrated to the built-in node:test runner (issue #6: "node --test гоняется в
// CI"). Every original assertion below is preserved verbatim — they guard stages
// 3/4/5 (wire codec, payload budget, language/translation fixes). The runner
// change is mechanical: the hand-rolled `test()` helper is replaced by
// node:test's, so each `test(name, fn)` case is now discovered by `node --test`.

var test = require('node:test');
var assert = require('node:assert');
var wirec = require('../../src/js/wirec');
var WIRE_KEYS = require('../../src/js/wirekeys');
var CONFIG_SPEC = require('../../src/js/config');

var CLAMP = wirec.clampFromSpec(CONFIG_SPEC);

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

// --- Case 5: payload-size budget (issue #4 / audit C4) ---
// The bulky translation strings (trans_*) travel by NAME. A normal save (numeric
// n[] + the two short string settings) is well under budget and must be sent; a
// full custom translation set that would overflow the watch inbox must be
// REFUSED, so the page can show a visible message instead of a silent drop.
// These reddens if the guard is dropped: estimateDictSize returning a constant,
// or payloadFits() always true, fails the oversized assertion; charging strings
// zero bytes (dropping the trans_* accounting) fails it too.
var BUDGET = wirec.PAYLOAD_BUDGET; // single source (src/js/wirec.js)
test('budget: normal save fits, oversized custom translations rejected', function () {
  // A realistic in-budget save: 32 positional numerics + the two by-name strings.
  var small = {
    n: new Array(WIRE_KEYS.length).fill(1),
    strftime_format: '%Y-%m-%d',
    language: 'IT'
  };
  assert.strictEqual(wirec.payloadFits(small, BUDGET), true, 'a normal save must fit the budget');

  // The measured size is stable and comfortably under budget (guards against a
  // future change that silently inflates the per-save payload).
  var smallSz = wirec.estimateDictSize(small);
  assert.ok(smallSz < BUDGET, 'normal save (' + smallSz + 'B) must be < budget');

  // An oversized payload: a full custom translation set (42 trans_* keys) each a
  // long accented string, simulating "Custom" language with verbose strings.
  var big = { n: new Array(WIRE_KEYS.length).fill(1), strftime_format: '%Y-%m-%d', language: 'custom' };
  for (var i = 0; i < 42; i++) {
    big['trans_field_' + i] = 'Uná stringa di traduzione molto lunga numero ' + i; // multibyte
  }
  var bigSz = wirec.estimateDictSize(big);
  assert.ok(bigSz > BUDGET, 'oversized custom set (' + bigSz + 'B) must exceed budget');
  assert.strictEqual(wirec.payloadFits(big, BUDGET), false, 'oversized payload must be REFUSED');

  // ACCEPTANCE (#4b): switching to a BUILT-IN language (Italiano) applies FULLY.
  // A full built-in set is 42 trans_* keys of NORMAL length (e.g. "Mercoledì"),
  // which must FIT the budget — otherwise the acceptance ("Italiano применяется
  // полностью") fails. This sits between `small` and `big`, the real boundary case.
  var builtin = { n: new Array(WIRE_KEYS.length).fill(1), strftime_format: '%Y-%m-%d', language: 'IT' };
  var words = ['Domenica','Lunedì','Martedì','Mercoledì','Giovedì','Venerdì','Sabato',
               'Gennaio','Febbraio','Marzo','Aprile','Maggio','Giugno','Luglio'];
  for (var b2 = 0; b2 < 42; b2++) { builtin['trans_field_' + b2] = words[b2 % words.length]; }
  var builtinSz = wirec.estimateDictSize(builtin);
  assert.strictEqual(wirec.payloadFits(builtin, BUDGET), true,
    'a built-in language (' + builtinSz + 'B) must FIT so it applies fully');

  // Multibyte accounting is by BYTES not chars: an accented string costs more
  // than its character count (so the estimate is not silently under-counting).
  var accented = { n: [], 'trans_x': 'ìììì' }; // 4 chars, 8 UTF-8 bytes
  // 1 (count) + 7 (overhead) + 8 (bytes) + 1 (NUL) = 17
  assert.strictEqual(wirec.estimateDictSize(accented), 17, 'UTF-8 bytes, not chars, must be charged');
});

// ============================================================================
// Stage 5 (issue #5): language / translation fixes C3, H10, H9, L19.
// Each assertion below reddens if its fix is reverted (noted per case).
// ============================================================================
var buildConfigPage = require('../../src/js/configpage');

// Helper: find a translation field spec by key across all config.js sections.
function fieldByKey(key) {
  for (var s = 0; s < CONFIG_SPEC.length; s++) {
    var fields = CONFIG_SPEC[s].fields || [];
    for (var i = 0; i < fields.length; i++) {
      if (fields[i].key === key) { return fields[i]; }
    }
  }
  return null;
}

// --- H10: Russian is reachable ---
// REDDENS if the RU langTable / LANG_OPTIONS entry is removed: LANGS.RU is
// undefined and the option list no longer contains Русский.
test('H10: LANGS.RU exists with the full EN/IT key set, and Русский is selectable', function () {
  var RU = buildConfigPage.LANGS.RU;
  var EN = buildConfigPage.LANGS.EN;
  assert.ok(RU, 'LANGS.RU must exist');
  var enKeys = Object.keys(EN).sort();
  var ruKeys = Object.keys(RU).sort();
  assert.deepStrictEqual(ruKeys, enKeys, 'RU must have the exact same key set as EN');
  // Spot-check real Cyrillic content (not transliteration or English fallthrough).
  assert.strictEqual(RU.trans_monday, 'Понедельник');
  assert.strictEqual(RU.trans_january, 'Январь');
  assert.strictEqual(RU.trans_abbr_monday, 'Пн');
  // Every RU string must be non-empty.
  ruKeys.forEach(function (k) { assert.ok(String(RU[k]).length > 0, 'RU.' + k + ' must be non-empty'); });
  // The dropdown offers Русский -> RU.
  var hasRU = buildConfigPage.LANG_OPTIONS.some(function (o) { return o[0] === 'Русский' && o[1] === 'RU'; });
  assert.ok(hasRU, "LANG_OPTIONS must contain ['Русский','RU']");
});

// --- H9: byte-buffer overflow avoided by clamping input maxlength ---
// REDDENS if the maxes are restored to 6 (AM/PM) or 11 (full months): a maxed
// 2-byte Cyrillic string would then exceed the watch's abbrTime/monthsNames
// buffers.
test('H9: config maxes are 5 (AM/PM) and 10 (full months)', function () {
  assert.strictEqual(fieldByKey('trans_time_am').max, 5, 'AM maxlength must be 5');
  assert.strictEqual(fieldByKey('trans_time_pm').max, 5, 'PM maxlength must be 5');
  assert.strictEqual(fieldByKey('trans_january').max, 10, 'full month maxlength must be 10');
  assert.strictEqual(fieldByKey('trans_december').max, 10, 'full month maxlength must be 10');
  // Unchanged neighbours guard against an over-broad edit.
  assert.strictEqual(fieldByKey('trans_abbr_january').max, 3, 'abbr month stays 3');
  assert.strictEqual(fieldByKey('trans_monday').max, 12, 'full day stays 12');
  // The chosen maxes keep the built-in RU strings within their byte buffers.
  function utf8Len(str) { return unescape(encodeURIComponent(str)).length; }
  var RU = buildConfigPage.LANGS.RU;
  assert.ok(utf8Len(RU.trans_september) <= 20, 'RU full month fits monthsNames[21] (20 bytes + NUL)');
  assert.ok(utf8Len(RU.trans_abbr_monday) <= 5, 'RU abbr day fits abbrDaysOfWeek[6]');
});

// --- L19a: unescaped JSON.stringify in <script> lets a value break out ---
// REDDENS if jsInline() drops the .replace(): "</script>" stays literal and can
// close the <script> element early.
test('L19: jsInline escapes < so </script> cannot close the element', function () {
  var out = buildConfigPage.jsInline({ trans_january: '</script><img src=x>' });
  assert.strictEqual(out.indexOf('</script>'), -1, 'no literal </script> may survive');
  assert.ok(out.indexOf('\\u003c/script>') !== -1, 'the < of </script> must become \\u003c');
  // The escaped text still parses back to the identical value.
  assert.strictEqual(JSON.parse(out).trans_january, '</script><img src=x>');
  // End-to-end: a hostile stored trans string cannot inject a raw </script>.
  var page = buildConfigPage(CONFIG_SPEC, { trans_january: 'x</script>y' });
  assert.ok(page.indexOf('x</script>y') === -1, 'generated page must not carry a raw injected </script>');
});

// --- L19b: app.js persists only known keys (allowlist) ---
// REDDENS if storableKeys is bypassed (the old `for (var k in full)` copy): an
// arbitrary key would ride into the stored blob.
test('L19: storableKeys keeps real settings and lang_mode, drops unknown keys', function () {
  var decoded = {
    theme: 3, language: 'RU', trans_january: 'Январь',
    lang_mode: 'custom', evil: 1, n: [1, 2, 3]
  };
  var out = wirec.storableKeys(decoded, WIRE_KEYS);
  assert.strictEqual(out.theme, 3, 'a wire key is kept');
  assert.strictEqual(out.language, 'RU', 'language is kept');
  assert.strictEqual(out.trans_january, 'Январь', 'trans_* is kept');
  assert.strictEqual(out.lang_mode, 'custom', 'lang_mode is kept (local-only flag)');
  assert.ok(!('evil' in out), 'an unknown key must be dropped');
  assert.ok(!('n' in out), "the positional 'n' array is not a stored key");
});

// --- C3: a persisted Custom choice keeps the selector on Custom ---
// REDDENS if langSelFor loses the lang_mode branch: {lang_mode:'custom'} would
// resolve back to the language code, the page would reopen on that language, and
// onLang()->langFill() would overwrite the user's custom strings.
test('C3: langSelFor keeps Custom persisted, otherwise resolves the language', function () {
  // Custom persisted -> stays Custom even though a real language code is stored.
  assert.strictEqual(buildConfigPage.langSelFor({ lang_mode: 'custom', language: 'EN' }), 'custom');
  assert.strictEqual(buildConfigPage.langSelFor({ language: 'custom' }), 'custom');
  // Non-custom: the stored language decides the selection.
  assert.strictEqual(buildConfigPage.langSelFor({ language: 'RU' }), 'RU');
  assert.strictEqual(buildConfigPage.langSelFor({ lang_mode: 'RU', language: 'RU' }), 'RU');
  assert.strictEqual(buildConfigPage.langSelFor({}), 'EN');
  // An unknown language falls back to Custom (existing strings preserved).
  assert.strictEqual(buildConfigPage.langSelFor({ language: 'ZZ' }), 'custom');
});
