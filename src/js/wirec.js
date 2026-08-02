'use strict';

// Pure, dependency-free wire codec shared by three contexts:
//   1. node (tests/js/roundtrip.test.js) — via require();
//   2. the PebbleKit JS runtime (app.js) — via require() through the bundler,
//      where decodeN() expands an incoming payload back to keys;
//   3. the browser config page — configpage.js inlines encodeN.toString() into
//      the generated <script>, so the SAME encode runs there with no require.
// Because of (3) these functions must reference nothing outside their own args
// (no Pebble / localStorage / DOM / module scope). WIRE_KEYS order is always
// passed in by the caller; this module never reorders or owns the wire contract.

// Build the positional numeric array from a settings object, in wireKeys order.
// A cleared numeric field yields Number("") === NaN; NaN != null, so the old
// `wv==null?0:wv` let it through and JSON.stringify turned it into null, nuking
// every setting in the batch. Number.isFinite() guards that (and undefined /
// Infinity) to 0. `clamp` is an optional { key: {min, max} } map (declared
// bounds from config.js) so an out-of-range value can't wrap the watch's uint8.
function encodeN(o, wireKeys, clamp) {
  var n = [];
  for (var i = 0; i < wireKeys.length; i++) {
    var key = wireKeys[i];
    var num = Number(o[key]);
    var v = Number.isFinite(num) ? num : 0;
    if (clamp && clamp[key]) {
      var c = clamp[key];
      if (c.min != null && v < c.min) { v = c.min; }
      if (c.max != null && v > c.max) { v = c.max; }
    }
    n.push(v);
  }
  return n;
}

// Expand a positional numeric array back to keyed settings. Returns null (the
// caller must NOT apply it) when the array length disagrees with the wire
// contract: a short array leaves an undefined tail, a long one misaligns every
// key after the first extra element. A legacy flat payload with no `n` passes
// its keys through unchanged.
function decodeN(dict, wireKeys) {
  var out = {};
  if (dict.n && dict.n.length) {
    if (dict.n.length !== wireKeys.length) { return null; }
    for (var i = 0; i < wireKeys.length; i++) { out[wireKeys[i]] = dict.n[i]; }
  }
  for (var k in dict) { if (k !== 'n') { out[k] = dict[k]; } }
  return out;
}

// Derive a { key: {min, max} } clamp map from the config.js spec: only fields
// that declare min/max (the `number` inputs) contribute, so selects/toggles are
// left untouched. Pure — walks the plain spec array, no side effects.
function clampFromSpec(spec) {
  var clamp = {};
  for (var s = 0; s < spec.length; s++) {
    var fields = spec[s].fields || [];
    for (var i = 0; i < fields.length; i++) {
      var f = fields[i];
      // Only NUMBER fields carry value bounds; a text field's `max` is a maxlength,
      // not a numeric clamp, so it must not enter CLAMP (harmless — it is never in
      // WIRE_KEYS — but it bloats every generated page and is semantically wrong).
      if (f.key && f.type === 'number' && (f.min != null || f.max != null)) {
        clamp[f.key] = { min: (f.min != null ? f.min : null), max: (f.max != null ? f.max : null) };
      }
    }
  }
  return clamp;
}

// Filter a decoded settings object down to the keys we are willing to PERSIST in
// localStorage (audit L19). Only real settings survive: the numeric wire keys,
// the two by-name string settings (strftime_format, language), any trans_*
// translation string, and lang_mode. lang_mode is a LOCAL-ONLY flag (whether the
// user chose "Custom") — stored so the page reopens on Custom (audit C3) but it
// is NOT a message key and app.js strips it from the watch payload. Any other key
// (an unknown or hostile one riding in via the returned payload) is DROPPED, so
// it can never enter the stored blob that configpage.js re-inlines into <script>.
// Pure — wireKeys order is passed in; this module never owns the wire contract.
function storableKeys(dict, wireKeys) {
  var known = {};
  for (var i = 0; i < wireKeys.length; i++) { known[wireKeys[i]] = true; }
  known.strftime_format = true;
  known.language = true;
  known.lang_mode = true; // localStorage-only; never sent to the watch
  var out = {};
  for (var k in dict) {
    if (!Object.prototype.hasOwnProperty.call(dict, k)) { continue; }
    if (known[k] === true || k.indexOf('trans_') === 0) { out[k] = dict[k]; }
  }
  return out;
}

// Estimate the on-device AppMessage inbox size (in bytes) the settings `send`
// object will occupy, so the config page can REFUSE an oversized save with a
// visible message instead of overflowing the watch inbox silently (issue #4 /
// audit C4). This measures the quantity that actually overflowed — the inbox
// Dictionary the watch must hold — NOT the URL / JSON string length.
//
// Flow: the page ships `send = { n:[...numerics...], strftime_format, language,
// changed trans_* }`; app.js runs decodeN(), which expands `n` into ONE numeric
// Tuple per wire key and passes every other key through as its own string Tuple,
// then calls Pebble.sendAppMessage(full). Pebble's dictionary format charges a
// fixed per-Tuple overhead (key u32 + type u8 + length u16 = 7 bytes) plus the
// value bytes: 4 for an int, and (UTF-8 byte length + 1 NUL) for a string; a
// 1-byte tuple count leads the whole dict.
//
// This is an ESTIMATE — the exact JS integer width and firmware framing are
// internal — deliberately biased to slightly OVER-count (ints charged the full
// 4 bytes) so the budget stays conservative and refuses BEFORE a real overflow.
// Self-contained (its own nested utf8Len, no module-scope refs) because
// configpage.js inlines this function's source text into the browser page.
function estimateDictSize(send) {
  var TUPLE_OVERHEAD = 7; // Pebble Tuple: key(4) + type(1) + length(2)
  var INT_BYTES = 4;      // PebbleKit JS sends a plain number as a 4-byte int
  // UTF-8 byte length of a JS string. Italian translation strings ("Lunedì",
  // "Mercoledì") are multibyte, and it is BYTES, not chars, the inbox holds.
  // Hand-rolled (no TextEncoder — absent in the old PebbleKit JS runtime and in
  // the inlined browser page).
  function utf8Len(s) {
    var n = 0;
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c < 0x80) { n += 1; }
      else if (c < 0x800) { n += 2; }
      else if (c >= 0xD800 && c <= 0xDBFF) { n += 4; i++; } // surrogate pair -> 4 bytes
      else { n += 3; }
    }
    return n;
  }
  var total = 1; // leading tuple-count byte
  for (var k in send) {
    if (!Object.prototype.hasOwnProperty.call(send, k)) { continue; }
    var v = send[k];
    if (k === 'n' && v && v.length != null) {
      // The positional numeric array expands to one int Tuple per wire key.
      total += v.length * (TUPLE_OVERHEAD + INT_BYTES);
    } else {
      total += TUPLE_OVERHEAD + utf8Len(String(v)) + 1;
    }
  }
  return total;
}

// Boolean over-budget decision, node-testable. True when `send` is safe to ship.
function payloadFits(send, budget) {
  return estimateDictSize(send) <= budget;
}

// Guarded CommonJS export: present under node and the PebbleKit bundler, absent
// (and harmless) when this file's function text is inlined into the browser page.
// The single source for the payload byte budget (used by configpage.js's page
// script and the tests). Set BELOW the firmware inbox ceiling (measured ~2044 on
// the target platforms) with headroom for this cheap estimate's error. A built-in
// language save is ~1022-1330 B, so it fits; only a pathological custom-string
// blob is refused.
var PAYLOAD_BUDGET = 1800;

if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    encodeN: encodeN, decodeN: decodeN, clampFromSpec: clampFromSpec,
    storableKeys: storableKeys,
    estimateDictSize: estimateDictSize, payloadFits: payloadFits,
    PAYLOAD_BUDGET: PAYLOAD_BUDGET
  };
}
