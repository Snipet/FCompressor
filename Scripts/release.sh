#!/bin/sh
#
# Scripts/release.sh [--test-bypass=tag[,stamp]] <build-dir> [<out-dir>]: sign, notarise and package (03 §5; S13.3).
# Scripts/release.sh --self-test: exercise every refusal in a scratch clone (<checkout>/build-selftest).
#
#   <build-dir>  a built `release` preset tree (GPU, Release, LTO, arm64: the v1 default, DECISIONS Q6), or `universal`.
#   <out-dir>    default <build-dir>/dist. Output: <out-dir>/FCompressor-<version>/ with FCompressor.component.zip,
#                FCompressor.vst3.zip, FCompressor.app.zip and MANIFEST.txt. An <out-dir> inside the checkout must be
#                git-ignored (e.g. out/), or the next run refuses the dirty tree.
#   FCMP_SIGNING_IDENTITY  "Developer ID Application: <name> (<team>)" in the login keychain. Empty or unset: ad-hoc
#                          signing, a dry run of every step except Apple's (not distributable: Gatekeeper refuses it).
#   FCMP_NOTARY_PROFILE    an `xcrun notarytool store-credentials` profile. Empty or unset: no notarisation. It uses the
#                          network and needs FCMP_SIGNING_IDENTITY (Apple does not notarise ad-hoc signatures).
#
# 1. Pre-flight. Every check runs and prints "ok" or "REFUSED <id>"; one refusal stops the release before anything is
#    signed. The source tree is the build's CMAKE_HOME_DIRECTORY; HEAD is its commit.
#      config        CMAKE_BUILD_TYPE is not Release
#      release-flag  FCOMPRESSOR_RELEASE is not ON (CMakeCache)
#      editor        a Generic editor: FCOMPRESSOR_HEADLESS or FCOMPRESSOR_DSP_ONLY is ON, or the FCompressor target
#                    is not compiled with FCOMPRESSOR_GPU_EDITOR=1 (a compile definition, read from build.ninja; D22)
#      arch          `lipo -archs` of a main binary is not arm64 (arm64 + x86_64 when FCOMPRESSOR_UNIVERSAL=ON; K2 #15)
#      x86-verify    a universal build without <src>/build-lead-x86/verify-passed-<HEAD>: the SSE backend must have run
#                    the verify suite under Rosetta (ADR-47)
#      override      fcmp-deps.txt shows a dependency override (K2 #26c)
#      tree          the source tree has uncommitted or untracked changes
#      tag           HEAD is not tagged v<project version>                                        [--test-bypass=tag]
#      stamp         no <src>/build-lead/verify-passed-<HEAD> (Scripts/verify.sh), or its last "validate:" line is not
#                    "validate: pass" (validate.sh; "validate --vst3-only" never counts; K2 #19)  [--test-bypass=stamp]
#      bundles       <build>/built-from-plugin.txt does not name HEAD and a clean tree (the bundles are not HEAD's)
#      exports       `nm -gU` of a main binary exports more than its entry points (K2 #26f): AU JuceAUFactory and the
#                    Info.plist factoryFunction; VST3 GetPluginFactory, bundleEntry, bundleExit; Standalone none
#      provisional   the release registry check: builds fcmp_probe_dsp in <build> and runs dsp.registry (ctest), which
#                    fails on a provisional Mode when FCOMPRESSOR_RELEASE is ON; its "provisional" list must be []
# 2. Clean each bundle: chflags -R nouchg (codesigned installs pick up immutable flags; B §6.2); chmod -R u+w (files
#    copied read-only from ~/audio/.deps, such as the bgfx licences, make xattr -c fail); xattr -cr.
# 3. Sign each bundle: codesign --force --options runtime --entitlements Resources/FCompressor.entitlements --timestamp
#    --sign <identity> (ad-hoc: --sign -); codesign --verify --strict; print the entitlement, identifier and team.
# 4. Package: ditto -c -k --keepParent into <out-dir>/FCompressor-<version>/.
# 5. Notarise (FCMP_NOTARY_PROFILE only), per zip: notarytool submit --wait (must be Accepted), stapler staple and
#    validate, codesign --verify --strict again, re-zip the stapled bundle.
# 6. MANIFEST.txt: version, git SHA and tag, signing, notarisation, the verify/validate stamp lines, the dependency tags
#    and SHAs (fcmp-deps.txt), the flags line (generated/fcmp/BuildInfo.cpp) and SHA256SUMS of the zips.
# 7. Print the post-flight checklist (not automated).
#
# --test-bypass=tag,stamp (either or both; nothing else can be bypassed) reports those refusals as BYPASSED, for the
# ad-hoc dry run of an untagged, unverified tree. It is refused with FCMP_SIGNING_IDENTITY or FCMP_NOTARY_PROFILE set,
# and MANIFEST.txt records it. --self-test covers it.
#
# --self-test clones this checkout's HEAD (git clone --depth 1) into <checkout>/build-selftest/repo, commits this script
# and a miniature fixture build there (three bundles exporting exactly the entry points, the generated files this script
# reads, a stand-in dsp.registry probe), and runs one case per refusal and usage error plus the passing runs (arm64,
# universal with the x86 stamp, the test bypass, and a simulated Developer ID + notarisation) through `codesign` and
# `xcrun` shims: the real codesign only ever signs ad-hoc (a fake identity is re-signed ad-hoc), and the real notarytool
# and stapler never run (the shim answers two fake profiles). It never touches the real repository, the keychain or the
# network.
#
# Exit: 0 released (or every self-test case passed); 1 refused by the pre-flight (or a self-test case failed);
# 2 usage or setup error; 3 a signing, packaging or notarisation step failed.
set -eu

PROG=release.sh
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE

say() { printf '%s\n' "$*"; }
die() {  # die <exit code> <message...>
  _rc=$1
  shift
  printf '%s: %s\n' "$PROG" "$*" >&2
  exit "$_rc"
}
usage() { awk 'NR == 1 { next } !/^#/ { exit } { sub(/^# ?/, ""); print }' "$0"; }
step_fail() { die 3 "step failed: $*"; }

# Signing, notarisation, lipo and the AU are Apple's; a Linux build (ADR-91) has no release path yet.
[ "$(uname -s)" = Darwin ] || die 2 "release.sh signs and notarises macOS bundles; it runs on macOS only (ADR-91)"
is_on() {  # is_on <value>: CMake's true constants
  case $(printf '%s' "$1" | tr '[:lower:]' '[:upper:]') in
    ON | TRUE | YES | Y | 1) return 0 ;;
    *) return 1 ;;
  esac
}
sorted_words() { tr ' ' '\n' | sed '/^$/d' | sort -u | tr '\n' ' ' | sed 's/ $//'; }

# ---- the release -----------------------------------------------------------------------------------------------------

cache_get() {  # cache_get <NAME>: the value of NAME:<TYPE>=<value> in the CMakeCache
  sed -n "s/^$1:[A-Z]*=//p" "$CACHE" | head -n 1
}
product_const() {  # product_const kName -> FCompressor (generated/FcmpProduct.h, as validate.sh reads it)
  sed -n "s/.*char $1\[\] *= *\"\([^\"]*\)\".*/\1/p" "$PRODUCT_H" | head -n 1
}
git_src() { git --no-optional-locks -C "$SRC" "$@"; }

check_ok() { printf '   ok       %-12s %s\n' "$1" "$2"; }
refuse() {  # refuse <id> <message>: a refusal, or a BYPASSED line when --test-bypass names <id>
  case " $BYPASS " in
    *" $1 "*)
      printf '   BYPASSED %s: %s (--test-bypass)\n' "$1" "$2"
      BYPASSED="$BYPASSED $1"
      ;;
    *)
      printf '   REFUSED  %s: %s\n' "$1" "$2"
      REFUSED="$REFUSED $1"
      ;;
  esac
}

gpu_editor_compiled() {  # the <name> target compiles with FCOMPRESSOR_GPU_EDITOR=1 and without CreateEditorGeneric.cpp
  awk -v t="CMakeFiles/$NAME.dir/" '
    /^build / { in_t = (index($2, t) == 1); if (in_t && $2 ~ /CreateEditorGeneric/) generic = 1; next }
    /^[^ ]/   { in_t = 0; next }
    in_t && /^  DEFINES = / && / -DFCOMPRESSOR_GPU_EDITOR=1( |$)/ { gpu = 1 }
    END { exit (gpu && !generic) ? 0 : 1 }' "$BUILD/build.ninja"
}

entry_points() {  # entry_points <bundle>: the symbols its main binary may export (nm names)
  case $1 in
    *.component)
      say _JuceAUFactory
      _ff=$(/usr/libexec/PlistBuddy -c 'Print :AudioComponents:0:factoryFunction' "$1/Contents/Info.plist" \
        2> /dev/null || true)
      [ -z "$_ff" ] || say "_$_ff"
      ;;
    *.vst3) printf '%s\n' _GetPluginFactory _bundleEntry _bundleExit ;;
    *.app) say __mh_execute_header ;;
  esac
}
extra_exports() {  # extra_exports <bundle>: exported symbols (every slice) that are not entry points
  _bin=$1/Contents/MacOS/$NAME
  entry_points "$1" > "$TMPD/allowed"
  _slices=$(lipo -archs "$_bin" 2> /dev/null) || {
    say "(lipo -archs failed: not a Mach-O binary)"
    return 0
  }
  for _a in $_slices; do
    nm -gUj -arch "$_a" "$_bin" || say "(nm -gU -arch $_a failed)"
  done | sed -e '/^$/d' -e '/ (for architecture .*):$/d' | sort -u | grep -vxF -f "$TMPD/allowed" || true
}

sign_one() {  # sign_one <bundle>: clean, sign, verify and describe one bundle
  say "-- $(basename "$1")"
  chflags -R nouchg "$1" || step_fail "chflags -R nouchg $1"
  chmod -R u+w "$1" || step_fail "chmod -R u+w $1"   # xattr -c fails on read-only files (the bgfx licences)
  xattr -cr "$1" || step_fail "xattr -cr $1"
  if [ -n "$ID" ]; then
    _s=$(codesign --force --options runtime --entitlements "$ENT" --timestamp --sign "$ID" "$1" 2>&1) || {
      printf '%s\n' "$_s" | sed 's/^/      /'
      step_fail "codesign --sign '$ID' $1"
    }
  else
    _s=$(codesign --force --options runtime --entitlements "$ENT" --sign - "$1" 2>&1) || {
      printf '%s\n' "$_s" | sed 's/^/      /'
      step_fail "codesign --sign - $1"
    }
  fi
  verify_one "$1"
  # codesign embeds entitlements in executables only: the Standalone must carry audio-input (the hardened runtime blocks
  # the microphone otherwise); in the AU and VST3 (loadable bundles) the host's entitlements apply.
  _ents=$(codesign -d --entitlements - "$1" 2> /dev/null || true)
  case $_ents in
    *com.apple.security.device.audio-input*) say "      entitlement: com.apple.security.device.audio-input" ;;
    *)
      case $1 in
        *.app) step_fail "$1 was signed without com.apple.security.device.audio-input ($ENT)" ;;
        *) say "      entitlement: none embedded (a loadable bundle: the host's entitlements apply)" ;;
      esac
      ;;
  esac
  _desc=$(codesign -dv "$1" 2>&1 || true)
  case $_desc in
    *"flags="*runtime*) ;;
    *) step_fail "$1 is not signed with the hardened runtime" ;;
  esac
  printf '%s\n' "$_desc" | grep -E '^(Identifier|TeamIdentifier|Authority|Signature|Timestamp|CodeDirectory)' |
    sed 's/^/      /' || true
}
verify_one() {  # verify_one <bundle>: codesign --verify --strict, printed
  if ! _v=$(codesign --verify --strict --verbose=2 "$1" 2>&1); then
    printf '%s\n' "$_v" | sed 's/^/      /'
    step_fail "codesign --verify --strict $1"
  fi
  printf '%s\n' "$_v" | sed 's/^/      /'
}
zip_one() {  # zip_one <bundle> <zip>
  rm -f "$2"
  ditto -c -k --keepParent "$1" "$2" || step_fail "ditto -c -k --keepParent $1 $2"
  say "      -> $2 ($(du -h "$2" | cut -f1 | tr -d ' '))"
}
notarise_one() {  # notarise_one <bundle> <zip>
  say "-- notarise $(basename "$1")"
  if ! _n=$(xcrun notarytool submit "$2" --keychain-profile "$PROFILE" --wait 2>&1); then
    printf '%s\n' "$_n" | sed 's/^/      /'
    step_fail "xcrun notarytool submit $2"
  fi
  printf '%s\n' "$_n" | sed 's/^/      /'
  printf '%s\n' "$_n" | grep -Eq '^[[:space:]]*status: Accepted' ||
    step_fail "$2 was not Accepted by the notary service (xcrun notarytool log <id> --keychain-profile $PROFILE)"
  for _act in staple validate; do
    _o=$(xcrun stapler "$_act" "$1" 2>&1) || {
      printf '%s\n' "$_o" | sed 's/^/      /'
      step_fail "xcrun stapler $_act $1"
    }
    printf '%s\n' "$_o" | sed 's/^/      /'
  done
  verify_one "$1"
  zip_one "$1" "$2"   # the stapled bundle is what ships
}
kv() { printf '%-12s %s\n' "$1" "$2"; }

release() {
  ID=${FCMP_SIGNING_IDENTITY:-}
  PROFILE=${FCMP_NOTARY_PROFILE:-}
  if [ -n "$BYPASS" ] && { [ -n "$ID" ] || [ -n "$PROFILE" ]; }; then
    die 2 "--test-bypass is for the ad-hoc dry run only: unset FCMP_SIGNING_IDENTITY and FCMP_NOTARY_PROFILE"
  fi
  if [ -n "$PROFILE" ] && [ -z "$ID" ]; then
    die 2 "FCMP_NOTARY_PROFILE needs FCMP_SIGNING_IDENTITY: Apple does not notarise ad-hoc signatures"
  fi

  # ---- setup: a configured and built FCompressor plugin tree (exit 2 otherwise) ----
  [ -d "$BUILD" ] || die 2 "$BUILD does not exist"
  BUILD=$(cd "$BUILD" && pwd -P)
  CACHE=$BUILD/CMakeCache.txt
  DEPS=$BUILD/fcmp-deps.txt
  PRODUCT_H=$BUILD/generated/FcmpProduct.h
  BUILDINFO=$BUILD/generated/fcmp/BuildInfo.cpp
  [ -f "$CACHE" ] || die 2 "$BUILD is not a configured build directory (no CMakeCache.txt)"
  for _f in "$DEPS" "$PRODUCT_H" "$BUILDINFO"; do
    [ -f "$_f" ] || die 2 "$BUILD is not a configured FCompressor plugin build (no $_f)"
  done
  SRC=$(cache_get CMAKE_HOME_DIRECTORY)
  [ -n "$SRC" ] && [ -d "$SRC" ] || die 2 "cannot find the source directory in $CACHE"
  SHA=$(git_src rev-parse -q --verify 'HEAD^{commit}' 2> /dev/null) || die 2 "$SRC is not a git checkout"
  NAME=$(product_const kName)
  VERSION=$(product_const kVersion)
  MANU=$(product_const kManufacturerCode)
  CODE=$(product_const kPluginCode)
  CFG=$(cache_get CMAKE_BUILD_TYPE)
  FLAGS=$(sed -n 's|^// FCMP_FLAGS_LINE: ||p' "$BUILDINFO" | head -n 1)
  [ -n "$NAME" ] && [ -n "$VERSION" ] && [ -n "$MANU" ] && [ -n "$CODE" ] && [ -n "$FLAGS" ] ||
    die 2 "cannot read the product name, version and codes from $PRODUCT_H or the flags line from $BUILDINFO"
  [ -n "$CFG" ] || die 2 "no CMAKE_BUILD_TYPE in $CACHE"
  ART=$BUILD/${NAME}_artefacts/$CFG
  AU=$ART/AU/$NAME.component
  V3=$ART/VST3/$NAME.vst3
  APP=$ART/Standalone/$NAME.app
  for _b in "$AU" "$V3" "$APP"; do
    [ -f "$_b/Contents/MacOS/$NAME" ] || die 2 "no $_b/Contents/MacOS/$NAME (build it: cmake --build --preset release)"
  done
  ENT=$SRC/Resources/$NAME.entitlements
  [ -f "$ENT" ] || die 2 "no $ENT"
  CMAKE=$(cache_get CMAKE_COMMAND)
  [ -x "$CMAKE" ] || CMAKE=cmake
  CTEST=$(cache_get CMAKE_CTEST_COMMAND)
  [ -x "$CTEST" ] || CTEST=ctest
  OUT=${OUT:-$BUILD/dist}
  TMPD=$(mktemp -d "${TMPDIR:-/tmp}/fcmp-release.XXXXXX")
  trap 'rm -rf "$TMPD"' EXIT

  say "== release.sh $BUILD"
  say "   $NAME $VERSION, source $SRC @ $SHA, $CFG"
  if [ -n "$BYPASS" ]; then
    say "   TEST BYPASS ($BYPASS): a dry run of an untagged or unverified tree; ad-hoc only, never distributable"
  fi

  # ---- 1. pre-flight ----
  say "== pre-flight"
  REFUSED=""
  BYPASSED=""
  if [ "$CFG" = Release ]; then
    check_ok config "CMAKE_BUILD_TYPE Release"
  else
    refuse config "CMAKE_BUILD_TYPE is '$CFG', not Release (cmake --preset release)"
  fi

  _rel=$(cache_get FCOMPRESSOR_RELEASE)
  if is_on "$_rel"; then
    check_ok release-flag "FCOMPRESSOR_RELEASE $_rel"
  else
    refuse release-flag "FCOMPRESSOR_RELEASE is '${_rel:-unset}', not ON (the release and universal presets set it)"
  fi

  _hl=$(cache_get FCOMPRESSOR_HEADLESS)
  _do=$(cache_get FCOMPRESSOR_DSP_ONLY)
  if is_on "$_hl" || is_on "$_do"; then
    refuse editor "a Generic editor: FCOMPRESSOR_HEADLESS=${_hl:-unset}, FCOMPRESSOR_DSP_ONLY=${_do:-unset}"
  elif [ ! -f "$BUILD/build.ninja" ]; then
    refuse editor "cannot tell the editor: no $BUILD/build.ninja (the release presets use Ninja)"
  elif gpu_editor_compiled; then
    check_ok editor "GPU editor (target $NAME compiles with FCOMPRESSOR_GPU_EDITOR=1)"
  else
    refuse editor "a Generic editor: target $NAME lacks FCOMPRESSOR_GPU_EDITOR=1 (Source/plugin/CreateEditorGpu.cpp?)"
  fi

  UNIVERSAL=$(cache_get FCOMPRESSOR_UNIVERSAL)
  if is_on "$UNIVERSAL"; then
    ARCHS="arm64 x86_64"
  else
    ARCHS=arm64
  fi
  _bad=""
  for _b in "$AU" "$V3" "$APP"; do
    _got=$(lipo -archs "$_b/Contents/MacOS/$NAME" 2> /dev/null | sorted_words || true)
    [ "$_got" = "$ARCHS" ] || _bad="$_bad $(basename "$_b")=[${_got:-?}]"
  done
  if [ -z "$_bad" ]; then
    check_ok arch "$ARCHS (all three binaries)"
  elif is_on "$UNIVERSAL"; then
    refuse arch "FCOMPRESSOR_UNIVERSAL=ON wants [$ARCHS]:$_bad"
  else
    refuse arch "v1 ships arm64 only (DECISIONS Q6; the release preset):$_bad"
  fi
  if is_on "$UNIVERSAL"; then
    if [ -f "$SRC/build-lead-x86/verify-passed-$SHA" ]; then
      check_ok x86-verify "$SRC/build-lead-x86/verify-passed-$SHA"
    else
      _m="a universal build needs $SRC/build-lead-x86/verify-passed-$SHA: the SSE backend has not run the verify suite"
      refuse x86-verify "$_m under Rosetta (K2 #15, ADR-47); v1 ships arm64 (DECISIONS Q6)"
    fi
  fi

  _ovr=$(awk -F '\t' '!/^#/ && $5 == "yes" { printf "%s%s at %s", sep, $1, $4; sep = "; " }' "$DEPS")
  if [ -z "$_ovr" ]; then
    check_ok override "no dependency override (fcmp-deps.txt)"
  else
    refuse override "fcmp-deps.txt shows an override: $_ovr (reconfigure without it)"
  fi

  _dirty=$(git_src status --porcelain 2> /dev/null || say "(git status failed)")
  if [ -z "$_dirty" ]; then
    check_ok tree "clean at $SHA"
  else
    refuse tree "uncommitted changes in $SRC: $(printf '%s\n' "$_dirty" | head -n 3 | tr '\n' ';' | sed 's/;$//')"
  fi

  _tagged=$(git_src rev-parse -q --verify "refs/tags/v$VERSION^{commit}" 2> /dev/null || true)
  if [ "$_tagged" = "$SHA" ]; then
    check_ok tag "HEAD is v$VERSION"
  elif [ -n "$_tagged" ]; then
    refuse tag "v$VERSION is $_tagged, not HEAD $SHA"
  else
    refuse tag "HEAD is not tagged v$VERSION (git tag -a v$VERSION)"
  fi

  STAMP=$SRC/build-lead/verify-passed-$SHA
  VERIFY_LINE=""
  VALIDATE_LINE=""
  if [ -f "$STAMP" ]; then
    VERIFY_LINE=$(head -n 1 "$STAMP")
    VALIDATE_LINE=$(grep '^validate: ' "$STAMP" | tail -n 1 || true)
  fi
  if [ ! -f "$STAMP" ]; then
    refuse stamp "no $STAMP (Scripts/verify.sh --integration build-lead; Scripts/validate.sh --install build-lead)"
  else
    case $VERIFY_LINE in
      "verify.sh: fully green"*)
        case $VALIDATE_LINE in
          "validate: pass"*) check_ok stamp "$STAMP: verify green, ${VALIDATE_LINE%% (*}" ;;
          "") refuse stamp "$STAMP records no validate.sh run (a 'validate --vst3-only' line does not count)" ;;
          *) refuse stamp "the last validate.sh run recorded in $STAMP did not pass: '$VALIDATE_LINE'" ;;
        esac
        ;;
      *) refuse stamp "$STAMP is not a verify.sh stamp (first line '$VERIFY_LINE')" ;;
    esac
  fi

  _bf=$(head -n 1 "$BUILD/built-from-plugin.txt" 2> /dev/null || true)
  case $_bf in
    "plugin $SHA clean "*) check_ok bundles "built from HEAD, clean tree (built-from-plugin.txt)" ;;
    "") refuse bundles "no $BUILD/built-from-plugin.txt: the last plugin build did not finish (build, then rerun)" ;;
    *) refuse bundles "the bundles are not HEAD's clean build (built-from-plugin.txt '$_bf'); rebuild, then rerun" ;;
  esac

  _nx=0
  for _b in "$AU" "$V3" "$APP"; do
    _x=$(extra_exports "$_b")
    if [ -n "$_x" ]; then
      _nx=$((_nx + 1))
      _m="$(basename "$_b") exports $(printf '%s\n' "$_x" | wc -l | tr -d ' ') symbol(s) beyond its entry points"
      _m="$_m ($(entry_points "$_b" | tr '\n' ' ' | sed 's/ $//'))"
      refuse exports "$_m, e.g. $(printf '%s\n' "$_x" | head -n 3 | tr '\n' ' ')(K2 #26f)"
    fi
  done
  [ "$_nx" -gt 0 ] || check_ok exports "entry points only (nm -gU, every slice)"

  RLOG=$BUILD/release-registry.log
  RJSON=$BUILD/probe-results/dsp.registry.json
  rm -f "$RJSON"
  if ! "$CMAKE" --build "$BUILD" --target fcmp_probe_dsp --parallel 6 > "$RLOG" 2>&1; then
    refuse provisional "building fcmp_probe_dsp for the release registry check failed (see $RLOG)"
  elif ! "$CTEST" --test-dir "$BUILD" -L 'probe:dsp\.registry' --no-tests=error --output-on-failure >> "$RLOG" 2>&1
  then
    _p=$(sed -n 's/.*"provisional":\(\[[^]]*\]\).*/\1/p' "$RJSON" 2> /dev/null || true)
    refuse provisional "the release registry check (dsp.registry) failed; provisional Modes: ${_p:-unknown} (see $RLOG)"
  elif grep -q '"provisional":\[\]' "$RJSON" 2> /dev/null; then
    check_ok provisional "dsp.registry passes, no provisional Mode"
  else
    _p=$(sed -n 's/.*"provisional":\(\[[^]]*\]\).*/\1/p' "$RJSON" 2> /dev/null || true)
    refuse provisional "dsp.registry lists provisional Modes: ${_p:-no provisional list in $RJSON}"
  fi

  if [ -n "$REFUSED" ]; then
    die 1 "REFUSED ($(printf '%s' "$REFUSED" | sorted_words)): nothing was signed or packaged"
  fi

  # ---- 2-3. clean and sign ----
  if [ -n "$ID" ]; then
    SIGN_DESC="$ID (hardened runtime, timestamp)"
    say "== sign as $ID"
  else
    SIGN_DESC="ad-hoc (dry run: not distributable)"
    say "== sign ad-hoc: no FCMP_SIGNING_IDENTITY (a dry run; Gatekeeper refuses ad-hoc downloads)"
  fi
  for _b in "$AU" "$V3" "$APP"; do
    sign_one "$_b"
  done

  # ---- 4. package ----
  DIST=$OUT/$NAME-$VERSION
  mkdir -p "$DIST" || step_fail "mkdir -p $DIST"
  DIST=$(cd "$DIST" && pwd -P)
  rm -f "$DIST/MANIFEST.txt"
  say "== package into $DIST"
  for _b in "$AU" "$V3" "$APP"; do
    zip_one "$_b" "$DIST/$(basename "$_b").zip"
  done

  # ---- 5. notarise ----
  if [ -n "$PROFILE" ]; then
    NOTARY_DESC="yes: notarytool Accepted, stapled (profile $PROFILE)"
    for _b in "$AU" "$V3" "$APP"; do
      notarise_one "$_b" "$DIST/$(basename "$_b").zip"
    done
  else
    NOTARY_DESC="no (FCMP_NOTARY_PROFILE unset)"
    say "== not notarised (FCMP_NOTARY_PROFILE unset)"
  fi

  # ---- 6. manifest ----
  if [ "$_tagged" = "$SHA" ]; then
    _gitdesc="$SHA (tag v$VERSION)"
  else
    _gitdesc="$SHA (NOT tagged v$VERSION)"
  fi
  {
    say "$NAME $VERSION release manifest (Scripts/release.sh; 03 §5)"
    kv version "$VERSION"
    kv git "$_gitdesc"
    kv date "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    kv archs "$ARCHS"
    kv signing "$SIGN_DESC"
    kv notarised "$NOTARY_DESC"
    kv verify "${VERIFY_LINE:-none}"
    kv validate "${VALIDATE_LINE:-none}"
    if [ -n "$BYPASSED" ]; then
      kv bypass "$(printf '%s' "$BYPASSED" | sorted_words) (--test-bypass: a dry run, not for distribution)"
    fi
    awk -F '\t' '!/^#/ && $1 != "configuration" { printf "%-12s %s %s (%s)\n", $1, $2, $3, $6 }' "$DEPS"
    kv flags "$FLAGS"
    say "SHA256SUMS"
    (cd "$DIST" && shasum -a 256 "$NAME.component.zip" "$NAME.vst3.zip" "$NAME.app.zip")
  } > "$DIST/MANIFEST.txt.tmp" || step_fail "writing $DIST/MANIFEST.txt"
  mv "$DIST/MANIFEST.txt.tmp" "$DIST/MANIFEST.txt"
  say "== manifest $DIST/MANIFEST.txt"
  sed 's/^/   /' "$DIST/MANIFEST.txt"

  # ---- 7. post-flight ----
  say "== post-flight checklist (by hand; 03 §5 step 8)"
  say "   [ ] on a clean user account, unzip and install $NAME.component into ~/Library/Audio/Plug-Ins/Components/,"
  say "       $NAME.vst3 into ~/Library/Audio/Plug-Ins/VST3/ and $NAME.app into /Applications/"
  say "   [ ] auval -v aufx $CODE $MANU"
  say "   [ ] load it in one AU host and one VST3 host; GarageBand for the sandboxed AU"
  if [ -n "$PROFILE" ]; then
    say "   [ ] Gatekeeper: spctl -a -vv -t exec /Applications/$NAME.app says 'Notarized Developer ID'"
  elif [ -n "$ID" ]; then
    say "   (not notarised: Gatekeeper blocks a downloaded copy; set FCMP_NOTARY_PROFILE for a release)"
  else
    say "   (ad-hoc and not notarised: this output is a dry run, never a release)"
  fi
  say "release.sh: done: $DIST"
}

# ---- --self-test -----------------------------------------------------------------------------------------------------

ST_VERSION=9.8.7   # the fixture's project version

st_git() {  # git in the scratch clone: its own identity, no signing, no hooks
  git -C "$REPO" -c user.name='release.sh self-test' -c user.email=self-test@invalid -c commit.gpgsign=false \
    -c tag.gpgsign=false -c core.hooksPath=/dev/null "$@"
}

st_write_fixture() {  # the miniature FCompressor build that replaces the clone's CMakeLists.txt
  mkdir -p "$REPO/selftest"
  cat > "$REPO/CMakeLists.txt" << 'EOF'
# Scripts/release.sh --self-test fixture (never the real build): three bundles that export exactly their entry points,
# the generated files release.sh reads, built-from-plugin.txt from the real cmake/FcmpBuiltFrom.cmake, and a stand-in
# dsp.registry probe. Each ST_* option breaks one thing a refusal must catch.
cmake_minimum_required(VERSION 3.30)
option(FCOMPRESSOR_UNIVERSAL "arm64 + x86_64" OFF)
if(FCOMPRESSOR_UNIVERSAL)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
project(FCompressor VERSION 9.8.7 LANGUAGES CXX)
option(FCOMPRESSOR_RELEASE  "" OFF)
option(FCOMPRESSOR_HEADLESS "" OFF)
option(FCOMPRESSOR_DSP_ONLY "" OFF)
option(ST_GPU_EDITOR   "compile the FCompressor target with FCOMPRESSOR_GPU_EDITOR=1" ON)
option(ST_EXTRA_EXPORT "export one symbol beyond the entry points" OFF)
option(ST_PROVISIONAL  "dsp.registry reports a provisional Mode" OFF)
option(ST_OVERRIDE     "fcmp-deps.txt shows a FunkGui override" OFF)
find_package(Git REQUIRED)

set(FCMP_PRODUCT_NAME FCompressor)
set(FCMP_COMPANY_NAME Funk)
set(FCMP_COMPANY_COPYRIGHT "self-test")
set(FCMP_MANUFACTURER_CODE Funk)
set(FCMP_PLUGIN_CODE Fcmp)
set(FCMP_BUNDLE_ID com.funk.fcompressor.selftest)
set(FCMP_ENV_PREFIX FCMP_)
set(FCMP_OBJC_PREFIX Fcmp)
set(FCMP_PREFS_FOLDER FCompressor)
configure_file(cmake/FcmpProduct.h.in ${CMAKE_BINARY_DIR}/generated/FcmpProduct.h @ONLY)
if(CMAKE_OSX_ARCHITECTURES)
  list(JOIN CMAKE_OSX_ARCHITECTURES "," _archs)
else()
  set(_archs ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()
file(WRITE ${CMAKE_BINARY_DIR}/generated/fcmp/BuildInfo.cpp
     "// FCMP_FLAGS_LINE: FCompressor ${PROJECT_VERSION} GPU config=${CMAKE_BUILD_TYPE} archs=${_archs} self-test\n")
if(ST_OVERRIDE)
  set(_ovr yes)
else()
  set(_ovr no)
endif()
set(_z 0000000000000000000000000000000000000000)
file(WRITE ${CMAKE_BINARY_DIR}/fcmp-deps.txt
     "# fcmp-deps 1 (cmake/FcmpDeps.cmake): name\ttag\tsha\tdir\toverride\tnote\n"
     "JUCE\tself-test\t${_z}\t/self-test/JUCE\tno\tversion self-test\n"
     "bgfx.cmake\tself-test\t${_z}\t/self-test/bgfx.cmake\tno\tapi self-test\n"
     "FunkGui\tself-test\t${_z}\t/self-test/FunkGui\t${_ovr}\tversion self-test\n"
     "configuration\tGPU\t-\t${PROJECT_SOURCE_DIR}\tno\t${CMAKE_BUILD_TYPE} [${_archs}]\n")

add_library(FCompressor STATIC selftest/editor.cpp)
target_compile_definitions(FCompressor PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)   # other defines, as in the real one
if(ST_GPU_EDITOR)
  target_compile_definitions(FCompressor PRIVATE FCOMPRESSOR_GPU_EDITOR=1)
endif()

set(_art ${CMAKE_BINARY_DIR}/FCompressor_artefacts/${CMAKE_BUILD_TYPE})
function(st_bundle tgt fmt dir ext)
  if(fmt STREQUAL "APP")
    add_executable(${tgt} MACOSX_BUNDLE selftest/entry.cpp)
    set_target_properties(${tgt} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${_art}/${dir})
  else()
    add_library(${tgt} MODULE selftest/entry.cpp)
    set_target_properties(${tgt} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION ${ext} LIBRARY_OUTPUT_DIRECTORY ${_art}/${dir})
  endif()
  set_target_properties(${tgt} PROPERTIES OUTPUT_NAME FCompressor CXX_VISIBILITY_PRESET hidden
                        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/selftest/Info-${fmt}.plist)
  target_compile_definitions(${tgt} PRIVATE ST_FORMAT_${fmt}=1 ST_EXTRA_EXPORT=$<BOOL:${ST_EXTRA_EXPORT}>)
endfunction()
st_bundle(st_au AU AU component)
st_bundle(st_vst3 VST3 VST3 vst3)
st_bundle(st_app APP Standalone app)

# Every build: a read-only licence file with an extended attribute in each bundle, as the bgfx licences arrive from the
# read-only ~/audio/.deps cache (release.sh must still clear it).
add_custom_target(st_resources ALL
    COMMAND /bin/sh ${PROJECT_SOURCE_DIR}/selftest/resources.sh $<TARGET_BUNDLE_CONTENT_DIR:st_au>
            $<TARGET_BUNDLE_CONTENT_DIR:st_vst3> $<TARGET_BUNDLE_CONTENT_DIR:st_app>
    VERBATIM)
add_dependencies(st_resources st_au st_vst3 st_app)
add_custom_target(st_built_from ALL
    COMMAND ${CMAKE_COMMAND} -DFCMP_SOURCE_DIR=${PROJECT_SOURCE_DIR} -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DFCMP_OUT=${CMAKE_BINARY_DIR}/built-from-plugin.txt -DFCMP_WHAT=plugin
            -P ${PROJECT_SOURCE_DIR}/cmake/FcmpBuiltFrom.cmake
    VERBATIM)
add_dependencies(st_built_from FCompressor st_resources)

if(ST_PROVISIONAL)
  set(ST_STATUS spec_fail)
  set(ST_SPEC_FAIL 1)
  set(ST_LIST "[\"bus-g\"]")
  set(ST_RC 1)
else()
  set(ST_STATUS pass)
  set(ST_SPEC_FAIL 0)
  set(ST_LIST "[]")
  set(ST_RC 0)
endif()
configure_file(selftest/fcmp_probe_dsp.in ${CMAKE_BINARY_DIR}/st/fcmp_probe_dsp.in @ONLY)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/st/fcmp_probe_dsp
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/st/fcmp_probe_dsp.in ${CMAKE_BINARY_DIR}/st/fcmp_probe_dsp
    DEPENDS ${CMAKE_BINARY_DIR}/st/fcmp_probe_dsp.in VERBATIM)
add_custom_target(fcmp_probe_dsp DEPENDS ${CMAKE_BINARY_DIR}/st/fcmp_probe_dsp)   # not in ALL, like the real one
enable_testing()
add_test(NAME dsp.registry
         COMMAND /bin/sh ${CMAKE_BINARY_DIR}/st/fcmp_probe_dsp dsp.registry --results ${CMAKE_BINARY_DIR}/probe-results)
set_tests_properties(dsp.registry PROPERTIES LABELS "verify;dsp;global;probe:dsp.registry")
EOF
  cat > "$REPO/selftest/entry.cpp" << 'EOF'
// Scripts/release.sh --self-test fixture: each format's entry points, and nothing else exported (hidden visibility).
#define ST_EXPORT extern "C" __attribute__((visibility("default")))
#if ST_FORMAT_AU
ST_EXPORT void* JuceAUFactory(const void*) { return nullptr; }
ST_EXPORT void* FCompressorAUFactory(const void* desc) { return JuceAUFactory(desc); }
#elif ST_FORMAT_VST3
ST_EXPORT void* GetPluginFactory() { return nullptr; }
ST_EXPORT bool bundleEntry(void*) { return true; }
ST_EXPORT bool bundleExit() { return true; }
#else
int main() { return 0; }
#endif
#if ST_EXTRA_EXPORT
ST_EXPORT int bgfxSelfTestLeak() { return 1; }   // the exports refusal: one symbol beyond the entry points
#endif
EOF
  cat > "$REPO/selftest/editor.cpp" << 'EOF'
// Scripts/release.sh --self-test fixture: the FCompressor target, compiled with FCOMPRESSOR_GPU_EDITOR=1 unless the
// case wants a Generic editor.
int fcmpSelfTestEditor() { return 0; }
EOF
  cat > "$REPO/selftest/resources.sh" << 'EOF'
#!/bin/sh
# Scripts/release.sh --self-test fixture: a read-only licence file with an extended attribute in each bundle's
# Contents/ (the arguments), as the bgfx licences arrive from the read-only dependency cache.
set -eu
for c in "$@"; do
  f=$c/Resources/licences/selftest-LICENSE.txt
  mkdir -p "$c/Resources/licences"
  rm -f "$f"
  echo 'self-test licence' > "$f"
  xattr -w org.funk.selftest 1 "$f"
  chmod 444 "$f"
done
EOF
  cat > "$REPO/selftest/fcmp_probe_dsp.in" << 'EOF'
#!/bin/sh
# Scripts/release.sh --self-test stand-in for `fcmp_probe_dsp dsp.registry`: the real probe fails a FCOMPRESSOR_RELEASE
# build on a provisional Mode and always writes the "provisional" list into its results JSON.
set -eu
results=.
while [ $# -gt 0 ]; do
  case $1 in --results) results=$2; shift ;; esac
  shift
done
mkdir -p "$results"
{
  printf '{"probe":"dsp.registry","mode":"","arch":"arm64","status":"@ST_STATUS@","spec_pass":1,'
  printf '"spec_fail":@ST_SPEC_FAIL@,"provisional":@ST_LIST@}\n'
} > "$results/dsp.registry.json"
exit @ST_RC@
EOF
  for _fmt in AU VST3 APP; do
    case $_fmt in
      APP) _type=APPL ;;
      *) _type=BNDL ;;
    esac
    cat > "$REPO/selftest/Info-$_fmt.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>FCompressor</string>
  <key>CFBundleIdentifier</key><string>com.funk.fcompressor.selftest</string>
  <key>CFBundleName</key><string>FCompressor</string>
  <key>CFBundlePackageType</key><string>$_type</string>
  <key>CFBundleShortVersionString</key><string>$ST_VERSION</string>
  <key>CFBundleVersion</key><string>$ST_VERSION</string>
  <key>AudioComponents</key>
  <array><dict>
    <key>factoryFunction</key><string>FCompressorAUFactory</string>
    <key>manufacturer</key><string>Funk</string>
    <key>subtype</key><string>Fcmp</string>
    <key>type</key><string>aufx</string>
  </dict></array>
</dict>
</plist>
EOF
  done
}

st_build() {  # st_build [-D...]: configure (every fixture option, then the case's) and build the fixture
  if ! "$ST_CMAKE" -S "$REPO" -B "$FB" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DFCOMPRESSOR_RELEASE=ON -DFCOMPRESSOR_HEADLESS=OFF -DFCOMPRESSOR_DSP_ONLY=OFF -DFCOMPRESSOR_UNIVERSAL=OFF \
    -DST_GPU_EDITOR=ON -DST_EXTRA_EXPORT=OFF -DST_PROVISIONAL=OFF -DST_OVERRIDE=OFF "$@" \
    > "$ST/logs/fixture.log" 2>&1 || ! "$ST_CMAKE" --build "$FB" >> "$ST/logs/fixture.log" 2>&1; then
    tail -n 30 "$ST/logs/fixture.log" >&2
    die 2 "--self-test: the fixture did not configure or build ($* ; $ST/logs/fixture.log)"
  fi
}
st_stamp() {  # st_stamp <dir> <sha> [<validate line>...]: a verify-passed stamp as verify.sh (+ validate.sh) write it
  _d=$REPO/$1
  _s=$2
  shift 2
  mkdir -p "$_d"
  {
    say "verify.sh: fully green at 2026-01-01T00:00:00Z"
    say "source $REPO @ $_s"
    say "built-from probes $_s clean 2026-01-01T00:00:00Z"
    cat "$FB/fcmp-deps.txt"
    for _l in "$@"; do
      say "$_l"
    done
  } > "$_d/verify-passed-$_s"
}
st_reset() {  # st_reset [-D...]: the passing state (fixture commit, tag, stamp, fresh build), then the case's options
  st_git reset -q --hard "$FIX"
  st_git clean -q -fd
  st_git tag -f -a "v$ST_VERSION" -m 'release.sh self-test' "$FIX" > /dev/null
  rm -rf "$REPO/build-lead" "$REPO/build-lead-x86" "$OUTD"
  st_build "$@"
  st_stamp build-lead "$FIX" "$ST_VALIDATE_PASS"
}
st_run() {  # st_run <case> <exit> <refusals> <text> <identity> <profile> [release.sh arguments...]
  _case=$1
  _want_rc=$2
  _want=$(printf '%s' "$3" | sorted_words)
  _text=$4
  _log=$ST/logs/$_case.log
  shift 4
  _id=$1
  _prof=$2
  shift 2
  set +e
  env FCMP_SIGNING_IDENTITY="$_id" FCMP_NOTARY_PROFILE="$_prof" PATH="$ST/shims:$PATH" \
    "$ST_SHELL" "$REPO/Scripts/release.sh" "$@" > "$_log" 2>&1
  _rc=$?
  set -e
  _got=$(sed -n 's/^   REFUSED  \([a-z0-9-]*\):.*/\1/p' "$_log" | tr '\n' ' ' | sorted_words)
  _why=""
  [ "$_rc" = "$_want_rc" ] || _why="exit $_rc, want $_want_rc"
  [ "$_got" = "$_want" ] || _why="$_why${_why:+; }refused [$_got], want [$_want]"
  if [ -n "$_text" ] && ! grep -qF -- "$_text" "$_log"; then
    _why="$_why${_why:+; }no '$_text' in the output"
  fi
  if grep -q 'self-test shim:' "$_log"; then
    _why="$_why${_why:+; }a shim refused a call (identity, notarytool or stapler)"
  fi
  if [ -z "$_why" ] && [ "$_want_rc" = 0 ]; then
    _why=$(st_check_release "$_case")
  fi
  ST_N=$((ST_N + 1))
  if [ -z "$_why" ]; then
    printf '   pass  %-22s exit %s  refused [%s]\n' "$_case" "$_rc" "$_got"
  else
    ST_FAILED="$ST_FAILED $_case"
    printf '   FAIL  %-22s %s (log %s)\n' "$_case" "$_why" "$_log"
  fi
}
st_shim_calls() {  # st_shim_calls <case> <regex> <count>: the simulated-apple shims saw <count> matching calls
  _n=$(grep -c -- "$2" "$ST/shims.log" 2> /dev/null || true)
  if [ "${_n:-0}" != "$3" ]; then
    ST_FAILED="$ST_FAILED $1"
    printf '   FAIL  %-22s shims.log has %s line(s) matching %s, want %s\n' "$1" "${_n:-0}" "$2" "$3"
  fi
}
st_check_release() {  # st_check_release <case>: the zips, the manifest's SHA256SUMS and every signature, or why not
  _dist=$OUTD/FCompressor-$ST_VERSION
  for _z in FCompressor.component.zip FCompressor.vst3.zip FCompressor.app.zip MANIFEST.txt; do
    [ -f "$_dist/$_z" ] || {
      say "no $_dist/$_z"
      return 0
    }
  done
  sed -n '/^SHA256SUMS$/,$p' "$_dist/MANIFEST.txt" | sed 1d > "$ST/sums.$1"
  [ "$(wc -l < "$ST/sums.$1" | tr -d ' ')" = 3 ] || {
    say "MANIFEST.txt has no 3-line SHA256SUMS"
    return 0
  }
  (cd "$_dist" && shasum -a 256 -c "$ST/sums.$1" > /dev/null 2>&1) || {
    say "SHA256SUMS do not match the zips"
    return 0
  }
  grep -qF "signing      $ST_WANT_SIGNING" "$_dist/MANIFEST.txt" || {
    say "MANIFEST.txt does not say 'signing $ST_WANT_SIGNING'"
    return 0
  }
  grep -qF "notarised    $ST_WANT_NOTARY" "$_dist/MANIFEST.txt" || {
    say "MANIFEST.txt does not say 'notarised $ST_WANT_NOTARY'"
    return 0
  }
  rm -rf "$ST/unzip.$1"
  mkdir -p "$ST/unzip.$1"
  for _z in "$_dist"/*.zip; do
    ditto -x -k "$_z" "$ST/unzip.$1" || {
      say "cannot unzip $_z"
      return 0
    }
  done
  for _b in "$FB/FCompressor_artefacts/Release"/*/FCompressor.* "$ST/unzip.$1"/FCompressor.*; do
    [ -f "$_b/Contents/Resources/licences/selftest-LICENSE.txt" ] || {
      say "$_b has no read-only licence file (the fixture's st_resources)"
      return 0
    }
    if xattr -lr "$_b" 2> /dev/null | grep -q org.funk.selftest; then
      say "$_b still carries the licence file's extended attribute (xattr -cr)"
      return 0
    fi
    codesign --verify --strict "$_b" > /dev/null 2>&1 || {
      say "codesign --verify --strict fails on $_b"
      return 0
    }
    case $_b in
      *.app)
        codesign -d --entitlements - "$_b" 2> /dev/null | grep -q com.apple.security.device.audio-input || {
          say "$_b lacks the audio-input entitlement"
          return 0
        }
        ;;
    esac
  done
}

self_test() {
  SELF=$(cd "$(dirname "$0")" && pwd -P)/$(basename "$0")
  ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
  git -C "$ROOT" rev-parse -q --verify 'HEAD^{commit}' > /dev/null 2>&1 ||
    die 2 "--self-test: $ROOT is not a git checkout"
  case $ROOT in *[[:space:]]*) die 2 "--self-test needs a checkout path without whitespace ($ROOT)" ;; esac
  ST_CMAKE=$(command -v cmake) || die 2 "--self-test needs cmake"
  command -v ninja > /dev/null || die 2 "--self-test needs ninja"
  # Run every case under the shell running this (so `dash Scripts/release.sh --self-test` checks POSIX sh).
  ST_SHELL=/bin/sh
  _comm=$(ps -o comm= -p $$ 2> /dev/null | sed 's/^-//' || true)
  case $(basename "${_comm:-sh}") in
    sh | dash | bash | ksh) ST_SHELL=$(command -v "$_comm" || say /bin/sh) ;;
  esac
  ST=$ROOT/build-selftest
  REPO=$ST/repo
  FB=$REPO/build-release
  OUTD=$ST/out
  ST_VALIDATE_PASS="validate: pass at 2026-01-01T00:00:00Z (pluginval v1.0.4; plugin <sha> clean 2026-01-01T00:00:00Z)"
  say "== release.sh --self-test: scratch clone $REPO (shell $ST_SHELL)"
  rm -rf "$ST"
  mkdir -p "$ST/shims" "$ST/logs"

  # Shims (first on every case's PATH): the real codesign signs ad-hoc only, and the real notarytool and stapler never
  # run. The simulated-apple cases stand in for Apple: the fake identity is re-signed ad-hoc (its --timestamp dropped),
  # and notarytool/stapler answer from the shim for the two fake profiles. Every call is logged to shims.log.
  ST_FAKE_ID='Developer ID Application: Self Test (SELFTEST00)'
  {
    say '#!/bin/sh'
    say "log='$ST/shims.log'"
    say "fake='$ST_FAKE_ID'"
    cat << 'EOF'
n=$#
prev=
while [ "$n" -gt 0 ]; do
  a=$1
  shift
  n=$((n - 1))
  if [ "$prev" = --sign ] || [ "$prev" = -s ]; then
    if [ "$a" = "$fake" ]; then
      echo "codesign --sign <fake identity>" >> "$log"
      a=-
    elif [ "$a" != - ]; then
      echo "self-test shim: codesign --sign '$a' refused (ad-hoc and the fake identity only)" >&2
      exit 97
    fi
  fi
  prev=$a
  if [ "$a" = --timestamp ]; then
    echo "codesign --timestamp" >> "$log"
    continue
  fi
  set -- "$@" "$a"
done
exec /usr/bin/codesign "$@"
EOF
  } > "$ST/shims/codesign"
  {
    say '#!/bin/sh'
    say "log='$ST/shims.log'"
    cat << 'EOF'
case ${1-} in
  notarytool)
    prof=
    prev=
    for a in "$@"; do
      [ "$prev" != --keychain-profile ] || prof=$a
      prev=$a
    done
    case $prof in
      selftest-accepted) status=Accepted ;;
      selftest-invalid) status=Invalid ;;
      *) echo "self-test shim: xcrun notarytool refused (no keychain, no network)" >&2; exit 97 ;;
    esac
    echo "notarytool ${2-} ${3-} $status" >> "$log"
    printf '  id: 00000000-0000-0000-0000-000000000000\n  status: %s\n' "$status"
    ;;
  stapler)
    grep -q '^notarytool .* Accepted$' "$log" 2> /dev/null || {
      echo "self-test shim: xcrun stapler refused (no simulated notarisation; no network)" >&2
      exit 97
    }
    echo "stapler ${2-} ${3-}" >> "$log"
    echo "The ${2-} action worked!"
    ;;
  *) exec /usr/bin/xcrun "$@" ;;
esac
EOF
  } > "$ST/shims/xcrun"
  chmod +x "$ST/shims/codesign" "$ST/shims/xcrun"

  git -c core.hooksPath=/dev/null clone -q --depth 1 --no-tags "file://$ROOT" "$REPO" ||
    die 2 "--self-test: git clone of $ROOT failed"
  cp "$SELF" "$REPO/Scripts/release.sh"
  st_write_fixture
  st_git add -A
  st_git commit -q -m 'release.sh self-test fixture'
  FIX=$(st_git rev-parse HEAD)
  ST_WANT_SIGNING=ad-hoc
  ST_WANT_NOTARY=no
  ST_VALIDATE_PASS=$(printf '%s' "$ST_VALIDATE_PASS" | sed "s/<sha>/$FIX/")
  ST_N=0
  ST_FAILED=""
  X="$FB $OUTD"   # the release.sh arguments of every case (word-split on purpose: $ROOT has no whitespace)

  say "== cases (logs in $ST/logs)"
  # shellcheck disable=SC2086 # $X is two paths
  {
    st_reset
    st_run ok 0 "" "release.sh: done" "" "" $X

    st_reset -DCMAKE_BUILD_TYPE=RelWithDebInfo
    st_run config 1 config "" "" "" $X
    st_reset -DFCOMPRESSOR_RELEASE=OFF
    st_run release-flag 1 release-flag "" "" "" $X
    st_reset -DST_GPU_EDITOR=OFF
    st_run editor-generic 1 editor "FCOMPRESSOR_GPU_EDITOR=1" "" "" $X
    st_reset -DFCOMPRESSOR_HEADLESS=ON -DST_GPU_EDITOR=OFF
    st_run editor-headless 1 editor "FCOMPRESSOR_HEADLESS=ON" "" "" $X
    st_reset -DCMAKE_OSX_ARCHITECTURES=x86_64
    st_run arch 1 arch "arm64 only" "" "" $X
    st_reset -DFCOMPRESSOR_UNIVERSAL=ON
    st_run universal-no-x86 1 x86-verify "build-lead-x86/verify-passed-$FIX" "" "" $X
    st_reset -DFCOMPRESSOR_UNIVERSAL=ON
    st_stamp build-lead-x86 "$FIX"
    st_run universal 0 "" "arm64 x86_64" "" "" $X
    st_reset -DST_OVERRIDE=ON
    st_run override 1 override "FunkGui at /self-test/FunkGui" "" "" $X

    st_reset
    : > "$REPO/untracked-file"
    st_run tree-untracked 1 tree "untracked-file" "" "" $X
    st_reset
    say "// edited" >> "$REPO/selftest/editor.cpp"
    st_run tree-modified 1 tree "selftest/editor.cpp" "" "" $X

    st_reset
    st_git tag -d "v$ST_VERSION" > /dev/null
    st_run tag-missing 1 tag "not tagged v$ST_VERSION" "" "" $X
    st_reset
    st_git commit -q --allow-empty -m 'another commit'
    st_git tag -f -a "v$ST_VERSION" -m 'release.sh self-test' > /dev/null
    st_git reset -q --hard "$FIX"
    st_run tag-elsewhere 1 tag "not HEAD" "" "" $X

    st_reset
    rm -rf "$REPO/build-lead"
    st_run stamp-missing 1 stamp "no $REPO/build-lead/verify-passed-$FIX" "" "" $X
    st_reset
    st_stamp build-lead "$FIX"
    st_run stamp-no-validate 1 stamp "records no validate.sh run" "" "" $X
    st_reset
    st_stamp build-lead "$FIX" "validate --vst3-only: pass at 2026-01-01T00:00:00Z (pluginval v1.0.4)"
    st_run stamp-vst3-only 1 stamp "records no validate.sh run" "" "" $X
    st_reset
    st_stamp build-lead "$FIX" "$ST_VALIDATE_PASS" "validate: FAIL (1 step(s)) at 2026-01-02T00:00:00Z (pluginval)"
    st_run stamp-validate-fail 1 stamp "did not pass" "" "" $X
    st_reset
    say "not a stamp" > "$REPO/build-lead/verify-passed-$FIX"
    st_run stamp-not-verify 1 stamp "is not a verify.sh stamp" "" "" $X

    st_reset
    st_git commit -q --allow-empty -m 'committed after the build, never rebuilt'
    _head=$(st_git rev-parse HEAD)
    st_git tag -f -a "v$ST_VERSION" -m 'release.sh self-test' > /dev/null
    rm -rf "$REPO/build-lead"
    st_stamp build-lead "$_head" "$ST_VALIDATE_PASS"
    st_run bundles 1 bundles "not HEAD's clean build" "" "" $X

    st_reset -DST_EXTRA_EXPORT=ON
    st_run exports 1 exports "_bgfxSelfTestLeak" "" "" $X
    st_reset -DST_PROVISIONAL=ON
    st_run provisional 1 provisional '["bus-g"]' "" "" $X

    st_reset
    : > "$REPO/untracked-file"
    st_git tag -d "v$ST_VERSION" > /dev/null
    rm -rf "$REPO/build-lead"
    st_run several 1 "tree tag stamp" "nothing was signed" "" "" $X

    st_reset
    st_git tag -d "v$ST_VERSION" > /dev/null
    rm -rf "$REPO/build-lead"
    st_run bypass 0 "" "BYPASSED stamp" "" "" --test-bypass=tag,stamp $X
    if [ -f "$OUTD/FCompressor-$ST_VERSION/MANIFEST.txt" ] &&
      ! grep -q '^bypass .*stamp tag' "$OUTD/FCompressor-$ST_VERSION/MANIFEST.txt"; then
      ST_FAILED="$ST_FAILED bypass"
      say "   FAIL  bypass                 MANIFEST.txt does not record the bypass"
    fi
    st_reset
    st_git tag -d "v$ST_VERSION" > /dev/null
    rm -rf "$REPO/build-lead"
    st_run bypass-tag-only 1 stamp "BYPASSED tag" "" "" --test-bypass=tag $X
    st_reset
    : > "$REPO/untracked-file"
    st_run bypass-not-tree 1 tree "" "" "" --test-bypass=tag,stamp $X
    st_run bypass-identity 2 "" "for the ad-hoc dry run only" "self-test (not an identity)" "" --test-bypass=tag $X
    st_run bypass-profile 2 "" "for the ad-hoc dry run only" "" "self-test-no-profile" --test-bypass=stamp $X
    st_run bypass-other 2 "" "'exports' cannot be bypassed" "" "" --test-bypass=tag,exports $X
    st_run profile-no-identity 2 "" "needs FCMP_SIGNING_IDENTITY" "" "self-test-no-profile" $X

    st_reset
    rm -f "$ST/shims.log"
    ST_WANT_SIGNING=$ST_FAKE_ID
    ST_WANT_NOTARY="yes: notarytool Accepted, stapled"
    st_run simulated-apple 0 "" "Notarized Developer ID" "$ST_FAKE_ID" selftest-accepted $X
    ST_WANT_SIGNING=ad-hoc
    ST_WANT_NOTARY=no
    st_shim_calls simulated-apple '^codesign --sign <fake identity>$' 3
    st_shim_calls simulated-apple '^codesign --timestamp$' 3
    st_shim_calls simulated-apple '^notarytool submit .*\.zip Accepted$' 3
    st_shim_calls simulated-apple '^stapler staple ' 3
    st_shim_calls simulated-apple '^stapler validate ' 3
    st_reset
    rm -f "$ST/shims.log"
    st_run notary-invalid 3 "" "was not Accepted" "$ST_FAKE_ID" selftest-invalid $X
    st_shim_calls notary-invalid '^stapler ' 0
    st_reset
    rm -rf "$FB/FCompressor_artefacts/Release/VST3"
    st_run missing-bundle 2 "" "no $FB/FCompressor_artefacts/Release/VST3/FCompressor.vst3" "" "" $X
    st_run no-build-dir 2 "" "does not exist" "" "" "$ST/no-such-build" "$OUTD"
  }

  if [ -n "$ST_FAILED" ]; then
    _f=$(printf '%s' "$ST_FAILED" | sorted_words)
    die 1 "--self-test: $(printf '%s\n' $_f | wc -l | tr -d ' ') of $ST_N case(s) FAILED: $_f (kept $ST)"
  fi
  rm -rf "$ST"
  say "release.sh --self-test: all $ST_N cases passed (scratch clone removed)"
}

# ---- arguments -------------------------------------------------------------------------------------------------------

SELF_TEST=0
BYPASS=""
_bypass_arg=""
BUILD=""
OUT=""
for _arg in "$@"; do
  case $_arg in
    --self-test) SELF_TEST=1 ;;
    --test-bypass=*) _bypass_arg=${_arg#--test-bypass=} ;;
    -h | --help)
      usage
      exit 0
      ;;
    -*) die 2 "unknown option '$_arg' (--help)" ;;
    *)
      if [ -z "$BUILD" ]; then
        BUILD=$_arg
      elif [ -z "$OUT" ]; then
        OUT=$_arg
      else
        die 2 "too many arguments (--help)"
      fi
      ;;
  esac
done
if [ "$SELF_TEST" = 1 ]; then
  [ -z "$BUILD" ] && [ -z "$_bypass_arg" ] || die 2 "--self-test takes no other argument"
  self_test
  exit 0
fi
if [ -n "$_bypass_arg" ]; then
  for _b in $(printf '%s' "$_bypass_arg" | tr ',' ' '); do
    case $_b in
      tag | stamp) BYPASS="$BYPASS $_b" ;;
      *) die 2 "--test-bypass: '$_b' cannot be bypassed (only tag and stamp)" ;;
    esac
  done
  BYPASS=$(printf '%s' "$BYPASS" | sorted_words)
  [ -n "$BYPASS" ] || die 2 "--test-bypass needs tag and/or stamp"
fi
if [ -z "$BUILD" ]; then
  usage >&2
  exit 2
fi
release
