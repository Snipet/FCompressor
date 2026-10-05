# cmake/FcmpWebSite.cmake: assembles <build>/site, the directory a static server serves (ADR-93, web Sprint D). Run as
# a script by the target fcmp_web_site (cmake/FcmpWeb.cmake):
#
#   cmake -DFCMP_SITE_ARGS=<build>/site-args.cmake -P cmake/FcmpWebSite.cmake
#
# site-args.cmake (written by the configure) sets what is not given with -D: FCMP_SOURCE_DIR (the repository),
# FCMP_SITE_DIR (<build>/site), FCMP_ENGINE_WASM, FCMP_UI_JS, FCMP_FUNKGUI_DIR (the FunkGui sources the build
# compiled), FCMP_FUNKGUI_SHA (the pinned commit), FCMP_FUNKGUI_OVERRIDE (1 when the configure took an override),
# FCMP_EMSCRIPTEN_ROOT, FCMP_EMSCRIPTEN_VERSION, GIT_EXECUTABLE. A -D on the command line wins (web/tests/site.mjs).
#
# The site holds
#   web/*.html, *.js, *.css, *.svg   the page (an explicit list of kinds, never a plain glob of web/: .DS_Store,
#                                    editor backups, package.json and web/tests stay out)
#   audio/loop.wav                   the sample loop, web/audio/loop.wav byte for byte (web/audio/*.wav, and nothing
#                                    else of web/audio: its README.md stays in the repository)
#   fcmp-ui.html                     index.html again: FunkGui's page runner opens <stem>.html beside <stem>.js, and
#                                    the page treats that path as its self-test
#   fcmp-engine.wasm, fcmp-ui.js, fcmp-ui.wasm
#   licences/GPL-3.0.txt             FCompressor's licence: the page distributes its object code
#   licences/JetBrainsMono-OFL.txt   the bundled face's
#   licences/THIRD-PARTY.txt         what the modules link from the toolchain: musl, libc++, libc++abi, compiler-rt and
#                                    Emscripten's runtime, each with its licence text as the pinned toolchain ships it
#   built-from.txt                   the commit the site was built from (cmake/FcmpBuiltFrom.cmake). The page links
#                                    that commit as the source of what it runs, so "clean" here covers FunkGui too
#                                    (more than half of fcmp-ui.wasm): the line says dirty unless the FunkGui the
#                                    build compiled is the pinned commit itself, with nothing changed in its tree
# It is made afresh every time (a file removed from web/ leaves no copy behind) and swapped in with one rename. Every
# URL in the page is relative, so the directory works from any path of any static host (GitHub Pages serves it under
# /FCompressor/). This script publishes nothing; CI's publish job deploys the site its gate tested.
cmake_minimum_required(VERSION 3.30)
if(FCMP_SITE_ARGS)
  include("${FCMP_SITE_ARGS}")
endif()
foreach(_v FCMP_SOURCE_DIR FCMP_SITE_DIR FCMP_ENGINE_WASM FCMP_UI_JS FCMP_FUNKGUI_DIR FCMP_FUNKGUI_SHA
           FCMP_EMSCRIPTEN_ROOT FCMP_EMSCRIPTEN_VERSION GIT_EXECUTABLE)
  if(NOT ${_v})
    message(FATAL_ERROR "FcmpWebSite.cmake: -D${_v}=... is required (or -DFCMP_SITE_ARGS=<build>/site-args.cmake)")
  endif()
endforeach()
string(REGEX REPLACE "\\.js$" ".wasm" _ui_wasm "${FCMP_UI_JS}")
set(_font_licence "${FCMP_FUNKGUI_DIR}/fonts/JetBrainsMono-LICENSE.txt")
foreach(_f ${FCMP_ENGINE_WASM} ${FCMP_UI_JS} ${_ui_wasm} ${_font_licence} ${FCMP_SOURCE_DIR}/LICENSE
           ${FCMP_SOURCE_DIR}/web/index.html ${FCMP_SOURCE_DIR}/web/audio/loop.wav)
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "FcmpWebSite.cmake: no file ${_f}")
  endif()
endforeach()

file(GLOB _static ${FCMP_SOURCE_DIR}/web/*.html ${FCMP_SOURCE_DIR}/web/*.js ${FCMP_SOURCE_DIR}/web/*.css
                  ${FCMP_SOURCE_DIR}/web/*.svg)
file(GLOB _audio ${FCMP_SOURCE_DIR}/web/audio/*.wav)
set(_tmp "${FCMP_SITE_DIR}.tmp")
file(REMOVE_RECURSE "${_tmp}")
file(MAKE_DIRECTORY "${_tmp}/licences" "${_tmp}/audio")
file(COPY ${_static} ${FCMP_ENGINE_WASM} ${FCMP_UI_JS} ${_ui_wasm} DESTINATION "${_tmp}"
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
file(COPY ${_audio} DESTINATION "${_tmp}/audio" FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
file(COPY_FILE "${FCMP_SOURCE_DIR}/web/index.html" "${_tmp}/fcmp-ui.html")
file(COPY_FILE "${FCMP_SOURCE_DIR}/LICENSE" "${_tmp}/licences/GPL-3.0.txt")
file(COPY_FILE "${_font_licence}" "${_tmp}/licences/JetBrainsMono-OFL.txt")

# ---- licences/THIRD-PARTY.txt: the toolchain's libraries inside the two modules, texts from the toolchain itself -----
set(_tp "FCompressor's WebAssembly modules (fcmp-engine.wasm, fcmp-ui.wasm) and fcmp-ui.js were built with Emscripten\n")
string(APPEND _tp "${FCMP_EMSCRIPTEN_VERSION} and contain parts of the libraries it links: the musl C library, LLVM's libc++,\n")
string(APPEND _tp "libc++abi and compiler-rt, and Emscripten's own runtime and JavaScript support code. Their licences follow, as\n")
string(APPEND _tp "that toolchain ships them. FCompressor itself is under the GNU General Public License, version 3\n")
string(APPEND _tp "(GPL-3.0.txt); the bundled typeface is under the SIL Open Font License (JetBrainsMono-OFL.txt).\n")
set(_parts
    "Emscripten|${FCMP_EMSCRIPTEN_ROOT}/LICENSE|${FCMP_EMSCRIPTEN_ROOT}/../LICENSE"   # emsdk's layout, Homebrew's
    "musl|${FCMP_EMSCRIPTEN_ROOT}/system/lib/libc/musl/COPYRIGHT"
    "libc++|${FCMP_EMSCRIPTEN_ROOT}/system/lib/libcxx/LICENSE.TXT"
    "libc++abi|${FCMP_EMSCRIPTEN_ROOT}/system/lib/libcxxabi/LICENSE.TXT"
    "compiler-rt|${FCMP_EMSCRIPTEN_ROOT}/system/lib/compiler-rt/LICENSE.TXT")
foreach(_part IN LISTS _parts)
  string(REPLACE "|" ";" _pair "${_part}")
  list(GET _pair 0 _name)
  list(SUBLIST _pair 1 -1 _candidates)
  set(_text "")
  foreach(_c IN LISTS _candidates)
    if(NOT _text AND EXISTS "${_c}")
      file(READ "${_c}" _text)
    endif()
  endforeach()
  if(NOT _text)
    message(FATAL_ERROR "FcmpWebSite.cmake: no licence text for ${_name} under ${FCMP_EMSCRIPTEN_ROOT} "
                        "(looked at: ${_candidates}). The site must carry it.")
  endif()
  string(APPEND _tp "\n================================================================================\n")
  string(APPEND _tp "${_name}\n")
  string(APPEND _tp "================================================================================\n\n")
  string(APPEND _tp "${_text}")
endforeach()
file(WRITE "${_tmp}/licences/THIRD-PARTY.txt" "${_tp}")

execute_process(COMMAND ${CMAKE_COMMAND} -DFCMP_SOURCE_DIR=${FCMP_SOURCE_DIR} -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                        -DFCMP_OUT=${_tmp}/built-from.txt -DFCMP_WHAT=site
                        -P ${CMAKE_CURRENT_LIST_DIR}/FcmpBuiltFrom.cmake
                COMMAND_ERROR_IS_FATAL ANY)

# FcmpBuiltFrom.cmake judged FCompressor's tree. Is the FunkGui in the modules exactly the pin?
function(_site_funkgui_git out)
  execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=GIT_DIR --unset=GIT_WORK_TREE --unset=GIT_INDEX_FILE
                          ${GIT_EXECUTABLE} --no-optional-locks -C "${FCMP_FUNKGUI_DIR}" ${ARGN}
                  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(${out} "${_o}" PARENT_SCOPE)
  set(${out}_RESULT "${_rc}" PARENT_SCOPE)
endfunction()
set(_funkgui "the pin")
_site_funkgui_git(_fg_top rev-parse --show-toplevel)
_site_funkgui_git(_fg_head rev-parse -q --verify "HEAD^{commit}")
_site_funkgui_git(_fg_status status --porcelain)
file(REAL_PATH "${FCMP_FUNKGUI_DIR}" _fg_dir)
if(_fg_top_RESULT EQUAL 0 AND NOT _fg_top STREQUAL "")
  file(REAL_PATH "${_fg_top}" _fg_top)
endif()
if(FCMP_FUNKGUI_OVERRIDE)
  set(_funkgui "an override")
elseif(NOT _fg_top_RESULT EQUAL 0 OR NOT _fg_top STREQUAL _fg_dir)
  set(_funkgui "not a git checkout of its own")
elseif(NOT _fg_head_RESULT EQUAL 0 OR NOT _fg_head STREQUAL FCMP_FUNKGUI_SHA)
  set(_funkgui "at ${_fg_head}, not the pin ${FCMP_FUNKGUI_SHA}")
elseif(NOT _fg_status_RESULT EQUAL 0 OR NOT _fg_status STREQUAL "")
  set(_funkgui "the pin with local changes")
endif()
if(NOT _funkgui STREQUAL "the pin")
  file(READ "${_tmp}/built-from.txt" _line)
  string(REGEX REPLACE "^(site [^ ]+) clean " "\\1 dirty " _line "${_line}")
  file(WRITE "${_tmp}/built-from.txt" "${_line}")
  message(STATUS "FCompressor: site: FunkGui is ${_funkgui}: built-from.txt says dirty")
endif()

file(REMOVE_RECURSE "${FCMP_SITE_DIR}")
file(RENAME "${_tmp}" "${FCMP_SITE_DIR}")
file(GLOB_RECURSE _all RELATIVE "${FCMP_SITE_DIR}" "${FCMP_SITE_DIR}/*")
list(LENGTH _all _n)
message(STATUS "FCompressor: site: ${_n} files in ${FCMP_SITE_DIR}")
