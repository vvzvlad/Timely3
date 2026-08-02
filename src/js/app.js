var CONFIG_SPEC = require('./config');
var buildConfigPage = require('./configpage');
var WIRE_KEYS = require('./wirekeys'); // decode the positional numeric settings (see webviewclosed)
var wirec = require('./wirec'); // pure decode (length-rejection), shared with configpage.js and tests

// The bundled SunCalc library (below) used to attach itself to `window`; under
// the CommonJS bundler there is no global object, so it populates this
// module-scoped variable instead (see its assignment further down).
var SunCalc;

var CLIMACON = {
  'cloud'            : '!',
  'cloud_day'        : '"',
  'cloud_night'      : '#',
  'rain'             : '$',
  'rain_day'         : '%',
  'rain_night'       : '&',
  'showers'          : "'",
  'showers_day'      : '(',
  'showers_night'    : ')',
  'downpour'         : '*',
  'downpour_day'     : '+',
  'downpour_night'   : ',',
  'drizzle'          : '-',
  'drizzle_day'      : '.',
  'drizzle_night'    : '/',
  'sleet'            : '0',
  'sleet_day'        : '1',
  'sleet_night'      : '2',
  'hail'             : '3',
  'hail_day'         : '4',
  'hail_night'       : '5',
  'flurries'         : '6',
  'flurries_day'     : '7',
  'flurries_night'   : '8',
  'snow'             : '9',
  'snow_day'         : ':',
  'snow_night'       : ';',
  'fog'              : '<',
  'fog_day'          : '=',
  'fog_night'        : '>',
  'haze'             : '?',
  'haze_day'         : '@',
  'haze_night'       : 'A',
  'wind'             : 'B',
  'wind_cloud'       : 'C',
  'wind_cloud_day'   : 'D',
  'wind_cloud_night' : 'E',
  'lightning'        : 'F',
  'lightning_day'    : 'G',
  'lightning_night'  : 'H',
// ---
  'sun'              : 'I',
   'set'             : 'J',
   'rise'            : 'K',
   'low'             : 'L',
   'lower'           : 'M',
  'moon'             : 'N',
   'new'             : 'O',
   'wax_cresc'       : 'P',
   'wax_quart'       : 'Q',
   'wax_gib'         : 'R',
   'full'            : 'S',
   'wane_cresc'      : 'T',
   'wane_quart'      : 'U',
   'wane_gib'        : 'V',
  'snowflake'        : 'W',
  'tornado'          : 'X',
  'thermometer'      : 'Y',
   'temp_low'        : 'Z',
   'temp_med-low'    : '[',
   'temp_med-high'   : "\\",
   'temp_high'       : ']',
   'temp_full'       : '^',
  'celsius'          : '`',
  'fahrenheit'       : '_',
  'compass'          : 'a',
   'north'           : 'b',
   'east'            : 'c',
   'south'           : 'd',
   'west'            : 'e',
  'umbrella'         : 'f',
  'sunglasses'       : 'g',
  'cloud_refresh'    : 'h',
  'cloud_up'         : 'i',
  'cloud_down'       : 'j'
};


function sendWeather(temp, cond_icon, city, lat, lon) {
  // Day/night (and the clear-night moon glyph) are already resolved by the
  // weather mapping using the provider's is_day flag.
  console.log('Sending Weather: ' + temp + '  ' + cond_icon + '  ' + (city || ''));
  var msg = {
    message_type: 106,
    weather_temp: temp,
    weather_cond: cond_icon,
    weather_city: city || '',
  };
  // Coordinates power the sunrise/sunset complications and the Auto theme.
  if (lat != null && lon != null) {
    msg.weather_lat = lat.toFixed(2);
    msg.weather_lon = lon.toFixed(2);
  }
  Pebble.sendAppMessage(msg);
}

var locationOptions = { "timeout": 15000, "maximumAge": 60000, "enableHighAccuracy": false }; // 15 second timeout, allow 1 min cached
//var cachedLocationOptions = { "timeout": 0, "maximumAge": 600000, "enableHighAccuracy": false }; // allow 10 min cached
var locationWatcher;
var lastCoordinates;
var weatherFormat;

// Report the phone's battery level to the watch (Web Battery API, where the
// runtime exposes it; silently skipped otherwise).
function updatePhoneBattery() {
    if (!navigator.getBattery) {
        var info = Pebble.getActiveWatchInfo ? Pebble.getActiveWatchInfo() : null;
        if (info && /qemu/.test(info.model)) {
            Pebble.sendAppMessage({ message_type: 110, phone_battery: 78 });
        }
        return;
    }
    navigator.getBattery().then(function (b) {
        Pebble.sendAppMessage({ message_type: 110, phone_battery: Math.round(b.level * 100) });
    }).catch(function () {});
}

Pebble.addEventListener("ready", function (e) {
    getWatchVersion();
    updatePhoneBattery();
    setInterval(updatePhoneBattery, 30 * 60 * 1000); // refresh every 30 min
});

Pebble.addEventListener("showConfiguration", function () {
    var current = {};
    try { current = JSON.parse(localStorage.getItem("timely_settings") || "{}"); } catch (err) {}
    var html = buildConfigPage(CONFIG_SPEC, current);
    // Self-contained page, no server: the watch hands the whole page to the phone.
    // charset=utf-8 (as Clay does) so accented translation strings survive.
    Pebble.openURL("data:text/html;charset=utf-8," + encodeURIComponent(html));
});

function getWatchVersion() {
    Pebble.sendAppMessage({ message_type: 104 },
        function (e) {
            console.log("Sent watch version request with transactionId=" + e.data.transactionId);
        },
        function (e) {
            console.log("Unable to deliver watch version request message with transactionId=" + (e.data && e.data.transactionId) + " Error is: " + (e.error && e.error.message));
        }
        );
}

function saveWatchVersion(e) {
    console.log("Watch Version: " + e.payload.send_watch_version);
    console.log("Config Version: " + e.payload.send_config_version);
    window.localStorage.version_watch = e.payload.send_watch_version;
    window.localStorage.version_config = e.payload.send_config_version;
}

function saveBatteryValue(e) {
    console.log("Battery: " + e.payload.send_batt_percent + "%, Charge: " + e.payload.send_batt_charging + ", Plugged: " + e.payload.send_batt_plugged);
/*
var currentdate = new Date(); 
var datetime = "Date: " + currentdate.getDate() + "/"
                + (currentdate.getMonth()+1)  + "/" 
                + currentdate.getFullYear() + " @ "  
                + currentdate.getHours() + ":"  
                + currentdate.getMinutes() + ":" 
                + currentdate.getSeconds();
console.log(datetime);
*/
    // TO BE DONE - actually store these in localStorage along with a date object in some useful manner
}

function sendWeatherToWatch(e) {
    weatherFormat = e.payload.weather_fmt;
    window.navigator.geolocation.getCurrentPosition(weatherLocationSuccess, locationError, locationOptions);
}

function sendTimezoneToWatch() {
    var offsetQuarterHours = Math.floor(new Date().getTimezoneOffset() / 15);
    // UTC offset in quarter hours ... 48 (UTC-12) through -56 (UTC+14) are the valid ranges
    Pebble.sendAppMessage({ message_type: 103, timezone_offset: offsetQuarterHours },
        function (e) {
            console.log("Sent TZ message (" + offsetQuarterHours + ") with transactionId=" + e.data.transactionId);
        },
        function (e) {
            console.log("Unable to deliver TZ message with transactionId=" + (e.data && e.data.transactionId) + " Error is: " + (e.error && e.error.message));
        }
        );
}

Pebble.addEventListener("appmessage", function (e) {
    //console.log("Received message: type " + e.payload.message_type)
    switch (e.payload.message_type) {
    case 100:
        saveBatteryValue(e);
        break;
    case 103:
        sendTimezoneToWatch();
        break;
    case 104:
        saveWatchVersion(e);
        sendTimezoneToWatch(); // a little bonus, since we know the watch is listening
        break;
    case 106:
        sendWeatherToWatch(e);
        break;
    }
});

// Map a WMO weather code (Open-Meteo) to a climacons glyph; day/night aware.
function wmoToClimacon(code, isDay) {
  if (code === 0)                  { return isDay ? CLIMACON['sun'] : getMoonIcon(); }   // clear
  if (code === 1 || code === 2)    { return isDay ? CLIMACON['cloud_day'] : CLIMACON['cloud_night']; } // mainly clear / partly cloudy
  if (code === 3)                  { return CLIMACON['cloud']; }       // overcast
  if (code === 45 || code === 48)  { return isDay ? CLIMACON['fog_day'] : CLIMACON['fog_night']; }     // fog
  if (code >= 51 && code <= 57)    { return CLIMACON['drizzle']; }     // drizzle (incl. freezing)
  if (code >= 61 && code <= 65)    { return CLIMACON['rain']; }        // rain
  if (code === 66 || code === 67)  { return CLIMACON['sleet']; }       // freezing rain
  if (code >= 71 && code <= 77)    { return CLIMACON['snow']; }        // snow
  if (code >= 80 && code <= 82)    { return CLIMACON['showers']; }     // rain showers
  if (code === 85 || code === 86)  { return CLIMACON['flurries']; }    // snow showers
  if (code >= 95)                  { return CLIMACON['lightning']; }   // thunderstorm
  return CLIMACON['cloud'];
}

// Reverse-geocode coordinates to a city name (free, no key, https). Best-effort:
// calls back with "" on any failure so weather still updates.
function reverseGeocode(latitude, longitude, cb) {
  var req = new XMLHttpRequest();
  req.open('GET', "https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=" +
    latitude + "&longitude=" + longitude + "&localityLanguage=en", true);
  req.timeout = 10000;
  req.onload = function() {
    var city = "";
    try { var r = JSON.parse(req.responseText); city = r.city || r.locality || ""; } catch (e) {}
    cb(city);
  };
  req.onerror = function() { cb(""); };
  req.ontimeout = function() { cb(""); };
  req.send(null);
}

function fetchWeather(latitude, longitude) {
  // Open-Meteo: https, no API key, returns is_day for reliable day/night.
  var unit = (weatherFormat === 1) ? "&temperature_unit=fahrenheit" : "";
  var req = new XMLHttpRequest();
  req.open('GET', "https://api.open-meteo.com/v1/forecast?latitude=" + latitude +
    "&longitude=" + longitude + "&current=temperature_2m,weather_code,is_day" + unit, true);
  req.timeout = 15000;
  req.onload = function() {
    if (req.readyState !== 4) { return; }
    if (req.status !== 200) { console.log("Weather HTTP " + req.status); return; }
    try {
      var r = JSON.parse(req.responseText);
      var temp = Math.round(r.current.temperature_2m);
      var icon = wmoToClimacon(r.current.weather_code, r.current.is_day === 1);
      console.log("Open-Meteo: " + temp + "; code " + r.current.weather_code + "; day " + r.current.is_day);
      reverseGeocode(latitude, longitude, function(city) { sendWeather(temp, icon, city, latitude, longitude); });
    } catch (e) { console.log("Weather parse error: " + e); }
  };
  req.onerror = function() { console.log("Weather request failed"); };
  req.ontimeout = function() { console.log("Weather request timed out"); };
  req.send(null);
}

function weatherLocationSuccess(pos) {
  lastCoordinates = pos.coords;
  fetchWeather(lastCoordinates.latitude, lastCoordinates.longitude);
}

function locationError(err) {
  console.warn('Weather: location error (' + err.code + '): ' + err.message);
  sendWeather(998, CLIMACON['compass']);
}

function isItNight() {
  var now = new Date();
  var sunInfo = SunCalc.getTimes(now, lastCoordinates.latitude, lastCoordinates.longitude);
  var night = sunInfo.sunset < now || now < sunInfo.sunrise;
  return night;
}

function getMoonIcon() {
    var now = new Date();
    var moon = "N";
    var moonInfo = SunCalc.getMoonIllumination(now);
    //console.log("moon: " + moonInfo.fraction + "  " + moonInfo.angle);
    if (moonInfo.fraction <= 0.05) { moon = "O"; }
    else if (moonInfo.fraction >= 0.95) { moon = "S"; }
    else if (moonInfo.angle < 0) { // waxing
      if (moonInfo.fraction <= 0.35) { moon = "P"; }
      else if (moonInfo.fraction <= 0.65) { moon = "Q"; }
      else { moon = "R"; }
    } else { // waning
      if (moonInfo.fraction <= 0.35) { moon = "T"; }
      else if (moonInfo.fraction <= 0.65) { moon = "U"; }
      else { moon = "V"; }
    }
/*
[INFO    ] JS: Timely 2.2d: moon: 0.5988898806207668  1.623056404454204
new moon = 0.0, full moon = 1.0
angle - == waxing
angle + == waning
N = generic moon/night
O = new moon        0.0
P = waxing crescent 0.25 -
Q = waxing quarter  0.5  -
R = waxing gibbous  0.75 -
S = full moon       1.00
T = waning gibbous  0.75 +
	U = waning quarter  0.50 +
V = waning crescent 0.25 +
*/
    return moon;
}

Pebble.addEventListener("webviewclosed", function (e) {
    if (!e || !e.response) { return; } // user cancelled

    // Decode like Clay: the phone app may hand back e.response already decoded.
    // Re-running decodeURIComponent on it throws "URI malformed" as soon as the
    // payload holds a literal % (strftime_format defaults to "%Y-%m-%d"), which
    // silently dropped EVERY save. Only decode when it is still percent-encoded
    // (an encoded payload starts with %7B, a decoded one with "{").
    //
    // The decode MUST sit inside the same try as JSON.parse: a truncated or
    // malformed %XX (or a double-encoded %257B) makes decodeURIComponent throw
    // "URI malformed", and outside the try that uncaught throw killed the whole
    // handler and lost the save silently. Now both failures land in one catch.
    var raw = e.response;
    var dict;
    try {
        var decoded = raw.match(/^\{/) ? raw : decodeURIComponent(raw);
        dict = JSON.parse(decoded);
    } catch (err) {
        console.log("Config decode/parse failed (" + err + "); raw[0..80]=" + String(raw).slice(0, 80));
        return;
    }

    // The page sends the numeric settings positionally in `n` (WIRE_KEYS order),
    // plus the string settings and any changed translations by name. Expand `n`
    // back to keys to rebuild the full desired state (a legacy flat payload with
    // no `n` passes its keys through). Reject a length mismatch: a short array
    // leaves an undefined tail, a long one misaligns every following key.
    var full = wirec.decodeN(dict, WIRE_KEYS);
    if (full === null) {
        console.log("Config rejected: n[] length " + (dict.n ? dict.n.length : 0) +
            " != wire contract " + WIRE_KEYS.length + "; not applying");
        return;
    }

    // Keep only recognised keys (audit L19): an unknown or hostile key in the
    // returned payload must reach neither the watch nor the stored blob (the blob
    // is re-inlined into the config page's <script>). storableKeys also KEEPS
    // lang_mode — a LOCAL-ONLY Custom flag (audit C3) — which we then strip from
    // the watch payload below, since it is not a message key.
    var storable = wirec.storableKeys(full, WIRE_KEYS);
    var toWatch = {};
    for (var wk in storable) { if (wk !== 'lang_mode') { toWatch[wk] = storable[wk]; } }

    // H5: persist ONLY from the success callback. The translation strings are a
    // delta against this stored blob, so writing it before the watch ACKs would
    // make the next page open think the strings already applied — the recomputed
    // delta would be empty and the watch stuck with the old strings forever. On
    // a NACK we leave the blob untouched so the full delta recomputes next open.
    Pebble.sendAppMessage(toWatch,
        function (ev) {
            var stored = {};
            try { stored = JSON.parse(localStorage.getItem("timely_settings") || "{}"); } catch (err) {}
            for (var sk in storable) { stored[sk] = storable[sk]; }
            localStorage.setItem("timely_settings", JSON.stringify(stored));
            console.log("Delivered config with transactionId=" + (ev && ev.data && ev.data.transactionId));
        },
        function (ev) {
            console.log("Unable to deliver config: " + (ev && ev.error && ev.error.message));
        }
        );
});


/*
(c) 2011-2014, Vladimir Agafonkin
SunCalc is a JavaScript library for calculating sun/mooon position and light phases.
https://github.com/mourner/suncalc
*/

(function () { "use strict";

// shortcuts for easier to read formulas

var PI = Math.PI,
    sin = Math.sin,
    cos = Math.cos,
    tan = Math.tan,
    asin = Math.asin,
    atan = Math.atan2,
    acos = Math.acos,
    rad = PI / 180;

// sun calculations are based on http://aa.quae.nl/en/reken/zonpositie.html formulas


// date/time constants and conversions

var dayMs = 1000 * 60 * 60 * 24,
    J1970 = 2440588,
    J2000 = 2451545;

function toJulian(date) {
    return date.valueOf() / dayMs - 0.5 + J1970;
}
function fromJulian(j) {
    return new Date((j + 0.5 - J1970) * dayMs);
}
function toDays(date) {
    return toJulian(date) - J2000;
}


// general calculations for position

var e = rad * 23.4397; // obliquity of the Earth

function getRightAscension(l, b) {
    return atan(sin(l) * cos(e) - tan(b) * sin(e), cos(l));
}
function getDeclination(l, b) {
    return asin(sin(b) * cos(e) + cos(b) * sin(e) * sin(l));
}
function getAzimuth(H, phi, dec) {
    return atan(sin(H), cos(H) * sin(phi) - tan(dec) * cos(phi));
}
function getAltitude(H, phi, dec) {
    return asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(H));
}
function getSiderealTime(d, lw) {
    return rad * (280.16 + 360.9856235 * d) - lw;
}


// general sun calculations

function getSolarMeanAnomaly(d) {
    return rad * (357.5291 + 0.98560028 * d);
}
function getEquationOfCenter(M) {
    return rad * (1.9148 * sin(M) + 0.02 * sin(2 * M) + 0.0003 * sin(3 * M));
}
function getEclipticLongitude(M, C) {
    var P = rad * 102.9372; // perihelion of the Earth
    return M + C + P + PI;
}
function getSunCoords(d) {

    var M = getSolarMeanAnomaly(d),
        C = getEquationOfCenter(M),
        L = getEclipticLongitude(M, C);

    return {
        dec: getDeclination(L, 0),
        ra: getRightAscension(L, 0)
    };
}


SunCalc = {};


// calculates sun position for a given date and latitude/longitude

SunCalc.getPosition = function (date, lat, lng) {

    var lw = rad * -lng,
        phi = rad * lat,
        d = toDays(date),

        c = getSunCoords(d),
        H = getSiderealTime(d, lw) - c.ra;

    return {
        azimuth: getAzimuth(H, phi, c.dec),
        altitude: getAltitude(H, phi, c.dec)
    };
};


// sun times configuration (angle, morning name, evening name)

var times = [
    [-0.83, 'sunrise', 'sunset' ],
    [ -0.3, 'sunriseEnd', 'sunsetStart' ],
    [ -6, 'dawn', 'dusk' ],
    [ -12, 'nauticalDawn', 'nauticalDusk'],
    [ -18, 'nightEnd', 'night' ],
    [ 6, 'goldenHourEnd', 'goldenHour' ]
];

// adds a custom time to the times config

SunCalc.addTime = function (angle, riseName, setName) {
    times.push([angle, riseName, setName]);
};


// calculations for sun times

var J0 = 0.0009;

function getJulianCycle(d, lw) {
    return Math.round(d - J0 - lw / (2 * PI));
}
function getApproxTransit(Ht, lw, n) {
    return J0 + (Ht + lw) / (2 * PI) + n;
}
function getSolarTransitJ(ds, M, L) {
    return J2000 + ds + 0.0053 * sin(M) - 0.0069 * sin(2 * L);
}
function getHourAngle(h, phi, d) {
    return acos((sin(h) - sin(phi) * sin(d)) / (cos(phi) * cos(d)));
}


// calculates sun times for a given date and latitude/longitude

SunCalc.getTimes = function (date, lat, lng) {

    var lw = rad * -lng,
        phi = rad * lat,
        d = toDays(date),

        n = getJulianCycle(d, lw),
        ds = getApproxTransit(0, lw, n),

        M = getSolarMeanAnomaly(ds),
        C = getEquationOfCenter(M),
        L = getEclipticLongitude(M, C),

        dec = getDeclination(L, 0),

        Jnoon = getSolarTransitJ(ds, M, L);


    // returns set time for the given sun altitude
    function getSetJ(h) {
        var w = getHourAngle(h, phi, dec),
            a = getApproxTransit(w, lw, n);

        return getSolarTransitJ(a, M, L);
    }


    var result = {
        solarNoon: fromJulian(Jnoon),
        nadir: fromJulian(Jnoon - 0.5)
    };

    var i, len, time, angle, morningName, eveningName, Jset, Jrise;

    for (i = 0, len = times.length; i < len; i += 1) {
        time = times[i];

        Jset = getSetJ(time[0] * rad);
        Jrise = Jnoon - (Jset - Jnoon);

        result[time[1]] = fromJulian(Jrise);
        result[time[2]] = fromJulian(Jset);
    }

    return result;
};


// moon calculations, based on http://aa.quae.nl/en/reken/hemelpositie.html formulas

function getMoonCoords(d) { // geocentric ecliptic coordinates of the moon

    var L = rad * (218.316 + 13.176396 * d), // ecliptic longitude
        M = rad * (134.963 + 13.064993 * d), // mean anomaly
        F = rad * (93.272 + 13.229350 * d), // mean distance

        l = L + rad * 6.289 * sin(M), // longitude
        b = rad * 5.128 * sin(F), // latitude
        dt = 385001 - 20905 * cos(M); // distance to the moon in km

    return {
        ra: getRightAscension(l, b),
        dec: getDeclination(l, b),
        dist: dt
    };
}

SunCalc.getMoonPosition = function (date, lat, lng) {

    var lw = rad * -lng,
        phi = rad * lat,
        d = toDays(date),

        c = getMoonCoords(d),
        H = getSiderealTime(d, lw) - c.ra,
        h = getAltitude(H, phi, c.dec);

    // altitude correction for refraction
    h = h + rad * 0.017 / tan(h + rad * 10.26 / (h + rad * 5.10));

    return {
        azimuth: getAzimuth(H, phi, c.dec),
        altitude: h,
        distance: c.dist
    };
};


// calculations for illumination parameters of the moon,
// based on http://idlastro.gsfc.nasa.gov/ftp/pro/astro/mphase.pro formulas and
// Chapter 48 of "Astronomical Algorithms" 2nd edition by Jean Meeus
// (Willmann-Bell, Richmond) 1998.

SunCalc.getMoonIllumination = function (date) {

    var d = toDays(date),
        s = getSunCoords(d),
        m = getMoonCoords(d),

        sdist = 149598000, // distance from Earth to Sun in km

        phi = acos(sin(s.dec) * sin(m.dec) + cos(s.dec) * cos(m.dec) * cos(s.ra - m.ra)),
        inc = atan(sdist * sin(phi), m.dist - sdist * cos(phi));

    return {
        fraction: (1 + cos(inc)) / 2,
        angle: atan(cos(s.dec) * sin(s.ra - m.ra), sin(s.dec) * cos(m.dec)
            - cos(s.dec) * sin(m.dec) * cos(s.ra - m.ra))
    };
};


// export as AMD module / Node module / browser variable

if (typeof define === 'function' && define.amd) {
    define(SunCalc);
} else if (typeof module !== 'undefined') {
    module.exports = SunCalc;
} else {
    window.SunCalc = SunCalc;
}

}());


/* Now, 155 lines of base64 decoding, to work around two issues:
 *  1)  Something in the e.response process is mangling 2-byte UTF8 characters into two 1-byte characters
 *  2)  Ejecta JSCore doesn't have an atob() function, so we must provide our own...
 *
 * Should Ejecta JSCore gain the atob() function, or the bug be fixed, I anticipate removing this.
 *
 *  ... leaving the encode functions in case I ever need to send, because, why not.
 */

/*
 * Copyright (c) 2010 Nick Galbreath
 * http://code.google.com/p/stringencoders/source/browse/#svn/trunk/javascript
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
*/

/* base64 encode/decode compatible with window.btoa/atob
 *
 * window.atob/btoa is a Firefox extension to convert binary data (the "b")
 * to base64 (ascii, the "a").
 *
 * It is also found in Safari and Chrome.  It is not available in IE.
 *
 * if (!window.btoa) window.btoa = base64.encode
 * if (!window.atob) window.atob = base64.decode
 *
 * The original spec's for atob/btoa are a bit lacking
 * https://developer.mozilla.org/en/DOM/window.atob
 * https://developer.mozilla.org/en/DOM/window.btoa
 *
 * window.btoa and base64.encode takes a string where charCodeAt is [0,255]
 * If any character is not [0,255], then an exception is thrown.
 *
 * window.atob and base64.decode take a base64-encoded string
 * If the input length is not a multiple of 4, or contains invalid characters
 *   then an exception is thrown.
 */
base64 = {};
base64.PADCHAR = '=';
base64.ALPHA = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
base64.getbyte64 = function(s,i) {
    // This is oddly fast, except on Chrome/V8.
    //  Minimal or no improvement in performance by using a
    //   object with properties mapping chars to value (eg. 'A': 0)
    var idx = base64.ALPHA.indexOf(s.charAt(i));
    if (idx == -1) {
        throw "Cannot decode base64";
    }
    return idx;
}

base64.decode = function(s) {
    // convert to string
    s = "" + s;
    var getbyte64 = base64.getbyte64;
    var pads, i, b10;
    var imax = s.length
    if (imax == 0) {
        return s;
    }

    if (imax % 4 != 0) {
        throw "Cannot decode base64";
    }

    pads = 0
    if (s.charAt(imax -1) == base64.PADCHAR) {
        pads = 1;
        if (s.charAt(imax -2) == base64.PADCHAR) {
            pads = 2;
        }
        // either way, we want to ignore this last block
        imax -= 4;
    }

    var x = [];
    for (i = 0; i < imax; i += 4) {
        b10 = (getbyte64(s,i) << 18) | (getbyte64(s,i+1) << 12) |
            (getbyte64(s,i+2) << 6) | getbyte64(s,i+3);
        x.push(String.fromCharCode(b10 >> 16, (b10 >> 8) & 0xff, b10 & 0xff));
    }

    switch (pads) {
    case 1:
        b10 = (getbyte64(s,i) << 18) | (getbyte64(s,i+1) << 12) | (getbyte64(s,i+2) << 6)
        x.push(String.fromCharCode(b10 >> 16, (b10 >> 8) & 0xff));
        break;
    case 2:
        b10 = (getbyte64(s,i) << 18) | (getbyte64(s,i+1) << 12);
        x.push(String.fromCharCode(b10 >> 16));
        break;
    }
    return x.join('');
}

base64.getbyte = function(s,i) {
    var x = s.charCodeAt(i);
    if (x > 255) {
        throw "INVALID_CHARACTER_ERR: DOM Exception 5";
    }
    return x;
}


base64.encode = function(s) {
    if (arguments.length != 1) {
        throw "SyntaxError: Not enough arguments";
    }
    var padchar = base64.PADCHAR;
    var alpha   = base64.ALPHA;
    var getbyte = base64.getbyte;

    var i, b10;
    var x = [];

    // convert to string
    s = "" + s;

    var imax = s.length - s.length % 3;

    if (s.length == 0) {
        return s;
    }
    for (i = 0; i < imax; i += 3) {
        b10 = (getbyte(s,i) << 16) | (getbyte(s,i+1) << 8) | getbyte(s,i+2);
        x.push(alpha.charAt(b10 >> 18));
        x.push(alpha.charAt((b10 >> 12) & 0x3F));
        x.push(alpha.charAt((b10 >> 6) & 0x3f));
        x.push(alpha.charAt(b10 & 0x3f));
    }
    switch (s.length - imax) {
    case 1:
        b10 = getbyte(s,i) << 16;
        x.push(alpha.charAt(b10 >> 18) + alpha.charAt((b10 >> 12) & 0x3F) +
               padchar + padchar);
        break;
    case 2:
        b10 = (getbyte(s,i) << 16) | (getbyte(s,i+1) << 8);
        x.push(alpha.charAt(b10 >> 18) + alpha.charAt((b10 >> 12) & 0x3F) +
               alpha.charAt((b10 >> 6) & 0x3f) + padchar);
        break;
    }
    return x.join('');
}
