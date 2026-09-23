# cmake/LintDeps.cmake: the lint.deps CTest (01 §2.2; 03 §2.9; K2 #2, #14). Run as a script:
#
#   cmake -DFCMP_SOURCE_DIR=<repository root> -P cmake/LintDeps.cmake
#
# Rules (each violation prints "<file>:<line>: <rule>: <text>"; any violation fails the test):
#   fcdsp.juce      Source/fcdsp/** includes no JUCE header (<juce_*>, JuceHeader.h)
#   fcdsp.funkgui   Source/fcdsp/** includes no funkgui/* header
#   fcdsp.layer     Source/fcdsp/** includes nothing under plugin/ or editor/
#   fcdsp.libm      Source/fcdsp/{core,engine,modes}/** calls no libm function: the regex
#                   \b(std::)?(tan|tanh|exp|expf|log|logf|log1p|pow|powf|sin|cos|tanf|tanhf)\s*\( on comment-stripped
#                   code, except the fcdsp replacements themselves: a call qualified `fcdsp::` (e.g. fcdsp::tanh(x)), a
#                   declaration or definition whose return type is float/double/f32x4/auto (FastMath.h's
#                   `simd::f32x4 tanh(simd::f32x4)`), and a member call (x.log(...), p->exp(...)). Inside fcdsp, call
#                   the replacements qualified. params/ and analysis/ are exempt (01 §2.2 rule 4).
#   fcdsp.static    Source/fcdsp/**: no indented `static` variable, i.e. no function-local static and no
#                   non-constexpr static data member (C D12: no lazy initialisation, no static constructors).
#                   `static constexpr`/`consteval`/`constinit`/`static_assert` and static member functions pass.
#   test-tap        FCMP_TEST_TAP appears nowhere under Source/ or Tools/, comments included (the test tap is a
#                   runtime pointer, K2 #2)
#   editor.facade   Source/editor/** reaches the processor only through plugin/ProcessorFacade.h: no other plugin/
#                   include, no fcdsp/engine/EngineHost.h include, no EngineHost token
#   editor.gpu      Source/editor/** outside gpu/ includes no funkgui/gpu/* or bgfx header
#   plugin.editor   Source/plugin/** includes nothing under editor/, except CreateEditorGpu.cpp
#   product         Source/** never uses JucePlugin_* (product constants come from the generated FcmpProduct.h)
# Zero files is fine: Sprint 0 starts with an empty Source/fcdsp.
cmake_minimum_required(VERSION 3.30)

if(NOT FCMP_SOURCE_DIR)
  get_filename_component(FCMP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
set(_src "${FCMP_SOURCE_DIR}/Source")
set(_tools "${FCMP_SOURCE_DIR}/Tools")
set_property(GLOBAL PROPERTY FCMP_LINT_VIOLATIONS 0)

function(_lint_fail file line rule text)
  file(RELATIVE_PATH _rel "${FCMP_SOURCE_DIR}" "${file}")
  string(STRIP "${text}" text)
  message("${_rel}:${line}: ${rule}: ${text}")
  get_property(_n GLOBAL PROPERTY FCMP_LINT_VIOLATIONS)
  math(EXPR _n "${_n} + 1")
  set_property(GLOBAL PROPERTY FCMP_LINT_VIOLATIONS ${_n})
endfunction()

# _lint_read(<out> <file>): the file as a CMake list of lines, with the characters CMake lists cannot hold
# neutralised (';' -> ',', '[' -> '{', ']' -> '}', '\' -> '/'). None of the rules depends on them.
function(_lint_read out file)
  file(READ "${file}" _c)
  string(REPLACE "\r" "" _c "${_c}")
  string(REPLACE "\\" "/" _c "${_c}")
  string(REPLACE ";" "," _c "${_c}")
  string(REPLACE "[" "{" _c "${_c}")
  string(REPLACE "]" "}" _c "${_c}")
  string(REPLACE "\n" ";" _c "${_c}")
  set(${out} "${_c}" PARENT_SCOPE)
endfunction()

# _lint_strip(<line> <in-block-var>): <line> without comments; <in-block-var> carries an open /* across lines.
macro(_lint_strip line inblock)
  if(${inblock})
    string(FIND "${${line}}" "*/" _p)
    if(_p EQUAL -1)
      set(${line} "")
    else()
      math(EXPR _p "${_p} + 2")
      string(SUBSTRING "${${line}}" ${_p} -1 ${line})
      set(${inblock} FALSE)
    endif()
  endif()
  string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " ${line} "${${line}}")
  string(FIND "${${line}}" "/*" _p)
  if(NOT _p EQUAL -1)
    string(SUBSTRING "${${line}}" 0 ${_p} ${line})
    set(${inblock} TRUE)
  endif()
  string(REGEX REPLACE "//.*$" "" ${line} "${${line}}")
endmacro()

set(_libm_re "(^|[^A-Za-z0-9_])((std::)?(tanhf|tanh|tanf|tan|expf|exp|logf|log1p|log|powf|pow|sin|cos))[ \t]*\\(")

# _lint_libm(<file> <lineno> <code>)
function(_lint_libm file n code)
  set(_rest "${code}")
  while(_rest MATCHES "${_libm_re}")
    set(_whole "${CMAKE_MATCH_0}")
    set(_pre "${CMAKE_MATCH_1}")
    set(_name "${CMAKE_MATCH_2}")
    string(FIND "${_rest}" "${_whole}" _at)
    string(LENGTH "${_pre}" _pl)
    math(EXPR _nameAt "${_at} + ${_pl}")
    string(SUBSTRING "${_rest}" 0 ${_nameAt} _before)
    set(_ok FALSE)
    if(NOT _name MATCHES "^std::")
      if(_before MATCHES "(^|[^A-Za-z0-9_])fcdsp::$")                                   # fcdsp::tanh(x)
        set(_ok TRUE)
      elseif(_before MATCHES "(\\.|->)[ \t]*$")                                         # member call
        set(_ok TRUE)
      elseif(_before MATCHES "(^|[^A-Za-z0-9_])(float|double|f32x4|auto)[ \t]+$")       # a declaration/definition
        set(_ok TRUE)
      endif()
    endif()
    if(NOT _ok)
      _lint_fail("${file}" ${n} fcdsp.libm "libm call '${_name}(' on the audio path; use fcdsp::log2/exp2/tanh/logCosh/tanPi/sinPi/cosPi: ${code}")
    endif()
    string(LENGTH "${_whole}" _wl)
    math(EXPR _next "${_at} + ${_wl}")
    string(SUBSTRING "${_rest}" ${_next} -1 _rest)
  endwhile()
endfunction()

# _lint_static(<file> <lineno> <code>): an indented `static` that declares a variable.
function(_lint_static file n code)
  if(NOT code MATCHES "^[ \t]+static[ \t]+(.*)$")
    return()
  endif()
  set(_decl "${CMAKE_MATCH_1}")
  if(_decl MATCHES "^(constexpr|consteval|constinit)([^A-Za-z0-9_]|$)" OR _decl MATCHES "^inline[ \t]+(constexpr|consteval)[^A-Za-z0-9_]")
    return()
  endif()
  if(_decl MATCHES "(^|[^A-Za-z0-9_])operator([^A-Za-z0-9_]|$)")
    return()                                                    # static operator...(...): a function
  endif()
  if(NOT _decl MATCHES "^thread_local([^A-Za-z0-9_]|$)")
    set(_prev "")
    while(NOT _decl STREQUAL _prev)                               # drop template argument lists: their commas are
      set(_prev "${_decl}")                                       # not declarator delimiters
      string(REGEX REPLACE "<[^<>]*>" "" _decl "${_decl}")
    endwhile()
    string(REGEX MATCH "[(={,]" _first "${_decl}")                # '[' was mapped to '{', ';' to ','
    if(_first STREQUAL "(" OR _first STREQUAL "")
      return()                                                  # a function declaration (or an unfinished line)
    endif()
  endif()
  _lint_fail("${file}" ${n} fcdsp.static "indented static variable (function-local static or non-constexpr static member): ${code}")
endfunction()

# ---- walk the trees ---------------------------------------------------------------------------------------------------
file(GLOB_RECURSE _files LIST_DIRECTORIES false "${_src}/*.h" "${_src}/*.hpp" "${_src}/*.cpp" "${_src}/*.mm" "${_src}/*.inc")
file(GLOB_RECURSE _tool_files LIST_DIRECTORIES false "${_tools}/*.h" "${_tools}/*.cpp" "${_tools}/*.mm" "${_tools}/*.inc")
list(SORT _files)
list(SORT _tool_files)
set(_n_fcdsp 0)

foreach(_f IN LISTS _tool_files)
  file(STRINGS "${_f}" _hits REGEX "FCMP_TEST_TAP")
  if(_hits)
    _lint_fail("${_f}" "-" test-tap "FCMP_TEST_TAP must never appear (the tap is a runtime pointer): ${_hits}")
  endif()
endforeach()

foreach(_f IN LISTS _files)
  file(RELATIVE_PATH _rel "${_src}" "${_f}")
  set(_in_fcdsp FALSE)
  set(_libm FALSE)
  set(_in_editor FALSE)
  set(_in_editor_gpu FALSE)
  set(_in_plugin FALSE)
  if(_rel MATCHES "^fcdsp/")
    set(_in_fcdsp TRUE)
    math(EXPR _n_fcdsp "${_n_fcdsp} + 1")
    if(_rel MATCHES "^fcdsp/(core|engine|modes)/")
      set(_libm TRUE)
    endif()
  elseif(_rel MATCHES "^editor/")
    set(_in_editor TRUE)
    if(_rel MATCHES "^editor/gpu/")
      set(_in_editor_gpu TRUE)
    endif()
  elseif(_rel MATCHES "^plugin/")
    set(_in_plugin TRUE)
  endif()
  get_filename_component(_base "${_f}" NAME)

  _lint_read(_lines "${_f}")
  set(_n 0)
  set(_block FALSE)
  foreach(_raw IN LISTS _lines)
    math(EXPR _n "${_n} + 1")
    if(_raw MATCHES "FCMP_TEST_TAP")
      _lint_fail("${_f}" ${_n} test-tap "FCMP_TEST_TAP must never appear (the tap is a runtime pointer): ${_raw}")
    endif()
    set(_code "${_raw}")
    _lint_strip(_code _block)
    if(_code STREQUAL "")
      continue()
    endif()
    if(_code MATCHES "JucePlugin_")
      _lint_fail("${_f}" ${_n} product "JucePlugin_* is not used in Source/ (use FcmpProduct.h): ${_code}")
    endif()
    set(_inc "")
    if(_code MATCHES "^[ \t]*#[ \t]*(include|import)[ \t]*[<\"]([^>\"]+)[>\"]")
      set(_inc "${CMAKE_MATCH_2}")
    endif()
    if(_in_fcdsp)
      if(_inc MATCHES "(^|/)(juce_[^/]*|JuceHeader\\.h)(/|$)")
        _lint_fail("${_f}" ${_n} fcdsp.juce "fcdsp is JUCE-free: ${_code}")
      endif()
      if(_inc MATCHES "(^|/)funkgui/")
        _lint_fail("${_f}" ${_n} fcdsp.funkgui "fcdsp never includes FunkGui: ${_code}")
      endif()
      if(_inc MATCHES "(^|/)(plugin|editor)/")
        _lint_fail("${_f}" ${_n} fcdsp.layer "fcdsp never includes plugin/ or editor/: ${_code}")
      endif()
      if(_libm)
        _lint_libm("${_f}" ${_n} "${_code}")
      endif()
      _lint_static("${_f}" ${_n} "${_code}")
    elseif(_in_editor)
      if(_inc MATCHES "(^|/)plugin/" AND NOT _inc MATCHES "(^|/)plugin/ProcessorFacade\\.h$")
        _lint_fail("${_f}" ${_n} editor.facade "editor/ reaches the processor only through plugin/ProcessorFacade.h: ${_code}")
      endif()
      if(_inc MATCHES "(^|/)fcdsp/engine/EngineHost\\.h$" OR _code MATCHES "(^|[^A-Za-z0-9_])EngineHost([^A-Za-z0-9_]|$)")
        _lint_fail("${_f}" ${_n} editor.facade "editor/ never reaches fcdsp::EngineHost: ${_code}")
      endif()
      if(NOT _in_editor_gpu AND _inc MATCHES "(^|/)(funkgui/gpu|bgfx|bx|bimg)/")
        _lint_fail("${_f}" ${_n} editor.gpu "GPU headers only under Source/editor/gpu/: ${_code}")
      endif()
    elseif(_in_plugin)
      if(_inc MATCHES "(^|/)editor/" AND NOT _base STREQUAL "CreateEditorGpu.cpp")
        _lint_fail("${_f}" ${_n} plugin.editor "plugin/ never includes editor/ (only CreateEditorGpu.cpp): ${_code}")
      endif()
    endif()
  endforeach()
endforeach()

list(LENGTH _files _n_files)
list(LENGTH _tool_files _n_tools)
get_property(_bad GLOBAL PROPERTY FCMP_LINT_VIOLATIONS)
message("lint.deps: ${_n_files} files under Source/ (${_n_fcdsp} under Source/fcdsp), ${_n_tools} under Tools/: "
        "${_bad} violation(s)")
if(_bad GREATER 0)
  message(FATAL_ERROR "lint.deps: ${_bad} violation(s)")
endif()
