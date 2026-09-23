#!/bin/bash
#
# Scripts/validate.sh [--install | --vst3-only] <build-dir>: host validation (03 §4.8 step 7; K2 #19).
#
#   (default)    the lead's sprint-end and pre-release gate, against the INSTALLED bundles:
#                  0. refuses (exit 2) unless the installed ~/Library/Audio/Plug-Ins/VST3/FCompressor.vst3 and
#                     Components/FCompressor.component carry exactly <build>'s binaries (Contents/MacOS/FCompressor
#                     byte-identical to <build>/FCompressor_artefacts/<config>/{VST3,AU}/...; FZ0 errata, R-B0 #11), so
#                     last sprint's install can never be validated in the name of this build
#                  1. killall -9 AudioComponentRegistrar; auval -strict -v aufx Fcmp Funk
#                  2. pluginval --strictness-level 10 --repeat 2 --randomise --timeout-ms 900000 on
#                     ~/Library/Audio/Plug-Ins/VST3/FCompressor.vst3 and ~/Library/Audio/Plug-Ins/Components/FCompressor.component
#                  3. appends "validate: pass" (or FAIL) to <build>/verify-passed-<HEAD sha> when that stamp exists AND
#                     <build>/built-from-plugin.txt says the bundles were built from that HEAD with a clean tree
#                     (cmake/FcmpBuiltFrom.cmake; R-B0 #12); release.sh requires the line
#   --install    (lead only) first replaces the installed VST3 and AU bundles with <build>'s (rm -rf, then ditto), then
#                runs the default gate. `Scripts/validate.sh --install build-lead` is 03 §4.8 step 7; the owner
#                install (step 9) replaces them again afterwards.
#   --vst3-only  for agents: pluginval with the same options on the UNINSTALLED <build>/FCompressor_artefacts/<config>/
#                VST3/FCompressor.vst3 (no auval: the AU is only reachable installed); the stamp line is
#                "validate --vst3-only: pass", which release.sh does not accept.
#
# pluginval comes from the machine cache ($FCOMPRESSOR_DEPS_DIR or ~/audio/.deps; tools/pluginval-<tag>/pluginval, the
# tag from DEPS.lock, built by Scripts/deps.sh). Everything is logged to <build>/validate.log.
# Exit: 0 all passed, 1 a validation failed, 2 usage or setup error (including installed bundles that are not <build>'s).
set -u

usage() {
  sed -n '3,25p' "$0" | sed 's/^# \{0,1\}//'
}

VST3_ONLY=0
INSTALL=0
BUILD=""
for arg in "$@"; do
  case "$arg" in
    --vst3-only) VST3_ONLY=1 ;;
    --install) INSTALL=1 ;;
    -h|--help) usage; exit 0 ;;
    -*) echo "validate.sh: unknown option '$arg'" >&2; usage >&2; exit 2 ;;
    *)
      if [ -n "$BUILD" ]; then
        echo "validate.sh: one build directory only" >&2
        exit 2
      fi
      BUILD="$arg" ;;
  esac
done
if [ -z "$BUILD" ] || [ ! -d "$BUILD" ]; then
  usage >&2
  exit 2
fi
if [ "$VST3_ONLY" = 1 ] && [ "$INSTALL" = 1 ]; then
  echo "validate.sh: --install and --vst3-only exclude each other (agents never install)" >&2
  exit 2
fi
BUILD="$(cd "$BUILD" && pwd -P)"
CACHE="$BUILD/CMakeCache.txt"
if [ ! -f "$CACHE" ]; then
  echo "validate.sh: $BUILD is not a configured build directory" >&2
  exit 2
fi
SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$CACHE")"
CFG="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$CACHE")"
DEPS_DIR="$(sed -n 's/^FCOMPRESSOR_DEPS_DIR:PATH=//p' "$CACHE")"
DEPS_DIR="${FCOMPRESSOR_DEPS_DIR:-${DEPS_DIR:-$HOME/audio/.deps}}"
PRODUCT_H="$BUILD/generated/FcmpProduct.h"
if [ ! -f "$PRODUCT_H" ]; then
  echo "validate.sh: $PRODUCT_H is missing (a DSP-only build has no plugin)" >&2
  exit 2
fi
product_const() {  # product_const kName -> FCompressor
  sed -n "s/.*char $1\[\] *= *\"\([^\"]*\)\".*/\1/p" "$PRODUCT_H" | head -1
}
NAME="$(product_const kName)"
MANU="$(product_const kManufacturerCode)"
CODE="$(product_const kPluginCode)"
if [ -z "$NAME" ] || [ -z "$MANU" ] || [ -z "$CODE" ] || [ -z "$CFG" ]; then
  echo "validate.sh: cannot read the product name/codes from $PRODUCT_H or CMAKE_BUILD_TYPE from $CACHE" >&2
  exit 2
fi
ART="$BUILD/${NAME}_artefacts/$CFG"
BUILT_VST3="$ART/VST3/$NAME.vst3"
BUILT_AU="$ART/AU/$NAME.component"

PV_TAG="$(awk -F'\t' '$1 == "pluginval" { print $2; exit }' "$DEPS_DIR/DEPS.lock" 2>/dev/null)"
PV_TAG="${PV_TAG:-v1.0.4}"
PV="$DEPS_DIR/tools/pluginval-$PV_TAG/pluginval"
if [ ! -x "$PV" ]; then
  echo "validate.sh: no pluginval at $PV (run Scripts/deps.sh)" >&2
  exit 2
fi
PV_ARGS=(--strictness-level 10 --repeat 2 --randomise --timeout-ms 900000)

LOG="$BUILD/validate.log"
: > "$LOG"
failures=0
step() {  # step <label> <command...>
  local label=$1
  shift
  echo "== $label" | tee -a "$LOG"
  echo "   $*" >> "$LOG"
  local t0=$SECONDS
  if "$@" >> "$LOG" 2>&1; then
    echo "   pass ($((SECONDS - t0)) s)" | tee -a "$LOG"
  else
    local rc=$?
    failures=$((failures + 1))
    echo "   FAIL (exit $rc, $((SECONDS - t0)) s); last lines of $LOG:" | tee -a "$LOG"
    tail -25 "$LOG" | sed 's/^/   | /'
  fi
}

echo "validate.sh: $NAME ($MANU/$CODE), $CFG bundles of $BUILD, pluginval $PV_TAG, log $LOG"
if [ "$VST3_ONLY" = 1 ]; then
  if [ ! -d "$BUILT_VST3" ]; then
    echo "validate.sh: no $BUILT_VST3 (build the plugin first)" >&2
    exit 2
  fi
  step "pluginval (strictness 10) $BUILT_VST3" "$PV" "${PV_ARGS[@]}" --validate "$BUILT_VST3"
  STAMP_LINE="validate --vst3-only"
else
  VST3="$HOME/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
  AU="$HOME/Library/Audio/Plug-Ins/Components/$NAME.component"
  for b in "$BUILT_VST3" "$BUILT_AU"; do
    if [ ! -f "$b/Contents/MacOS/$NAME" ]; then
      echo "validate.sh: no $b/Contents/MacOS/$NAME (build the plugin in $BUILD first)" >&2
      exit 2
    fi
  done
  if [ "$INSTALL" = 1 ]; then
    for pair in "$BUILT_VST3|$VST3" "$BUILT_AU|$AU"; do
      from="${pair%%|*}"
      to="${pair#*|}"
      echo "== install $from -> $to"
      mkdir -p "$(dirname "$to")" && rm -rf "$to" && ditto "$from" "$to" || {
        echo "validate.sh: installing $from as $to failed" >&2
        exit 2
      }
    done
  fi
  for pair in "$BUILT_VST3|$VST3" "$BUILT_AU|$AU"; do
    from="${pair%%|*}"
    to="${pair#*|}"
    if [ ! -d "$to" ]; then
      echo "validate.sh: $to is not installed (Scripts/validate.sh --install $BUILD installs this build's bundles)" >&2
      exit 2
    fi
    if ! cmp -s "$from/Contents/MacOS/$NAME" "$to/Contents/MacOS/$NAME"; then
      echo "validate.sh: the installed $to is not $BUILD's build (Contents/MacOS/$NAME differs from $from);" \
           "refusing to validate it in this build's name. Scripts/validate.sh --install $BUILD installs this build's" \
           "bundles first." >&2
      exit 2
    fi
  done
  killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
  step "auval -strict -v aufx $CODE $MANU" auval -strict -v aufx "$CODE" "$MANU"
  step "pluginval (strictness 10) $VST3" "$PV" "${PV_ARGS[@]}" --validate "$VST3"
  step "pluginval (strictness 10) $AU" "$PV" "${PV_ARGS[@]}" --validate "$AU"
  STAMP_LINE="validate"
fi

if [ "$failures" -eq 0 ]; then
  RESULT="pass"
else
  RESULT="FAIL ($failures step(s))"
fi
# The stamp names HEAD; the bundles must be HEAD's too (built-from-plugin.txt: "plugin <sha> <clean|dirty> <time>").
if [ -n "$SRC" ]; then
  SHA="$(git --no-optional-locks -C "$SRC" rev-parse -q --verify 'HEAD^{commit}' 2>/dev/null || true)"
  if [ -n "$SHA" ] && [ -f "$BUILD/verify-passed-$SHA" ]; then
    BUILT="$(head -1 "$BUILD/built-from-plugin.txt" 2>/dev/null || true)"
    read -r _ BUILT_SHA BUILT_STATE _ <<< "$BUILT"
    TREE=clean
    if [ -n "$(git --no-optional-locks -C "$SRC" status --porcelain 2>/dev/null | head -1)" ]; then
      TREE=dirty
    fi
    if [ "$BUILT_SHA" = "$SHA" ] && [ "$BUILT_STATE" = "clean" ] && [ "$TREE" = "clean" ]; then
      echo "$STAMP_LINE: $RESULT at $(date -u +%Y-%m-%dT%H:%M:%SZ) (pluginval $PV_TAG; $BUILT)" \
        >> "$BUILD/verify-passed-$SHA"
      echo "validate.sh: recorded in $BUILD/verify-passed-$SHA"
    else
      echo "validate.sh: NOT recorded in $BUILD/verify-passed-$SHA: the bundles are not HEAD's clean build" \
           "(built-from-plugin.txt: '${BUILT:-missing}'; the tree is $TREE now); rebuild, then rerun"
    fi
  fi
fi
echo "validate.sh: $RESULT"
[ "$failures" -eq 0 ]
