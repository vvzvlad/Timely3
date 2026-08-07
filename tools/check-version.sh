#!/usr/bin/env sh
# Version single-source: package.json "version" is authoritative. Fail if the
# APP_VERSION string baked into src/splash.c (shown on the loading splash) drifts
# from it. A check is deliberately preferred over generating the header at build
# time: no codegen step to break a build, and the drift is caught in CI.
#
# NOTE: package-lock.json currently says "3.0.0", a stale npm-only mismatch that
# never reaches the watch. This check governs the on-watch splash string; the
# lockfile is out of scope (fix it by regenerating the lock, not here).
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

pkg=$(node -p "require('./package.json').version")
app=$(grep -oE '#define APP_VERSION +"[^"]+"' src/splash.c | grep -oE '"[^"]+"' | tr -d '"')

if [ -z "$app" ]; then
  echo "check-version: could not read APP_VERSION from src/splash.c" >&2
  exit 2
fi

if [ "$pkg" != "$app" ]; then
  echo "check-version: MISMATCH: package.json version=$pkg but src/splash.c APP_VERSION=$app" >&2
  echo "  update the #define APP_VERSION in src/splash.c to \"$pkg\" (package.json is the source)." >&2
  exit 1
fi
echo "check-version: OK (version $pkg matches package.json and src/splash.c)"
