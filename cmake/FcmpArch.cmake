# cmake/FcmpArch.cmake: architectures, the floating-point contract and warning flags (03 §2.6; HR CMakeLists.txt:62-91,
# :366-387, renamed).
#
# Defines
#   FCMP_ARCHS            the slices this configuration compiles (arm64 and/or x86_64)
#   FCMP_ARCH_COUNT       1 or 2
#   FCMP_RUN_ARCH         the arch the probes EXECUTE as: a single arch -> that arch; universal -> the host. It selects
#                         the golden overlay (03 §3.3) through the probes' --arch argument.
#   FCMP_CAN_RUN_PROBES   ON if a one-line program built for FCMP_RUN_ARCH runs here; OFF -> probe tests are DISABLED
#   fcmp_flags            INTERFACE: ISA flags, -O3 -fno-math-errno -fno-trapping-math -ffp-contract=off, never
#                         -ffast-math. Linked by fcdsp (PUBLIC), the plugin and every probe.
#   fcmp_lto              INTERFACE: -flto (compile + link) in Release when FCOMPRESSOR_LTO; fcdsp only (PRIVATE)
#   fcmp_warnings         INTERFACE: FCMP_WARNING_FLAGS, target-wide on targets made only of our sources (fcdsp)
#   FCMP_WARNING_FLAGS    -Wall -Wextra -Wshadow -Wpedantic [+ -Werror]
#   fcmp_warn_sources(<file>...)  the same flags per source file, for targets that also compile JUCE module TUs
#                         (the plugin, fcmp_probe_plugin): JUCE's own TUs never get -Werror (03 §2.2)
include_guard(GLOBAL)

# ---- architectures -------------------------------------------------------------------------------------------------
if(CMAKE_OSX_ARCHITECTURES)
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
  if(NOT _a MATCHES "^(arm64|x86_64)$")
    message(FATAL_ERROR "FCompressor: unsupported architecture '${_a}' (arm64 and x86_64 only)")
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
set(FCMP_ARCH_FLAGS_arm64  -mcpu=apple-m1)
set(FCMP_ARCH_FLAGS_x86_64 -mavx2 -mfma)
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

set(FCMP_WARNING_FLAGS -Wall -Wextra -Wshadow -Wpedantic)
if(FCOMPRESSOR_WERROR)
  list(APPEND FCMP_WARNING_FLAGS -Werror)
endif()
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
if(NOT DEFINED FCMP_CAN_RUN_PROBES_${FCMP_RUN_ARCH})
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
      "A program built for ${FCMP_RUN_ARCH} runs on this machine")
endif()
set(FCMP_CAN_RUN_PROBES ${FCMP_CAN_RUN_PROBES_${FCMP_RUN_ARCH}})
if(NOT FCMP_CAN_RUN_PROBES)
  message(WARNING "FCompressor: ${FCMP_RUN_ARCH} programs cannot run on this machine, so every probe test is DISABLED. "
                  "To enable them on Apple silicon: softwareupdate --install-rosetta --agree-to-license")
endif()
