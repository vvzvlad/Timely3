'use strict';

var WIRE_KEYS = require('./wirekeys'); // numeric settings sent positionally (see below)
var wirec = require('./wirec'); // pure encode + clamp, shared with the browser page and app.js

// Builds the offline settings page as a self-contained HTML string from the
// config spec. No external CSS/JS/host: app.js serves it via a data: URI, so it
// works with no network. On submit the page redirects to `return_to` (provided
// by the emulator as a query param) or, on a real watch, to pebblejs://close#.
//
// Field types: select (int), toggle (0/1), number (int), text (string),
// time (HH:MM stored as a 10-minute increment 0-143, h*6 + floor(m/10)).

function esc(s) {
  return String(s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function pad2(n) { return ('0' + n).slice(-2); }
function incrToTime(incr) {
  incr = Number(incr) || 0;
  return pad2(Math.floor(incr / 6)) + ':' + pad2((incr % 6) * 10);
}

// Built-in translation tables (keyed by message key) for the language selector.
function langTable(months, abbrM, days, abbrD, conn, disc) {
  var DOW = ['sunday', 'monday', 'tuesday', 'wedsday', 'thursday', 'friday', 'saturday'];
  var MON = ['january', 'february', 'march', 'april', 'may', 'june',
             'july', 'august', 'september', 'october', 'november', 'december'];
  var t = { trans_time_am: 'AM', trans_time_pm: 'PM', trans_connected: conn, trans_disconnected: disc };
  for (var i = 0; i < 12; i++) { t['trans_' + MON[i]] = months[i]; t['trans_abbr_' + MON[i]] = abbrM[i]; }
  for (var j = 0; j < 7; j++) { t['trans_' + DOW[j]] = days[j]; t['trans_abbr_' + DOW[j]] = abbrD[j]; }
  return t;
}
var LANGS = {
  EN: langTable(
    ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'],
    ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'],
    ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'],
    ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa'], 'Linked', 'NOLINK'),
  IT: langTable(
    ['Gennaio', 'Febbraio', 'Marzo', 'Aprile', 'Maggio', 'Giugno', 'Luglio', 'Agosto', 'Settembre', 'Ottobre', 'Novembre', 'Dicembre'],
    ['Gen', 'Feb', 'Mar', 'Apr', 'Mag', 'Giu', 'Lug', 'Ago', 'Set', 'Ott', 'Nov', 'Dic'],
    ['Domenica', 'Lunedì', 'Martedì', 'Mercoledì', 'Giovedì', 'Venerdì', 'Sabato'],
    ['Do', 'Lu', 'Ma', 'Me', 'Gi', 'Ve', 'Sa'], 'Connesso', 'Assente')
};
var LANG_OPTIONS = [['Follow system', 'system'], ['English', 'EN'], ['Italiano', 'IT'], ['Custom', 'custom']];

function selOptions(options, val) {
  var s = '';
  for (var i = 0; i < options.length; i++) {
    s += '<option value="' + esc(options[i][1]) + '"' +
      (Number(val) === Number(options[i][1]) ? ' selected' : '') + '>' + esc(options[i][0]) + '</option>';
  }
  return s;
}

// A "scheduled" control: a mode select plus a From/To window (shown only in the
// time-period mode). The window inputs carry no data-key; app.js derives the
// watch keys from them in augment() so equal From/To can be rejected. `id` is
// "vibe" or "dnd" — augment() knows how each maps to message keys.
function renderSched(id, label, modes, modeVal, fromVal, toVal, note) {
  var h = '<label class="row"><span>' + esc(label) + '</span><select id="' + id + 'Mode">' +
    selOptions(modes, modeVal) + '</select></label>';
  if (note) { h += '<p class="note">' + esc(note) + '</p>'; }
  h += '<div id="' + id + 'Window">' +
    '<label class="row"><span>From</span><input type="time" id="' + id + 'From" value="' + incrToTime(fromVal) + '"></label>' +
    '<label class="row"><span>To</span><input type="time" id="' + id + 'To" value="' + incrToTime(toVal) + '"></label></div>';
  return h;
}

function renderField(f, current) {
  if (f.type === 'vibe-sched') {
    var vs = Number((current && current.vibe_start) || 0);
    var ve = Number((current && current.vibe_stop) || 0);
    // mode comes from vibe_days; fall back to deriving it from older saves.
    var vmode = (current && current.vibe_days != null) ? Number(current.vibe_days)
      : (Number((current && current.vibe_hour) || 0) === 0 ? 0 : (vs === ve ? 1 : 2));
    var vpat = Number((current && current.vibe_hour) || 0);
    if (vpat <= 1) { vpat = 2; } // 0/1 came from the old on-off encoding; default to 2x
    var h = renderSched('vibe', 'Hourly vibration',
      [['Off', 0], ['Always On', 1], ['Time Period', 2], ['Follow Do Not Disturb', 3]], vmode, vs, ve,
      'Time Period: From and To must differ. Follow DND: vibrates hourly except during Do Not Disturb.');
    h += '<label class="row"><span>Vibration pattern</span><select id="vibePat">' +
      selOptions(f.patterns || [['1x', 1]], vpat) + '</select></label>';
    return h;
  }
  if (f.type === 'dnd-sched') {
    var dm = Number((current && current.dnd_noaccel) || 0);
    var ds = Number((current && current.dnd_start) || 0);
    var de = Number((current && current.dnd_stop) || 0);
    return renderSched('dnd', 'Mode',
      [['Off', 0], ['Follow watch', 1], ['Time period', 2]], dm, ds, de,
      'Follow watch uses the system Quiet Time. Time period: From and To must differ.');
  }
  if (f.type === 'lang-sel') {
    var lc = (current && current.language) ? String(current.language).toUpperCase() : 'EN';
    var sel = LANGS[lc] ? lc : 'custom'; // known language -> that; else Custom (keep existing strings)
    var opts = '';
    for (var li = 0; li < LANG_OPTIONS.length; li++) {
      opts += '<option value="' + esc(LANG_OPTIONS[li][1]) + '"' +
        (LANG_OPTIONS[li][1] === sel ? ' selected' : '') + '>' + esc(LANG_OPTIONS[li][0]) + '</option>';
    }
    return '<label class="row"><span>Language</span><select id="langSel">' + opts + '</select></label>' +
      '<p class="note">Pick a language to fill the strings below, or Custom to edit them by hand.</p>';
  }
  var cur = (current && current[f.key] != null) ? current[f.key] : f.def;
  var attrs = 'data-key="' + esc(f.key) + '"';
  var control;
  switch (f.type) {
    case 'toggle':
      control = '<input type="checkbox" ' + attrs + ' data-type="bool"' +
        (Number(cur) ? ' checked' : '') + '>';
      break;
    case 'number':
      control = '<input type="number" ' + attrs + ' data-type="int" value="' + esc(cur) +
        '" min="' + (f.min != null ? f.min : 0) + '" max="' + (f.max != null ? f.max : 100) + '">';
      break;
    case 'time':
      control = '<input type="time" ' + attrs + ' data-type="time" value="' + incrToTime(cur) + '">';
      break;
    case 'text':
      control = '<input type="text" ' + attrs + ' data-type="str" value="' + esc(cur || '') +
        '"' + (f.max ? ' maxlength="' + f.max + '"' : '') + '>';
      break;
    default: // select
      control = '<select ' + attrs + ' data-type="int">';
      for (var i = 0; i < f.options.length; i++) {
        var o = f.options[i];
        control += '<option value="' + esc(o[1]) + '"' +
          (Number(cur) === Number(o[1]) ? ' selected' : '') + '>' + esc(o[0]) + '</option>';
      }
      control += '</select>';
  }
  var row = '<label class="row"><span>' + esc(f.label) + '</span>' + control + '</label>';
  if (f.note) { row += '<p class="note">' + esc(f.note) + '</p>'; }
  if (f.showWhen) {
    row = '<div class="cond" data-wkey="' + esc(f.showWhen.key) + '" data-wval="' +
      esc(f.showWhen.val) + '">' + row + '</div>';
  }
  if (f.langCustom) { row = '<div class="langcustom">' + row + '</div>'; }
  return row;
}

var CSS = [
  '*{box-sizing:border-box}',
  'body{margin:0;padding:14px 14px 92px;background:#1b1b1d;color:#eee;',
  'font-family:-apple-system,Roboto,Segoe UI,Helvetica,Arial,sans-serif;-webkit-text-size-adjust:100%}',
  'h1{font-size:22px;font-weight:600;margin:4px 2px 16px}',
  'details{background:#262629;border-radius:10px;padding:0 14px;margin:0 0 12px;overflow:hidden}',
  'summary{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:#ff9f0a;',
  'font-weight:600;padding:13px 0;cursor:pointer;list-style:none}',
  'summary::-webkit-details-marker{display:none}',
  'summary:after{content:"+";float:right;color:#888;font-weight:400}',
  'details[open] summary:after{content:"\\2212"}',
  '.row{display:flex;align-items:center;justify-content:space-between;gap:12px;',
  'padding:11px 0;border-top:1px solid #333;font-size:16px}',
  'select,input[type=number],input[type=text],input[type=time]{font-size:16px;background:#333;color:#eee;',
  'border:1px solid #454547;border-radius:7px;padding:8px 9px;max-width:55%}',
  'input[type=text]{max-width:48%}',
  'input[type=checkbox]{width:26px;height:26px;accent-color:#ff9f0a;flex:none}',
  '.note{font-size:12px;color:#9a9a9a;margin:2px 0 8px}',
  '.bar{position:fixed;left:0;right:0;bottom:0;display:flex;gap:10px;padding:11px 14px;',
  'background:#1b1b1de6;border-top:1px solid #333}',
  'button{flex:1;font-size:17px;font-weight:600;border:0;border-radius:8px;padding:13px;color:#fff}',
  '#cancel{background:#3a3a3d}#save{background:#ff9f0a;color:#1b1b1d}'
].join('');

function buildConfigPage(spec, current) {
  current = current || {};
  var body = '';
  // Baseline = the value each control starts at. It is only used to delta the
  // bulky translation strings (trans_*): every other setting is always sent so
  // the watch ends up matching exactly what the page shows (WYSIWYG). The phone
  // copy used to seed this baseline is not authoritative — it can drift from the
  // watch — so a "shown but unchanged" value must never be silently dropped.
  var baseline = {};
  for (var s = 0; s < spec.length; s++) {
    var sec = spec[s];
    body += '<details' + (sec.open === false ? '' : ' open') + '><summary>' + esc(sec.title) + '</summary>';
    for (var i = 0; i < sec.fields.length; i++) {
      var f = sec.fields[i];
      body += renderField(f, current);
      if (f.key) { baseline[f.key] = (current[f.key] != null) ? current[f.key] : f.def; }
    }
    body += '</details>';
  }
  // Keys derived from the scheduled (vibe/dnd) controls also need a baseline.
  var derived = ['vibe_days', 'vibe_hour', 'vibe_start', 'vibe_stop', 'dnd_noaccel', 'dnd_start', 'dnd_stop', 'language'];
  for (var d2 = 0; d2 < derived.length; d2++) {
    var dflt = (derived[d2] === 'language') ? 'EN' : 0;
    baseline[derived[d2]] = (current[derived[d2]] != null) ? current[derived[d2]] : dflt;
  }
  // Inline the pure encoder by its OWN source + own name (survives a bundler that
  // renames/anonymises it). __wn (not `n`) holds the result so a rename to a short
  // name can never collide with the result variable.
  var encSrc = wirec.encodeN.toString();
  var encName = (encSrc.match(/function\s+([A-Za-z0-9_$]+)/) || [])[1];
  var encInline = encName ? encSrc : ('var encodeN=' + encSrc);
  var encCall = encName || 'encodeN';
  // Same single-source inline trick for the payload-size estimator (issue #4):
  // the browser page must run the IDENTICAL byte accounting the node test asserts.
  var szSrc = wirec.estimateDictSize.toString();
  var szName = (szSrc.match(/function\s+([A-Za-z0-9_$]+)/) || [])[1];
  var szInline = szName ? szSrc : ('var estimateDictSize=' + szSrc);
  var szCall = szName || 'estimateDictSize';
  var script =
    // Read return_to from the full href: data:/file: URIs do not populate
    // location.search, but the emulator appends ?return_to= and the encoded page
    // body has no literal ? or &, so the match here is unambiguous.
    'function qp(n,d){var m=(location.href||"").match(new RegExp("[?&]"+n+"=([^&#]*)"));' +
    'return m?decodeURIComponent(m[1]):d;}' +
    'var RET=qp("return_to","pebblejs://close#");' +
    'var BASELINE=' + JSON.stringify(baseline) + ';' +
    'var WK=' + JSON.stringify(WIRE_KEYS) + ';' +
    // Declared min/max bounds (from config.js) so the page clamps before sending.
    'var CLAMP=' + JSON.stringify(wirec.clampFromSpec(spec)) + ';' +
    // The exact same pure encoder app.js/tests use — inlined so the browser page
    // (which has no require) runs identical NaN-guard + clamp logic (see wirec.js).
    // Derive the call name from the function's OWN source so a bundler that renames
    // (or anonymises) the top-level `encodeN` can never diverge the definition from
    // the call site below (a silent config-save death). Single source of truth.
    encInline + ';' +
    // The payload-size estimator (issue #4 / audit C4), inlined identically.
    szInline + ';' +
    // BUDGET: the AppMessage inbox size (bytes) the page must stay under. The C
    // side now opens the inbox at app_message_inbox_size_maximum() (Timely.c),
    // whose measured value on this hardware was 2044 bytes (see the log comment
    // there). We hold ~244 bytes (~12%) back for estimate error (JS int width /
    // firmware framing this cheap model does not capture) and cross-platform
    // variance, giving 1800. That comfortably clears the Italiano worst case
    // (~1330 bytes AppMessage), so language switching now succeeds where the old
    // 1280 inbox dropped it, while still refusing a pathological custom-string
    // blob. APPROXIMATE: without an on-device measurement per platform this is a
    // conservative constant, not a proven ceiling (flagged to the orchestrator).
    'var BUDGET=' + wirec.PAYLOAD_BUDGET + ';' +
    'var LANGS=' + JSON.stringify(LANGS) + ';' +
    // Language selector: resolve the chosen code, fill the (hidden) translation
    // fields from the table, and toggle the Custom editor.
    'function langResolve(v){if(v==="system"){var n=(navigator.language||"en").slice(0,2).toUpperCase();return LANGS[n]?n:"EN";}if(v==="custom")return (BASELINE.language||"EN");return v;}' +
    'function langFill(code){var t=LANGS[code];if(!t)return;for(var k in t){var e=document.querySelector(\'[data-key="\'+k+\'"]\');if(e)e.value=t[k];}}' +
    'function langCustomShow(on){var cs=document.querySelectorAll(".langcustom");for(var i=0;i<cs.length;i++)cs[i].style.display=on?"":"none";}' +
    'function onLang(){var s=byId("langSel");if(!s)return;var v=s.value;if(v==="custom"){langCustomShow(true);}else{langCustomShow(false);langFill(langResolve(v));}}' +
    'function wireLang(){var s=byId("langSel");if(s){s.onchange=onLang;onLang();}}' +
    'function val(e){var t=e.getAttribute("data-type");' +
    'if(t==="bool")return e.checked?1:0;' +
    'if(t==="str")return e.value;' +
    'if(t==="time"){var p=(e.value||"0:0").split(":");' +
    'return (parseInt(p[0],10)||0)*6+Math.floor((parseInt(p[1],10)||0)/10);}' +
    'return Number(e.value);}' +
    'function t2i(v){var p=(v||"0:0").split(":");return (parseInt(p[0],10)||0)*6+Math.floor((parseInt(p[1],10)||0)/10);}' +
    'function byId(x){return document.getElementById(x);}' +
    // The From/To window is only relevant in mode 2 (time period); hide it otherwise.
    'function sync(id){var s=byId(id+"Mode"),w=byId(id+"Window");if(s&&w)w.style.display=(+s.value===2?"":"none");}' +
    'function wire(id){var s=byId(id+"Mode");if(s){s.onchange=function(){sync(id);};sync(id);}}' +
    'wire("vibe");wire("dnd");wireLang();' +
    // Conditional fields (.cond): shown only when their source control has the value.
    'function syncCond(){var cs=document.querySelectorAll(".cond");for(var i=0;i<cs.length;i++){' +
    'var c=cs[i],src=document.querySelector(\'[data-key="\'+c.getAttribute("data-wkey")+\'"]\');' +
    'c.style.display=(src&&String(src.value)===String(c.getAttribute("data-wval")))?"":"none";}}' +
    'var allk=document.querySelectorAll("[data-key]");for(var z=0;z<allk.length;z++){allk[z].addEventListener("change",syncCond);}' +
    'syncCond();' +
    // Derive the watch keys from the scheduled controls; return false to abort.
    'function augment(o){var v=byId("vibeMode");if(v){var m=+v.value;o.vibe_days=m;' +
    'var vp=byId("vibePat");o.vibe_hour=(m===0?0:(vp?+vp.value:1));' +
    'if(m===2){var f=t2i(byId("vibeFrom").value),t=t2i(byId("vibeTo").value);' +
    'if(f===t){alert("Hourly vibration: From and To must differ.");return false;}' +
    'o.vibe_start=f;o.vibe_stop=t;}else{o.vibe_start=0;o.vibe_stop=0;}}' +
    'var d=byId("dndMode");if(d){var n=+d.value;o.dnd_noaccel=n;' +
    'if(n===2){var f2=t2i(byId("dndFrom").value),t2=t2i(byId("dndTo").value);' +
    'if(f2===t2){alert("Do Not Disturb: From and To must differ.");return false;}' +
    'o.dnd_start=f2;o.dnd_stop=t2;}else{o.dnd_start=0;o.dnd_stop=0;}}' +
    'var ls=byId("langSel");if(ls){o.language=langResolve(ls.value);}return true;}' +
    'document.getElementById("cancel").onclick=function(){document.location=RET;};' +
    'document.getElementById("save").onclick=function(){var o={},' +
    'els=document.querySelectorAll("[data-key]");for(var i=0;i<els.length;i++){' +
    'o[els[i].getAttribute("data-key")]=val(els[i]);}' +
    'if(augment(o)===false)return;' +
    // WYSIWYG: send every numeric setting positionally (in WK order) so the watch
    // matches the page exactly — a drifted phone-side baseline must never leave a
    // shown value unapplied. Encoding values-only keeps the pebblejs://close
    // payload tiny so a real watch carries it without truncation (the old verbose
    // per-key dump got dropped). The two short string settings ride along by name,
    // and changed translation strings are appended (delta — they are bulk).
    'var __wn=' + encCall + '(o,WK,CLAMP);' +
    'var send={n:__wn};' +
    'if(o.strftime_format!=null)send.strftime_format=o.strftime_format;' +
    'if(o.language!=null)send.language=o.language;' +
    'for(var k in o){if(k.indexOf("trans_")===0&&String(o[k])!==String(BASELINE[k]))send[k]=o[k];}' +
    // Budget guard (issue #4 / audit C4): the bulky trans_* strings travel by
    // NAME, so a full custom translation set can exceed the watch inbox. Measure
    // the estimated on-device dict size and, if it is over BUDGET, do NOT send.
    // Show a VISIBLE, actionable message instead of the old silent overflow that
    // dropped the whole batch (and left localStorage stale, so retries repeated).
    // TODO(#4, deferred stage): the robust fix is CHUNKING — ship the translation
    // strings as a SECOND AppMessage once the first is ACKed. That is a two-message
    // protocol change (C-side handling of the follow-up message), deliberately
    // out of scope here; (a) maxed inbox + (b) this visible refusal satisfy the
    // acceptance ("либо разбит, либо пользователь видит отказ").
    'var __sz=' + szCall + '(send);' +
    'if(__sz>BUDGET){alert("These settings are too large to send to the watch ("+__sz+" of "+BUDGET+" bytes).\\n\\nPick a built-in language, or shorten the custom translation strings, then Save again.");return;}' +
    'document.location=RET+encodeURIComponent(JSON.stringify(send));};';
  return '<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>TimelyNG settings</title><style>' + CSS + '</style></head><body>' +
    '<h1>TimelyNG</h1>' + body +
    '<div class="bar"><button type="button" id="cancel">Cancel</button>' +
    '<button type="button" id="save">Save</button></div>' +
    '<script>' + script + '<\/script></body></html>';
}

module.exports = buildConfigPage;
