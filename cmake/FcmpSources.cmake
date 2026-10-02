# cmake/FcmpSources.cmake: the glob -> target map (03 §2.1; K3 #2), product identity, and the generated files.
# Written once in Sprint 0 (B0) and never edited again: a new Mode, policy, view, probe or factory file is picked up by
# a CONFIGURE_DEPENDS glob (Ninja re-checks them on every build), so it needs no CMake edit and no manual reconfigure.
# Every glob is rooted at ${PROJECT_SOURCE_DIR}/Source or /Tools, never ** from the repository root, because agent
# worktrees live under .claude/worktrees/ inside the checkout (K3 #26).
#
#   Source/fcdsp/**/*.cpp (+ generated BuildInfo.cpp)          FCMP_DSP_SOURCES         -> fcdsp
#   Source/plugin/*.cpp, Source/plugin/factory/*.cpp,
#     Source/plugin/portable/*.cpp, minus CreateEditor*.cpp     FCMP_PLUGIN_SOURCES      -> plugin, fcmp_probe_plugin
#     (portable/: the plugin's model code with no JUCE in it, lint rule plugin.portable; the browser demo's facade
#     builds from the same files, ADR-93)
#   Source/plugin/CreateEditorGpu.cpp if present (GPU only),
#     else CreateEditorGeneric.cpp                              FCMP_CREATE_EDITOR       -> plugin
#   Source/plugin/CreateEditorGeneric.cpp                       FCMP_CREATE_EDITOR_GENERIC -> fcmp_probe_plugin
#   Source/editor/**/*.cpp, minus gpu/                          FCMP_EDITOR_SOURCES      -> plugin, fcmp_probe_plugin
#   Source/editor/gpu/*.{cpp,mm}                                FCMP_EDITOR_GPU_SOURCES  -> GPU plugin only
#   Tools/probes/common/*.cpp                                   FCMP_PROBE_COMMON_SOURCES -> both probe executables
#   Tools/probes/dsp/*.cpp                                      FCMP_PROBE_DSP_SOURCES   -> fcmp_probe_dsp
#   Tools/probes/plugin/*.cpp                                   FCMP_PROBE_PLUGIN_SOURCES -> fcmp_probe_plugin
#   Tools/bench/*.cpp                                           FCMP_BENCH_SOURCES       -> fcmp_bench (if any)
#   Source/plugin/factory/*.inc, in Modes.def slot order        ${FCMP_GENERATED_DIR}/fcmp/FactoryIncludes.h
#
# Modes.def (01 §8.1) is parsed here into FCMP_MODE_KEYS / FCMP_MODE_SLOTS (registered Modes, slot order is file
# order) and FCMP_RETIRED_KEYS. It drives only the CTest matrix (FcmpProbes.cmake) and the factory include order.
include_guard(GLOBAL)

set(FCMP_SOURCE_ROOT ${PROJECT_SOURCE_DIR}/Source)
set(FCMP_TOOLS_ROOT  ${PROJECT_SOURCE_DIR}/Tools)
set(FCMP_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${FCMP_GENERATED_DIR}/fcmp)

# ---- product identity: the one place (juce_add_plugin fields, funkgui_configure_product, FcmpProduct.h) ----------
set(FCMP_PRODUCT_NAME       "FCompressor")
set(FCMP_COMPANY_NAME       "Funk")
set(FCMP_COMPANY_COPYRIGHT  "Copyright (c) 2026 Sean Funk")
set(FCMP_MANUFACTURER_CODE  "Funk")
set(FCMP_PLUGIN_CODE        "Fcmp")
set(FCMP_BUNDLE_ID          "com.funk.fcompressor")
set(FCMP_ENV_PREFIX         "FCMP_")
set(FCMP_OBJC_PREFIX        "Fcmp")
set(FCMP_PREFS_FOLDER       "FCompressor")
configure_file(${PROJECT_SOURCE_DIR}/cmake/FcmpProduct.h.in ${FCMP_GENERATED_DIR}/FcmpProduct.h @ONLY)

# ---- Modes.def -----------------------------------------------------------------------------------------------------
set(FCMP_MODES_DEF ${FCMP_SOURCE_ROOT}/fcdsp/modes/Modes.def)
if(NOT EXISTS ${FCMP_MODES_DEF})
  message(FATAL_ERROR "FCompressor: ${FCMP_MODES_DEF} is missing")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FCMP_MODES_DEF})   # a new Mode re-runs configure
set(FCMP_MODE_KEYS "")
set(FCMP_MODE_SLOTS "")
set(FCMP_RETIRED_KEYS "")
set(_slots_seen "")
file(STRINGS ${FCMP_MODES_DEF} _lines REGEX "^FCMP_")    # a commented reservation (// FCMP_MODE(...)) never matches
foreach(_l IN LISTS _lines)
  if(_l MATCHES "^FCMP_MODE\\( *([0-9]+) *, *\"([a-z0-9-]+)\" *, *([A-Za-z_][A-Za-z0-9_]*) *\\) *(//.*)?$")
    set(_slot ${CMAKE_MATCH_1})
    set(_key ${CMAKE_MATCH_2})
    list(APPEND FCMP_MODE_KEYS ${_key})
    list(APPEND FCMP_MODE_SLOTS ${_slot})
  elseif(_l MATCHES "^FCMP_RETIRED\\( *([0-9]+) *, *\"([a-z0-9-]+)\" *, *\"([a-z0-9-]+)\" *\\) *(//.*)?$")
    set(_slot ${CMAKE_MATCH_1})
    set(_key ${CMAKE_MATCH_2})
    list(APPEND FCMP_RETIRED_KEYS ${_key})
  else()
    message(FATAL_ERROR "Modes.def: malformed line: ${_l}")
  endif()
  string(LENGTH "${_key}" _len)
  if(_slot GREATER 127 OR _len GREATER 24)
    message(FATAL_ERROR "Modes.def: slot ${_slot} or key '${_key}' out of range (slots 0..127, keys <= 24 chars)")
  endif()
  if(_slot IN_LIST _slots_seen)
    message(FATAL_ERROR "Modes.def: slot ${_slot} appears twice")
  endif()
  list(APPEND _slots_seen ${_slot})
endforeach()
set(_all_keys "${FCMP_MODE_KEYS};${FCMP_RETIRED_KEYS}")
list(REMOVE_ITEM _all_keys "")
set(_uniq "${_all_keys}")
list(REMOVE_DUPLICATES _uniq)
if(NOT "${_uniq}" STREQUAL "${_all_keys}")
  message(FATAL_ERROR "Modes.def: a key appears twice (${_all_keys})")
endif()
list(LENGTH FCMP_MODE_KEYS FCMP_MODE_COUNT)

# ---- source globs --------------------------------------------------------------------------------------------------
file(GLOB_RECURSE FCMP_DSP_SOURCES CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/fcdsp/*.cpp)
list(APPEND FCMP_DSP_SOURCES ${FCMP_GENERATED_DIR}/fcmp/BuildInfo.cpp)

file(GLOB FCMP_PLUGIN_SOURCES CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/plugin/*.cpp ${FCMP_SOURCE_ROOT}/plugin/factory/*.cpp
                                                ${FCMP_SOURCE_ROOT}/plugin/portable/*.cpp)
list(FILTER FCMP_PLUGIN_SOURCES EXCLUDE REGEX "/CreateEditor[^/]*\\.cpp$")

file(GLOB_RECURSE FCMP_EDITOR_SOURCES CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/editor/*.cpp)
list(FILTER FCMP_EDITOR_SOURCES EXCLUDE REGEX "/Source/editor/gpu/")
file(GLOB FCMP_EDITOR_GPU_SOURCES CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/editor/gpu/*.cpp ${FCMP_SOURCE_ROOT}/editor/gpu/*.mm)

file(GLOB FCMP_PROBE_COMMON_SOURCES CONFIGURE_DEPENDS ${FCMP_TOOLS_ROOT}/probes/common/*.cpp)
file(GLOB FCMP_PROBE_DSP_SOURCES    CONFIGURE_DEPENDS ${FCMP_TOOLS_ROOT}/probes/dsp/*.cpp)
file(GLOB FCMP_PROBE_PLUGIN_SOURCES CONFIGURE_DEPENDS ${FCMP_TOOLS_ROOT}/probes/plugin/*.cpp)
file(GLOB FCMP_BENCH_SOURCES        CONFIGURE_DEPENDS ${FCMP_TOOLS_ROOT}/bench/*.cpp)
# The web demo (ADR-93; cmake/FcmpWeb.cmake): the engine wrapper names neither JUCE nor Emscripten (lint web.engine),
# so the same sources build natively for its checks; Tools/web holds those checks' programs.
file(GLOB FCMP_WEB_ENGINE_SOURCES   CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/web/engine/*.cpp)
file(GLOB FCMP_WEB_TOOL_SOURCES     CONFIGURE_DEPENDS ${FCMP_TOOLS_ROOT}/web/*.cpp)
# The web facade (ADR-93, Sprint C): the browser's ProcessorFacade, portable C++ (lint web.facade). It builds natively
# into fcmp_probe_plugin, where proc.webnull, proc.webpresets and ui.web hold it to the processor, and as wasm32 with
# the editor. FCMP_PLUGIN_PORTABLE_SOURCES is the part of FCMP_PLUGIN_SOURCES the web build shares (lint
# plugin.portable).
file(GLOB FCMP_WEB_FACADE_SOURCES   CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/web/facade/*.cpp)
file(GLOB FCMP_PLUGIN_PORTABLE_SOURCES CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/plugin/portable/*.cpp)
# The editor module's glue (ADR-93, Sprint D): WebMain and PortLink, the only sources that include an Emscripten header
# (lint web.emscripten, web.ui). They build for the web only. Tools/web/port holds the node check of PortLink.
file(GLOB FCMP_WEB_UI_SOURCES       CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/web/ui/*.cpp)
file(GLOB FCMP_WEB_PORT_SOURCES     CONFIGURE_DEPENDS ${FCMP_TOOLS_ROOT}/web/port/*.cpp)

# ---- editor choice (SPRINTS §7 D22): exactly one CreateEditor*.cpp per target, chosen here, never by #if ------------
set(FCMP_CREATE_EDITOR_GENERIC ${FCMP_SOURCE_ROOT}/plugin/CreateEditorGeneric.cpp)
set(FCMP_GPU_EDITOR OFF)
if(NOT FCOMPRESSOR_DSP_ONLY)
  if(NOT EXISTS ${FCMP_CREATE_EDITOR_GENERIC})
    message(FATAL_ERROR "FCompressor: ${FCMP_CREATE_EDITOR_GENERIC} is missing")
  endif()
  file(GLOB _gpu_editor CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/plugin/CreateEditorGpu.cpp)   # re-checked every build
  if(FCOMPRESSOR_HEADLESS)
    set(FCMP_CREATE_EDITOR ${FCMP_CREATE_EDITOR_GENERIC})
    message(STATUS "FCompressor: editor: Generic (headless configuration)")
  elseif(_gpu_editor)
    set(FCMP_CREATE_EDITOR ${_gpu_editor})
    set(FCMP_GPU_EDITOR ON)                                  # FCOMPRESSOR_GPU_EDITOR=1 only for this case
    message(STATUS "FCompressor: GPU editor: Gpu (Source/plugin/CreateEditorGpu.cpp)")
  else()
    set(FCMP_CREATE_EDITOR ${FCMP_CREATE_EDITOR_GENERIC})
    message(STATUS "FCompressor: GPU editor: Generic (Source/plugin/CreateEditorGpu.cpp not present yet)")
  endif()
endif()

# ---- BuildInfo.cpp: keeps fcdsp non-empty and carries the flags line (release.sh's MANIFEST.txt, 03 §5 step 7) ----
if(CMAKE_BUILD_TYPE)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg)
  set(_cfg_flags "${CMAKE_CXX_FLAGS_${_cfg}}")
else()
  set(_cfg_flags "")
endif()
if(FCOMPRESSOR_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set(_lto on)
else()
  set(_lto off)
endif()
list(JOIN FCMP_ARCHS "," _archs)
string(STRIP "${CMAKE_CXX_FLAGS} ${_cfg_flags} ${FCMP_FLAGS_LINE}" _flags)
string(CONCAT FCMP_BUILDINFO_LINE "FCompressor ${PROJECT_VERSION} ${FCMP_CONFIGURATION} config=${CMAKE_BUILD_TYPE} archs=${_archs} "
                        "compiler=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION} lto=${_lto} flags=${_flags}")
string(REPLACE "\\" "\\\\" _lit "${FCMP_BUILDINFO_LINE}")
string(REPLACE "\"" "\\\"" _lit "${_lit}")
file(CONFIGURE OUTPUT ${FCMP_GENERATED_DIR}/fcmp/BuildInfo.cpp CONTENT [==[
// Generated by cmake/FcmpSources.cmake at configure time. Do not edit.
// It keeps the fcdsp archive non-empty before any Source/fcdsp/*.cpp exists, and it carries the flags line that
// Scripts/release.sh copies into MANIFEST.txt (03 §5 step 7), read from the marker line below.
// FCMP_FLAGS_LINE: @FCMP_BUILDINFO_LINE@
namespace fcdsp::buildinfo
{
extern const char kFlagsLine[];
constinit const char kFlagsLine[] = "@_lit@";
} // namespace fcdsp::buildinfo
]==] @ONLY)

# ---- FactoryIncludes.h: Source/plugin/factory/<key>.inc in Modes.def slot order (01 §9.2; K1 #31, K3 #18) ----------
# FCMP_FACTORY_BANK_REVISION = the first 32 bits of the SHA-256 of the included files' concatenated contents, in that
# order; computed at configure time, never a hand-edited counter. Every .inc is a configure dependency, so an edit
# re-runs configure and moves the revision. An .inc whose key has no FCMP_MODE line is not included (its Mode is not
# registered, like an unregistered Mode's TU).
file(GLOB _incs CONFIGURE_DEPENDS ${FCMP_SOURCE_ROOT}/plugin/factory/*.inc)
set(_ordered "")
set(_concat "")
foreach(_key IN LISTS FCMP_MODE_KEYS)
  set(_inc ${FCMP_SOURCE_ROOT}/plugin/factory/${_key}.inc)
  if(_inc IN_LIST _incs)
    list(APPEND _ordered ${_key})
    file(READ ${_inc} _body)
    string(APPEND _concat "${_body}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inc})
    list(REMOVE_ITEM _incs ${_inc})
  endif()
endforeach()
foreach(_inc IN LISTS _incs)
  message(STATUS "FCompressor: ${_inc} is not included in the factory bank (no FCMP_MODE line for its key)")
endforeach()
string(SHA256 _sha "${_concat}")
string(SUBSTRING "${_sha}" 0 8 _rev)
set(FCMP_FACTORY_BANK_REVISION "0x${_rev}u")
set(_fi "// Generated by cmake/FcmpSources.cmake from Source/plugin/factory/*.inc, in Modes.def slot order. Do not edit.\n")
string(APPEND _fi "// Include it once for FCMP_FACTORY_BANK_REVISION; include it again with FCMP_FACTORY_ENTRIES defined, inside\n")
string(APPEND _fi "// the bank's initialiser, to expand the per-Mode entry files (Source/plugin/portable/FactoryData.cpp).\n")
string(APPEND _fi "#ifndef FCMP_FACTORY_BANK_REVISION\n")
string(APPEND _fi "#define FCMP_FACTORY_BANK_REVISION ${FCMP_FACTORY_BANK_REVISION}   // SHA-256[0:32] of the files below\n")
string(APPEND _fi "#endif\n")
list(LENGTH _ordered _n)
string(APPEND _fi "#define FCMP_FACTORY_INC_COUNT ${_n}\n")
string(APPEND _fi "#ifdef FCMP_FACTORY_ENTRIES\n")
foreach(_key IN LISTS _ordered)
  string(APPEND _fi "#include \"plugin/factory/${_key}.inc\"\n")
endforeach()
string(APPEND _fi "#endif\n")
file(CONFIGURE OUTPUT ${FCMP_GENERATED_DIR}/fcmp/FactoryIncludes.h CONTENT "${_fi}" @ONLY)
