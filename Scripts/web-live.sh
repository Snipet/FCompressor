#!/bin/sh
#
# Scripts/web-live.sh: the browser gate, web.live (ADR-93, the web lead phase; gui-live.sh is its native counterpart).
# The built site in a real headless Chrome must draw, view for view, what the same editor code draws under node, the
# pixels must be SoftRaster's, the page's own self-test must pass, and every page of the live directory and the
# scripted user must find nothing wrong.
#
#   Scripts/web-live.sh <build-web> [options]                      the gate on a web build tree: node values from the
#                                                                  build, <build>/site and <build>/live in Chrome
#   Scripts/web-live.sh --dir <site> --live <dir> --expect <dir> [options]
#                                                                  the gate on a downloaded artifact: nothing is built
#                                                                  or computed
#   Scripts/web-live.sh --url <base> --expect <dir> [--commit <sha>] [--wait <s>] [options]
#                                                                  the published site, after a push: no server, nothing
#                                                                  built or computed (below)
#   Scripts/web-live.sh --serve <build-web>                        serves only and prints the URLs, for a human with
#   Scripts/web-live.sh --serve --dir <site> --live <dir> --expect <dir>   another browser; Ctrl-C stops it
#   Scripts/web-live.sh --help (or -h)                             this text
#   Options: --out <dir>      where the results go: default <build-web>/web-live; with --dir, web-live beside the
#                             site (<site>/../web-live); with --url, web-live beside the expectations
#                             (<expect>/../web-live). Never the current directory. It must be new, empty, the gate's
#                             (it has .web-live), hold an expect/ directory alone (an artifact as downloaded), or be an
#                             earlier run's from before .web-live; any other is refused (exit 2), nothing in it touched
#            --chrome <path>  the browser (default $CHROME, else the macOS application, else google-chrome on PATH)
#            --timeout <s>    how long one page may take to give its verdict (default 120); the scenario is given four
#                             times that as its own bound (--timeout 480 at the default) and stopped at five times that
#            --gpu <g>        default (the default): ANGLE on Metal on macOS, SwiftShader elsewhere; swiftshader: the
#                             software renderer for every Chrome of the run, the gate's and the scenario's, as on the
#                             CI runner (a rehearsal of it on a Mac)
#            --commit <sha>   --url only: the build the published site must say it is (7 to 40 hex digits)
#            --wait <s>       --url only: how long <base>/built-from.txt is asked for that build (default 600)
#   --chrome, --timeout and --gpu do nothing with --serve, which starts no browser.
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
#   --url            The site as it is published (http or https, the address of its index.html's directory). First
#                    <base>/built-from.txt is asked until it says "site <commit> clean" (a CDN may serve the previous
#                    build for a while): every --wait / 20 seconds (1 to 15), a NOTE each time, at most --wait seconds;
#                    the row "published" fails, and nothing more is run, when it never does. Then the row "audio":
#                    <base>/audio/loop.wav, the sample loop, has the size and the SHA-256 of the repository's file
#                    (the pages are run all the same when it has not). Then the capture pages (against <expect>,
#                    which a build-form run wrote as <out>/expect) and the self-test, both ways, against <base>. The
#                    live pages and the scenario are not on the published site and are not run (NOTE lines say so).
#   Lines: "EQUAL|DIFFERS|FAIL <view>.theme<t>: ...", the rows "PASS|FAIL|NOTE web.live <row>: ...", the pages' own
#   lines two columns in, and last "web-live: N/M passed (results in <out>)" ("web-live: no verdict (...)" when there
#   is none). Results in <out>: expect/ (a build tree), frames/<view>.theme<t>.live.fp, png/, selftest.log,
#   selftest.autoplay.log, <page>.log, scenario.log, summary.txt, .web-live (the mark of a results directory). Before a
#   run the gate removes an earlier run's results there (and, on a build tree, expect/), nothing else. <out>/expect is
#   what --expect takes on another machine, beside the site and the live directory.
#
# Environment: CHROME the browser, when --chrome is not given. NODE the node of the runner (22 or later); else the
# build's own node (the toolchain's emulator), else node on PATH. FCMP_WEB_LIVE_NO_SANDBOX=1 starts every Chrome
# without its sandbox, for a container that gives it no user namespace. TMPDIR holds the throwaway Chrome profiles and
# the node values' scratch preferences, all removed at the end.
#
# Nothing is built: a tree that is not a web build, or has no built site, is refused (the verify-web-live target of a
# web build builds first, then runs this script). No lock: headless Chrome takes no window. It is never part of
# `verify`. Chrome's flags are fixed (--gpu chooses between two fixed sets), and it is always muted. On a machine with
# no audio device, where no AudioContext renders, the Chrome of the running-context rows is started with the
# browser's null sink (--disable-audio-output), and a NOTE says so. A signal (INT, TERM, HUP) ends a run at any step
# with exit 2: the scratch directory, every Chrome, its profile and the scenario (its process group, sent SIGTERM so
# its own cleanup runs) go with it.
#
# Exit: 0 every row passed; 1 a view differs or a row failed (with --url: the published site never said the commit);
# 2 usage, no node 22 or later, no Chrome, the results directory refused, no verdict, a signal.
set -u

VIEWS="panel chars.sidechain chars.colour modebrowser presetbrowser settings"
THEMES="0 1"
MODE=clean
HOST=WEB-LIVE
NODE_MAJOR=22

usage() {
  sed -n '3,/^set -u/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

die() {
  echo "web-live.sh: $*" >&2
  exit 2
}

bad() {  # a usage error: the reason, then how to call it
  echo "web-live.sh: $*" >&2
  sed -n '/^#   Scripts\/web-live.sh </,/^#   --chrome, --timeout and --gpu/p' "$0" | sed 's/^# \{0,1\}//' >&2
  exit 2
}

# A signal at any step is exit 2, and exit removes the scratch directory (dash runs no EXIT trap for a signal it does
# not catch). Both are reset before the runner takes this process's place.
SCRATCH=""
trap '[ -z "$SCRATCH" ] || rm -rf "$SCRATCH"' EXIT
trap 'exit 2' INT TERM HUP

# ---- the arguments ---------------------------------------------------------------------------------------------------
BUILD_ARG=""
SITE=""
LIVE=""
EXPECT=""
URL=""
COMMIT=""
WAIT=""
OUT=""
CHROME_ARG=""
TIMEOUT=120
GPU=default
SERVE=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --serve) SERVE=1 ;;
    --dir|--live|--expect|--url|--commit|--wait|--out|--chrome|--timeout|--gpu)
      [ $# -ge 2 ] && [ -n "$2" ] || bad "$1 needs a value"
      case "$1" in
        --dir) SITE=$2 ;;
        --live) LIVE=$2 ;;
        --expect) EXPECT=$2 ;;
        --url) URL=$2 ;;
        --commit) COMMIT=$2 ;;
        --wait) WAIT=$2 ;;
        --out) OUT=$2 ;;
        --chrome) CHROME_ARG=$2 ;;
        --timeout) TIMEOUT=$2 ;;
        --gpu) GPU=$2 ;;
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
case "$GPU" in
  default|swiftshader) ;;
  *) bad "--gpu is default or swiftshader, not '$GPU'" ;;
esac
if [ -n "$URL" ]; then
  [ -z "$BUILD_ARG$SITE$LIVE" ] || bad "--url is the published site: not with a build directory, --dir or --live"
  [ "$SERVE" = 0 ] || bad "--serve serves a site of this machine: not with --url"
  case "$URL" in
    http://?*|https://?*) ;;
    *) bad "--url is the site's http or https address, not '$URL'" ;;
  esac
  case "$URL" in
    *'?'*|*'#'*) bad "--url is the site's address, with no query and no fragment, not '$URL'" ;;
  esac
  [ -n "$EXPECT" ] || bad "--url needs --expect <dir>"
  if [ -n "$COMMIT" ]; then
    case "$COMMIT" in
      *[!0-9a-f]*) bad "--commit is 7 to 40 lower-case hex digits, not '$COMMIT'" ;;
    esac
    [ ${#COMMIT} -ge 7 ] && [ ${#COMMIT} -le 40 ] || bad "--commit is 7 to 40 lower-case hex digits, not '$COMMIT'"
  fi
  case "$WAIT" in
    *[!0-9]*) bad "--wait is a whole number of seconds, not '$WAIT'" ;;
  esac
else
  [ -z "$COMMIT$WAIT" ] || bad "--commit and --wait go with --url"
  if [ -n "$BUILD_ARG" ]; then
    [ -z "$SITE$LIVE$EXPECT" ] || bad "a build directory, or --dir with --live and --expect: not both"
  else
    [ -n "$SITE" ] || bad "a build directory, --dir <site> --live <dir> --expect <dir>, or --url <base> --expect <dir>"
    [ -n "$LIVE" ] && [ -n "$EXPECT" ] || bad "--dir needs --live <dir> and --expect <dir>"
  fi
fi

HERE=$(cd "$(dirname "$0")" && pwd -P) || die "cannot find the script's own directory"
RUNNER="$HERE/web/live.mjs"
[ -f "$RUNNER" ] || die "no $RUNNER"

# ---- the build, the artifact, or the published site ------------------------------------------------------------------
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
elif [ -n "$URL" ]; then
  [ -d "$EXPECT" ] || die "no expectation directory $EXPECT"
  EXPECT=$(cd "$EXPECT" && pwd -P) || die "cannot enter $EXPECT"
  [ -n "$OUT" ] || OUT="$(dirname "$EXPECT")/web-live"
else
  [ -d "$SITE" ] || die "no site directory $SITE"
  SITE=$(cd "$SITE" && pwd -P) || die "cannot enter $SITE"
  [ -f "$SITE/built-from.txt" ] || die "$SITE is not a built site (no built-from.txt)"
  [ -d "$LIVE" ] || die "no live directory $LIVE"
  LIVE=$(cd "$LIVE" && pwd -P) || die "cannot enter $LIVE"
  [ -d "$EXPECT" ] || die "no expectation directory $EXPECT"
  EXPECT=$(cd "$EXPECT" && pwd -P) || die "cannot enter $EXPECT"
  [ -n "$OUT" ] || OUT="$(dirname "$SITE")/web-live"
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

# ---- the results directory: the runner's guard (live.mjs prepareOut), before anything is written there ---------------
if [ "$SERVE" = 0 ] || [ -n "$BUILD" ]; then
  set -- --prepare-out "$OUT"
  [ -z "$LIVE" ] || set -- "$@" --live "$LIVE"
  [ -z "$BUILD" ] || set -- "$@" --with-expect
  "$RUNNER_NODE" "$RUNNER" "$@" || exit 2                   # the runner has said why <out> is refused
  mkdir -p "$OUT" || die "cannot create $OUT"
  OUT=$(cd "$OUT" && pwd -P) || die "cannot enter $OUT"
fi
if [ "$SERVE" = 0 ]; then
  {
    if [ -n "$BUILD" ]; then
      echo "web-live.sh: $BUILD"
      echo "  probe       $PROBE (under $PROBE_NODE)"
      [ -f "$BUILD/built-from-probes.txt" ] && echo "  built-from  $(head -1 "$BUILD/built-from-probes.txt")"
    elif [ -n "$URL" ]; then
      echo "web-live.sh: $URL (the published site: nothing is built, computed or served)"
    else
      echo "web-live.sh: $SITE (an artifact: nothing is built or computed)"
    fi
    [ -n "$URL" ] || echo "  built-from  $(head -1 "$SITE/built-from.txt")"
    echo "  node        $RUNNER_NODE ($("$RUNNER_NODE" --version))"
    echo "  chrome      $CHROME_PATH"
    [ "$GPU" = default ] || echo "  gpu         $GPU (forced for every Chrome of the run)"
  } | tee "$OUT/summary.txt"
fi

# ---- 1. the node values, 2. the expectations -------------------------------------------------------------------------
if [ -n "$BUILD" ]; then
  EXPECT="$OUT/expect"
  mkdir -p "$EXPECT" || die "cannot create $EXPECT"
  SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/fcmp-web-live.XXXXXX") || die "mktemp failed"
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
  SCRATCH=""
fi

# ---- 3. the runner: it takes this process's place, so a signal reaches it (and its Chrome) directly ------------------
trap - EXIT INT TERM HUP
if [ -n "$URL" ]; then
  set -- --url "$URL" --expect "$EXPECT"
  [ -z "$COMMIT" ] || set -- "$@" --commit "$COMMIT"
  [ -z "$WAIT" ] || set -- "$@" --wait "$WAIT"
else
  set -- --dir "$SITE" --live "$LIVE" --expect "$EXPECT"
fi
if [ "$SERVE" = 1 ]; then
  set -- --serve "$@"
else
  set -- "$@" --out "$OUT" --timeout "$TIMEOUT" --gpu "$GPU" --chrome "$CHROME_PATH" --keep-summary
fi
exec "$RUNNER_NODE" "$RUNNER" "$@"
