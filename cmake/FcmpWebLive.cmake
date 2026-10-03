# cmake/FcmpWebLive.cmake: assembles <build>/live, the TEST-ONLY directory of the browser gate (ADR-93, the web lead
# phase). Run as a script by the target fcmp_web_live (cmake/FcmpWeb.cmake):
#
#   cmake -DFCMP_SOURCE_DIR=<repository> -DFCMP_LIVE_DIR=<build>/live -DFCMP_LIVE_OBJ_DIR=<build>/live-obj
#         -P cmake/FcmpWebLive.cmake
#
# Scripts/web-live.sh serves the site at / and this directory at /live/, so a page here reaches the shipped files as
# ../fcmp-worklet.js and ../fcmp-engine.wasm: the gate runs what a visitor gets, and nothing here is ever part of the
# site (web.size holds the site to its exact list of files). The directory holds
#   web/live/*.html, *.js, *.css   the gate's own pages (the print rows through the real worklet, the tail and
#                                  flush rows)
#   *.wasm                         the test-only modules built into <build>/live-obj (fcmp-print.wasm)
#   golden/<key>.dsp.print.txt     the blessed print rows, one file per Mode, copied verbatim from
#                                  tests/golden/base/modes/<key>/dsp.print.txt: a page reads the TSV itself
# It is made afresh every time and swapped in with one rename.
cmake_minimum_required(VERSION 3.30)
foreach(_v FCMP_SOURCE_DIR FCMP_LIVE_DIR FCMP_LIVE_OBJ_DIR)
  if(NOT ${_v})
    message(FATAL_ERROR "FcmpWebLive.cmake: -D${_v}=... is required")
  endif()
endforeach()
set(_tmp "${FCMP_LIVE_DIR}.tmp")
file(REMOVE_RECURSE "${_tmp}")
file(MAKE_DIRECTORY "${_tmp}/golden")
file(GLOB _pages ${FCMP_SOURCE_DIR}/web/live/*.html ${FCMP_SOURCE_DIR}/web/live/*.js ${FCMP_SOURCE_DIR}/web/live/*.css)
file(GLOB _modules ${FCMP_LIVE_OBJ_DIR}/*.wasm)
if(_pages OR _modules)
  file(COPY ${_pages} ${_modules} DESTINATION "${_tmp}"
       FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()
file(GLOB _goldens ${FCMP_SOURCE_DIR}/tests/golden/base/modes/*/dsp.print.txt)
foreach(_g IN LISTS _goldens)
  get_filename_component(_dir "${_g}" DIRECTORY)
  get_filename_component(_key "${_dir}" NAME)
  file(COPY_FILE "${_g}" "${_tmp}/golden/${_key}.dsp.print.txt")
endforeach()
file(REMOVE_RECURSE "${FCMP_LIVE_DIR}")
file(RENAME "${_tmp}" "${FCMP_LIVE_DIR}")
file(GLOB_RECURSE _all RELATIVE "${FCMP_LIVE_DIR}" "${FCMP_LIVE_DIR}/*")
list(LENGTH _all _n)
message(STATUS "FCompressor: live: ${_n} files in ${FCMP_LIVE_DIR}")
