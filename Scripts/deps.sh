#!/usr/bin/env bash
#
# Scripts/deps.sh -- populate and verify the machine-wide, read-only dependency cache ~/audio/.deps
# (docs/design/03-build-verify-process.md §1.3, §2.4, §2.5, §4.8; DECISIONS.md ADR-36, ADR-37, Q8).
#
#   Scripts/deps.sh                   clone JUCE and bgfx.cmake (with submodules) from upstream
#   Scripts/deps.sh --seed-from-hr    copy them out of HardwareReverb's build/_deps instead (a read, never a dependency)
#   Scripts/deps.sh --seed-from DIR   copy them out of DIR (a FetchContent _deps dir: juce-src, bgfx-src; or another .deps)
#   Scripts/deps.sh --verify          check an existing cache and print DEPS.lock; writes nothing
#
#   --deps-dir DIR    the cache (default: $FCOMPRESSOR_DEPS_DIR, else ~/audio/.deps)
#   --jobs N          build parallelism for shaderc and pluginval (default: all cores)
#   --no-pluginval    skip pluginval (e.g. offline); its DEPS.lock row is kept only if it is already built
#
# What it produces (idempotent: every step is skipped when its result is present and verified):
#   JUCE-8.0.4/                         HEAD 51d11a2b...; chmod -R a-w
#   bgfx.cmake-v1.153.9385-561/         HEAD 99752df3..., submodules bgfx/bx/bimg at the pinned SHAs; chmod -R a-w
#   tools/shaderc-<bgfx tag>/shaderc    Release, host arch, HR's bgfx tool options; shaderc.stamp = "bgfx.cmake <sha>"
#   tools/pluginval-<tag>/pluginval     symlink to pluginval.app/Contents/MacOS/pluginval (Linux: the executable);
#                                       pluginval.stamp
#   build/                              scratch builds and the pluginval source clone (deletable)
#   DEPS.lock                           name<TAB>tag<TAB>sha<TAB>populated-at (UTC)
#
# Any pinned-SHA mismatch deletes the offending checkout and exits 1. Nothing in a build may write into the source
# trees; the shaderc build checks that. This script is the only writer of the cache.
#
# macOS and Linux (ADR-91). On Linux the copies are cp -a instead of ditto, and pluginval is a plain executable rather
# than an .app bundle. The tools build with CMake's default compiler on either (they are not FCompressor's code).

set -euo pipefail

# ---- pins (03 §1.3, §2.3; pluginval: latest upstream release, pinned by tag and SHA) --------------------------------
JUCE_TAG=8.0.4
JUCE_SHA=51d11a2be6d5c97ccf12b4e5e827006e19f0555a
JUCE_URL=https://github.com/juce-framework/JUCE.git
BGFX_TAG=v1.153.9385-561
BGFX_SHA=99752df38e40179cf998bb880fe4c16c0b3d60ca
BGFX_URL=https://github.com/bkaradzic/bgfx.cmake.git
BGFX_SUBS="bgfx=c7684e20da1e385edc439ef39cdb42b8c661016f bx=0b001f5f36579e8aea07efa5af139ca18dad9505 bimg=3b4baab0128ac499c5c3bc37202781bf54084049"
PLUGINVAL_TAG=v1.0.4
PLUGINVAL_SHA=ed19c2c16b57a6d94db391bea3ef4a80b769d5bf            # commit the annotated tag points to
PLUGINVAL_JUCE_TAG=8.0.3                                           # pluginval's own modules/juce submodule
PLUGINVAL_JUCE_SHA=5179f4e720d8406ebd1b5401c86aea8da6cc83c9
PLUGINVAL_URL=https://github.com/Tracktion/pluginval.git
HR_DEPS="$HOME/audio/plugins/HardwareReverb/build/_deps"           # read-only; only ever copied out of

# ---- arguments -----------------------------------------------------------------------------------------------------
DEPS="${FCOMPRESSOR_DEPS_DIR:-$HOME/audio/.deps}"
SEED=""
MODE=populate
WITH_PLUGINVAL=1
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)"

usage() { sed -n '3,13p' "$0" | sed 's/^# \{0,1\}//'; }
while [ $# -gt 0 ]; do
  case "$1" in
    --seed-from-hr) SEED="$HR_DEPS" ;;
    --seed-from)    [ $# -ge 2 ] || { usage >&2; exit 2; }; SEED="$2"; shift ;;
    --verify)       MODE=verify ;;
    --deps-dir)     [ $# -ge 2 ] || { usage >&2; exit 2; }; DEPS="$2"; shift ;;
    --jobs)         [ $# -ge 2 ] || { usage >&2; exit 2; }; JOBS="$2"; shift ;;
    --no-pluginval) WITH_PLUGINVAL=0 ;;
    -h|--help)      usage; exit 0 ;;
    *)              echo "deps.sh: unknown argument '$1'" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

# Never let a caller's git environment redirect the checks; never let git refresh an index opportunistically.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_CEILING_DIRECTORIES
export GIT_OPTIONAL_LOCKS=0

log()  { printf '[deps] %s\n' "$*"; }
warn() { printf '[deps] %s\n' "$*" >&2; }
die()  { printf '[deps] ERROR: %s\n' "$*" >&2; exit 1; }
now_utc() { date -u +%Y-%m-%dT%H:%M:%SZ; }
phys() { (cd "$1" && pwd -P); }

[ "$MODE" = verify ] || mkdir -p "$DEPS" || die "cannot create $DEPS"
[ -d "$DEPS" ] || die "$DEPS does not exist"
DEPS="$(phys "$DEPS")"

JUCE_DIR="$DEPS/JUCE-$JUCE_TAG"
BGFX_DIR="$DEPS/bgfx.cmake-$BGFX_TAG"
TOOLS="$DEPS/tools"
SCRATCH="$DEPS/build"
LOCK="$DEPS/DEPS.lock"
SHADERC_DIR="$TOOLS/shaderc-$BGFX_TAG"
SHADERC_BIN="$SHADERC_DIR/shaderc"
SHADERC_STAMP_TEXT="bgfx.cmake $BGFX_SHA"
PV_DIR="$TOOLS/pluginval-$PLUGINVAL_TAG"
PV_BIN="$PV_DIR/pluginval"
PV_STAMP_TEXT="pluginval $PLUGINVAL_TAG $PLUGINVAL_SHA juce $PLUGINVAL_JUCE_SHA"

# Host architecture, even when this shell runs under Rosetta.
ARCH="$(uname -m)"
[ "$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)" = 1 ] && ARCH=arm64
[ "$ARCH" = aarch64 ] && ARCH=arm64                                  # Linux's name for it

# macOS copies with ditto (it keeps what cp may not on APFS); Linux with cp -a.
if [ "$(uname -s)" = Darwin ]; then
  copy_tree() { ditto "$1" "$2"; }
else
  copy_tree() { cp -a "$1" "$2"; }
fi

# Delete a path, but only inside the cache (read-only trees need u+w first).
rm_tree() {
  case "$1" in "$DEPS"/?*) ;; *) die "refusing to delete '$1' (outside $DEPS)" ;; esac
  if [ -e "$1" ] || [ -L "$1" ]; then chmod -R u+w "$1" 2>/dev/null || true; rm -rf "$1"; fi
}

# ---- verification --------------------------------------------------------------------------------------------------
# check_repo DIR SHA LABEL ROOT: DIR must be its own repository root, keep its git dir inside ROOT (so a copied tree
# never resolves back to where it was copied from), and have HEAD == SHA. Read-only git commands only.
check_repo() {
  local dir=$1 want=$2 label=$3 root=$4 top gitdir head
  top="$(git -C "$dir" rev-parse --show-toplevel 2>/dev/null)" || { warn "  $label: $dir is not a git checkout"; return 1; }
  [ "$(phys "$top")" = "$(phys "$dir")" ] || { warn "  $label: $dir is not its own repository root ($top)"; return 1; }
  gitdir="$(git -C "$dir" rev-parse --absolute-git-dir 2>/dev/null)" || { warn "  $label: no git dir"; return 1; }
  case "$(phys "$gitdir")/" in "$(phys "$root")"/*) ;;
    *) warn "  $label: git dir $gitdir lies outside $root"; return 1 ;; esac
  head="$(git -C "$dir" rev-parse -q --verify 'HEAD^{commit}' 2>/dev/null)" || { warn "  $label: no HEAD"; return 1; }
  [ "$head" = "$want" ] || { warn "  $label: HEAD $head != pinned $want"; return 1; }
  log "  $label $head ok"
}
verify_juce() { check_repo "$1" "$JUCE_SHA" "JUCE" "$1"; }
verify_bgfx() {
  local dir=$1 kv ok=0
  check_repo "$dir" "$BGFX_SHA" "bgfx.cmake" "$dir" || ok=1
  for kv in $BGFX_SUBS; do check_repo "$dir/${kv%%=*}" "${kv#*=}" "bgfx.cmake/${kv%%=*}" "$dir" || ok=1; done
  return $ok
}
verify_pluginval_src() {
  local ok=0
  check_repo "$1" "$PLUGINVAL_SHA" "pluginval" "$1" || ok=1
  check_repo "$1/modules/juce" "$PLUGINVAL_JUCE_SHA" "pluginval/modules/juce" "$1" || ok=1
  return $ok
}
# The working tree (submodules included) must equal HEAD, with no untracked or ignored files either: the SHA alone
# does not prove a seeded copy is unmodified.
check_clean() {
  local out
  out="$(git -C "$2" status --porcelain --untracked-files=all --ignored=matching --ignore-submodules=none)" \
    || { warn "  $1: git status failed"; return 1; }
  [ -z "$out" ] || { warn "  $1: working tree differs from HEAD:"; printf '%s\n' "$out" | head -20 >&2; return 1; }
  log "  $1 working tree clean"
}
check_readonly() {
  local w
  w="$(find "$1" ! -type l \( -perm -u+w -o -perm -g+w -o -perm -o+w \) -print -quit 2>/dev/null)"
  [ -z "$w" ] || { warn "  $(basename "$1"): writable path $w"; return 1; }
  log "  $(basename "$1") read-only ok"
}
stamp_is() { [ -f "$1" ] && [ "$(cat "$1")" = "$2" ]; }
shaderc_ok()   { [ -x "$SHADERC_BIN" ] && stamp_is "$SHADERC_DIR/shaderc.stamp" "$SHADERC_STAMP_TEXT"; }
pluginval_ok() { [ -x "$PV_BIN" ] && stamp_is "$PV_DIR/pluginval.stamp" "$PV_STAMP_TEXT"; }

# ---- DEPS.lock -----------------------------------------------------------------------------------------------------
# A row keeps its populated-at from the previous lock unless this run (re)populated it.
T_JUCE=""; T_BGFX=""; T_SHADERC=""; T_PLUGINVAL=""
lock_row() {  # NAME TAG SHA FRESH_TIME
  local at=$4
  if [ -z "$at" ] && [ -f "$LOCK" ]; then
    at="$(awk -F'\t' -v n="$1" -v t="$2" -v s="$3" '$1==n && $2==t && $3==s {print $4; exit}' "$LOCK")"
  fi
  printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "${at:-$(now_utc)}"
}
write_lock() {
  local tmp="$DEPS/.DEPS.lock.tmp" kv
  {
    printf '# name\ttag\tsha\tpopulated-at (UTC). Written by FCompressor Scripts/deps.sh; do not edit.\n'
    lock_row JUCE "$JUCE_TAG" "$JUCE_SHA" "$T_JUCE"
    lock_row bgfx.cmake "$BGFX_TAG" "$BGFX_SHA" "$T_BGFX"
    for kv in $BGFX_SUBS; do lock_row "bgfx.cmake/${kv%%=*}" "$BGFX_TAG" "${kv#*=}" "$T_BGFX"; done
    if shaderc_ok; then lock_row shaderc "$BGFX_TAG" "$BGFX_SHA" "$T_SHADERC"; fi
    if pluginval_ok; then
      lock_row pluginval "$PLUGINVAL_TAG" "$PLUGINVAL_SHA" "$T_PLUGINVAL"
      lock_row pluginval/modules/juce "$PLUGINVAL_JUCE_TAG" "$PLUGINVAL_JUCE_SHA" "$T_PLUGINVAL"
    fi
  } > "$tmp"
  mv -f "$tmp" "$LOCK"
  log "wrote $LOCK"
}

# ---- --verify: check only, write nothing ---------------------------------------------------------------------------
if [ "$MODE" = verify ]; then
  bad=0
  log "verifying $DEPS"
  if [ -d "$JUCE_DIR" ]; then verify_juce "$JUCE_DIR" && check_readonly "$JUCE_DIR" || bad=1
  else warn "  missing $JUCE_DIR"; bad=1; fi
  if [ -d "$BGFX_DIR" ]; then verify_bgfx "$BGFX_DIR" && check_readonly "$BGFX_DIR" || bad=1
  else warn "  missing $BGFX_DIR"; bad=1; fi
  if shaderc_ok && "$SHADERC_BIN" --version >/dev/null 2>&1; then log "  shaderc ok: $("$SHADERC_BIN" --version 2>&1 | head -1)"
  else warn "  shaderc missing, unstamped or not runnable: $SHADERC_BIN"; bad=1; fi
  if pluginval_ok && "$PV_BIN" --version >/dev/null 2>&1; then log "  pluginval ok: $("$PV_BIN" --version 2>&1 | head -1)"
  else warn "  pluginval missing, unstamped or not runnable: $PV_BIN"; [ "$WITH_PLUGINVAL" = 0 ] || bad=1; fi
  if [ -f "$LOCK" ]; then cat "$LOCK"; else warn "  missing $LOCK"; bad=1; fi
  [ "$bad" = 0 ] && log "cache OK" || die "cache verification failed"
  exit 0
fi

# ---- populate ------------------------------------------------------------------------------------------------------
command -v git   >/dev/null || die "git not found"
command -v cmake >/dev/null || die "cmake not found"
command -v ninja >/dev/null || die "ninja not found"
if [ -n "$SEED" ]; then [ -d "$SEED" ] || die "seed directory $SEED does not exist"; SEED="$(phys "$SEED")"; fi

RUNNING="$DEPS/.deps-sh.running"
mkdir "$RUNNING" 2>/dev/null || die "another deps.sh is running on $DEPS (if not, rmdir $RUNNING)"
trap 'rmdir "$RUNNING" 2>/dev/null || true' EXIT
mkdir -p "$TOOLS" "$SCRATCH"
log "cache $DEPS, host arch $ARCH, jobs $JOBS${SEED:+, seed $SEED}"

# Shallow clone with git's automatic maintenance off: git >= 2.47 detaches `maintenance run --auto`, which can keep
# writing pack temporaries into a clone after it has been verified and moved (seen with pluginval's JUCE submodule).
# `-c` settings reach the submodule clones through GIT_CONFIG_PARAMETERS.
git_clone() { git -c advice.detachedHead=false -c gc.auto=0 -c maintenance.auto=false clone --quiet --depth 1 "$@"; }

# seed_dir CANDIDATE...: the first existing checkout under $SEED.
seed_dir() { local c; for c in "$@"; do [ -d "$SEED/$c" ] && { printf '%s\n' "$SEED/$c"; return 0; }; done; return 1; }

# populate LABEL FINAL VERIFY_FN URL TAG SUBMODULES(0|1) SEED_CANDIDATES...  Sets FRESH=1 if it (re)populated FINAL.
# Always called as a plain command (never in an `if`), so `set -e` stays in force inside it.
FRESH=0
populate() {
  local label=$1 final=$2 verify=$3 url=$4 tag=$5 subs=$6 src="" tmp
  shift 6
  FRESH=0
  if [ -e "$final" ]; then
    log "$label: present at $final"
    if "$verify" "$final"; then return 0; fi
    rm_tree "$final"
    die "$label: $final does not match the pins; deleted it. Rerun deps.sh."
  fi
  tmp="$DEPS/.incoming-$(basename "$final")"
  rm_tree "$tmp"
  if [ -n "$SEED" ]; then
    src="$(seed_dir "$@")" || die "$label: no checkout under $SEED (looked for: $*)"
    log "$label: copying $src (.git and submodule git dirs included)"
    copy_tree "$src" "$tmp"
    # Finder litter is not part of any checkout. Deleting a *tracked* .DS_Store would fail the clean check below.
    find "$tmp" -name .DS_Store -type f -print -delete | sed "s|^$tmp/|[deps]   removed Finder file |"
  else
    log "$label: cloning $url @ $tag"
    if [ "$subs" = 1 ]; then
      git_clone --branch "$tag" \
        --recurse-submodules --shallow-submodules "$url" "$tmp"
    else
      git_clone --branch "$tag" "$url" "$tmp"
    fi
  fi
  if ! "$verify" "$tmp" || ! check_clean "$label" "$tmp"; then
    rm_tree "$tmp"
    die "$label: ${src:-$url} does not match the pins or is not clean; deleted the copy."
  fi
  mv "$tmp" "$final"
  "$verify" "$final" >/dev/null || { rm_tree "$final"; die "$label: verification failed after the move; deleted."; }
  log "$label: populated $final"
  FRESH=1
}

populate JUCE "$JUCE_DIR" verify_juce "$JUCE_URL" "$JUCE_TAG" 0 juce-src "JUCE-$JUCE_TAG" JUCE
if [ "$FRESH" = 1 ]; then T_JUCE="$(now_utc)"; fi
populate bgfx.cmake "$BGFX_DIR" verify_bgfx "$BGFX_URL" "$BGFX_TAG" 1 bgfx-src "bgfx.cmake-$BGFX_TAG" bgfx.cmake
if [ "$FRESH" = 1 ]; then T_BGFX="$(now_utc)"; fi

# run_logged LOGFILE CMD...: full output to the log; its tail on failure.
run_logged() {
  local logf=$1; shift
  if ! "$@" >>"$logf" 2>&1; then warn "command failed: $*"; tail -40 "$logf" >&2; warn "full log: $logf"; return 1; fi
}

# ---- shaderc (03 §2.5): Release, host arch, the bgfx tool options of HR's CMakeLists.txt, stamped with the SHA ------
build_shaderc() {
  local b="$SCRATCH/shaderc-$BGFX_TAG" logf="$SCRATCH/shaderc-$BGFX_TAG.log" marker="$SCRATCH/.shaderc-start"
  local t0 t1 written out tmp
  if shaderc_ok; then log "shaderc: present, stamp ok ($SHADERC_BIN)"; return 0; fi
  log "shaderc: building (Release, $ARCH) in $b; log $logf"
  : > "$logf"; touch "$marker"; t0=$(date +%s)
  run_logged "$logf" cmake -S "$BGFX_DIR" -B "$b" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    -DBGFX_BUILD_TOOLS=ON -DBGFX_BUILD_TOOLS_SHADER=ON -DBGFX_BUILD_TOOLS_TEXTURE=OFF \
    -DBGFX_BUILD_TOOLS_GEOMETRY=OFF -DBGFX_BUILD_TOOLS_BIN2C=OFF -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_INSTALL=OFF \
    || die "shaderc: configure failed"
  run_logged "$logf" cmake --build "$b" --target shaderc -j "$JOBS" || die "shaderc: build failed"
  t1=$(date +%s)
  written="$(find "$BGFX_DIR" -newer "$marker" -print -quit)"
  [ -z "$written" ] || die "shaderc: the build wrote into the read-only source tree ($written)"
  out="$b/cmake/bgfx/shaderc"
  [ -x "$out" ] || out="$(find "$b" -type f -name shaderc -perm -u+x -print -quit)"
  [ -n "$out" ] && [ -x "$out" ] || die "shaderc: built binary not found under $b"
  "$out" --version >/dev/null 2>&1 || die "shaderc: $out --version failed"
  tmp="$TOOLS/.incoming-shaderc-$BGFX_TAG"
  rm_tree "$tmp"; mkdir -p "$tmp"
  cp "$out" "$tmp/shaderc"
  printf '%s\n' "$SHADERC_STAMP_TEXT" > "$tmp/shaderc.stamp"         # last: the stamp certifies a complete tool
  rm_tree "$SHADERC_DIR"; mv "$tmp" "$SHADERC_DIR"
  T_SHADERC="$(now_utc)"
  log "shaderc: built in $((t1 - t0)) s wall -> $SHADERC_BIN"
}
build_shaderc

write_lock
log "chmod -R a-w $JUCE_DIR $BGFX_DIR"
chmod -R a-w "$JUCE_DIR" "$BGFX_DIR"
check_readonly "$JUCE_DIR" || die "JUCE tree is not read-only"
check_readonly "$BGFX_DIR" || die "bgfx.cmake tree is not read-only"

# ---- pluginval (03 §4.8): pinned upstream tag, Release, host arch; the .app bundle is kept intact --------------------
build_pluginval() {
  local src="$SCRATCH/pluginval-src-$PLUGINVAL_TAG" b="$SCRATCH/pluginval-$PLUGINVAL_TAG"
  local logf="$SCRATCH/pluginval-$PLUGINVAL_TAG.log" tmp app t0 t1
  if pluginval_ok; then log "pluginval: present, stamp ok ($PV_BIN)"; return 0; fi
  if [ -e "$src" ]; then
    log "pluginval: source present at $src"
    verify_pluginval_src "$src" || { rm_tree "$src"; die "pluginval: $src does not match the pins; deleted it."; }
  else
    tmp="$SCRATCH/.incoming-pluginval-src-$PLUGINVAL_TAG"
    rm_tree "$tmp"
    log "pluginval: cloning $PLUGINVAL_URL @ $PLUGINVAL_TAG (with its JUCE submodule)"
    git_clone --branch "$PLUGINVAL_TAG" \
      --recurse-submodules --shallow-submodules "$PLUGINVAL_URL" "$tmp"
    verify_pluginval_src "$tmp" || { rm_tree "$tmp"; die "pluginval: clone does not match the pins; deleted it."; }
    mv "$tmp" "$src"
  fi
  log "pluginval: building (Release, $ARCH) in $b; log $logf"
  : > "$logf"; t0=$(date +%s)
  run_logged "$logf" cmake -S "$src" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    || die "pluginval: configure failed"
  run_logged "$logf" cmake --build "$b" --target pluginval -j "$JOBS" || die "pluginval: build failed"
  t1=$(date +%s)
  tmp="$TOOLS/.incoming-pluginval-$PLUGINVAL_TAG"
  rm_tree "$tmp"; mkdir -p "$tmp"
  if [ "$(uname -s)" = Darwin ]; then
    app="$b/pluginval_artefacts/Release/pluginval.app"
    [ -x "$app/Contents/MacOS/pluginval" ] || app="$(find "$b" -type d -name pluginval.app -print -quit)"
    [ -n "$app" ] && [ -x "$app/Contents/MacOS/pluginval" ] || die "pluginval: pluginval.app not found under $b"
    copy_tree "$app" "$tmp/pluginval.app"
    ln -s pluginval.app/Contents/MacOS/pluginval "$tmp/pluginval"
  else
    app="$b/pluginval_artefacts/Release/pluginval"
    [ -x "$app" ] || app="$(find "$b" -type f -name pluginval -perm -u+x -print -quit)"
    [ -n "$app" ] && [ -x "$app" ] || die "pluginval: the pluginval executable was not found under $b"
    cp "$app" "$tmp/pluginval"
  fi
  "$tmp/pluginval" --version >/dev/null 2>&1 || die "pluginval: $tmp/pluginval --version failed"
  printf '%s\n' "$PV_STAMP_TEXT" > "$tmp/pluginval.stamp"
  rm_tree "$PV_DIR"; mv "$tmp" "$PV_DIR"
  T_PLUGINVAL="$(now_utc)"
  log "pluginval: built in $((t1 - t0)) s wall -> $PV_BIN"
}
if [ "$WITH_PLUGINVAL" = 1 ]; then
  build_pluginval
  write_lock
else
  log "pluginval: skipped (--no-pluginval)"
fi

log "done"
cat "$LOCK"
