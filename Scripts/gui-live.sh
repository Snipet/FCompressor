#!/bin/sh
#
# Scripts/gui-live.sh <build-dir>: live parity, ui.live (03 §3.6; 02 §3.9, §5.1; C G1; U7). The real GPU editor in the
# real Standalone must draw, frame for frame, what the headless probe draws for the same state.
#
#   For each view of {panel, chars.sidechain, chars.colour, modebrowser, presetbrowser} x Mode clean (the 5 views, S12):
#   1. headless  fcmp_probe_plugin ui.dump --mode clean -- --view <id> --dpi 2 --theme 0: a FakeFacade at its defaults,
#                Panel{skipHint, syncPreview}, HeadlessHost settle at 1/60 s, the settled frame as dump v2.
#   2. live      FunkGui's tools/capture-frame.sh on <build>'s Standalone with the product's environment prefix (FCMP_):
#                FCMP_UI_VIEW=<id>, FCMP_UI_FIXED_DT=0.0166666675 (1/60 s as a float, the headless dt, written %.9g),
#                FCMP_UI_NO_HINT=1, FCMP_UI_NO_LIVE=1, FCMP_UI_THEME=0, FCMP_UI_SCALE=2, a scratch FCMP_PREFS_DIR and
#                FCMP_PRESETS_DB, FCMP_GPU_LOG=1, and FCMP_CANVAS_DUMP_AFTER = max(8, headless settle frames + 2), so the
#                editor has ticked at least as long as the probe settled. The EditorHost records the frame it submits
#                to Metal and writes it as dump v2.
#   3. compare   funkgui_framerender --fingerprint of both dumps: every line must be equal except `live` (the count of
#                live-flagged primitives, which the hashes, counts, extents and tag counts already leave out, 02 §3.9).
#                The live dump must also say "clock fixed" and "dpi 2" (the capture hooks reached the editor).
#   Results go to <build>/gui-live/: <id>.{headless,live}.dump, <id>.{headless,live}.fp, <id>.live.png (the live frame
#   rendered by funkgui_framerender at 2x supersampling: what the GPU was given, as a picture), <id>.capture.log and
#   summary.txt. The last line printed is "gui-live: N/5 equal".
#
# Isolation. The Standalone runs with CFFIXED_USER_HOME=<scratch>/home, so JUCE's Standalone settings file (audio
# device, saved plug-in state: ~/Library/Application Support/<product>.settings) is neither read nor written: the
# Standalone opens at its defaults (Mode slot 0 = clean), whatever the user did with it, and never changes the user's
# file (C §3.2, §6). Preferences and presets go to scratch paths too (02 §5.1). The scratch directory is removed at the
# end. The Standalone asks for microphone access the first time a new build of it runs (MICROPHONE_PERMISSION_ENABLED);
# the capture does not wait for the answer.
#
# Serialisation. Every live GUI run on the machine holds /tmp/fcmp-gui.lock (lockf -k -t 900; FunkGui's
# fg.gallery.live takes the same lock), so two captures never fight over the window server. It needs a logged-in,
# awake session (a window server and Metal); it is never part of `verify`.
#
# Executables. It runs <build>'s FCompressor Standalone, fcmp_probe_plugin and funkgui_framerender. An executable that
# does not exist yet is built first (cmake --build <build> --target <them>); an existing one is used as it is, so build
# the tree before running this (the verify-gui-live target of a GPU build does exactly that, then runs this script).
#
# Exit: 0 all views equal; 1 a view differs or could not be captured; 2 usage, setup or lock error.
set -u

VIEWS="panel chars.sidechain chars.colour modebrowser presetbrowser"
MODE=clean
DT=0.0166666675
LOCK=/tmp/fcmp-gui.lock

usage() {
  sed -n '3,37p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "gui-live.sh: $*" >&2
  exit 2
}

LOCKED=0
if [ "${1:-}" = "--locked" ]; then                      # internal: the re-run under lockf
  LOCKED=1
  shift
fi
case "${1:-}" in
  -h|--help) usage; exit 0 ;;
esac
if [ $# -ne 1 ] || [ -z "$1" ]; then
  usage >&2
  exit 2
fi
[ -d "$1" ] || die "no build directory $1"
BUILD=$(cd "$1" && pwd -P) || die "cannot enter $1"

if [ "$LOCKED" = 0 ]; then
  command -v lockf >/dev/null 2>&1 || die "lockf(1) not found"
  echo "gui-live.sh: waiting for $LOCK (at most 900 s) ..."
  lockf -k -t 900 "$LOCK" /bin/sh "$0" --locked "$BUILD"
  rc=$?
  if [ "$rc" -eq 75 ]; then                             # EX_TEMPFAIL: another GUI run held the lock for 900 s
    die "$LOCK is still held by another live GUI run after 900 s"
  fi
  exit "$rc"
fi

# ---- the build ---------------------------------------------------------------------------------------------------------
CACHE="$BUILD/CMakeCache.txt"
[ -f "$CACHE" ] || die "$BUILD is not a configured build directory"
cache() {  # cache <NAME> -> its value
  sed -n "s/^$1:[A-Z]*=//p" "$CACHE" | head -1
}
SRC=$(cache CMAKE_HOME_DIRECTORY)
CFG=$(cache CMAKE_BUILD_TYPE)
[ -n "$SRC" ] && [ -n "$CFG" ] || die "cannot read CMAKE_HOME_DIRECTORY / CMAKE_BUILD_TYPE from $CACHE"
case "$(cache FCOMPRESSOR_HEADLESS)$(cache FCOMPRESSOR_DSP_ONLY)" in
  *ON*|*TRUE*|*1*) die "$BUILD is a headless or DSP-only configuration; live parity needs a GPU build (agent-gui, lead)" ;;
esac
PRODUCT_H="$BUILD/generated/FcmpProduct.h"
[ -f "$PRODUCT_H" ] || die "$PRODUCT_H is missing"
product_const() {  # product_const kName -> FCompressor
  sed -n "s/.*char $1\[\] *= *\"\([^\"]*\)\".*/\1/p" "$PRODUCT_H" | head -1
}
NAME=$(product_const kName)
PREFIX=$(product_const kEnvPrefix)
[ -n "$NAME" ] && [ -n "$PREFIX" ] || die "cannot read kName / kEnvPrefix from $PRODUCT_H"
case "$PREFIX" in
  *[!A-Za-z0-9_]*) die "kEnvPrefix '$PREFIX' is not letters, digits and underscores" ;;   # it is eval'ed below
esac
DEPS="$BUILD/fcmp-deps.txt"
[ -f "$DEPS" ] || die "$DEPS is missing (configure the build directory first)"
FUNKGUI_DIR=$(awk -F'\t' '$1 == "FunkGui" { print $4; exit }' "$DEPS")
CAPTURE="$FUNKGUI_DIR/tools/capture-frame.sh"
[ -f "$CAPTURE" ] || die "no $CAPTURE (FunkGui v0.7.0 and later ship tools/capture-frame.sh)"
ARCHS=$(awk -F'\t' '$1 == "configuration" { print $6; exit }' "$DEPS" | sed -n 's/.*\[\(.*\)\].*/\1/p')
case "$ARCHS" in
  arm64|x86_64) ARCH=$ARCHS ;;
  *) ARCH=$(uname -m) ;;                                # universal: the probes run as the host (03 §3.3)
esac

APP="$BUILD/${NAME}_artefacts/$CFG/Standalone/$NAME.app/Contents/MacOS/$NAME"
PROBE="$BUILD/fcmp_probe_plugin_artefacts/$CFG/fcmp_probe_plugin"
find_framerender() {
  find "$BUILD" -path "*/funkgui_framerender_artefacts/$CFG/funkgui_framerender" -type f -perm -u+x 2>/dev/null | head -1
}
FR=$(find_framerender)
MISSING=""
[ -x "$APP" ] || MISSING="$MISSING ${NAME}_Standalone"
[ -x "$PROBE" ] || MISSING="$MISSING fcmp_probe_plugin"
[ -n "$FR" ] || MISSING="$MISSING funkgui_framerender"
if [ -n "$MISSING" ]; then
  echo "gui-live.sh: building$MISSING (missing in $BUILD)"
  # shellcheck disable=SC2086
  cmake --build "$BUILD" --target $MISSING || die "cmake --build $BUILD --target$MISSING failed"
  FR=$(find_framerender)
  [ -x "$APP" ] && [ -x "$PROBE" ] && [ -n "$FR" ] || die "still missing after the build:$MISSING"
fi

OUT="$BUILD/gui-live"
rm -rf "$OUT"
mkdir -p "$OUT" || die "cannot create $OUT"
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/fcmp-gui-live.XXXXXX") || die "mktemp failed"
trap 'rm -rf "$SCRATCH"' EXIT
trap 'exit 2' INT TERM
mkdir -p "$SCRATCH/home" "$SCRATCH/prefs" || die "cannot create $SCRATCH"

# The one variable set (02 §5.1), for the headless probe and the Standalone alike. <P> is the product's prefix.
setenv() {  # setenv <NAME> <value>: export <PREFIX><NAME>=<value>
  eval "${PREFIX}$1=\$2"
  eval "export ${PREFIX}$1"
}
setenv PREFS_DIR "$SCRATCH/prefs"
setenv PRESETS_DB "$SCRATCH/presets.db"
CAPTURE_TIMEOUT_SEC=${CAPTURE_TIMEOUT_SEC:-60}           # capture-frame.sh's 20 s is tight while other builds run
export CAPTURE_TIMEOUT_SEC

{
  echo "gui-live.sh: $NAME $CFG ($ARCH), $BUILD"
  echo "  standalone  $APP"
  echo "  probe       $PROBE"
  echo "  framerender $FR"
  echo "  capture     $CAPTURE"
  for w in plugin probes; do
    [ -f "$BUILD/built-from-$w.txt" ] && echo "  built-from  $(head -1 "$BUILD/built-from-$w.txt")"
  done
} | tee "$OUT/summary.txt"

total=0
equal=0
for VIEW in $VIEWS; do
  total=$((total + 1))
  H="$OUT/$VIEW.headless"
  L="$OUT/$VIEW.live"

  # ---- 1. headless -------------------------------------------------------------------------------------------------
  "$PROBE" ui.dump --mode "$MODE" --golden-root "$SRC/tests/golden" --arch "$ARCH" -- \
      --view "$VIEW" --out "$H.dump" --dpi 2 --theme 0 > "$H.log" 2>&1
  rc=$?
  settle=$(sed -n 's/.*settled in \([0-9][0-9]*\) frames.*/\1/p' "$H.log" | head -1)
  if [ "$rc" -ne 0 ] || [ ! -f "$H.dump" ] || [ -z "$settle" ]; then
    echo "FAIL     $VIEW: the headless ui.dump failed (exit $rc); see $H.log" | tee -a "$OUT/summary.txt"
    continue
  fi
  after=$((settle + 2))
  [ "$after" -lt 8 ] && after=8

  # ---- 2. live -----------------------------------------------------------------------------------------------------
  (
    setenv UI_VIEW "$VIEW"
    setenv UI_FIXED_DT "$DT"
    setenv UI_NO_HINT 1
    setenv UI_NO_LIVE 1
    setenv UI_THEME 0
    setenv UI_SCALE 2
    setenv CANVAS_DUMP_AFTER "$after"
    setenv GPU_LOG 1
    CFFIXED_USER_HOME="$SCRATCH/home"
    export CFFIXED_USER_HOME
    exec /bin/sh "$CAPTURE" "$APP" "$L.dump" "$PREFIX"
  ) > "$OUT/$VIEW.capture.log" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ] || [ ! -f "$L.dump" ]; then
    echo "FAIL     $VIEW: no live frame (capture-frame.sh exit $rc: no window server, a locked screen, or the GPU" \
         "path never came up); see $OUT/$VIEW.capture.log" | tee -a "$OUT/summary.txt"
    continue
  fi

  # ---- 3. compare --------------------------------------------------------------------------------------------------
  if ! "$FR" --fingerprint "$H.dump" > "$H.fp" 2>&1 || ! "$FR" --fingerprint "$L.dump" > "$L.fp" 2>&1; then
    echo "FAIL     $VIEW: funkgui_framerender --fingerprint failed; see $H.fp, $L.fp" | tee -a "$OUT/summary.txt"
    continue
  fi
  "$FR" "$L.dump" "$L.png" 2 > /dev/null 2>&1 || echo "NOTE     $VIEW: could not render $L.png" | tee -a "$OUT/summary.txt"
  hooks=$(sed -n 's/^view .* dpi \([^ ]*\) .* clock \([a-z]*\).*/dpi \1 clock \2/p' "$L.dump" | head -1)
  hgeo=$(sed -n 's/^geometry //p' "$H.fp")
  htext=$(sed -n 's/^text //p' "$H.fp")
  lgeo=$(sed -n 's/^geometry //p' "$L.fp")
  ltext=$(sed -n 's/^text //p' "$L.fp")
  if [ "$hooks" != "dpi 2 clock fixed" ]; then
    echo "FAIL     $VIEW: the live dump says '$hooks', not 'dpi 2 clock fixed' (the capture hooks did not reach" \
         "the editor); see $OUT/$VIEW.capture.log" | tee -a "$OUT/summary.txt"
  elif grep -v '^live ' "$H.fp" > "$H.fp.cmp" && grep -v '^live ' "$L.fp" > "$L.fp.cmp" && cmp -s "$H.fp.cmp" "$L.fp.cmp"
  then
    equal=$((equal + 1))
    echo "EQUAL    $VIEW: geometry $hgeo text $htext (settle $settle, dump after $after)" | tee -a "$OUT/summary.txt"
  else
    echo "DIFFERS  $VIEW: headless geometry $hgeo text $htext, live geometry $lgeo text $ltext" \
      | tee -a "$OUT/summary.txt"
    diff "$H.fp.cmp" "$L.fp.cmp" | sed 's/^/         /' | head -20 | tee -a "$OUT/summary.txt"
  fi
  rm -f "$H.fp.cmp" "$L.fp.cmp"
done

echo "gui-live: $equal/$total equal (results in $OUT)" | tee -a "$OUT/summary.txt"
[ "$equal" -eq "$total" ]
