# cmake/FcmpWebSite.cmake: assembles <build>/site, the directory a static server serves (ADR-93, web Sprint D). Run as
# a script by the target fcmp_web_site (cmake/FcmpWeb.cmake):
#
#   cmake -DFCMP_SOURCE_DIR=<repository> -DFCMP_SITE_DIR=<build>/site -DFCMP_ENGINE_WASM=<fcmp-engine.wasm>
#         -DFCMP_UI_JS=<fcmp-ui.js> -DFCMP_FUNKGUI_DIR=<FunkGui's sources> -DFCMP_EMSCRIPTEN_ROOT=<Emscripten>
#         -DFCMP_EMSCRIPTEN_VERSION=<x.y.z> -DGIT_EXECUTABLE=<git> -P cmake/FcmpWebSite.cmake
#
# The site holds
#   web/*.html, *.js, *.css, *.svg   the page (an explicit list of kinds, never a plain glob of web/: .DS_Store,
#                                    editor backups, package.json and web/tests stay out)
#   fcmp-ui.html                     index.html again: FunkGui's page runner opens <stem>.html beside <stem>.js, and
#                                    the page treats that path as its self-test
#   fcmp-engine.wasm, fcmp-ui.js, fcmp-ui.wasm
#   licences/GPL-3.0.txt             FCompressor's licence: the page distributes its object code
#   licences/JetBrainsMono-OFL.txt   the bundled face's
#   licences/THIRD-PARTY.txt         what the modules link from the toolchain: musl, libc++, libc++abi, compiler-rt and
#                                    Emscripten's runtime, each with its licence text as the pinned toolchain ships it
#   built-from.txt                   the commit the site was built from (cmake/FcmpBuiltFrom.cmake)
# It is made afresh every time (a file removed from web/ leaves no copy behind) and swapped in with one rename. Every
# URL in the page is relative, so the directory works from any path of any static host. Nothing publishes it.
cmake_minimum_required(VERSION 3.30)
foreach(_v FCMP_SOURCE_DIR FCMP_SITE_DIR FCMP_ENGINE_WASM FCMP_UI_JS FCMP_FUNKGUI_DIR FCMP_EMSCRIPTEN_ROOT
           FCMP_EMSCRIPTEN_VERSION GIT_EXECUTABLE)
  if(NOT ${_v})
    message(FATAL_ERROR "FcmpWebSite.cmake: -D${_v}=... is required")
  endif()
endforeach()
string(REGEX REPLACE "\\.js$" ".wasm" _ui_wasm "${FCMP_UI_JS}")
set(_font_licence "${FCMP_FUNKGUI_DIR}/fonts/JetBrainsMono-LICENSE.txt")
foreach(_f ${FCMP_ENGINE_WASM} ${FCMP_UI_JS} ${_ui_wasm} ${_font_licence} ${FCMP_SOURCE_DIR}/LICENSE
           ${FCMP_SOURCE_DIR}/web/index.html)
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "FcmpWebSite.cmake: no file ${_f}")
  endif()
endforeach()

file(GLOB _static ${FCMP_SOURCE_DIR}/web/*.html ${FCMP_SOURCE_DIR}/web/*.js ${FCMP_SOURCE_DIR}/web/*.css
                  ${FCMP_SOURCE_DIR}/web/*.svg)
set(_tmp "${FCMP_SITE_DIR}.tmp")
file(REMOVE_RECURSE "${_tmp}")
file(MAKE_DIRECTORY "${_tmp}/licences")
file(COPY ${_static} ${FCMP_ENGINE_WASM} ${FCMP_UI_JS} ${_ui_wasm} DESTINATION "${_tmp}"
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
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
file(REMOVE_RECURSE "${FCMP_SITE_DIR}")
file(RENAME "${_tmp}" "${FCMP_SITE_DIR}")
file(GLOB_RECURSE _all RELATIVE "${FCMP_SITE_DIR}" "${FCMP_SITE_DIR}/*")
list(LENGTH _all _n)
message(STATUS "FCompressor: site: ${_n} files in ${FCMP_SITE_DIR}")
