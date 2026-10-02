# cmake/FcmpWeb.cmake: the browser demo (ADR-93). Included by CMakeLists.txt in every configuration.
#
# The demo is two wasm modules joined by a MessagePort: fcmp-engine.wasm (fcdsp behind a C ABI, in an AudioWorklet) and,
# from a later sprint, the editor module. This file builds what Sprint A delivers: the engine and its checks.
#
# Sources (cmake/FcmpSources.cmake globs; adding a file edits no CMake):
#   Source/web/engine/*.cpp   the engine wrapper: the C ABI over fcdsp::EngineHost and the byte protocol. It names
#                             neither JUCE nor Emscripten (lint.deps rule web.engine), so it builds natively too.
#   Tools/web/*.cpp           fcmp_web_check: one program, one subcommand per check (Tools/web/WebCheck.h; Main.cpp
#                             is the lead's dispatcher). It checks the wasm arithmetic against the native contract and
#                             drives the wrapper as the worklet does (128-frame quanta) against the native goldens.
#   web/tests/*.mjs           node scripts run against the built wasm (the web configuration only).
#
# Targets
#   fcmp_web_engine_lib   STATIC, every configuration: Source/web/engine over fcdsp.
#   fcmp_web_check        every configuration: Tools/web over fcdsp, and over fcmp_web_engine_lib once it exists.
#                         Natively a plain executable; for the web a node program (NODERAWFS, so it reads the goldens
#                         from the source tree).
#   fcmp_web_engine       web only: fcmp-engine.wasm, a standalone module with no JavaScript glue and no imports. It
#                         exports the functions the wrapper marks with the compiler's own wasm attribute,
#                         __attribute__((export_name("fcmp_...")))  under `#if defined(__wasm__)` (no Emscripten
#                         header; natively the macro is empty), plus malloc and free. Nothing else: no
#                         --export-dynamic, which would export the C++ runtime's default-visible symbols too.
#   fcmp_web_ui           web only (Sprint D): fcmp-ui.js and fcmp-ui.wasm, the editor module for a browser's main
#                         thread: the editor outside gpu/, the portable model code, the facade and Source/web/ui
#                         (WebMain, PortLink) over FunkGui's JUCE-free core and FunkGui::web.
#   fcmp_web_port_check   web only (Sprint D): fcmp-port-check.mjs, PortLink and the facade as a node module, from
#                         Tools/web/port/*.cpp; web/tests/port.mjs drives it over a MessageChannel.
#   fcmp_web_site         web only (Sprint D): build-web/site, assembled by cmake/FcmpWebSite.cmake from web/*.html,
#                         *.js, *.css, *.svg, the two modules, the licences and built-from.txt. A build output:
#                         nothing publishes it.
#   fcmp_web              web only: the engine, the checks, the editor module, the site and the probes under node
#                         (cmake/FcmpProbes.cmake's fcmp_probe_web): the `web` build preset's target.
# Natively, fcmp_probe_plugin links fcmp_web_engine_lib and compiles Source/web/facade/*.cpp (Sprint C), so the
# probes proc.webnull, proc.webpresets and ui.web hold the facade to the real processor.
# None of them exists before its directory has a source, so the skeleton configures on its own.
#
# Tests register themselves from the first matching line of a file, as the probes do (cmake/FcmpProbes.cmake):
#
#   // FCMP_WEB_TEST name=web.engine.print timeout=300 on=all args=print,{golden}
#
#   name=     the CTest name; it must start with "web." and be unique
#   timeout=  seconds
#   on=       native | web | all: the configurations that register it. Default: all for Tools/web/*.cpp, web for
#             web/tests/*.mjs (a .mjs test is web-only whatever it says).
#   args=     comma-separated arguments (optional). Placeholders: {golden} = <source>/tests/golden, {source} = the
#             repository root, {build} = the build directory, {engine} = the built fcmp-engine.wasm (web only).
# A Tools/web/*.cpp test runs `fcmp_web_check <args>` (under node for the web); a web/tests/*.mjs test runs
# `node <file> <args>`. One file may carry several lines. Labels: verify;web, so Scripts/verify.sh runs them and judges
# them by exit code (they are not Harness probes: no results JSON, no golden candidates). A test whose name ends in
# .speed or .tail measures time and runs alone (RUN_SERIAL). A malformed line is a configure error.
include_guard(GLOBAL)

set(FCMP_WEB_SOURCE_DIR ${FCMP_SOURCE_ROOT}/web)
set(FCMP_WEB_TESTS_DIR  ${PROJECT_SOURCE_DIR}/web/tests)
file(GLOB FCMP_WEB_NODE_TESTS CONFIGURE_DEPENDS ${FCMP_WEB_TESTS_DIR}/*.mjs)

# ---- the engine wrapper ----------------------------------------------------------------------------------------------
if(FCMP_WEB_ENGINE_SOURCES)
  add_library(fcmp_web_engine_lib STATIC ${FCMP_WEB_ENGINE_SOURCES})
  set_target_properties(fcmp_web_engine_lib PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  target_link_libraries(fcmp_web_engine_lib PUBLIC fcdsp PRIVATE fcmp_warnings fcmp_lto)
  target_include_directories(fcmp_web_engine_lib PUBLIC ${PROJECT_SOURCE_DIR}/Source)
endif()

# ---- the check program -----------------------------------------------------------------------------------------------
if(FCMP_WEB_TOOL_SOURCES)
  add_executable(fcmp_web_check ${FCMP_WEB_TOOL_SOURCES})
  target_link_libraries(fcmp_web_check PRIVATE fcdsp)
  # The probes' warning list, not fcdsp's: the subcommands register themselves with static constructors.
  target_compile_options(fcmp_web_check PRIVATE ${FCMP_WARNING_FLAGS})
  if(TARGET fcmp_web_engine_lib)
    target_link_libraries(fcmp_web_check PRIVATE fcmp_web_engine_lib)
  endif()
  # Tools/ for "web/WebCheck.h" and the probes' JUCE-free helpers ("probes/common/Signals.h", "PrintProgram.h").
  target_include_directories(fcmp_web_check PRIVATE ${FCMP_TOOLS_ROOT} ${FCMP_TOOLS_ROOT}/probes/common)
  if(FCOMPRESSOR_LTO)
    target_link_options(fcmp_web_check PRIVATE $<$<CONFIG:Release>:-flto>)
  endif()
  if(FCOMPRESSOR_WEB)
    # A node program: the real file system (the goldens), an exit code node passes on, and room to render.
    target_link_options(fcmp_web_check PRIVATE
                        -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
                        -sENVIRONMENT=node)
  endif()
endif()

# ---- fcmp-engine.wasm ------------------------------------------------------------------------------------------------
if(FCOMPRESSOR_WEB AND TARGET fcmp_web_engine_lib)
  # The module is the wrapper's own objects (not the static library: an export_name symbol in an archive member that
  # nothing references would never be linked in).
  add_executable(fcmp_web_engine ${FCMP_WEB_ENGINE_SOURCES})
  set_target_properties(fcmp_web_engine PROPERTIES OUTPUT_NAME fcmp-engine SUFFIX .wasm
                                                   CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  target_link_libraries(fcmp_web_engine PRIVATE fcdsp fcmp_warnings fcmp_lto)
  target_include_directories(fcmp_web_engine PRIVATE ${PROJECT_SOURCE_DIR}/Source)
  target_link_options(fcmp_web_engine PRIVATE
                      -sSTANDALONE_WASM=1 --no-entry
                      -sEXPORTED_FUNCTIONS=_malloc,_free
                      -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=16777216 -sSTACK_SIZE=262144)
  set(FCMP_WEB_ENGINE_WASM $<TARGET_FILE:fcmp_web_engine>)
endif()

# ---- the web facade, natively: in fcmp_probe_plugin beside the processor (Sprint C) ----------------------------------
if(TARGET fcmp_probe_plugin)
  if(TARGET fcmp_web_engine_lib)
    target_link_libraries(fcmp_probe_plugin PRIVATE fcmp_web_engine_lib)
  endif()
  if(FCMP_WEB_FACADE_SOURCES)
    target_sources(fcmp_probe_plugin PRIVATE ${FCMP_WEB_FACADE_SOURCES})
    fcmp_warn_sources(${FCMP_WEB_FACADE_SOURCES})
  endif()
endif()

# ---- the editor module, the site and the node check of the port link (Sprint D) ------------------------------------
if(FCOMPRESSOR_WEB AND FCMP_WEB_UI_SOURCES)
  # fcmp-ui.js and fcmp-ui.wasm: the editor outside gpu/, the portable model code, the facade and Source/web/ui over
  # FunkGui's JUCE-free core and FunkGui::web, for a browser's main thread. FunkGui's own sources compile in this
  # target (an INTERFACE library's sources), so our warning list goes on our files only, as in fcmp_probe_plugin.
  set(_fcmp_web_ui_own ${FCMP_EDITOR_SOURCES} ${FCMP_PLUGIN_PORTABLE_SOURCES} ${FCMP_WEB_FACADE_SOURCES}
                       ${FCMP_WEB_UI_SOURCES})
  add_executable(fcmp_web_ui ${_fcmp_web_ui_own})
  set_target_properties(fcmp_web_ui PROPERTIES OUTPUT_NAME fcmp-ui)
  fcmp_warn_sources(${_fcmp_web_ui_own})
  target_include_directories(fcmp_web_ui PRIVATE ${FCMP_SOURCE_ROOT} ${FCMP_GENERATED_DIR})
  target_link_libraries(fcmp_web_ui PRIVATE fcdsp FunkGui::core FunkGui::web)
  funkgui_configure_product(fcmp_web_ui PRODUCT ${FCMP_PRODUCT_NAME} OBJC_PREFIX ${FCMP_OBJC_PREFIX}
                            ENV_PREFIX ${FCMP_ENV_PREFIX} PREFS_FOLDER ${FCMP_PREFS_FOLDER})
  # A browser's main thread: the runtime outlives main() (the page goes on from events), memory may grow, and the
  # stack is larger than Emscripten's 64 KB (a drag, a menu and a preview request all run on it).
  target_link_options(fcmp_web_ui PRIVATE -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
endif()

if(FCOMPRESSOR_WEB AND FCMP_WEB_PORT_SOURCES)
  # fcmp-port-check.mjs: PortLink and the facade as an ES module for node (web/tests/port.mjs). Tools/web/port holds
  # its C++ (outside the flat Tools/web glob, which also builds natively, where PortLink cannot compile).
  set(_fcmp_web_port_own ${FCMP_WEB_PORT_SOURCES} ${FCMP_SOURCE_ROOT}/web/ui/PortLink.cpp
                         ${FCMP_PLUGIN_PORTABLE_SOURCES} ${FCMP_WEB_FACADE_SOURCES})
  add_executable(fcmp_web_port_check ${_fcmp_web_port_own})
  set_target_properties(fcmp_web_port_check PROPERTIES OUTPUT_NAME fcmp-port-check SUFFIX .mjs)
  target_compile_options(fcmp_web_port_check PRIVATE ${FCMP_WARNING_FLAGS})
  target_include_directories(fcmp_web_port_check PRIVATE ${FCMP_SOURCE_ROOT} ${FCMP_GENERATED_DIR})
  target_link_libraries(fcmp_web_port_check PRIVATE fcdsp FunkGui::harness)
  target_link_options(fcmp_web_port_check PRIVATE -sENVIRONMENT=node -sMODULARIZE=1 -sEXPORT_ES6=1
                      -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0)
endif()

if(FCOMPRESSOR_WEB AND TARGET fcmp_web_engine AND TARGET fcmp_web_ui)
  # build-web/site: what a static server serves (cmake/FcmpWebSite.cmake). Nothing publishes it.
  # Its inputs go through a file (<build>/site-args.cmake), so that web/tests/site.mjs can run the same script with
  # the same inputs into a scratch directory.
  if(FETCHCONTENT_SOURCE_DIR_FUNKGUI)
    set(_fcmp_site_override 1)
  else()
    set(_fcmp_site_override 0)
  endif()
  set(_fcmp_site_args "# site-args.cmake (cmake/FcmpWeb.cmake): the inputs of cmake/FcmpWebSite.cmake for this build.\n")
  foreach(_kv "FCMP_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "FCMP_SITE_DIR=${CMAKE_BINARY_DIR}/site"
              "FCMP_ENGINE_WASM=$<TARGET_FILE:fcmp_web_engine>" "FCMP_UI_JS=$<TARGET_FILE:fcmp_web_ui>"
              "FCMP_FUNKGUI_DIR=${FCMP_FUNKGUI_DIR}" "FCMP_FUNKGUI_SHA=${FCMP_FUNKGUI_SHA}"
              "FCMP_FUNKGUI_OVERRIDE=${_fcmp_site_override}" "FCMP_EMSCRIPTEN_ROOT=${EMSCRIPTEN_ROOT_PATH}"
              "FCMP_EMSCRIPTEN_VERSION=${EMSCRIPTEN_VERSION}" "GIT_EXECUTABLE=${GIT_EXECUTABLE}")
    string(REGEX MATCH "^[^=]+" _k "${_kv}")
    string(REGEX REPLACE "^[^=]+=" "" _v "${_kv}")
    string(APPEND _fcmp_site_args "if(NOT DEFINED ${_k})\n  set(${_k} [==[${_v}]==])\nendif()\n")
  endforeach()
  file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/site-args.cmake CONTENT "${_fcmp_site_args}")
  add_custom_target(fcmp_web_site
      COMMAND ${CMAKE_COMMAND} -DFCMP_SITE_ARGS=${CMAKE_BINARY_DIR}/site-args.cmake
              -P ${PROJECT_SOURCE_DIR}/cmake/FcmpWebSite.cmake
      VERBATIM)
  add_dependencies(fcmp_web_site fcmp_web_engine fcmp_web_ui)
endif()

# What the gate's stamp covers. fcmp_probes writes built-from-probes.txt, which Scripts/verify.sh reads before it
# stamps a pass: it must be written only after everything a `verify` test runs has been built from the same tree. The
# web tests run fcmp_web_check (every configuration), and in the web tree the engine, the editor module, the port
# check and the site.
foreach(_t fcmp_web_check fcmp_web_engine fcmp_web_ui fcmp_web_port_check fcmp_web_site)
  if(TARGET ${_t} AND TARGET fcmp_probes)
    add_dependencies(fcmp_probes ${_t})
  endif()
endforeach()

if(FCOMPRESSOR_WEB)
  add_custom_target(fcmp_web)
  foreach(_t fcmp_web_engine fcmp_web_check fcmp_web_ui fcmp_web_port_check fcmp_web_site fcmp_probes)
    if(TARGET ${_t})
      add_dependencies(fcmp_web ${_t})
    endif()
  endforeach()
endif()

# ---- self-registered tests -------------------------------------------------------------------------------------------
set(_fcmp_web_test_names "")
function(_fcmp_web_register file kind)
  file(STRINGS "${file}" _lines REGEX "^//[ \t]*FCMP_WEB_TEST[ \t]")
  foreach(_line IN LISTS _lines)
    if(NOT _line MATCHES "^//[ \t]*FCMP_WEB_TEST[ \t]+name=(web\\.[a-z0-9_.]+)[ \t]+timeout=([0-9]+)([ \t]+on=(native|web|all))?([ \t]+args=([^ \t]+))?[ \t]*$")
      message(FATAL_ERROR "FcmpWeb: malformed FCMP_WEB_TEST line in ${file}: ${_line}\n"
                          "  // FCMP_WEB_TEST name=web.<x> timeout=<s> [on=native|web|all] [args=<a,b,...>]")
    endif()
    set(_name ${CMAKE_MATCH_1})
    set(_timeout ${CMAKE_MATCH_2})
    set(_on "${CMAKE_MATCH_4}")
    set(_args "${CMAKE_MATCH_6}")
    if(kind STREQUAL "node")
      set(_on web)
    elseif(_on STREQUAL "")
      set(_on all)
    endif()
    if(_name IN_LIST _fcmp_web_test_names)
      message(FATAL_ERROR "FcmpWeb: test name ${_name} is declared twice (${file})")
    endif()
    list(APPEND _fcmp_web_test_names ${_name})
    set(_fcmp_web_test_names "${_fcmp_web_test_names}" PARENT_SCOPE)
    if((_on STREQUAL "web" AND NOT FCOMPRESSOR_WEB) OR (_on STREQUAL "native" AND FCOMPRESSOR_WEB))
      continue()
    endif()
    string(REPLACE "," ";" _args "${_args}")
    set(_cmd_args "")
    foreach(_a IN LISTS _args)
      string(REPLACE "{golden}" "${PROJECT_SOURCE_DIR}/tests/golden" _a "${_a}")
      string(REPLACE "{source}" "${PROJECT_SOURCE_DIR}" _a "${_a}")
      string(REPLACE "{build}" "${CMAKE_BINARY_DIR}" _a "${_a}")
      if(_a MATCHES "{engine}")
        if(NOT TARGET fcmp_web_engine)
          message(FATAL_ERROR "FcmpWeb: ${_name} (${file}) uses {engine}, but fcmp_web_engine does not exist here")
        endif()
        string(REPLACE "{engine}" "$<TARGET_FILE:fcmp_web_engine>" _a "${_a}")
      endif()
      list(APPEND _cmd_args "${_a}")
    endforeach()
    if(kind STREQUAL "node")
      add_test(NAME ${_name} COMMAND ${FCMP_NODE} ${file} ${_cmd_args})
    else()
      if(NOT TARGET fcmp_web_check)
        message(FATAL_ERROR "FcmpWeb: ${_name} (${file}) needs fcmp_web_check, which has no sources")
      endif()
      add_test(NAME ${_name} COMMAND fcmp_web_check ${_cmd_args})     # under node for the web (the toolchain's emulator)
    endif()
    set_tests_properties(${_name} PROPERTIES LABELS "verify;web;global" TIMEOUT ${_timeout})
    # A test that measures time (web.engine.speed, web.engine.tail) runs alone: beside other tests on a small CI
    # runner its figure is the neighbours' load.
    if(_name MATCHES "\\.(speed|tail)$")
      set_tests_properties(${_name} PROPERTIES RUN_SERIAL TRUE)
    endif()
  endforeach()
endfunction()

if(FCOMPRESSOR_WEB)
  # The node that runs the wasm: the toolchain's emulator (Emscripten's own choice), so tests and try_run agree.
  list(GET CMAKE_CROSSCOMPILING_EMULATOR 0 FCMP_NODE)
  if(NOT FCMP_NODE)
    message(FATAL_ERROR "FcmpWeb: the Emscripten toolchain set no CMAKE_CROSSCOMPILING_EMULATOR (node)")
  endif()
endif()
foreach(_f IN LISTS FCMP_WEB_TOOL_SOURCES)
  _fcmp_web_register("${_f}" tool)
endforeach()
if(FCOMPRESSOR_WEB)
  foreach(_f IN LISTS FCMP_WEB_NODE_TESTS)
    _fcmp_web_register("${_f}" node)
  endforeach()
endif()
list(LENGTH _fcmp_web_test_names _fcmp_web_n)
if(FCMP_WEB_ENGINE_SOURCES OR FCOMPRESSOR_WEB)
  message(STATUS "FCompressor: web (ADR-93): ${_fcmp_web_n} web.* test(s) declared")
endif()
