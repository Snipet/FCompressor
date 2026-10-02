# cmake/FcmpDeps.cmake: dependency pins, the ~/audio/.deps defaults, FetchContent in dependency order (JUCE -> bgfx ->
# FunkGui), and an assertion on every dependency, override or not (03 §0.4, §1.2, §2.3-§2.5; 02 §1.9; K2 #26).
#
# - JUCE and bgfx.cmake default to the read-only machine cache written by Scripts/deps.sh, as NORMAL variables (never
#   cached), and only when the user gave no FETCHCONTENT_SOURCE_DIR_<NAME> of their own. FETCHCONTENT_SOURCE_DIR_*
#   bypasses GIT_TAG completely (B §7.2), hence the version and SHA assertions below.
# - FunkGui is never defaulted to a source dir: without an override it clones the pinned tag from FCOMPRESSOR_FUNKGUI_REPO
#   (the local repository when this machine has one, else GitHub: CMakeLists.txt, ADR-87). An override
#   (-DFETCHCONTENT_SOURCE_DIR_FUNKGUI=<dir>) must be a git checkout whose HEAD descends from the pinned SHA; it is printed
#   loudly, it is sticky in the cache, and `Scripts/verify.sh --integration` refuses it (03 §4.5).
# - Never FETCHCONTENT_FULLY_DISCONNECTED (B §7.6): it would also stop FunkGui's local clone.
# - ${CMAKE_BINARY_DIR}/fcmp-deps.txt records what was used; verify.sh, validate.sh, golden.py, check-headers.sh and
#   release.sh read it.
include_guard(GLOBAL)
include(FetchContent)
find_package(Git REQUIRED)

# ---- pins (03 §1.3, §2.3) ----------------------------------------------------------------------------------------------
set(FCMP_JUCE_TAG     8.0.4)
set(FCMP_JUCE_SHA     51d11a2be6d5c97ccf12b4e5e827006e19f0555a)
set(FCMP_BGFX_TAG     v1.153.9385-561)
set(FCMP_BGFX_SHA     99752df38e40179cf998bb880fe4c16c0b3d60ca)
set(FCMP_BGFX_API     153)
set(FCMP_BGFX_SUB_SHAS bgfx=c7684e20da1e385edc439ef39cdb42b8c661016f
                       bx=0b001f5f36579e8aea07efa5af139ca18dad9505
                       bimg=3b4baab0128ac499c5c3bc37202781bf54084049)
# FunkGui: the lead bumps these three together at a sprint boundary (03 §4.8 step 3). v0.0.1 = the G0 snapshot with
# Harness v2 and placeholder core/gpu/presets targets and placeholder funkgui_* functions (SPRINTS §7 D1, D19).
# FCMP_FUNKGUI_SHA is the tagged COMMIT (`git rev-parse v0.1.0^{commit}`); the annotated tag object's SHA (what a bare
# `git rev-parse v0.1.0` prints) is accepted too and peeled to its commit by every check below (R-B0 #10).
set(FCMP_FUNKGUI_TAG     v0.14.0)
set(FCMP_FUNKGUI_SHA     f9501188556f03c309a1f34991a0a1be1e008d97)
set(FCMP_FUNKGUI_VERSION 0.14.0)

# ---- helpers -------------------------------------------------------------------------------------------------------
# fcmp_git(<out> <dir> <args>...): read-only git in <dir>; <out> = stripped stdout, <out>_RESULT = exit code. The
# caller's GIT_DIR/GIT_WORK_TREE never redirect it, and --no-optional-locks keeps even `describe --dirty` from
# refreshing an index (the .deps trees and lead-made pin worktrees are never written).
function(fcmp_git out dir)
  execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=GIT_DIR --unset=GIT_WORK_TREE --unset=GIT_INDEX_FILE
                          ${GIT_EXECUTABLE} --no-optional-locks -C "${dir}" ${ARGN}
                  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
  set(${out} "${_o}" PARENT_SCOPE)
  set(${out}_RESULT "${_rc}" PARENT_SCOPE)
  set(${out}_ERROR "${_e}" PARENT_SCOPE)
endfunction()

# fcmp_git_root(<out> <dir>): <out> = TRUE if <dir> is the root of its own git checkout. A populated _deps/x-src that is
# not a checkout would otherwise resolve to the enclosing FCompressor worktree (03 §2.3).
function(fcmp_git_root out dir)
  set(${out} FALSE PARENT_SCOPE)
  fcmp_git(_top "${dir}" rev-parse --show-toplevel)
  if(_top_RESULT EQUAL 0 AND IS_DIRECTORY "${_top}")
    file(REAL_PATH "${dir}" _d)
    file(REAL_PATH "${_top}" _t)
    if(_d STREQUAL _t)
      set(${out} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

# fcmp_assert_git(<dir> <sha> <name>): FATAL on a SHA mismatch; WARNING if <dir> is not its own git checkout (the
# version checks still apply). Read-only commands only. The pin is peeled with <sha>^{commit} (FZ0 errata, R-B0 #10):
# FunkGui's tags are annotated, so `git rev-parse v0.1.0` gives the tag OBJECT, whose SHA is never a HEAD; a commit SHA
# peels to itself. A pin that is not in <dir> at all is FATAL too.
function(fcmp_assert_git dir sha name)
  fcmp_git_root(_is_root "${dir}")
  if(NOT _is_root)
    message(WARNING "FCompressor: ${name} at ${dir} is not a git checkout; its SHA cannot be verified")
    return()
  endif()
  fcmp_git(_want "${dir}" rev-parse -q --verify "${sha}^{commit}")
  if(NOT _want_RESULT EQUAL 0 OR _want STREQUAL "")
    message(FATAL_ERROR "FCompressor: ${name} at ${dir} does not contain the pinned object ${sha} (or it is not a "
                        "commit or a tag of one). A stale cache or a mistyped pin? (.deps: rerun Scripts/deps.sh; a "
                        "populated _deps/<name>-src: delete it)")
  endif()
  fcmp_git(_head "${dir}" rev-parse -q --verify "HEAD^{commit}")
  if(NOT _head STREQUAL "${_want}")
    fcmp_git(_type "${dir}" cat-file -t "${sha}")
    if(_type STREQUAL "tag")
      set(_pin "${sha} (an annotated tag of commit ${_want})")
    elseif(_want STREQUAL "${sha}")
      set(_pin "${sha}")
    else()
      set(_pin "${sha} (commit ${_want})")
    endif()
    message(FATAL_ERROR "FCompressor: ${name} at ${dir} is at ${_head}, the pin is ${_pin}. A stale cache or a mistyped "
                        "override? (.deps: rerun Scripts/deps.sh; a populated _deps/<name>-src: delete it)")
  endif()
endfunction()

# 1. Default JUCE and bgfx.cmake to the machine cache, as normal variables, unless the user set them.
macro(fcmp_default_source_dir NAME SUBDIR)
  set(FCMP_${NAME}_DEFAULT_DIR "${FCOMPRESSOR_DEPS_DIR}/${SUBDIR}")
  if(NOT FETCHCONTENT_SOURCE_DIR_${NAME} AND EXISTS "${FCMP_${NAME}_DEFAULT_DIR}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_${NAME} "${FCMP_${NAME}_DEFAULT_DIR}")
  endif()
endmacro()

# fcmp_source_dir_used(<NAME> <source dir>): after MakeAvailable. Sets FCMP_<NAME>_OVERRIDE to "no" when the source is
# the machine cache's default directory, else "yes". FetchContent copies the normal variable into its cache entry;
# the entry is dropped again when it only holds the default, so the default stays a normal variable (never cached) and
# follows FCOMPRESSOR_DEPS_DIR, while a user's -DFETCHCONTENT_SOURCE_DIR_<NAME> (anything else) stays sticky.
macro(fcmp_source_dir_used NAME dir)
  file(REAL_PATH "${dir}" _fcmp_used)
  set(FCMP_${NAME}_OVERRIDE yes)
  if(EXISTS "${FCMP_${NAME}_DEFAULT_DIR}")
    file(REAL_PATH "${FCMP_${NAME}_DEFAULT_DIR}" _fcmp_default)
    if(_fcmp_used STREQUAL _fcmp_default)
      set(FCMP_${NAME}_OVERRIDE no)
      unset(FETCHCONTENT_SOURCE_DIR_${NAME} CACHE)
    endif()
  endif()
endmacro()
fcmp_default_source_dir(JUCE JUCE-${FCMP_JUCE_TAG})
fcmp_default_source_dir(BGFX bgfx.cmake-${FCMP_BGFX_TAG})

# 2. Declarations, identical to HR's for JUCE and bgfx (HR CMakeLists.txt:112-117, 236-241), except that bgfx is
#    SYSTEM EXCLUDE_FROM_ALL (FZ0 errata, R-B0 #13): FCompressor declares bgfx first, so this declaration wins over
#    FunkGui's own SYSTEM EXCLUDE_FROM_ALL one, and without it every GPU `all` build also compiled bimg_encode,
#    bimg_decode and their bundled third-party code; now only what FunkGui::gpu (and a from-source shaderc) link is
#    built, and bgfx's headers are system headers to our -Werror sources. FunkGui: GIT_TAG stays the tag (user
#    decision); the SHA assertion after population refuses a moved tag. No GIT_SHALLOW for a local path.
FetchContent_Declare(JUCE    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git     GIT_TAG ${FCMP_JUCE_TAG} GIT_SHALLOW TRUE)
FetchContent_Declare(bgfx    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git    GIT_TAG ${FCMP_BGFX_TAG} GIT_SHALLOW TRUE
                             SYSTEM EXCLUDE_FROM_ALL)
FetchContent_Declare(FunkGui GIT_REPOSITORY ${FCOMPRESSOR_FUNKGUI_REPO}                     GIT_TAG ${FCMP_FUNKGUI_TAG} GIT_SHALLOW FALSE)

set(FCMP_DEPS_ROWS "")        # name|tag|sha|dir|override|note, one list item per dependency, for fcmp-deps.txt
macro(fcmp_deps_row name tag sha dir override note)
  list(APPEND FCMP_DEPS_ROWS "${name}\t${tag}\t${sha}\t${dir}\t${override}\t${note}")
endmacro()

# 3a. JUCE (every configuration but DSP-only).
if(NOT FCOMPRESSOR_DSP_ONLY)
  FetchContent_MakeAvailable(JUCE)
  set(_hdr "${juce_SOURCE_DIR}/modules/juce_core/system/juce_StandardHeader.h")
  if(NOT EXISTS "${_hdr}")
    message(FATAL_ERROR "FCompressor: ${juce_SOURCE_DIR} is not a JUCE checkout (no ${_hdr})")
  endif()
  file(STRINGS "${_hdr}" _v REGEX "^#define JUCE_(MAJOR_VERSION|MINOR_VERSION|BUILDNUMBER) +[0-9]+")
  set(_jv "")
  foreach(_k MAJOR_VERSION MINOR_VERSION BUILDNUMBER)
    string(REGEX MATCH "#define JUCE_${_k} +([0-9]+)" _m "${_v}")
    list(APPEND _jv "${CMAKE_MATCH_1}")
  endforeach()
  list(JOIN _jv "." _jv)
  if(NOT _jv STREQUAL FCMP_JUCE_TAG)
    message(FATAL_ERROR "FCompressor: JUCE at ${juce_SOURCE_DIR} is ${_jv}, the pin is ${FCMP_JUCE_TAG}")
  endif()
  fcmp_assert_git("${juce_SOURCE_DIR}" ${FCMP_JUCE_SHA} JUCE)
  fcmp_source_dir_used(JUCE "${juce_SOURCE_DIR}")
  # ADR-92: Clang 20 and later put -Wnontrivial-memcall in -Wall, and it fires inside JUCE 8.0.4's bundled HarfBuzz and
  # VST3 SDK, which our targets compile with JUCE's recommended warnings (03 §2.2). Third-party code: silenced on JUCE's
  # module translation units only, as a source property (it follows JUCE's own -Wall, which a target option would not),
  # and only where the compiler has the warning (not AppleClang 17). Our sources keep it.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-Wnontrivial-memcall FCMP_CXX_HAS_WNONTRIVIAL_MEMCALL)
  if(FCMP_CXX_HAS_WNONTRIVIAL_MEMCALL)
    file(GLOB _fcmp_juce_module_tus "${juce_SOURCE_DIR}/modules/juce_*/juce_*.cpp")
    set_source_files_properties(${_fcmp_juce_module_tus} DIRECTORY ${PROJECT_SOURCE_DIR}
                                PROPERTIES COMPILE_OPTIONS -Wno-nontrivial-memcall)
  endif()
  fcmp_deps_row(JUCE ${FCMP_JUCE_TAG} ${FCMP_JUCE_SHA} "${juce_SOURCE_DIR}" ${FCMP_JUCE_OVERRIDE} "version ${_jv}")
endif()

# 3b. bgfx (GPU configuration only), after choosing shaderc (03 §2.5).
macro(fcmp_select_shaderc)
  set(_sc_dir "${FCOMPRESSOR_DEPS_DIR}/tools/shaderc-${FCMP_BGFX_TAG}")
  set(_sc_first "")
  if(EXISTS "${_sc_dir}/shaderc.stamp")
    file(STRINGS "${_sc_dir}/shaderc.stamp" _sc_first LIMIT_COUNT 1)
  endif()
  if(IS_EXECUTABLE "${_sc_dir}/shaderc" AND _sc_first STREQUAL "bgfx.cmake ${FCMP_BGFX_SHA}")
    set(BGFX_BUILD_TOOLS OFF)                       # normal variables (CMP0077), never CACHE ... FORCE (K2 #17)
    set(FUNKGUI_SHADERC "${_sc_dir}/shaderc")
    set(FCMP_SHADERC_KIND prebuilt)
    message(STATUS "FCompressor: shaderc: prebuilt ${FUNKGUI_SHADERC} (stamp: ${_sc_first})")
  else()
    # Fallback: build shaderc from the pinned sources with HR's tool options (HR CMakeLists.txt:307-313).
    set(BGFX_BUILD_TOOLS ON)
    set(BGFX_BUILD_TOOLS_SHADER ON)
    set(BGFX_BUILD_TOOLS_TEXTURE OFF)
    set(BGFX_BUILD_TOOLS_GEOMETRY OFF)
    set(BGFX_BUILD_TOOLS_BIN2C OFF)
    unset(FUNKGUI_SHADERC)
    set(FCMP_SHADERC_KIND built)
    message(STATUS "FCompressor: shaderc: no prebuilt stamped 'bgfx.cmake ${FCMP_BGFX_SHA}' in ${_sc_dir}; "
                   "building it from bgfx.cmake (about 2,000 CPU-s)")
  endif()
endmacro()

if(NOT FCOMPRESSOR_HEADLESS)
  fcmp_select_shaderc()
  set(BGFX_BUILD_EXAMPLES OFF)
  set(BGFX_INSTALL OFF)
  # Linux (ADR-92): the editor draws into an X11 child window (JUCE's peers are X11; a Wayland desktop runs them through
  # XWayland), so bgfx never sees a wl_surface, and with its Wayland backend bgfx would link libwayland-egl into the
  # plugin. As FunkGui's cmake/FunkGuiDeps.cmake sets it for its own fetch.
  set(BGFX_WITH_WAYLAND OFF)
  FetchContent_MakeAvailable(bgfx)
  # Hidden symbols: release.sh checks with `nm -gU` that a plugin binary exports only its entry points (K2 #26f).
  foreach(_t bgfx bx bimg bimg_decode bimg_encode)
    if(TARGET ${_t})
      set_target_properties(${_t} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
      # Linux (ADR-92): the pinned bgfx warns under Clang 22 in its own sources (renderer_gl.cpp's
      # -Wtautological-constant-compare, bimg's miniz #pragma message). Third-party code, never edited (03 §2.2).
      if(NOT APPLE)
        target_compile_options(${_t} PRIVATE -w)
      endif()
    endif()
  endforeach()
  # metal-cpp's private implementation (bgfx/src/renderer_mtl.cpp) marks ~2,000 MTL/NS/CA symbols visibility("default")
  # unless this is defined; the preset above cannot hide them (found by R1's release.sh dry run).
  target_compile_definitions(bgfx PRIVATE METALCPP_SYMBOL_VISIBILITY_HIDDEN)
  file(STRINGS "${bgfx_SOURCE_DIR}/bgfx/include/bgfx/defines.h" _api
       REGEX "^#define BGFX_API_VERSION UINT32_C\\(${FCMP_BGFX_API}\\)")
  if(NOT _api)
    message(FATAL_ERROR "FCompressor: bgfx at ${bgfx_SOURCE_DIR} is not API ${FCMP_BGFX_API} (bgfx.cmake ${FCMP_BGFX_TAG})")
  endif()
  fcmp_assert_git("${bgfx_SOURCE_DIR}" ${FCMP_BGFX_SHA} bgfx.cmake)
  foreach(_kv IN LISTS FCMP_BGFX_SUB_SHAS)
    string(REPLACE "=" ";" _kv "${_kv}")
    list(GET _kv 0 _sub)
    list(GET _kv 1 _sha)
    fcmp_assert_git("${bgfx_SOURCE_DIR}/${_sub}" ${_sha} bgfx.cmake/${_sub})
  endforeach()
  fcmp_source_dir_used(BGFX "${bgfx_SOURCE_DIR}")
  fcmp_deps_row(bgfx.cmake ${FCMP_BGFX_TAG} ${FCMP_BGFX_SHA} "${bgfx_SOURCE_DIR}" ${FCMP_BGFX_OVERRIDE} "api ${FCMP_BGFX_API}")
  if(FCMP_SHADERC_KIND STREQUAL "prebuilt")
    fcmp_deps_row(shaderc ${FCMP_BGFX_TAG} ${FCMP_BGFX_SHA} "${FUNKGUI_SHADERC}" no prebuilt)
  else()
    fcmp_deps_row(shaderc ${FCMP_BGFX_TAG} ${FCMP_BGFX_SHA} "${bgfx_BINARY_DIR}" no built)
  endif()
endif()

# 3b'. Emscripten, the web configuration's pinned tool (ADR-93): its version is a provenance row.
if(FCOMPRESSOR_WEB)
  # The compiler is part of the arithmetic's provenance (it is Clang, and the runtime library is its own): one pinned
  # version for the lead, the agents and CI (emsdk installs exactly it), moved deliberately like any other pin.
  set(FCMP_EMSCRIPTEN_VERSION 6.0.3)
  if(NOT "${EMSCRIPTEN_VERSION}" VERSION_EQUAL "${FCMP_EMSCRIPTEN_VERSION}")
    message(FATAL_ERROR "FCompressor: the web configuration is pinned to Emscripten ${FCMP_EMSCRIPTEN_VERSION}, but "
                        "the toolchain is ${EMSCRIPTEN_VERSION} (${EMSCRIPTEN_ROOT_PATH}). Install that version "
                        "(emsdk install ${FCMP_EMSCRIPTEN_VERSION}) or move the pin in cmake/FcmpDeps.cmake.")
  endif()
  fcmp_deps_row(Emscripten "${EMSCRIPTEN_VERSION}" - "${EMSCRIPTEN_ROOT_PATH}" no "wasm32, node ${CMAKE_CROSSCOMPILING_EMULATOR}")
endif()

# 3c. FunkGui: its options as normal variables (02 §1.9), then the override check BEFORE its CMake runs. Every
#     configuration fetches it. The web configuration (ADR-93, web Sprint C) takes the JUCE-free core and FunkGui::web
#     and none of FunkGui's own tools; its engine module still links none of it.
if(FCOMPRESSOR_WEB)
  set(FUNKGUI_WITH_JUCE OFF)
  set(FUNKGUI_WITH_BGFX OFF)
  set(FUNKGUI_WITH_PRESETS OFF)
  set(FUNKGUI_HARNESS_ONLY OFF)
  set(FUNKGUI_BUILD_TOOLS OFF)
else()
  if(FCOMPRESSOR_HEADLESS)
    set(FUNKGUI_WITH_BGFX OFF)
  else()
    set(FUNKGUI_WITH_BGFX ON)
  endif()
  if(FCOMPRESSOR_DSP_ONLY)                  # no JUCE: FunkGui provides FunkGui::harness only
    set(FUNKGUI_WITH_PRESETS OFF)
    set(FUNKGUI_HARNESS_ONLY ON)
  else()
    set(FUNKGUI_WITH_PRESETS ON)
    set(FUNKGUI_HARNESS_ONLY OFF)
  endif()
  set(FUNKGUI_BUILD_TOOLS ON)               # funkgui_framerender for the DoD and verify-gui-live (K2 #26d)
endif()

set(FCMP_FUNKGUI_OVERRIDE "")
if(FETCHCONTENT_SOURCE_DIR_FUNKGUI)
  set(_dir "${FETCHCONTENT_SOURCE_DIR_FUNKGUI}")
  fcmp_git_root(_is_root "${_dir}")
  if(NOT _is_root)
    message(FATAL_ERROR "FCompressor: FunkGui override ${_dir} is not the root of a git checkout. Allowed overrides: "
                        "your own FunkGui worktree, or a lead-made FunkGui.wt/pin-<sha7> (03 §4.5)")
  endif()
  fcmp_git(_anc "${_dir}" merge-base --is-ancestor "${FCMP_FUNKGUI_SHA}^{commit}" HEAD)
  fcmp_git(_head "${_dir}" rev-parse -q --verify "HEAD^{commit}")
  if(NOT _anc_RESULT EQUAL 0)
    message(FATAL_ERROR "FCompressor: FunkGui override ${_dir} (HEAD ${_head}) does not descend from the pin "
                        "${FCMP_FUNKGUI_TAG} = ${FCMP_FUNKGUI_SHA} (git merge-base --is-ancestor exited ${_anc_RESULT}"
                        "${_anc_ERROR}). Use your own FunkGui worktree or a lead-made FunkGui.wt/pin-<sha7> (K2 #26b)")
  endif()
  fcmp_git(_desc "${_dir}" describe --tags --always --dirty)
  set(FCMP_FUNKGUI_OVERRIDE "${_dir}")
  message(WARNING "FunkGui OVERRIDE ${_dir} @ ${_desc} (pin ${FCMP_FUNKGUI_TAG}). Declare it in the handoff; "
                  "Scripts/verify.sh --integration refuses it. Remove it with -UFETCHCONTENT_SOURCE_DIR_FUNKGUI.")
endif()

FetchContent_MakeAvailable(FunkGui)
FetchContent_GetProperties(FunkGui SOURCE_DIR FCMP_FUNKGUI_DIR)
fcmp_git(_fg_head "${FCMP_FUNKGUI_DIR}" rev-parse -q --verify "HEAD^{commit}")
if(FCMP_FUNKGUI_OVERRIDE)
  fcmp_deps_row(FunkGui "${_desc}" "${_fg_head}" "${FCMP_FUNKGUI_DIR}" yes "version ${FUNKGUI_VERSION}, pin ${FCMP_FUNKGUI_TAG}")
else()
  fcmp_assert_git("${FCMP_FUNKGUI_DIR}" ${FCMP_FUNKGUI_SHA} FunkGui)
  if(NOT FUNKGUI_VERSION STREQUAL FCMP_FUNKGUI_VERSION)
    message(FATAL_ERROR "FCompressor: FunkGui ${FCMP_FUNKGUI_TAG} reports FUNKGUI_VERSION '${FUNKGUI_VERSION}', "
                        "the pin says ${FCMP_FUNKGUI_VERSION}")
  endif()
  fcmp_deps_row(FunkGui ${FCMP_FUNKGUI_TAG} ${_fg_head} "${FCMP_FUNKGUI_DIR}" no "version ${FUNKGUI_VERSION}")   # the commit
endif()

# The target and function names FCompressor links against freeze at FZ0 (SPRINTS §0.3); fail here, not at link time.
set(_fg_need FunkGui::harness)
if(FCOMPRESSOR_WEB)
  list(APPEND _fg_need FunkGui::core FunkGui::web)
elseif(NOT FCOMPRESSOR_DSP_ONLY)
  list(APPEND _fg_need FunkGui::core FunkGui::presets)
endif()
if(NOT FCOMPRESSOR_HEADLESS)
  list(APPEND _fg_need FunkGui::gpu)
endif()
foreach(_t IN LISTS _fg_need)
  if(NOT TARGET ${_t})
    message(FATAL_ERROR "FCompressor: FunkGui ${FUNKGUI_VERSION} at ${FCMP_FUNKGUI_DIR} defines no ${_t}")
  endif()
endforeach()
foreach(_f funkgui_configure_product funkgui_compile_shaders funkgui_add_font)
  if(NOT COMMAND ${_f})
    message(FATAL_ERROR "FCompressor: FunkGui ${FUNKGUI_VERSION} at ${FCMP_FUNKGUI_DIR} defines no ${_f}()")
  endif()
endforeach()

# 4. Provenance. One row per dependency this configuration uses (TAB-separated; see the header line).
set(_txt "# fcmp-deps 1 (cmake/FcmpDeps.cmake): name\ttag\tsha\tdir\toverride\tnote\n")
foreach(_row IN LISTS FCMP_DEPS_ROWS)
  string(APPEND _txt "${_row}\n")
endforeach()
string(APPEND _txt "configuration\t${FCMP_CONFIGURATION}\t-\t${PROJECT_SOURCE_DIR}\tno\t${CMAKE_BUILD_TYPE} [${FCMP_ARCHS}]\n")
file(WRITE "${CMAKE_BINARY_DIR}/fcmp-deps.txt" "${_txt}")
