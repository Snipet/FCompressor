#!/bin/bash
#
# Scripts/check-headers.sh [<build-dir>]: every frozen header compiles standalone (CTest lint.headers; 03 §4.7 step 2).
#
#   Source/fcdsp/**/*.h                                   always (zero headers is fine: Sprint 0 starts empty)
#   Source/plugin/ProcessorFacade.h                       when present
#   Source/editor/{SubView,Panel,Layout,Tags,HistoryStore,PreviewWorker,SlotModel}.h, Source/editor/views/*.h,
#   Tools/probes/plugin/FakeFacade.h                      when present (FZ4)
#
# Each header is compiled alone with
#   $CXX -std=c++20 -fsyntax-only <WARN> -Wmissing-variable-declarations -Werror -ffp-contract=off -I Source -x c++-header
# where <WARN> is the one warning list of every translation unit of ours (cmake/FcmpArch.cmake FCMP_WARNING_FLAGS:
# JUCE 8.0.4's clang list + -Wextra, without -Wfloat-equal), so a header that passes here also compiles inside the
# plugin, which builds with that list (FZ0 errata, R-B0 #6). -Wmissing-prototypes (in <WARN>) and
# -Wmissing-variable-declarations make a non-inline function or variable definition in a header an error: every helper
# is inline (03 §4.7; R-B0 #8). Source/fcdsp headers also get -Wglobal-constructors -Wexit-time-destructors (no static
# constructors in fcdsp, C D12; R-B0 #3). CTest's lint.headers passes FcmpArch.cmake's list in FCMP_HEADER_CHECK_FLAGS,
# and the script fails (exit 2) if its own copy below differs.
# (-x c++-header, not -x c++: a #pragma once header compiled as a plain TU fails -Wpragma-once-outside-header; S0 lead
# revision 1). That also makes every size static_assert and every constexpr helper live. The plugin and editor headers
# also get <build>/generated (FcmpProduct.h), and FunkGui's include/ and JUCE's modules/ as system directories (their
# headers are checked by their own projects), located through <build>/fcmp-deps.txt; in a DSP-only build (no JUCE)
# they are skipped with a note, and the JUCE builds check them. $CXX defaults to clang++ (CTest passes CMake's
# compiler); SDKROOT defaults to xcrun's.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
BUILD="${1:-}"
CXX="${CXX:-clang++}"
if [ -z "${SDKROOT:-}" ]; then
  SDKROOT="$(xcrun --show-sdk-path 2>/dev/null || true)"
fi

# Keep identical to cmake/FcmpArch.cmake's FCMP_WARNING_FLAGS (its C++ part, FCMP_HEADER_CHECK_FLAGS), same order.
WARN=(-Wextra
      -Wall -Wshadow-all -Wshorten-64-to-32 -Wstrict-aliasing -Wuninitialized -Wunused-parameter -Wconversion
      -Wsign-compare -Wint-conversion -Wconditional-uninitialized -Wconstant-conversion -Wsign-conversion
      -Wbool-conversion -Wextra-semi -Wunreachable-code -Wcast-align -Wshift-sign-overflow -Wmissing-prototypes
      -Wnullable-to-nonnull-conversion -Wno-ignored-qualifiers -Wswitch-enum -Wpedantic -Wdeprecated
      -Wmissing-field-initializers
      -Wzero-as-null-pointer-constant -Wunused-private-field -Woverloaded-virtual -Wreorder
      -Winconsistent-missing-destructor-override
      -Wno-float-equal)
if [ -n "${FCMP_HEADER_CHECK_FLAGS:-}" ] && [ "$FCMP_HEADER_CHECK_FLAGS" != "${WARN[*]}" ]; then
  echo "check-headers: the warning list differs from cmake/FcmpArch.cmake's FCMP_WARNING_FLAGS; update both together" >&2
  echo "  cmake: $FCMP_HEADER_CHECK_FLAGS" >&2
  echo "  here:  ${WARN[*]}" >&2
  exit 2
fi
FCDSP_ONLY=(-Wglobal-constructors -Wexit-time-destructors)

COMMON=(-std=c++20 -fsyntax-only "${WARN[@]}" -Wmissing-variable-declarations -Werror -ffp-contract=off
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
         Source/editor/Tags.h Source/editor/HistoryStore.h Source/editor/PreviewWorker.h Source/editor/SlotModel.h \
         Tools/probes/plugin/FakeFacade.h; do
  if [ -f "$ROOT/$h" ]; then
    OTHER+=("$h")
  fi
done
# FZ4 (S5 lead revision): every editor view header is frozen too.
while IFS= read -r h; do
  OTHER+=("$h")
done < <(cd "$ROOT" && find Source/editor/views -type f -name '*.h' 2>/dev/null | LC_ALL=C sort)

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
    EXTRA=(-I "$BUILD/generated" -isystem "$FUNKGUI/include" -isystem "$JUCE/modules" -DNDEBUG=1
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
  check "$h" "${FCDSP_ONLY[@]}"
done
for h in ${OTHER[@]+"${OTHER[@]}"}; do
  check "$h" "${EXTRA[@]}"
done

echo "check-headers: ${checked} header(s) checked (${#FCDSP[@]} under Source/fcdsp), ${failed} failed"
[ "$failed" -eq 0 ]
