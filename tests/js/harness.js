'use strict';

// Dependency-free node harness for driving src/js/app.js's REAL event handlers
// (specifically the `webviewclosed` config-save path). It is NOT a test file
// (name deliberately excludes the *.test.js pattern) so `node --test` does not
// execute it directly — it is required by handler.test.js.
//
// app.js references the Pebble / localStorage / navigator GLOBALS at module load
// (the top-level `Pebble.addEventListener(...)` calls), so those globals MUST be
// installed BEFORE `require('app.js')`. This harness installs stubs that CAPTURE
// what the handlers do:
//   - Pebble.addEventListener records each registered handler by event name;
//   - Pebble.sendAppMessage records every (msg, ok, fail) call so a test can
//     inspect the exact watch payload and later fire the success/fail callback;
//   - localStorage is a plain in-memory map (the persist-on-ACK path uses it).
// It then requires app.js and returns the captured surface.

function makeHarness() {
  var handlers = {};   // event name -> registered handler fn
  var sent = [];       // captured sendAppMessage calls: { msg, ok, fail }
  var store = {};      // localStorage backing map

  var localStorageStub = {
    getItem: function (k) {
      return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null;
    },
    setItem: function (k, v) { store[k] = String(v); },
    removeItem: function (k) { delete store[k]; },
    clear: function () { for (var k in store) { delete store[k]; } }
  };

  global.Pebble = {
    addEventListener: function (name, fn) { handlers[name] = fn; },
    sendAppMessage: function (msg, ok, fail) { sent.push({ msg: msg, ok: ok, fail: fail }); },
    openURL: function () {},
    getActiveWatchInfo: function () { return { model: 'qemu' }; }
  };
  global.localStorage = localStorageStub;
  // Node >= 21 exposes `navigator` as a getter-only global, so a plain
  // assignment throws TypeError and the whole suite fails to load. defineProperty
  // works on both (CI pins Node 20, local machines are usually newer).
  Object.defineProperty(global, 'navigator', {
    value: { language: 'en' }, configurable: true, writable: true,
  });
  // Some app.js paths reach through window.localStorage / window.navigator.
  global.window = { localStorage: localStorageStub, navigator: global.navigator };

  // Require AFTER the globals exist so the top-level addEventListener calls land
  // in `handlers`. Drop any cached copy first so the registration re-runs cleanly.
  var appPath = require.resolve('../../src/js/app.js');
  delete require.cache[appPath];
  require(appPath);

  return {
    handlers: handlers,
    sent: sent,
    store: store,
    // Reset captured state between tests without re-requiring app.js: the handler
    // closure keeps referencing this same Pebble/localStorage, so emptying the
    // capture array (in place) and the backing store is enough.
    reset: function () {
      sent.length = 0;
      localStorageStub.clear();
    }
  };
}

module.exports = makeHarness;
