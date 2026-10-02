#!/bin/sh
#
# Scripts/web-live.sh: the browser gate, web.live (ADR-93, the web lead phase; gui-live.sh is its native counterpart).
# The built site in a real headless Chrome must draw, view for view, what the same editor code draws under node, the
# pixels must be SoftRaster's, the page's own self-test must pass, and every page of the live directory and the
# scripted user must find nothing wrong.
#
#   Scripts/web-live.sh <build-web>                                the gate on a web build tree: node values from the
#                                                                  build, <build>/site and <build>/live in Chrome
#   Scripts/web-live.sh --dir <site> --live <dir> --expect <dir>   the gate on a downloaded artifact: nothing is built
#                                                                  or computed
#   Scripts/web-live.sh --serve <build-web>                        serves only and prints the URLs, for a human with
#   Scripts/web-live.sh --serve --dir <site> --live <dir> --expect <dir>   another browser; Ctrl-C stops it
#   Options: --out <dir>      where the results go (default <build-web>/web-live; ./web-live with --dir)
#            --chrome <path>  the browser (default $CHROME, else the macOS application, else google-chrome on PATH)
#            --timeout <s>    how long one page may take to give its verdict (default 120)
#
#   1. node values   A build tree only. For each view of {panel, chars.sidechain, chars.colour, modebrowser,
#                    presetbrowser, settings} x theme {0, 1}:
#                      <node> <build>/fcmp_probe_web.js ui.dump --mode clean --golden-dir <source>/tests/golden
#                        --out <scratch>.json -- --view <id> --dpi 2 --theme <t> --facade web --host WEB-LIVE
#                        --nolive 1 --fp <out>/expect/<id>.theme<t>.node.fp
#                    the editor as wasm32 under node over a WebFacade whose link drops everything (the page before
#                    START), settled at 1/60 s, its fingerprint written as text. FCMP_PREFS_DIR and FCMP_PRESETS_DB
#                    are scratch paths, so the user's preferences never reach the frame. ui.dump prints "HARNESS ERROR
#                    unknown flag" lines by design: it is judged by its exit code and the file. A view whose node
#                    value could not be made has no expectation, which the runner reports as that view's FAIL (its
#                    output is kept beside it as <id>.theme<t>.node.log).
#   2. expectations  A build tree only, and only when the source has Tools/web/live/expect.mjs:
#                      <node> Tools/web/live/expect.mjs --site <build>/site --live <build>/live --out <out>/expect
#                    writes what the live pages compare with and node can compute. Its failure ends the gate (exit 2).
#   3. the runner    Scripts/web/live.mjs (read its head for the rows): its own server on 127.0.0.1 gives the site at
#                    /, the live directory at /live/ and the expectations at /expect/; headless Chrome, always muted,
#                    with a throwaway profile, opens the capture pages
#                      index.html?view=<id>&theme=<t>&zoom=100&scale=2&dt=0.0166666675&nohint=1&nolive=1&host=WEB-LIVE
#                    (START never pressed), the page's self-test with and without a running AudioContext, and every
#                    page of the live directory; then Scripts/web/scenario.mjs runs as the scripted user.
#   Lines: "EQUAL|DIFFERS|FAIL <view>.theme<t>: ...", the rows "PASS|FAIL|NOTE web.live <row>: ...", the pages' own
#   lines two columns in, and last "web-live: N/M passed (results in <out>)". Results in <out>: expect/ (a build tree),
#   frames/<view>.theme<t>.live.fp, png/, selftest.log, selftest.autoplay.log, <page>.log, scenario.log, summary.txt.
#   <out>/expect is what --expect takes on another machine, beside the site and the live directory.
#
# It needs node 22 or later for the runner ($NODE when set; else the build's own node, the toolchain's emulator; else
# node on PATH) and Chrome. Nothing is built: a tree that is not a web build, or has no built site, is refused (the
# verify-web-live target of a web build builds first, then runs this script). No lock: headless Chrome takes no
# window. It is never part of `verify`. Chrome's flags are fixed, and it is always muted; FCMP_WEB_LIVE_NO_SANDBOX=1
# in the environment starts it without its sandbox, for a container that gives it no user namespace.
#
# Exit: 0 every row passed; 1 a view differs or a row failed; 2 usage, no node 22 or later, no Chrome, no verdict.
set -u

VIEWS="panel chars.sidechain chars.colour modebrowser presetbrowser settings"
THEMES="0 1"
MODE=clean
HOST=WEB-LIVE
NODE_MAJOR=22

usage() {
  sed -n '3,/^# Exit:/p' "$0" | sed 's/^# \{0,1\}//'
}

die() {
  echo "web-live.sh: $*" >&2
  exit 2
}

bad() {  # a usage error: the reason, then how to call it
  echo "web-live.sh: $*" >&2
  sed -n '/^#   Scripts\/web-live.sh </,/^#            --timeout/p' "$0" | sed 's/^# \{0,1\}//' >&2
  exit 2
}

# ---- the arguments ---------------------------------------------------------------------------------------------------
BUILD_ARG=""
SITE=""
LIVE=""
EXPECT=""
OUT=""
CHROME_ARG=""
TIMEOUT=120
SERVE=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --serve) SERVE=1 ;;
    --dir|--live|--expect|--out|--chrome|--timeout)
      [ $# -ge 2 ] && [ -n "$2" ] || bad "$1 needs a value"
      case "$1" in
        --dir) SITE=$2 ;;
        --live) LIVE=$2 ;;
        --expect) EXPECT=$2 ;;
        --out) OUT=$2 ;;
        --chrome) CHROME_ARG=$2 ;;
        --timeout) TIMEOUT=$2 ;;
      esac
      shift ;;
    -*) bad "unknown option $1" ;;
    *)
      [ -z "$BUILD_ARG" ] || bad "one build directory, not two ($BUILD_ARG and $1)"
      BUILD_ARG=$1 ;;
  esac
  shift
done
case "$TIMEOUT" in
  ''|*[!0-9]*|0*) bad "--timeout is a whole number of seconds above 0, not '$TIMEOUT'" ;;
esac
if [ -n "$BUILD_ARG" ]; then
  [ -z "$SITE$LIVE$EXPECT" ] || bad "a build directory, or --dir with --live and --expect: not both"
else
  [ -n "$SITE" ] || bad "a build directory, or --dir <site> --live <dir> --expect <dir>"
  [ -n "$LIVE" ] && [ -n "$EXPECT" ] || bad "--dir needs --live <dir> and --expect <dir>"
fi

HERE=$(cd "$(dirname "$0")" && pwd -P) || die "cannot find the script's own directory"
RUNNER="$HERE/web/live.mjs"
[ -f "$RUNNER" ] || die "no $RUNNER"

# ---- the build, or the artifact --------------------------------------------------------------------------------------
BUILD=""
PROBE_NODE=""
if [ -n "$BUILD_ARG" ]; then
  [ -d "$BUILD_ARG" ] || die "no build directory $BUILD_ARG"
  BUILD=$(cd "$BUILD_ARG" && pwd -P) || die "cannot enter $BUILD_ARG"
  CACHE="$BUILD/CMakeCache.txt"
  [ -f "$CACHE" ] || die "$BUILD is not a configured build directory"
  cache() {  # cache <NAME> -> its value
    sed -n "s/^$1:[A-Z]*=//p" "$CACHE" | head -1
  }
  SRC=$(cache CMAKE_HOME_DIRECTORY)
  [ -n "$SRC" ] || die "cannot read CMAKE_HOME_DIRECTORY from $CACHE"
  case "$(cache FCOMPRESSOR_WEB)" in
    ON|TRUE|YES|1) ;;
    *) die "$BUILD is not a web build (FCOMPRESSOR_WEB is not ON): the gate needs the tree of the web preset" ;;
  esac
  SITE="$BUILD/site"
  LIVE="$BUILD/live"
  PROBE="$BUILD/fcmp_probe_web.js"
  PROBE_NODE=$(cache CMAKE_CROSSCOMPILING_EMULATOR | sed 's/;.*//')
  [ -f "$SITE/built-from.txt" ] || die "$SITE is not a built site (no built-from.txt): cmake --build --preset web"
  [ -d "$LIVE" ] || die "no live directory $LIVE: cmake --build --preset web"
  [ -f "$PROBE" ] || die "no $PROBE: cmake --build --preset web"
  [ -n "$PROBE_NODE" ] || die "cannot read CMAKE_CROSSCOMPILING_EMULATOR (the build's node) from $CACHE"
  [ -n "$OUT" ] || OUT="$BUILD/web-live"
else
  [ -d "$SITE" ] || die "no site directory $SITE"
  SITE=$(cd "$SITE" && pwd -P) || die "cannot enter $SITE"
  [ -f "$SITE/built-from.txt" ] || die "$SITE is not a built site (no built-from.txt)"
  [ -d "$LIVE" ] || die "no live directory $LIVE"
  LIVE=$(cd "$LIVE" && pwd -P) || die "cannot enter $LIVE"
  [ -d "$EXPECT" ] || die "no expectation directory $EXPECT"
  EXPECT=$(cd "$EXPECT" && pwd -P) || die "cannot enter $EXPECT"
  [ -n "$OUT" ] || OUT="web-live"
fi

# ---- node and Chrome -------------------------------------------------------------------------------------------------
node_major() {  # node_major <node> -> 24, or nothing when it is not a node
  "$1" --version 2>/dev/null | sed -n 's/^v\([0-9][0-9]*\)\..*/\1/p' | head -1
}
RUNNER_NODE=""
TRIED=""
if [ -n "${NODE:-}" ]; then
  set -- "$NODE"
else
  set -- "$PROBE_NODE" node
fi
for candidate in "$@"; do
  [ -n "$candidate" ] || continue
  major=$(node_major "$candidate")
  if [ -n "$major" ] && [ "$major" -ge "$NODE_MAJOR" ]; then
    RUNNER_NODE=$candidate
    break
  fi
  if [ -n "$major" ]; then
    TRIED="$TRIED${TRIED:+, }$candidate (node $major)"
  else
    TRIED="$TRIED${TRIED:+, }$candidate (not a node)"
  fi
done
[ -n "$RUNNER_NODE" ] || die "node $NODE_MAJOR or later is needed, and there is none: $TRIED"

CHROME_PATH=""
if [ "$SERVE" = 0 ]; then
  set -- --find-chrome
  [ -z "$CHROME_ARG" ] || set -- "$@" --chrome "$CHROME_ARG"
  CHROME_PATH=$("$RUNNER_NODE" "$RUNNER" "$@") || exit 2    # the runner has said where it looked
  [ -n "$CHROME_PATH" ] || die "no Chrome"
fi

# ---- the results directory -------------------------------------------------------------------------------------------
SCRATCH=""
if [ "$SERVE" = 0 ] || [ -n "$BUILD" ]; then
  mkdir -p "$OUT" || die "cannot create $OUT"
  OUT=$(cd "$OUT" && pwd -P) || die "cannot enter $OUT"
fi
if [ "$SERVE" = 0 ]; then
  {
    if [ -n "$BUILD" ]; then
      echo "web-live.sh: $BUILD"
      echo "  probe       $PROBE (under $PROBE_NODE)"
      [ -f "$BUILD/built-from-probes.txt" ] && echo "  built-from  $(head -1 "$BUILD/built-from-probes.txt")"
    else
      echo "web-live.sh: $SITE (an artifact: nothing is built or computed)"
    fi
    echo "  built-from  $(head -1 "$SITE/built-from.txt")"
    echo "  node        $RUNNER_NODE ($("$RUNNER_NODE" --version))"
    echo "  chrome      $CHROME_PATH"
  } | tee "$OUT/summary.txt"
fi

# ---- 1. the node values, 2. the expectations -------------------------------------------------------------------------
if [ -n "$BUILD" ]; then
  EXPECT="$OUT/expect"
  rm -rf "$EXPECT"
  mkdir -p "$EXPECT" || die "cannot create $EXPECT"
  SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/fcmp-web-live.XXXXXX") || die "mktemp failed"
  trap 'rm -rf "$SCRATCH"' EXIT
  trap 'exit 2' INT TERM
  wanted=0
  made=0
  for VIEW in $VIEWS; do
    for THEME in $THEMES; do
      wanted=$((wanted + 1))
      FP="$EXPECT/$VIEW.theme$THEME.node.fp"
      LOG="$EXPECT/$VIEW.theme$THEME.node.log"
      FCMP_PREFS_DIR="$SCRATCH/prefs" FCMP_PRESETS_DB="$SCRATCH/presets.db" \
        "$PROBE_NODE" "$PROBE" ui.dump --mode "$MODE" --golden-dir "$SRC/tests/golden" --out "$SCRATCH/frame.json" -- \
          --view "$VIEW" --dpi 2 --theme "$THEME" --facade web --host "$HOST" --nolive 1 --fp "$FP" > "$LOG" 2>&1
      rc=$?
      if [ "$rc" -eq 0 ] && [ -s "$FP" ]; then
        made=$((made + 1))
        rm -f "$LOG"
      else
        rm -f "$FP"
        echo "web-live.sh: no node value for $VIEW.theme$THEME (ui.dump exit $rc); see $LOG" >&2
      fi
    done
  done
  LINE="  expect      $made of $wanted node values in $EXPECT"
  if [ "$SERVE" = 0 ]; then echo "$LINE" | tee -a "$OUT/summary.txt"; else echo "web-live.sh:$LINE"; fi

  TOOL="$SRC/Tools/web/live/expect.mjs"
  if [ -f "$TOOL" ]; then
    "$RUNNER_NODE" "$TOOL" --site "$SITE" --live "$LIVE" --out "$EXPECT" > "$SCRATCH/expect.out" 2>&1
    rc=$?
    if [ "$SERVE" = 0 ]; then
      sed 's/^/  /' "$SCRATCH/expect.out" | tee -a "$OUT/summary.txt"
    else
      sed 's/^/  /' "$SCRATCH/expect.out"
    fi
    [ "$rc" -eq 0 ] || die "the expectation tool failed (exit $rc): $TOOL"
  fi
  rm -rf "$SCRATCH"
  trap - EXIT INT TERM
fi

# ---- 3. the runner: it takes this process's place, so a signal reaches it (and its Chrome) directly ------------------
set -- --dir "$SITE" --live "$LIVE" --expect "$EXPECT"
if [ "$SERVE" = 1 ]; then
  set -- --serve "$@"
else
  set -- "$@" --out "$OUT" --timeout "$TIMEOUT" --chrome "$CHROME_PATH" --keep-summary
fi
exec "$RUNNER_NODE" "$RUNNER" "$@"
