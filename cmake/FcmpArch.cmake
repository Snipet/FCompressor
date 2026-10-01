# cmake/FcmpArch.cmake: architectures, the floating-point contract and warning flags (03 §2.6; HR CMakeLists.txt:62-91,
# :366-387, renamed).
#
# Defines
#   FCMP_ARCHS            the slices this configuration compiles (arm64 and/or x86_64; wasm32 alone for the web)
#   FCMP_ARCH_COUNT       1 or 2
#   FCMP_RUN_ARCH         the arch the probes EXECUTE as: a single arch -> that arch; universal -> the host. It selects
#                         the golden overlay (03 §3.3) through the probes' --arch argument.
#   FCMP_CAN_RUN_PROBES   ON if a one-line program built for FCMP_RUN_ARCH runs here; OFF -> probe tests are DISABLED
#   fcmp_flags            INTERFACE: ISA flags, -O3 -fno-math-errno -fno-trapping-math -ffp-contract=off, never
#                         -ffast-math. Linked by fcdsp (PUBLIC), the plugin and every probe.
#   fcmp_lto              INTERFACE: -flto (compile + link) in Release when FCOMPRESSOR_LTO; fcdsp only (PRIVATE)
#   fcmp_warnings         INTERFACE: FCMP_WARNING_FLAGS + FCMP_WARNING_FLAGS_FCDSP, target-wide on fcdsp (made only of
#                         our sources)
#   FCMP_WARNING_FLAGS    the ONE warning list of every translation unit of ours: JUCE 8.0.4's clang list
#                         (juce_recommended_warning_flags) + -Wextra, without -Wfloat-equal [+ -Werror]. The same list
#                         is Scripts/check-headers.sh's (lint.headers), so a header that builds in fcdsp or a probe also
#                         builds in the plugin (R-B0 #6)
#   FCMP_WARNING_FLAGS_FCDSP  fcdsp only: -Wglobal-constructors -Wexit-time-destructors (no static constructors, C D12;
#                         R-B0 #3) [+ -Wfunction-effects in the rtsan configuration]
#   FCMP_HEADER_CHECK_FLAGS  FCMP_WARNING_FLAGS as check-headers.sh must repeat them (no -Werror, no generator
#                         expressions); lint.headers passes it and the script fails if its own copy differs
#   fcmp_warn_sources(<file>...)  FCMP_WARNING_FLAGS per source file, for targets that also compile JUCE module TUs
#                         (the plugin, fcmp_probe_plugin): JUCE's own TUs never get -Werror (03 §2.2)
include_guard(GLOBAL)

# ---- architectures -------------------------------------------------------------------------------------------------
# CMAKE_OSX_ARCHITECTURES means something on Apple only; Linux builds the host's architecture (ADR-92), so a preset that
# pins one (lead-x86, release) cannot cross-compile there by accident.
if(NOT APPLE AND FCOMPRESSOR_UNIVERSAL)
  message(FATAL_ERROR "FCompressor: FCOMPRESSOR_UNIVERSAL (arm64 + x86_64 in one binary) exists on macOS only")
endif()
if(FCMP_PLATFORM STREQUAL "web")
  set(FCMP_ARCHS wasm32)                     # ADR-93: whatever the host is; the probes of it run under node
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
  set(FCMP_ARCHS ${CMAKE_OSX_ARCHITECTURES})
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(FCMP_ARCHS arm64)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(FCMP_ARCHS x86_64)
else()
  message(FATAL_ERROR "FCompressor: unsupported host processor '${CMAKE_HOST_SYSTEM_PROCESSOR}'")
endif()
list(REMOVE_DUPLICATES FCMP_ARCHS)
foreach(_a IN LISTS FCMP_ARCHS)
  if(NOT _a MATCHES "^(arm64|x86_64|wasm32)$")
    message(FATAL_ERROR "FCompressor: unsupported architecture '${_a}' (arm64, x86_64, and wasm32 for the web)")
  endif()
endforeach()
list(LENGTH FCMP_ARCHS FCMP_ARCH_COUNT)

if(FCMP_ARCH_COUNT EQUAL 1)
  set(FCMP_RUN_ARCH ${FCMP_ARCHS})
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(FCMP_RUN_ARCH arm64)
else()
  set(FCMP_RUN_ARCH x86_64)
endif()

# ---- flags (one set for the plugin and the probes, so a fingerprint certifies the shipped arithmetic) --------------
# ISA flags per slice. A universal build keeps each pair intact with SHELL: (CMake de-duplicates repeated options, which
# turned HR's "-Xarch_x86_64 -mavx2 -Xarch_x86_64 -mfma" into an unscoped -mfma, a hard error on arm64; 03 §2.6).
# Linux arm64 (ADR-92) cannot assume an Apple core: the AArch64 baseline, which has every NEON operation fcdsp's Simd.h
# uses. x86_64 is the same everywhere (Haswell and later).
if(APPLE)
  set(FCMP_ARCH_FLAGS_arm64 -mcpu=apple-m1)
else()
  set(FCMP_ARCH_FLAGS_arm64 -march=armv8-a)
endif()
set(FCMP_ARCH_FLAGS_x86_64 -mavx2 -mfma)
# wasm32 (ADR-93): fixed-width SIMD only. Never -mrelaxed-simd: its fused multiply-add is implementation-defined, and
# fcdsp's arithmetic has to be the same in every browser.
set(FCMP_ARCH_FLAGS_wasm32 -msimd128)
set(FCMP_ISA_FLAGS "")
foreach(_a IN LISTS FCMP_ARCHS)
  foreach(_f IN LISTS FCMP_ARCH_FLAGS_${_a})
    if(FCMP_ARCH_COUNT EQUAL 1)
      list(APPEND FCMP_ISA_FLAGS ${_f})
    else()
      list(APPEND FCMP_ISA_FLAGS "SHELL:-Xarch_${_a} ${_f}")
    endif()
  endforeach()
endforeach()

# -ffp-contract=off is plain (both slices need it: Apple clang fuses a*b+c by default). Intended FMAs go through
# fcdsp::simd::fma. Never -ffast-math (HR :361-365; JUCE's constexpr infinity() breaks under -ffinite-math-only).
set(FCMP_FP_FLAGS -O3 -fno-math-errno -fno-trapping-math -ffp-contract=off)

add_library(fcmp_flags INTERFACE)
target_compile_options(fcmp_flags INTERFACE ${FCMP_ISA_FLAGS} ${FCMP_FP_FLAGS})

add_library(fcmp_lto INTERFACE)
if(FCOMPRESSOR_LTO)
  target_compile_options(fcmp_lto INTERFACE $<$<CONFIG:Release>:-flto>)
  target_link_options(fcmp_lto INTERFACE $<$<CONFIG:Release>:-flto>)
endif()

# Warnings (FZ0 errata, R-B0 #6). Before FZ0 the plugin's own sources compiled with JUCE's strict list (the plugin links
# juce::juce_recommended_warning_flags) plus our -Werror, while fcdsp, the probes and lint.headers used only
# -Wall -Wextra -Wshadow -Wpedantic: an fcdsp header could build everywhere except in the plugin. Now every TU of ours
# and every checked header gets the same list:
# - JUCE 8.0.4's clang list, verbatim and in its order (extras/Build/CMake/JUCEHelperTargets.cmake:51-86), with the
#   Objective-C-only pair behind a generator expression as JUCE has it;
# - plus -Wextra, first, so that JUCE's -Wno-ignored-qualifiers after it still wins;
# - minus -Wfloat-equal (-Wno-float-equal last, which also overrides the plugin target's own copy of JUCE's list, since
#   per-file options follow target options): exact float comparisons are intended in fcdsp (HostParams.cpp's table
#   checks), in probes (bit-exact checks) and in FunkGui's Harness.h, which our probe TUs include as a user header.
set(FCMP_WARNING_FLAGS
    -Wextra
    -Wall -Wshadow-all -Wshorten-64-to-32 -Wstrict-aliasing -Wuninitialized -Wunused-parameter -Wconversion
    -Wsign-compare -Wint-conversion -Wconditional-uninitialized -Wconstant-conversion -Wsign-conversion
    -Wbool-conversion -Wextra-semi -Wunreachable-code -Wcast-align -Wshift-sign-overflow -Wmissing-prototypes
    -Wnullable-to-nonnull-conversion -Wno-ignored-qualifiers -Wswitch-enum -Wpedantic -Wdeprecated
    -Wmissing-field-initializers
    -Wzero-as-null-pointer-constant -Wunused-private-field -Woverloaded-virtual -Wreorder
    -Winconsistent-missing-destructor-override
    -Wno-float-equal)
set(FCMP_HEADER_CHECK_FLAGS ${FCMP_WARNING_FLAGS})          # check-headers.sh compiles C++ headers only
list(APPEND FCMP_WARNING_FLAGS
    $<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-Wunguarded-availability>
    $<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-Wunguarded-availability-new>)
if(FCOMPRESSOR_WERROR)
  list(APPEND FCMP_WARNING_FLAGS -Werror)
endif()
# Linux (ADR-92): libstdc++ 15+ puts `#pragma GCC unroll 4` in std::find_if (bits/stl_algobase.h), and Clang reports
# every loop it then cannot unroll as -Wpass-failed, at the library's line, in our translation units. None of our code
# asks for a loop transformation, so the warning can only be the library's. Not in FCMP_HEADER_CHECK_FLAGS: an optimiser
# warning never fires under lint.headers' -fsyntax-only.
if(NOT APPLE)
  list(APPEND FCMP_WARNING_FLAGS -Wno-pass-failed)
endif()
# fcdsp only: a namespace-scope object with a dynamic initialiser or an exit-time destructor (std::vector<float> g(64);,
# static std::vector<float> g = ...;) or a function-local static with a destructor is a compile error (R-B0 #3; LintDeps
# covers the trivially destructible function-local statics).
set(FCMP_WARNING_FLAGS_FCDSP -Wglobal-constructors -Wexit-time-destructors)
add_library(fcmp_warnings INTERFACE)
target_compile_options(fcmp_warnings INTERFACE ${FCMP_WARNING_FLAGS})

# ---- real-time checking (the rtsan preset; K2 #18) ------------------------------------------------------------------
# FCOMPRESSOR_RTSAN=ON asks for RealtimeSanitizer. FCMP_RTSAN_MODE is then "sanitizer" when the compiler supports
# -fsanitize=realtime (compile and link flags on every target through fcmp_flags; -Wfunction-effects on fcdsp only,
# through fcmp_warnings), else "interposer": the option is refused with a message and the probes link the
# Tools/probes/common/RtInterposer.cpp dylib, which counts malloc/free/pthread_mutex_lock/os_unfair_lock_lock/write/
# mach_msg calls on the armed (audio) thread (FcmpProbes.cmake). "off" without the option.
set(FCMP_RTSAN_MODE off)
if(FCOMPRESSOR_RTSAN)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS -fsanitize=realtime)
  set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=realtime)
  check_cxx_source_compiles("[[clang::nonblocking]] static int f() noexcept { return 0; }\nint main() { return f(); }\n"
                            FCMP_HAVE_RTSAN)
  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  if(FCMP_HAVE_RTSAN)
    set(FCMP_RTSAN_MODE sanitizer)
    target_compile_options(fcmp_flags INTERFACE -fsanitize=realtime)
    target_link_options(fcmp_flags INTERFACE -fsanitize=realtime)
    list(APPEND FCMP_WARNING_FLAGS_FCDSP -Wfunction-effects)
  elseif(NOT APPLE)
    # The RtInterposer is dyld's __interpose (macOS only); on Linux the sanitizer itself is the check (Clang >= 20).
    message(FATAL_ERROR "FCompressor: FCOMPRESSOR_RTSAN on Linux needs -fsanitize=realtime, which "
                        "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not support")
  else()
    set(FCMP_RTSAN_MODE interposer)
    message(STATUS "FCompressor: FCOMPRESSOR_RTSAN: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not "
                   "support -fsanitize=realtime; the probes use the RtInterposer fallback instead")
  endif()
endif()
if(FCMP_WARNING_FLAGS_FCDSP)
  target_compile_options(fcmp_warnings INTERFACE ${FCMP_WARNING_FLAGS_FCDSP})
endif()

# Our warning flags on individual files of a target that also compiles JUCE module TUs.
function(fcmp_warn_sources)
  if(ARGN)
    set_source_files_properties(${ARGN} DIRECTORY ${PROJECT_SOURCE_DIR}
                                PROPERTIES COMPILE_OPTIONS "${FCMP_WARNING_FLAGS}")
  endif()
endfunction()

# The flags line (BuildInfo.cpp and release.sh's manifest; 03 §5 step 7): what fcmp_flags gives every target.
string(JOIN " " FCMP_FLAGS_LINE ${FCMP_ISA_FLAGS} ${FCMP_FP_FLAGS})
string(REPLACE "SHELL:" "" FCMP_FLAGS_LINE "${FCMP_FLAGS_LINE}")

# ---- can the probes run here? ----------------------------------------------------------------------------------------
# A one-line program built for FCMP_RUN_ARCH. On an Apple-silicon Mac without Rosetta 2 an x86_64 binary fails with
# "bad CPU type in executable": the probe tests are then DISABLED (03 §3.3), which verify.sh reports as blocking.
# Only a success is cached (FZ0 errata, R-B0 #9): after an OFF result every configure checks again (about 1 s), so
# installing Rosetta and reconfiguring enables the probes without deleting a cache entry.
if(NOT FCMP_CAN_RUN_PROBES_${FCMP_RUN_ARCH})
  try_run(_fcmp_run_result _fcmp_compile_result
          SOURCE_FROM_CONTENT fcmp_can_run.c "int main(void) { return 0; }\n"
          CMAKE_FLAGS "-DCMAKE_OSX_ARCHITECTURES=${FCMP_RUN_ARCH}"
          NO_CACHE
          COMPILE_OUTPUT_VARIABLE _fcmp_compile_log
          RUN_OUTPUT_VARIABLE _fcmp_run_log)
  if(_fcmp_compile_result AND "${_fcmp_run_result}" STREQUAL "0")
    set(_fcmp_can_run ON)
  else()
    set(_fcmp_can_run OFF)
  endif()
  set(FCMP_CAN_RUN_PROBES_${FCMP_RUN_ARCH} ${_fcmp_can_run} CACHE INTERNAL
      "A program built for ${FCMP_RUN_ARCH} runs on this machine (only ON is kept; OFF is checked again)")
endif()
set(FCMP_CAN_RUN_PROBES ${FCMP_CAN_RUN_PROBES_${FCMP_RUN_ARCH}})
if(NOT FCMP_CAN_RUN_PROBES)
  if(APPLE)
    set(_fcmp_hint "To enable them on Apple silicon: softwareupdate --install-rosetta --agree-to-license")
  else()
    set(_fcmp_hint "A program built by ${CMAKE_C_COMPILER} did not run; see CMakeFiles/CMakeConfigureLog.yaml")
  endif()
  message(WARNING "FCompressor: ${FCMP_RUN_ARCH} programs cannot run on this machine, so every probe test is DISABLED. "
                  "${_fcmp_hint}")
endif()
