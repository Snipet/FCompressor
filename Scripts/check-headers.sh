#!/bin/bash
#
# Scripts/check-headers.sh [<build-dir>]: every frozen header compiles standalone (CTest lint.headers; 03 §4.7 step 2).
#
#   Source/fcdsp/**/*.h                                   always (zero headers is fine: Sprint 0 starts empty)
#   Source/plugin/ProcessorFacade.h                       when present
#   Source/editor/{SubView,Panel,Layout,Tags}.h           when present
#
# Each header is compiled alone with
#   $CXX -std=c++20 -fsyntax-only -Wall -Wextra -Wshadow -Wpedantic -Werror -ffp-contract=off -I Source -x c++-header
# (-x c++-header, not -x c++: a #pragma once header compiled as a plain TU fails -Wpragma-once-outside-header; S0 lead
# revision 1). That also makes every size static_assert and every constexpr helper live. The plugin and editor headers
# also get <build>/generated (FcmpProduct.h), FunkGui's include/ and JUCE's modules/ (as a system directory), located
# through <build>/fcmp-deps.txt; in a DSP-only build (no JUCE) they are skipped with a note, and the JUCE builds check
# them. $CXX defaults to clang++ (CTest passes CMake's compiler); SDKROOT defaults to xcrun's.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
BUILD="${1:-}"
CXX="${CXX:-clang++}"
if [ -z "${SDKROOT:-}" ]; then
  SDKROOT="$(xcrun --show-sdk-path 2>/dev/null || true)"
fi

COMMON=(-std=c++20 -fsyntax-only -Wall -Wextra -Wshadow -Wpedantic -Werror -ffp-contract=off
        "-mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET:-14.0}")
if [ -n "$SDKROOT" ]; then
  COMMON+=(-isysroot "$SDKROOT")
fi

FCDSP=()
while IFS= read -r h; do
  FCDSP+=("$h")
done < <(cd "$ROOT" && find Source/fcdsp -type f -name '*.h' 2>/dev/null | LC_ALL=C sort)

OTHER=()
for h in Source/plugin/ProcessorFacade.h Source/editor/SubView.h Source/editor/Panel.h Source/editor/Layout.h \
         Source/editor/Tags.h; do
  if [ -f "$ROOT/$h" ]; then
    OTHER+=("$h")
  fi
done

EXTRA=()
if [ "${#OTHER[@]}" -gt 0 ]; then
  if [ -z "$BUILD" ] || [ ! -f "$BUILD/fcmp-deps.txt" ]; then
    echo "check-headers: ${OTHER[*]} need a configured build directory (for fcmp-deps.txt): $0 <build-dir>" >&2
    exit 2
  fi
  BUILD="$(cd "$BUILD" && pwd -P)"
  FUNKGUI="$(awk -F'\t' '$1 == "FunkGui" { print $4; exit }' "$BUILD/fcmp-deps.txt")"
  JUCE="$(awk -F'\t' '$1 == "JUCE" { print $4; exit }' "$BUILD/fcmp-deps.txt")"
  if [ -z "$FUNKGUI" ]; then
    echo "check-headers: $BUILD/fcmp-deps.txt has no FunkGui row" >&2
    exit 2
  fi
  if [ -z "$JUCE" ]; then
    echo "check-headers: NOTE: DSP-only build (no JUCE): skipping ${OTHER[*]}; the JUCE builds check them"
    OTHER=()
  else
    EXTRA=(-I "$BUILD/generated" -I "$FUNKGUI/include" -isystem "$JUCE/modules" -DNDEBUG=1
           -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1
           -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0)
  fi
fi

failed=0
checked=0
check() {  # check <header> <extra flags...>
  local h=$1 out
  shift
  checked=$((checked + 1))
  if out="$(cd "$ROOT" && "$CXX" "${COMMON[@]}" -I "$ROOT/Source" "$@" -x c++-header "$ROOT/$h" 2>&1)"; then
    echo "ok    $h"
  else
    failed=$((failed + 1))
    echo "FAIL  $h"
    printf '%s\n' "$out" | sed 's/^/      /'
  fi
}

for h in ${FCDSP[@]+"${FCDSP[@]}"}; do
  check "$h"
done
for h in ${OTHER[@]+"${OTHER[@]}"}; do
  check "$h" "${EXTRA[@]}"
done

echo "check-headers: ${checked} header(s) checked (${#FCDSP[@]} under Source/fcdsp), ${failed} failed"
[ "$failed" -eq 0 ]
