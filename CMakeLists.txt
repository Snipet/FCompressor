# FCompressor: all-in-one compressor (AU / VST3 / Standalone on macOS, VST3 / Standalone on Linux: ADR-91) on JUCE 8.0.4
# and FunkGui.
#
# The build is docs/design/03-build-verify-process.md §2. Written once in Sprint 0 (card B0) and lead-owned from FZ0:
# every source list is a per-directory glob (cmake/FcmpSources.cmake) and every probe registers itself from its first
# line (cmake/FcmpProbes.cmake), so adding a Mode, a stage policy, a view or a probe edits no CMake.
#
# Three configurations (03 §0.6):
#   GPU        (default)                   bgfx + the real editor
#   headless   -DFCOMPRESSOR_HEADLESS=ON   no bgfx; the editor is juce::GenericAudioProcessorEditor; every probe builds
#   DSP-only   -DFCOMPRESSOR_DSP_ONLY=ON   no JUCE at all: fcdsp, fcmp_probe_dsp and the dsp.* tests (configure ~2 s)
# Use the presets (CMakePresets.json): dsp, agent, agent-gui for agents; owner, lead, ... for the lead.

cmake_minimum_required(VERSION 3.30)    # CMP0168: FetchContent populates directly, without a sub-build

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS")

# Universal is opt-in and not sticky: a NORMAL variable set before project() (HR CMakeLists.txt:30-35). An explicit
# -DCMAKE_OSX_ARCHITECTURES (e.g. x86_64 for the lead-x86 preset) is honoured while the option is OFF.
option(FCOMPRESSOR_UNIVERSAL "Build arm64 + x86_64 (release builds only)" OFF)
if(FCOMPRESSOR_UNIVERSAL)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

# Linux (ADR-91): Clang, as on macOS, unless the caller chose a compiler (CC/CXX or -DCMAKE_CXX_COMPILER); a first
# configure only, since the choice is cached. cmake/FcmpPlatform.cmake refuses any other compiler.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT DEFINED CMAKE_CXX_COMPILER
   AND NOT DEFINED ENV{CXX} AND NOT DEFINED ENV{CC})
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
endif()

project(FCompressor VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules anywhere: without this, Ninja and a Clang with clang-scan-deps (Linux) scan every source twice.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Release unless told otherwise (HR :48-52): an empty build type means no optimisation at all.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel)
endif()

# ---- options (03 §2.2) --------------------------------------------------------------------------------------------
option(FCOMPRESSOR_HEADLESS            "No bgfx, no shaders, no FunkGui::gpu; generic editor; every probe builds" OFF)
option(FCOMPRESSOR_DSP_ONLY            "No JUCE at all: fcdsp, fcmp_probe_dsp, fcmp_bench, dsp.* tests"           OFF)
option(FCOMPRESSOR_INSTALL_AFTER_BUILD "Copy the plugins into ~/Library/Audio/Plug-Ins or ~/.vst3 after each build" OFF)
option(FCOMPRESSOR_LTO                 "-flto in Release: the plugin, fcdsp's objects, and probe links"          ON)
option(FCOMPRESSOR_WERROR              "-Werror on our own sources (never on JUCE or dependency TUs)"            ON)
option(FCOMPRESSOR_RELEASE             "Release build: dsp.registry fails on any provisional Mode"               OFF)
option(FCOMPRESSOR_RTSAN               "-fsanitize=realtime, or the RtInterposer fallback where unsupported"     OFF)
set(FCOMPRESSOR_DEPS_DIR "$ENV{HOME}/audio/.deps" CACHE PATH
    "Machine-wide read-only dependency cache written by Scripts/deps.sh (03 §2.4)")
# FunkGui's repository (ADR-87): this machine's local repository when it has one, so the lead's and the agents' builds
# never go to the network for it, else the public GitHub repository (CI, anyone else). The pinned tag and its SHA
# assertion are cmake/FcmpDeps.cmake's either way.
if(EXISTS "$ENV{HOME}/audio/libraries/FunkGui/.git")
  set(_fcmp_funkgui_repo "$ENV{HOME}/audio/libraries/FunkGui")
else()
  set(_fcmp_funkgui_repo "https://github.com/Snipet/FunkGui.git")
endif()
set(FCOMPRESSOR_FUNKGUI_REPO "${_fcmp_funkgui_repo}" CACHE STRING
    "FunkGui GIT_REPOSITORY (the local repository if present, else GitHub; the pinned tag is cmake/FcmpDeps.cmake's)")
set(FCMP_TEST_JOBS 4 CACHE STRING "ctest -j for the verify convenience target")

if(FCOMPRESSOR_DSP_ONLY)
  set(FCOMPRESSOR_HEADLESS ON)          # DSP-only implies headless (a normal variable shadows the option)
endif()
if(FCOMPRESSOR_DSP_ONLY)
  set(FCMP_CONFIGURATION "DSP-only")
elseif(FCOMPRESSOR_HEADLESS)
  set(FCMP_CONFIGURATION "headless")
else()
  set(FCMP_CONFIGURATION "GPU")
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(FcmpPlatform)   # FCMP_PLATFORM, FCMP_PLUGIN_FORMATS; Clang only; JUCE 8.0.4's Clang workaround (ADR-91)
include(FcmpArch)       # FCMP_ARCHS, FCMP_RUN_ARCH, FCMP_CAN_RUN_PROBES, fcmp_flags / fcmp_lto / fcmp_warnings
include(FcmpDeps)       # JUCE -> bgfx (GPU) -> FunkGui, with version and SHA assertions; fcmp-deps.txt
include(FcmpSources)    # the glob -> target map (FCMP_*_SOURCES), BuildInfo.cpp, FcmpProduct.h, FactoryIncludes.h

# fcdsp: the JUCE-free engine (01 §2.1). The generated BuildInfo.cpp keeps it non-empty before any fcdsp .cpp exists.
add_library(fcdsp STATIC ${FCMP_DSP_SOURCES})
# Hidden, so a plugin binary exports only its entry points (K2 #26f; release.sh checks nm -gU; found by R1's dry run).
# Position-independent, because it links into the plugin's shared objects (ADR-91): Linux compilers build a static
# library as PIE code by default, and under LTO its PIE bitcode made the merged plugin module PIE, whose local-exec TLS
# accesses (JUCE's thread_locals) a shared object cannot link. macOS code is always position-independent.
set_target_properties(fcdsp PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
                                       POSITION_INDEPENDENT_CODE ON)
target_link_libraries(fcdsp PUBLIC fcmp_flags PRIVATE fcmp_warnings fcmp_lto)
target_include_directories(fcdsp PUBLIC ${PROJECT_SOURCE_DIR}/Source)

if(NOT FCOMPRESSOR_DSP_ONLY)
  include(FcmpPlugin)   # juce_add_plugin(FCompressor ...), editor choice, PRIVATE FunkGui links and their assert
endif()

enable_testing()
include(FcmpProbes)     # probe executables, self-registered CTest tests over Modes.def, lints, verify targets

message(STATUS "FCompressor ${PROJECT_VERSION}: ${FCMP_PLATFORM}, ${FCMP_CONFIGURATION}, ${CMAKE_BUILD_TYPE}, "
               "archs [${FCMP_ARCHS}], probes run as ${FCMP_RUN_ARCH} (runnable: ${FCMP_CAN_RUN_PROBES}), LTO ${FCOMPRESSOR_LTO}, "
               "Werror ${FCOMPRESSOR_WERROR}, install ${FCOMPRESSOR_INSTALL_AFTER_BUILD}")
