#include "timefmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int daysInMonth(int mon, int year) {
  mon++; // callers pass 0-based months (0 = January)
  if (mon == 4 || mon == 6 || mon == 9 || mon == 11) {
    return 30;
  } else if (mon == 2) {
    if (year % 400 == 0) return 29;
    if (year % 100 == 0) return 28;
    if (year % 4 == 0) return 29;
    return 28;
  }
  return 31;
}

int days_left_in_year(int year, int yday) {
  int days_this_year = (daysInMonth(1, year) == 29) ? 366 : 365;
  return days_this_year - yday - 1;
}

void format_days_left_in_year(int days_left, char *buf, size_t n) {
  if (days_left < 0)   days_left = 0;
  if (days_left > 365) days_left = 365;
  snprintf(buf, n, "R%03d", days_left);
}

void format_timezone_offset(int tz_offset, char *buf, size_t n) {
  if (tz_offset == TIMEZONE_UNINITIALIZED) {
    snprintf(buf, n, "UTC ?");
    return;
  }
  // Clamp to the real timezone domain (UTC-14..+14 in quarter-hours): guards
  // against malformed config and bounds the formatted width.
  if (tz_offset < -56) tz_offset = -56;
  if (tz_offset > 56)  tz_offset = 56;
  int tz_mins  = (tz_offset % 4) * 15;
  int tz_hours = (tz_offset - (tz_offset % 4)) / 4;
  // Sign convention is intentionally inverted to match the watchface's
  // historical display: positive offset prints "UTC-", non-positive "UTC+".
  if (tz_offset > 0) {
    if (tz_mins == 0) snprintf(buf, n, "UTC-%d", tz_hours);
    else              snprintf(buf, n, "UTC-%d:%d", tz_hours, tz_mins);
  } else {
    if (tz_mins == 0) snprintf(buf, n, "UTC+%d", abs(tz_hours));
    else              snprintf(buf, n, "UTC+%d:%d", abs(tz_hours), abs(tz_mins));
  }
}

const char *datefmt_table_entry(uint8_t date_format) {
  // 60 non-localized strftime formats for date_format codes 195..254
  // (index = code - 195). Copied verbatim, in order, from the old inline table
  // in Timely.c format_current_date().
  static const char *const datestr[] = {
    // MM DD YYYY (%m %d %Y)
    "%m.%d.%Y", // 195 MM.DD.YYYY
    "%m-%d-%Y", // 196 MM-DD-YYYY
    "%m/%d/%Y", // 197 MM/DD/YYYY
    "%m %d %Y", // 198 MM DD YYYY
    "%m%d%Y",   // 199 MMDDYYYY
    // MM DD YY (%m %d %y)
    "%m.%d.%y", // 200 MM.DD.YY
    "%m-%d-%y", // 201 MM-DD-YY
    "%m/%d/%y", // 202 MM/DD/YY
    "%m %d %y", // 203 MM DD YY
    "%m%d%y",   // 204 MMDDYY
    // MM dd YYYY (%m %e %Y)
    "%m.%e.%Y", // 205 MM.dd.YYYY
    "%m-%e-%Y", // 206 MM-dd-YYYY
    "%m/%e/%Y", // 207 MM/dd/YYYY
    "%m %e %Y", // 208 MM dd YYYY
    "%m%e%Y",   // 209 MMddYYYY
    // MM dd YY (%m %e %y)
    "%m.%e.%y", // 210 MM.dd.YY
    "%m-%e-%y", // 211 MM-dd-YY
    "%m/%e/%y", // 212 MM/dd/YY
    "%m %e %y", // 213 MM dd YY
    "%m%e%y",   // 214 MMddYY
    // DD MM YYYY (%d %m %Y)
    "%d.%m.%Y", // 215 DD.MM.YYYY
    "%d-%m-%Y", // 216 DD-MM-YYYY
    "%d/%m/%Y", // 217 DD/MM/YYYY
    "%d %m %Y", // 218 DD MM YYYY
    "%d%m%Y",   // 219 DDMMYYYY
    // DD MM YY (%d %m %y)
    "%d.%m.%y", // 220 DD.MM.YY
    "%d-%m-%y", // 221 DD-MM-YY
    "%d/%m/%y", // 222 DD/MM/YY
    "%d %m %y", // 223 DD MM YY
    "%d%m%y",   // 224 DDMMYY
    // dd MM YYYY (%e %m %Y)
    "%e.%m.%Y", // 225 dd.MM.YYYY
    "%e-%m-%Y", // 226 dd-MM-YYYY
    "%e/%m/%Y", // 227 dd/MM/YYYY
    "%e %m %Y", // 228 dd MM YYYY
    "%e%m%Y",   // 229 ddMMYYYY
    // dd MM YY (%e %m %y)
    "%e.%m.%y", // 230 dd.MM.YY
    "%e-%m-%y", // 231 dd-MM-YY
    "%e/%m/%y", // 232 dd/MM/YY
    "%e %m %y", // 233 dd MM YY
    "%e%m%y",   // 234 ddMMYY
    // YYYY MM DD (%Y %m %d)
    "%Y.%m.%d", // 235 YYYY.MM.DD
    "%Y-%m-%d", // 236 YYYY-MM-DD
    "%Y/%m/%d", // 237 YYYY/MM/DD
    "%Y %m %d", // 238 YYYY MM DD
    "%Y%m%d",   // 239 YYYYMMDD
    // YY MM DD (%y %m %d)
    "%y.%m.%d", // 240 YY.MM.DD
    "%y-%m-%d", // 241 YY-MM-DD
    "%y/%m/%d", // 242 YY/MM/DD
    "%y %m %d", // 243 YY MM DD
    "%y%m%d",   // 244 YYMMDD
    // YYYY MM dd (%Y %m %e)
    "%Y.%m.%e", // 245 YYYY.MM.dd
    "%Y-%m-%e", // 246 YYYY-MM-dd
    "%Y/%m/%e", // 247 YYYY/MM/dd
    "%Y %m %e", // 248 YYYY MM dd
    "%Y%m%e",   // 249 YYYYMMdd
    // YY MM dd (%y %m %e)
    "%y.%m.%e", // 250 YY.MM.dd
    "%y-%m-%e", // 251 YY-MM-dd
    "%y/%m/%e", // 252 YY/MM/dd
    "%y %m %e", // 253 YY MM dd
    "%y%m%e",   // 254 YYMMdd
  };
  if (date_format < 195 || date_format > 254) return NULL;
  return datestr[date_format - 195];
}

void datefmt_render(uint8_t date_format, const char *custom_fmt,
                    const struct tm *t, char *out, size_t n) {
  if (n == 0) return;
  out[0] = '\0';
  const char *fmt;
  if (date_format == 255) {
    fmt = custom_fmt;                       // sentinel FIRST: user custom format
  } else {
    fmt = datefmt_table_entry(date_format); // 195..254 -> table, else NULL
  }
  if (fmt == NULL) return;                  // localized (<195) or absent custom -> ""
  // strftime returns 0 when the result (incl. terminator) does not fit; the
  // buffer contents are then unspecified, so keep the "" written above.
  if (strftime(out, n, fmt, t) == 0) {
    out[0] = '\0';
  }
}

uint8_t datefmt_migrate_v11_to_v12(uint8_t old) {
  // Codes 235..253 shift up by one; 254 saturates (stays 254) so we never emit
  // the custom sentinel 255. Everything else passes through.
  return (old > 234 && old < 254) ? (uint8_t)(old + 1) : old;
}

uint8_t datefmt_clamp(uint8_t v) {
  // Localized (<195), table (195..254) and the custom sentinel (255) cover the
  // full uint8_t range, so every value is currently valid; the fallback to 0 is
  // a defensive choke point for any future-unhandled value.
  if (v < 195 || (v >= 195 && v <= 254) || v == 255) return v;
  return 0;
}
