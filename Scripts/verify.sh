#!/bin/bash
#
# Scripts/verify.sh [--integration] <build-dir>: the definition-of-done gate (03 §0.9, §3.2.4, §4.7; SPRINTS §7 D8).
#
#   1. Refuses --quick (the gate always runs the full grids). With --integration (the lead's sprint-end run) it refuses a
#      build configured with any dependency override in fcmp-deps.txt (K2 #26c); otherwise it prints the override.
#   2. Wipes <build>/golden-candidates, <build>/probe-results and any old verify-passed-* stamp.
#   3. Runs `ctest -L verify -j4` once; it never stops at the first failure.
#   4. Classifies every test itself, from CTest's JUnit report and probe-results/*.json (Harness v2 statuses):
#        BLOCKING  spec_fail, harness_error, crash or timeout, missing results, disabled or not run, a failed lint
#        DRIFT     golden_drift (with its <probe>.diff)          } candidates: allowed only with a one-line reason
#        MISSING   golden_missing (candidate files to review)     } per key group in the handoff
#        IMPROVED  le:/ge: golden rows that moved the good way (informational)
#   5. Prints the counts, every non-pass test and the 10 slowest tests.
#   6. Writes <build>/verify-passed-<HEAD sha> only when fully green (no blocking, no drift, no missing) and the source
#      tree has no uncommitted changes (release.sh requires it; validate.sh appends its result to it).
#
# Exit: 0 = no blocking results (candidates allowed), 1 = blocking results, 2 = usage or setup error.
set -u

usage() {
  sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
}

INTEGRATION=0
BUILD=""
for arg in "$@"; do
  case "$arg" in
    --quick|--quick=*)
      echo "verify.sh: --quick is refused: the gate runs every probe on its full grids (03 §4.7)" >&2
      exit 2 ;;
    --integration) INTEGRATION=1 ;;
    -h|--help) usage; exit 0 ;;
    -*) echo "verify.sh: unknown option '$arg'" >&2; usage >&2; exit 2 ;;
    *)
      if [ -n "$BUILD" ]; then
        echo "verify.sh: one build directory only" >&2
        exit 2
      fi
      BUILD="$arg" ;;
  esac
done
if [ -z "$BUILD" ]; then
  usage >&2
  exit 2
fi
if [ ! -d "$BUILD" ]; then
  echo "verify.sh: $BUILD does not exist" >&2
  exit 2
fi
BUILD="$(cd "$BUILD" && pwd -P)"
DEPS="$BUILD/fcmp-deps.txt"
if [ ! -f "$BUILD/CTestTestfile.cmake" ] || [ ! -f "$DEPS" ]; then
  echo "verify.sh: $BUILD is not a configured FCompressor build directory (no CTestTestfile.cmake / fcmp-deps.txt)" >&2
  exit 2
fi
SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt")"
if [ -z "$SRC" ] || [ ! -d "$SRC" ]; then
  echo "verify.sh: cannot find the source directory in $BUILD/CMakeCache.txt" >&2
  exit 2
fi
command -v python3 >/dev/null || { echo "verify.sh: python3 not found" >&2; exit 2; }
CTEST="$(sed -n 's/^CMAKE_CTEST_COMMAND:INTERNAL=//p' "$BUILD/CMakeCache.txt")"
[ -x "$CTEST" ] || CTEST=ctest

echo "== verify.sh $BUILD"
echo "   source $SRC"
awk -F'\t' '!/^#/ { printf "   %-14s %-26s %s  %s%s\n", $1, $2, substr($3, 1, 12), $4, ($5 == "yes" ? "   OVERRIDE" : "") }' "$DEPS"
OVERRIDES="$(awk -F'\t' '!/^#/ && $5 == "yes" { print $1 " " $4 }' "$DEPS")"
if [ -n "$OVERRIDES" ]; then
  echo "   dependency override(s): $(printf '%s' "$OVERRIDES" | tr '\n' ';')"
  if [ "$INTEGRATION" = 1 ]; then
    echo "verify.sh: --integration refuses a build with a dependency override (K2 #26c); reconfigure without it" >&2
    exit 1
  fi
fi

rm -rf "$BUILD/golden-candidates" "$BUILD/probe-results"
rm -f "$BUILD"/verify-passed-* "$BUILD/verify-junit.xml" "$BUILD/verify-tests.json"
mkdir -p "$BUILD/probe-results"

if ! "$CTEST" --test-dir "$BUILD" -L verify --show-only=json-v1 > "$BUILD/verify-tests.json" 2> "$BUILD/verify-ctest.log"; then
  cat "$BUILD/verify-ctest.log" >&2
  echo "verify.sh: ctest --show-only failed" >&2
  exit 2
fi

echo "== ctest -L verify -j4"
"$CTEST" --test-dir "$BUILD" -L verify -j4 --no-tests=error --output-on-failure \
         --output-junit "$BUILD/verify-junit.xml" 2>&1 | tee "$BUILD/verify-ctest.log"
CTEST_RC=${PIPESTATUS[0]}

python3 - "$BUILD" "$CTEST_RC" <<'PY'
import json, os, re, sys, xml.etree.ElementTree as ET

build, ctest_rc = sys.argv[1], int(sys.argv[2])
results_dir = os.path.join(build, "probe-results")
cand_dir = os.path.join(build, "golden-candidates")

with open(os.path.join(build, "verify-tests.json")) as f:
    listed = json.load(f).get("tests", [])

def props(t):
    out = {}
    for p in t.get("properties", []):
        out[p.get("name")] = p.get("value")
    return out

def arg_after(cmd, flag):
    for i, a in enumerate(cmd[:-1]):
        if a == flag:
            return cmd[i + 1]
    return None

junit = {}
jpath = os.path.join(build, "verify-junit.xml")
if os.path.exists(jpath):
    for tc in ET.parse(jpath).getroot().iter("testcase"):
        failure = tc.find("failure")
        junit[tc.get("name")] = {
            "status": tc.get("status", ""),
            "time": float(tc.get("time", "0") or 0),
            "message": (failure.get("message", "") if failure is not None else ""),
        }

def load_golden(path):
    rows = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                parts = line.split("\t")
                if len(parts) == 3:
                    rows[parts[0]] = (parts[1], parts[2])
    return rows

def improved_rows(cmd, res):
    """le:/ge: golden rows that passed but moved in the good direction (the candidate holds the measured rows)."""
    root, arch = arg_after(cmd, "--golden-root"), arg_after(cmd, "--arch")
    if not root or not arch:
        return []
    scope = "modes/" + res["mode"] if res.get("mode") else "global"
    name = res["probe"] + ".txt"
    golden = load_golden(os.path.join(root, "base", scope, name))
    golden.update(load_golden(os.path.join(root, arch, scope, name)))
    measured = load_golden(os.path.join(cand_dir, arch, scope, name))
    out = []
    for key, (value, tol) in measured.items():
        if key not in golden:
            continue
        gv, gt = golden[key]
        if not (gt.startswith("le:") or gt.startswith("ge:")) or gt != tol:
            continue
        try:
            got, want = float(value), float(gv)
        except ValueError:
            continue
        if (gt.startswith("le:") and got < want) or (gt.startswith("ge:") and got > want):
            out.append("%s: golden %s -> %s (%s)" % (key, gv, value, gt))
    return out

LAYERS = {"dsp", "proc", "ui"}
groups = {"PASS": [], "BLOCKING": [], "DRIFT": [], "MISSING": [], "IMPROVED": []}
times = []
for t in listed:
    name = t["name"]
    p = props(t)
    labels = set(p.get("LABELS") or [])
    j = junit.get(name)
    if j:
        times.append((j["time"], name))
    is_probe = bool(labels & LAYERS)
    if p.get("DISABLED"):
        groups["BLOCKING"].append((name, "disabled (the probes cannot run on this machine or the configuration is broken)"))
        continue
    if j is None or j["status"] in ("notrun", "disabled"):
        groups["BLOCKING"].append((name, "not run"))
        continue
    ok = j["status"] == "run"
    if not is_probe:
        if ok:
            groups["PASS"].append(name)
        else:
            groups["BLOCKING"].append((name, "failed" + (": " + j["message"] if j["message"] else "")))
        continue
    rpath = os.path.join(results_dir, name + ".json")
    res = None
    if os.path.exists(rpath):
        try:
            with open(rpath) as f:
                res = json.load(f)
        except (OSError, ValueError) as e:
            groups["BLOCKING"].append((name, "unreadable results %s: %s" % (rpath, e)))
            continue
    if res is None:
        groups["BLOCKING"].append((name, "no results file (crash or timeout%s)" % (": " + j["message"] if j["message"] else "")))
        continue
    status = res.get("status", "?")
    if status in ("spec_fail", "harness_error"):
        groups["BLOCKING"].append((name, "%s (spec_fail %s)" % (status, res.get("spec_fail"))))
    elif not ok:
        groups["BLOCKING"].append((name, "status %s but the process failed (crash after its results?%s)"
                                   % (status, " " + j["message"] if j["message"] else "")))
    elif status == "pass":
        groups["PASS"].append(name)
        for row in improved_rows(t.get("command", []), res):
            groups["IMPROVED"].append((name, row))
    elif status == "golden_drift":
        scope = "modes/" + res["mode"] if res.get("mode") else "global"
        diff = os.path.join(cand_dir, res.get("arch", "?"), scope, res["probe"] + ".diff")
        groups["DRIFT"].append((name, "%s rows differ, %s new, %s missing; %s" % (
            res.get("golden_fail"), res.get("golden_new"), res.get("golden_missing_rows"), diff)))
    elif status == "golden_missing":
        scope = "modes/" + res["mode"] if res.get("mode") else "global"
        groups["MISSING"].append((name, "%s rows; candidate in %s" % (
            res.get("golden_rows"), os.path.join(cand_dir, res.get("arch", "?"), scope))))
    else:
        groups["BLOCKING"].append((name, "unknown status '%s'" % status))

if ctest_rc != 0 and not groups["BLOCKING"]:
    groups["BLOCKING"].append(("ctest", "exited %d with no failing test recorded (see verify-ctest.log)" % ctest_rc))

print("")
print("== verify.sh summary: %d tests: PASS %d, BLOCKING %d, DRIFT %d, MISSING %d, IMPROVED rows %d" % (
    len(listed), len(groups["PASS"]), len(groups["BLOCKING"]), len(groups["DRIFT"]), len(groups["MISSING"]),
    len(groups["IMPROVED"])))
for g in ("BLOCKING", "DRIFT", "MISSING", "IMPROVED"):
    for name, why in groups[g]:
        print("   %-9s %-40s %s" % (g, name, why))
print("== 10 slowest tests")
for sec, name in sorted(times, reverse=True)[:10]:
    print("   %8.2f s  %s" % (sec, name))
if groups["BLOCKING"]:
    sys.exit(1)
sys.exit(3 if (groups["DRIFT"] or groups["MISSING"]) else 0)
PY
RC=$?

case "$RC" in
  0)
    SHA="$(git -C "$SRC" rev-parse -q --verify 'HEAD^{commit}' 2>/dev/null || true)"
    DIRTY="$(git -C "$SRC" status --porcelain 2>/dev/null | head -1)"
    if [ -n "$SHA" ] && [ -z "$DIRTY" ]; then
      {
        echo "verify.sh: fully green at $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "source $SRC @ $SHA"
        cat "$DEPS"
      } > "$BUILD/verify-passed-$SHA"
      echo "== fully green: wrote $BUILD/verify-passed-$SHA"
    else
      echo "== fully green (no stamp: the source tree has uncommitted changes)"
    fi
    exit 0 ;;
  3)
    echo "== no blocking results; the DRIFT/MISSING groups above need a one-line reason each in the handoff"
    exit 0 ;;
  *)
    echo "== BLOCKING results: verify.sh fails"
    exit 1 ;;
esac
