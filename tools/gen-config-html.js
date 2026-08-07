// Render the offline settings page to a standalone HTML file.
// Used for previewing in a browser and for emulator testing
// (`pebble emu-app-config --file <out>`), which provides return_to so the
// page's pebblejs://close# fallback is not needed there.
// Usage: node tools/gen-config-html.js [out.html] [current-settings.json]
// The optional 2nd arg is a path to a JSON object of current settings; when
// given, the page pre-selects those values (otherwise it shows defaults).
var fs = require('fs');
var spec = require('../src/js/config');
var build = require('../src/js/configpage');
var out = process.argv[2] || 'build/config.html';
var currentPath = process.argv[3];
var current = currentPath ? JSON.parse(fs.readFileSync(currentPath, 'utf8')) : {};
fs.writeFileSync(out, build(spec, current));
console.log('wrote ' + out);
