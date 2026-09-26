# FCompressor: all-in-one compressor (AU / VST3 / Standalone, macOS) on JUCE 8.0.4 and FunkGui.
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

project(FCompressor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release unless told otherwise (HR :48-52): an empty build type means no optimisation at all.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel)
endif()

if(NOT APPLE)
  message(FATAL_ERROR "FCompressor targets macOS only.")
endif()

# ---- options (03 §2.2) --------------------------------------------------------------------------------------------
option(FCOMPRESSOR_HEADLESS            "No bgfx, no shaders, no FunkGui::gpu; generic editor; every probe builds" OFF)
option(FCOMPRESSOR_DSP_ONLY            "No JUCE at all: fcdsp, fcmp_probe_dsp, fcmp_bench, dsp.* tests"           OFF)
option(FCOMPRESSOR_INSTALL_AFTER_BUILD "Copy the plugins into ~/Library/Audio/Plug-Ins after each build"         ON)
option(FCOMPRESSOR_LTO                 "-flto in Release: the plugin, fcdsp's objects, and probe links"          ON)
option(FCOMPRESSOR_WERROR              "-Werror on our own sources (never on JUCE or dependency TUs)"            ON)
option(FCOMPRESSOR_RELEASE             "Release build: dsp.registry fails on any provisional Mode"               OFF)
option(FCOMPRESSOR_RTSAN               "-fsanitize=realtime, or the RtInterposer fallback where unsupported"     OFF)
set(FCOMPRESSOR_DEPS_DIR "$ENV{HOME}/audio/.deps" CACHE PATH
    "Machine-wide read-only dependency cache written by Scripts/deps.sh (03 §2.4)")
set(FCOMPRESSOR_FUNKGUI_REPO "/Users/seanfunk/audio/libraries/FunkGui" CACHE STRING
    "FunkGui GIT_REPOSITORY (a local repository; the pinned tag is cmake/FcmpDeps.cmake's)")
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
include(FcmpArch)       # FCMP_ARCHS, FCMP_RUN_ARCH, FCMP_CAN_RUN_PROBES, fcmp_flags / fcmp_lto / fcmp_warnings
include(FcmpDeps)       # JUCE -> bgfx (GPU) -> FunkGui, with version and SHA assertions; fcmp-deps.txt
include(FcmpSources)    # the glob -> target map (FCMP_*_SOURCES), BuildInfo.cpp, FcmpProduct.h, FactoryIncludes.h

# fcdsp: the JUCE-free engine (01 §2.1). The generated BuildInfo.cpp keeps it non-empty before any fcdsp .cpp exists.
add_library(fcdsp STATIC ${FCMP_DSP_SOURCES})
# Hidden, so a plugin binary exports only its entry points (K2 #26f; release.sh checks nm -gU; found by R1's dry run).
set_target_properties(fcdsp PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_libraries(fcdsp PUBLIC fcmp_flags PRIVATE fcmp_warnings fcmp_lto)
target_include_directories(fcdsp PUBLIC ${PROJECT_SOURCE_DIR}/Source)

if(NOT FCOMPRESSOR_DSP_ONLY)
  include(FcmpPlugin)   # juce_add_plugin(FCompressor ...), editor choice, PRIVATE FunkGui links and their assert
endif()

enable_testing()
include(FcmpProbes)     # probe executables, self-registered CTest tests over Modes.def, lints, verify targets

message(STATUS "FCompressor ${PROJECT_VERSION}: ${FCMP_CONFIGURATION}, ${CMAKE_BUILD_TYPE}, archs [${FCMP_ARCHS}], "
               "probes run as ${FCMP_RUN_ARCH} (runnable: ${FCMP_CAN_RUN_PROBES}), LTO ${FCOMPRESSOR_LTO}, "
               "Werror ${FCOMPRESSOR_WERROR}, install ${FCOMPRESSOR_INSTALL_AFTER_BUILD}")
