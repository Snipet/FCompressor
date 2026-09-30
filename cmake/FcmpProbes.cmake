# cmake/FcmpProbes.cmake: probe executables, self-registered CTest tests over Modes.def, lints, and the verify targets
# (03 §2.7, §2.9, §3.2.2; K3 #3).
#
# A probe is one file, Tools/probes/dsp/<name>.cpp (layer dsp) or Tools/probes/plugin/<name>.cpp (layers proc and ui;
# UI files are ui_<name>.cpp), whose first matching line declares it:
#
#   // FCMP_PROBE layer=dsp name=static scope=mode timeout=60 [platform=apple|linux]
#
# scope=mode registers <layer>.<name>.<key> for every registered Mode in Modes.def (none until F3 activates slot 0);
# scope=global registers <layer>.<name>. platform= (ADR-91) registers the test on that platform only; the file compiles
# everywhere. It is for the rare probe whose golden differs by platform (ui.font's atlas hash: ui.font on macOS,
# ui.font_linux on Linux). Files without the line (helpers such as FakeFacade.cpp) register no test; a malformed line
# is a configure error, and so is a duplicate test name. Changing the line needs a reconfigure, which every workflow
# runs.
#
# Test properties: labels verify;<layer>;global|mode:<key>;probe:<layer>.<name>; TIMEOUT from the line; the sandbox
# environment (FCMP_PREFS_DIR, FCMP_PRESETS_DB, FCMP_UI_THEME=0; ProbeMain empties <build>/sandbox/<test> before each
# run); DISABLED when the probes cannot run on this machine (FcmpArch.cmake).
#
# CTest's verdict comes from the EXIT CODE (FZ0 errata, R-B0 #1). B0 used PASS_REGULAR_EXPRESSION on the RESULT line,
# which makes CTest ignore the exit code: a sanitizer report after a passing RESULT line (by default TSan exits 66, ASan
# and UBSan with -fno-sanitize-recover exit 1) still passed. Each probe test now runs the probe under
# /bin/sh (_fcmp_probe_sh), which passes exit 0, and exit 2 or 3 only when the probe's own results JSON (deleted before
# the run, so it is this run's) reports golden_drift or golden_missing: the two non-blocking Harness v2 statuses
# (03 §3.2.4), so the workflow presets still succeed with golden candidates. Every other exit code, a signal, a timeout
# and a missing JSON fail. Scripts/verify.sh then classifies probe-results/*.json into BLOCKING / DRIFT / MISSING /
# IMPROVED, and reports "status X but the process failed" for a RESULT line followed by a failing exit.
#
# fcmp_probes (the probe executables) ends by writing <build>/built-from-probes.txt (cmake/FcmpBuiltFrom.cmake; R-B0
# #12): the source state the probes were built from, which verify.sh checks before it writes a verify-passed-<sha> stamp.
include_guard(GLOBAL)

set(FCMP_PROBE_COMMON_DIR ${FCMP_TOOLS_ROOT}/probes/common)
if(FCOMPRESSOR_RELEASE)
  set(_fcmp_release 1)
else()
  set(_fcmp_release 0)
endif()
if(FCMP_RTSAN_MODE STREQUAL "interposer")
  set(_fcmp_interposer 1)
else()
  set(_fcmp_interposer 0)
endif()

# Probes link LTO only (Release): the link optimises fcdsp's bitcode, never JUCE (03 §2.6).
function(fcmp_probe_target_common tgt)
  target_include_directories(${tgt} PRIVATE ${FCMP_PROBE_COMMON_DIR} ${FCMP_GENERATED_DIR})
  target_compile_definitions(${tgt} PRIVATE FCOMPRESSOR_RELEASE=${_fcmp_release} FCMP_RT_INTERPOSER=${_fcmp_interposer})
  target_link_libraries(${tgt} PRIVATE fcdsp fcmp_flags FunkGui::harness)
  if(FCOMPRESSOR_LTO)
    target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:-flto>)
  endif()
  if(TARGET fcmp_rt_interposer)
    target_link_libraries(${tgt} PRIVATE fcmp_rt_interposer)
  endif()
  set_target_properties(${tgt} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

# The RtInterposer fallback (rtsan preset on a compiler without -fsanitize=realtime). dyld applies __interpose tuples
# only from images other than the main executable, hence a dylib the probes link; RtInterposer.cpp's other half (in
# the probe executables through the common glob) finds it at run time.
if(FCMP_RTSAN_MODE STREQUAL "interposer")
  add_library(fcmp_rt_interposer SHARED ${FCMP_PROBE_COMMON_DIR}/RtInterposer.cpp)
  target_compile_definitions(fcmp_rt_interposer PRIVATE FCMP_RT_INTERPOSER_DYLIB=1)
  target_compile_options(fcmp_rt_interposer PRIVATE ${FCMP_WARNING_FLAGS})
  target_link_libraries(fcmp_rt_interposer PRIVATE fcmp_flags)
  set_target_properties(fcmp_rt_interposer PROPERTIES EXCLUDE_FROM_ALL TRUE CXX_VISIBILITY_PRESET hidden)
endif()

# built-from-probes.txt is deleted before the probe executables compile and rewritten by fcmp_probes after they all
# linked (R-B0 #12): a failed or partial build leaves no file, so verify.sh cannot stamp it.
set(FCMP_BUILT_FROM_PROBES ${CMAKE_BINARY_DIR}/built-from-probes.txt)
add_custom_target(fcmp_probes_building COMMAND ${CMAKE_COMMAND} -E rm -f ${FCMP_BUILT_FROM_PROBES} VERBATIM)

# ---- fcmp_probe_dsp: fcdsp + harness, no JUCE -----------------------------------------------------------------------
add_executable(fcmp_probe_dsp ${FCMP_PROBE_COMMON_SOURCES} ${FCMP_PROBE_DSP_SOURCES})
fcmp_probe_target_common(fcmp_probe_dsp)
fcmp_warn_sources(${FCMP_PROBE_COMMON_SOURCES} ${FCMP_PROBE_DSP_SOURCES})
set(_fcmp_probe_exes fcmp_probe_dsp)

# ---- fcmp_probe_plugin: processor + editor Panel + FunkGui core + harness; JUCE compiled once for all probes --------
if(NOT FCOMPRESSOR_DSP_ONLY)
  juce_add_console_app(fcmp_probe_plugin PRODUCT_NAME "fcmp_probe_plugin")
  set(_own ${FCMP_PLUGIN_SOURCES} ${FCMP_EDITOR_SOURCES} ${FCMP_CREATE_EDITOR_GENERIC}
           ${FCMP_PROBE_COMMON_SOURCES} ${FCMP_PROBE_PLUGIN_SOURCES})
  target_sources(fcmp_probe_plugin PRIVATE ${_own})
  fcmp_warn_sources(${_own})
  fcmp_probe_target_common(fcmp_probe_plugin)
  target_include_directories(fcmp_probe_plugin PRIVATE ${FCMP_SOURCE_ROOT})
  target_compile_definitions(fcmp_probe_plugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
  target_link_libraries(fcmp_probe_plugin PRIVATE
      FunkGui::core FunkGui::presets
      juce::juce_audio_processors juce::juce_dsp          # juce_dsp only for proc.osref (a reference, never a dependency)
      juce::juce_recommended_config_flags)
  funkgui_configure_product(fcmp_probe_plugin PRODUCT ${FCMP_PRODUCT_NAME} OBJC_PREFIX ${FCMP_OBJC_PREFIX}
                            ENV_PREFIX ${FCMP_ENV_PREFIX} PREFS_FOLDER ${FCMP_PREFS_FOLDER})
  list(APPEND _fcmp_probe_exes fcmp_probe_plugin)
endif()

# ---- fcmp_bench: only once Tools/bench/*.cpp exists (F4); never gating ------------------------------------------------
if(FCMP_BENCH_SOURCES)
  add_executable(fcmp_bench ${FCMP_BENCH_SOURCES})
  fcmp_probe_target_common(fcmp_bench)
  fcmp_warn_sources(${FCMP_BENCH_SOURCES})
endif()

add_custom_target(fcmp_probes
    COMMAND ${CMAKE_COMMAND} -DFCMP_SOURCE_DIR=${PROJECT_SOURCE_DIR} -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DFCMP_OUT=${FCMP_BUILT_FROM_PROBES} -DFCMP_WHAT=probes -P ${PROJECT_SOURCE_DIR}/cmake/FcmpBuiltFrom.cmake
    VERBATIM)
add_dependencies(fcmp_probes ${_fcmp_probe_exes})
if(TARGET fcmp_bench)          # S3 lead fix (F4 finding): build the bench with the probes so the `bench` label runs
  add_dependencies(fcmp_probes fcmp_bench)
endif()
foreach(_e IN LISTS _fcmp_probe_exes)
  add_dependencies(${_e} fcmp_probes_building)
endforeach()

# ---- self-registered tests -------------------------------------------------------------------------------------------
# The probe wrapper (see the top of this file): /bin/sh -c <this> fcmp-probe <results json> <probe command...>.
set(_fcmp_probe_sh [=[r=$1; shift; rm -f "$r"; "$@"; rc=$?; case $rc in 2) s=golden_drift ;; 3) s=golden_missing ;; *) exit $rc ;; esac; if grep -q "\"status\":\"$s\"" "$r" 2>/dev/null; then exit 0; fi; echo "fcmp-probe: exit $rc, but $r does not report $s" >&2; exit 1]=])
set(FCMP_TEST_NAMES "")
# S4 lead fix: sanitizer builds run ~5-15x slower; scale every probe timeout so TSan/ASan gates judge correctness, not speed.
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=" OR FCOMPRESSOR_RTSAN)
  set(FCMP_PROBE_TIMEOUT_SCALE 5)
else()
  set(FCMP_PROBE_TIMEOUT_SCALE 1)
endif()
function(fcmp_probe_test layer exe probe mode timeout)
  math(EXPR timeout "${timeout} * ${FCMP_PROBE_TIMEOUT_SCALE}")
  if(mode)
    set(name ${layer}.${probe}.${mode})
    set(margs --mode ${mode})
    set(mlabel mode:${mode})
  else()
    set(name ${layer}.${probe})
    set(margs "")
    set(mlabel global)
  endif()
  if(name IN_LIST FCMP_TEST_NAMES)
    message(FATAL_ERROR "FCompressor: duplicate test ${name} (two probe files declare ${layer}.${probe}?)")
  endif()
  set(FCMP_TEST_NAMES ${FCMP_TEST_NAMES} ${name} PARENT_SCOPE)
  # The results JSON is <results>/<probe>[.<mode>].json (Harness v2), i.e. <test name>.json.
  add_test(NAME ${name}
           COMMAND /bin/sh -c "${_fcmp_probe_sh}" fcmp-probe ${CMAKE_BINARY_DIR}/probe-results/${name}.json
                   $<TARGET_FILE:${exe}> ${layer}.${probe} ${margs}
                   --golden-root ${PROJECT_SOURCE_DIR}/tests/golden --arch ${FCMP_RUN_ARCH}
                   --bless-to ${CMAKE_BINARY_DIR}/golden-candidates --results ${CMAKE_BINARY_DIR}/probe-results)
  set(sb ${CMAKE_BINARY_DIR}/sandbox/${name})
  set_tests_properties(${name} PROPERTIES
      LABELS "verify;${layer};${mlabel};probe:${layer}.${probe}"
      TIMEOUT ${timeout}
      ENVIRONMENT "FCMP_PREFS_DIR=${sb};FCMP_PRESETS_DB=${sb}/presets.db;FCMP_UI_THEME=0")
  if(NOT FCMP_CAN_RUN_PROBES)
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
  endif()
endfunction()

function(fcmp_register_probes exe dir layers)
  file(GLOB _files CONFIGURE_DEPENDS ${FCMP_TOOLS_ROOT}/probes/${dir}/*.cpp)
  foreach(f IN LISTS _files)
    file(STRINGS ${f} _hdr LIMIT_COUNT 1 REGEX "^// FCMP_PROBE ")
    if(NOT _hdr)
      continue()                               # helpers (FakeFacade.cpp, ...) carry no FCMP_PROBE line
    endif()
    set(_re "^// FCMP_PROBE layer=(dsp|proc|ui) name=([a-z0-9_]+) scope=(global|mode) timeout=([0-9]+)")
    if(NOT _hdr MATCHES "${_re}( platform=(apple|linux))?$")
      message(FATAL_ERROR "${f}: malformed FCMP_PROBE line '${_hdr}' (03 §2.9: "
                          "'// FCMP_PROBE layer=<dsp|proc|ui> name=<[a-z0-9_]+> scope=<global|mode> timeout=<s>"
                          "[ platform=<apple|linux>]')")
    endif()
    set(layer ${CMAKE_MATCH_1})
    set(probe ${CMAKE_MATCH_2})
    set(scope ${CMAKE_MATCH_3})
    set(t ${CMAKE_MATCH_4})
    set(only "${CMAKE_MATCH_6}")               # platform=apple|linux, or empty (FCMP_PLATFORM calls Apple "macos")
    if(only STREQUAL "apple")
      set(only macos)
    endif()
    if(only AND NOT only STREQUAL FCMP_PLATFORM)
      continue()                               # another platform's probe (ADR-91)
    endif()
    if(NOT layer IN_LIST layers)
      message(FATAL_ERROR "${f}: layer=${layer} does not belong in Tools/probes/${dir}/ (allowed: ${layers})")
    endif()
    if(scope STREQUAL "mode")
      foreach(key IN LISTS FCMP_MODE_KEYS)
        fcmp_probe_test(${layer} ${exe} ${probe} ${key} ${t})
      endforeach()
    else()
      fcmp_probe_test(${layer} ${exe} ${probe} "" ${t})
    endif()
  endforeach()
  set(FCMP_TEST_NAMES ${FCMP_TEST_NAMES} PARENT_SCOPE)
endfunction()

fcmp_register_probes(fcmp_probe_dsp dsp "dsp")
if(NOT FCOMPRESSOR_DSP_ONLY)
  fcmp_register_probes(fcmp_probe_plugin plugin "proc;ui")
endif()

# ---- lints (no probe executable; exit code only) --------------------------------------------------------------------
add_test(NAME lint.deps COMMAND ${CMAKE_COMMAND} -DFCMP_SOURCE_DIR=${PROJECT_SOURCE_DIR}
                                -P ${PROJECT_SOURCE_DIR}/cmake/LintDeps.cmake)
# lint.headers hands check-headers.sh FcmpArch.cmake's warning list; the script fails if its own copy differs (R-B0 #6).
# FCMP_HEADER_CHECK_JUCE_FLAGS: FcmpPlatform.cmake's JUCE 8.0.4 workaround, for the headers that include JUCE (ADR-91).
# FCMP_HEADER_CHECK_TARGET_FLAGS: on Linux, whose builds always target the host, the ISA flags (an x86-64 fcdsp header
# needs -mfma: Simd.h refuses to compile without it); macOS keeps checking for the host, which lead-x86 does not target.
string(JOIN " " _fcmp_hdr_flags ${FCMP_HEADER_CHECK_FLAGS})
string(JOIN " " _fcmp_hdr_juce_flags ${FCMP_JUCE804_WORKAROUND})
set(_fcmp_hdr_target_flags "")
if(NOT APPLE)
  string(JOIN " " _fcmp_hdr_target_flags ${FCMP_ISA_FLAGS})
endif()
add_test(NAME lint.headers COMMAND ${CMAKE_COMMAND} -E env CXX=${CMAKE_CXX_COMPILER}
                                   "FCMP_HEADER_CHECK_FLAGS=${_fcmp_hdr_flags}"
                                   "FCMP_HEADER_CHECK_JUCE_FLAGS=${_fcmp_hdr_juce_flags}"
                                   "FCMP_HEADER_CHECK_TARGET_FLAGS=${_fcmp_hdr_target_flags}"
                                   /bin/bash ${PROJECT_SOURCE_DIR}/Scripts/check-headers.sh ${CMAKE_BINARY_DIR})
set_tests_properties(lint.deps lint.headers PROPERTIES LABELS "verify;lint;global" TIMEOUT 600)

# ---- not in verify: bench.<key> (label bench; run alone by the lead, never while agents build) -------------------------
if(TARGET fcmp_bench)
  foreach(key IN LISTS FCMP_MODE_KEYS)
    add_test(NAME bench.${key} COMMAND $<TARGET_FILE:fcmp_bench> --mode ${key})
    set_tests_properties(bench.${key} PROPERTIES LABELS "bench;mode:${key}" TIMEOUT 600)
  endforeach()
endif()

# ---- convenience targets ---------------------------------------------------------------------------------------------
# verify: a wrapper around the one ctest call; Scripts/verify.sh is the gate (it also classifies the results).
add_custom_target(verify
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} -L verify -j ${FCMP_TEST_JOBS} --output-on-failure
    USES_TERMINAL VERBATIM)
add_dependencies(verify fcmp_probes)

# verify-gui-live (GPU only): live Standalone capture == headless fingerprint (03 §3.6). Needs a window server; lead or
# a GPU-editor agent on request, never in parallel. Scripts/gui-live.sh arrives with U7.
if(NOT FCOMPRESSOR_HEADLESS)
  add_custom_target(verify-gui-live
      COMMAND /bin/sh -c "test -f \"$1\" || { echo \"verify-gui-live: $1 does not exist yet (card U7)\" >&2; exit 1; }; exec /bin/sh \"$1\" \"$2\""
              verify-gui-live ${PROJECT_SOURCE_DIR}/Scripts/gui-live.sh ${CMAKE_BINARY_DIR}
      USES_TERMINAL VERBATIM)
  add_dependencies(verify-gui-live FCompressor_Standalone fcmp_probe_plugin)
  if(TARGET funkgui_framerender)
    add_dependencies(verify-gui-live funkgui_framerender)
  endif()
endif()
