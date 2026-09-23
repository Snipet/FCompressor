# cmake/FcmpBuiltFrom.cmake: records which source state a build's binaries were built from (FZ0 errata, R-B0 #12).
# Run as a script by the fcmp_probes target (after both probe executables are up to date) and, in JUCE configurations,
# by fcmp_plugin_built_from (after the AU/VST3/Standalone targets):
#
#   cmake -DFCMP_SOURCE_DIR=<repository> -DGIT_EXECUTABLE=<git> -DFCMP_OUT=<file> -DFCMP_WHAT=<probes|plugin>
#         -P cmake/FcmpBuiltFrom.cmake
#
# <file> gets one line, "<what> <HEAD sha|none> <clean|dirty> <UTC time>", written atomically. "clean" means
# `git status --porcelain` printed nothing (tracked and untracked files; the build-* trees are ignored), so the binaries
# are exactly HEAD's. Every build first deletes <file> (fcmp_probes_building / fcmp_plugin_building, which the
# executables and the plugin's shared code depend on), and this script runs last (a custom target always runs, and
# only after every dependency built): after a successful build the file names the tree it was built from, and after a
# failed or partial build there is no file.
# Scripts/verify.sh writes verify-passed-<sha> only when built-from-probes.txt says "<HEAD> clean", and
# Scripts/validate.sh appends its result only when built-from-plugin.txt does: a commit, merge or checkout without a
# rebuild can no longer certify stale binaries. Read-only git only (--no-optional-locks: no index refresh).
cmake_minimum_required(VERSION 3.30)

foreach(_v FCMP_SOURCE_DIR GIT_EXECUTABLE FCMP_OUT FCMP_WHAT)
  if(NOT ${_v})
    message(FATAL_ERROR "FcmpBuiltFrom.cmake: -D${_v}=... is required")
  endif()
endforeach()

function(_bf_git out)
  execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=GIT_DIR --unset=GIT_WORK_TREE --unset=GIT_INDEX_FILE
                          ${GIT_EXECUTABLE} --no-optional-locks -C "${FCMP_SOURCE_DIR}" ${ARGN}
                  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(${out} "${_o}" PARENT_SCOPE)
  set(${out}_RESULT "${_rc}" PARENT_SCOPE)
endfunction()

_bf_git(_sha rev-parse -q --verify "HEAD^{commit}")
if(NOT _sha_RESULT EQUAL 0 OR NOT _sha MATCHES "^[0-9a-f]+$")
  set(_sha none)
endif()
_bf_git(_status status --porcelain)
if(_sha STREQUAL "none" OR NOT _status_RESULT EQUAL 0 OR NOT _status STREQUAL "")
  set(_state dirty)
else()
  set(_state clean)
endif()
string(TIMESTAMP _now "%Y-%m-%dT%H:%M:%SZ" UTC)

file(WRITE "${FCMP_OUT}.tmp" "${FCMP_WHAT} ${_sha} ${_state} ${_now}\n")
file(RENAME "${FCMP_OUT}.tmp" "${FCMP_OUT}")
message(STATUS "FCompressor: ${FCMP_WHAT} built from ${_sha} (${_state})")
