#!/usr/bin/env bash
# Scripts/cloudflare-build.sh: the build command of a Cloudflare import of this repository (wrangler.jsonc): it makes
# build-web/site, the browser demo's site (ADR-93), on Cloudflare's build image. In the dashboard the build command is
#
#   bash Scripts/cloudflare-build.sh
#
# and the deploy command stays `npx wrangler deploy`. It must be run by bash, by name as above: Cloudflare runs a build
# command in plain sh, where emsdk's emsdk_env.sh cannot find its own directory, says so and returns without failing
# (the first import's build failed that way: cmake then found no em-config).
#
# What it does, each step only if the machine lacks it:
#   CMake 3.30 or later and Ninja   from pip (the build image has neither)
#   Emscripten                      the version cmake/FcmpDeps.cmake pins (its configure refuses any other), through
#                                   emsdk, cloned into $FCMP_EMSDK_DIR (default $HOME/emsdk): OUTSIDE the checkout on
#                                   purpose. An untracked directory in the tree makes built-from.txt say `dirty`, and
#                                   the page's footer then drops the link to its source commit.
#   the site                        cmake --preset web, then the target fcmp_web_site alone (no test binary)
# It prints the site's built-from.txt and its files at the end. Exit 0, or the failing step's status.
#
# Cloudflare compiles its own copy: what it serves is this commit's site, but not the artifact CI's browser gate
# tested (that one is what GitHub Pages serves).
set -eo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

say() { printf 'cloudflare-build: %s\n' "$*"; }

PIN=$(sed -n 's/^ *set(FCMP_EMSCRIPTEN_VERSION \([0-9][0-9.]*\))$/\1/p' cmake/FcmpDeps.cmake)
[ -n "$PIN" ] || { say "cmake/FcmpDeps.cmake names no FCMP_EMSCRIPTEN_VERSION"; exit 1; }

# CMake 3.30 or later, and Ninja.
cmake_ok() {
    command -v cmake > /dev/null 2>&1 || return 1
    v=$(cmake --version | sed -n '1s/^cmake version \([0-9]*\)\.\([0-9]*\).*/\1 \2/p')
    set -- $v
    [ "${1:-0}" -gt 3 ] || { [ "${1:-0}" -eq 3 ] && [ "${2:-0}" -ge 30 ]; }
}
if ! cmake_ok || ! command -v ninja > /dev/null 2>&1; then
    say "installing CMake and Ninja with pip"
    pip install 'cmake>=3.30' ninja
fi
cmake_ok || { say "no CMake 3.30 or later after pip install"; exit 1; }
command -v ninja > /dev/null 2>&1 || { say "no ninja after pip install"; exit 1; }

# Emscripten at the pin.
if ! command -v em-config > /dev/null 2>&1; then
    EMSDK_HOME=${FCMP_EMSDK_DIR:-$HOME/emsdk}
    case "$EMSDK_HOME/" in
        "$PWD"/*) say "FCMP_EMSDK_DIR ($EMSDK_HOME) is inside the checkout: the site would say it was built dirty"; exit 1 ;;
    esac
    if [ ! -x "$EMSDK_HOME/emsdk" ]; then
        say "cloning emsdk into $EMSDK_HOME"
        git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_HOME"
    fi
    "$EMSDK_HOME/emsdk" install "$PIN"
    "$EMSDK_HOME/emsdk" activate "$PIN"
    # shellcheck disable=SC1091
    . "$EMSDK_HOME/emsdk_env.sh"
fi
command -v em-config > /dev/null 2>&1 || { say "em-config is still not on PATH after emsdk_env.sh"; exit 1; }
say "Emscripten: $(emcc --version | head -n 1) (the pin is $PIN)"

cmake --preset web
cmake --build build-web --target fcmp_web_site

[ -f build-web/site/index.html ] && [ -f build-web/site/built-from.txt ] || { say "the build made no site"; exit 1; }
say "built-from.txt: $(cat build-web/site/built-from.txt)"
(cd build-web/site && find . -type f | sort | sed 's/^\.\//  /')
