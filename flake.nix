{
  description = "TimelyNG watchface for Pebble - SDK dev shell + host unit tests";

  inputs = {
    pebble.url = "github:pebble-dev/pebble.nix";
    flake-utils.url = "github:numtide/flake-utils";
    # Reuse pebble.nix's pinned nixpkgs (no extra fetch) for host tooling/tests.
    nixpkgs.follows = "pebble/nixpkgs";
  };

  # Usage (official Pebble SDK flow, https://developer.repebble.com/sdk/):
  #   nix develop
  #   pebble sdk install latest          # first time only; caches in ~/.pebble-sdk
  #   pebble build                       # produces build/PebbleTimely.pbw
  #   pebble install --emulator emery    # color emulator (or basalt/flint/...)
  #   pebble login && pebble install --cloudpebble   # push to a paired watch
  # Host unit tests (pure modules in src/, no Pebble SDK):
  #   nix flake check                          # reproducible, CI
  #   nix develop .#test --command just test   # or: make test
  # Tests live in a SEPARATE shell on purpose: a host C compiler in the pebble
  # shell hijacks the ARM cross-build ("gcc: unrecognized option -mthumb").
  outputs =
    { nixpkgs, pebble, flake-utils, ... }:
    flake-utils.lib.eachSystem [
      "x86_64-linux"
      "x86_64-darwin"
      "aarch64-darwin"
    ]
      (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          # Pebble SDK shell: pebble CLI + ARM toolchain + QEMU emulator, plus
          # just/make task runners. No host cc here (it hijacks the ARM build).
          devShells.default = pebble.pebbleEnv.${system} {
            emulatorTarget = "emery";
            packages = [
              pkgs.just
              pkgs.gnumake
              pkgs.gum # interactive prompts for the guided `just` recipes
            ];
            # No CFLAGS override here: exporting -Wno-error silently defeated the
            # SDK's -Werror in the dev shell, so warnings that fail the cloud build
            # (and tools/strict-check.sh) passed locally. Warnings are surfaced by
            # strict-check.sh under the exact cloud flag set instead.
          };

          # Host shell for the unit tests: C compiler + just/make.
          devShells.test = pkgs.mkShell {
            packages = [
              pkgs.gcc
              pkgs.gnumake
              pkgs.just
            ];
          };

          # Reproducible host run of the pure-module unit suite (no Pebble SDK).
          # Copy the src/ and tests/ trees whole and glob tests/test_*.c (the same
          # single source as Makefile / justfile / tools/test.sh) so a newly added
          # tests/test_*.c is picked up here too; only the host-compilable pure
          # modules from src/ are linked. Same flag set as everywhere else.
          checks.default = pkgs.runCommandCC "timelycolor-tests" { } ''
            cp -r ${./src} src
            cp -r ${./tests} tests
            cc -std=c11 -Wall -Wextra -Isrc -Itests \
              tests/test_*.c \
              src/timefmt.c src/layout.c src/calendar.c src/vibes.c src/suntimes.c src/math.c -o test_suite
            ./test_suite
            touch $out
          '';
        }
      );
}
