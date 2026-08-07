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

// Guarded CommonJS export: present under node and the PebbleKit bundler, absent
// (and harmless) when this file's function text is inlined into the browser page.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { encodeN: encodeN, decodeN: decodeN, clampFromSpec: clampFromSpec };
}
