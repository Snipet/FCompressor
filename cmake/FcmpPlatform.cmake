# cmake/FcmpPlatform.cmake: the platforms FCompressor builds on (ADR-91): macOS with AppleClang, and Linux with Clang.
# Included by CMakeLists.txt after project() and before the dependencies, so the compile options it adds reach every
# target made after it: ours, JUCE's module sources compiled inside them, and FunkGui's own tools.
#
# Defines
#   FCMP_PLATFORM         macos | linux
#   FCMP_PLUGIN_FORMATS   the juce_add_plugin FORMATS: AU VST3 Standalone on macOS; VST3 Standalone on Linux (no AU
#                         outside Apple)
#
# Clang only, on both: FCMP_WARNING_FLAGS (cmake/FcmpArch.cmake) and lint.headers are JUCE's Clang list, and fcdsp's
# -Wglobal-constructors / -Wexit-time-destructors exist only in Clang. CMakeLists.txt makes clang the default compiler
# on Linux; another compiler is refused here.
#
# JUCE 8.0.4 and upstream Clang. juce_audio_processors/processors/juce_AudioPluginInstance.h:173 declares
#
#   template <size_t numLayouts>
#   AudioPluginInstance (const short channelLayoutList[numLayouts][2]) : AudioProcessor (channelLayoutList) {}
#
# whose parameter decays to the non-dependent `const short (*)[2]`: upstream Clang (22 on Arch Linux) resolves the base
# constructor call where the template is defined, finds none, and fails every translation unit that includes
# juce_audio_processors.h. AppleClang does not. Where the compiler rejects exactly that pattern, the constructor's body
# is deferred to an instantiation that never happens with -fdelayed-template-parsing, and Clang's deprecation warning
# for that flag in C++20 (a driver warning -Werror would make an error) is silenced with it. JUCE configurations only:
# the DSP-only build compiles no JUCE and keeps its flags. FunkGui's own builds do the same (its
# cmake/FunkGuiPlatform.cmake). It goes when the JUCE pin moves past 8.0.4.
include_guard(GLOBAL)
include(CheckCXXSourceCompiles)

if(APPLE)
  set(FCMP_PLATFORM macos)
  set(FCMP_PLUGIN_FORMATS AU VST3 Standalone)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(FCMP_PLATFORM linux)
  set(FCMP_PLUGIN_FORMATS VST3 Standalone)
else()
  message(FATAL_ERROR "FCompressor builds on macOS and Linux (ADR-91), not on ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
  message(FATAL_ERROR "FCompressor needs Clang (the warning list and the lints are Clang's), not "
                      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER}. On Linux install clang and configure a fresh "
                      "build directory (clang is the default there unless CC/CXX or CMAKE_CXX_COMPILER say otherwise).")
endif()

if(FCOMPRESSOR_DSP_ONLY)
  return()                                   # no JUCE: nothing to work around, and fcdsp keeps its own flags
endif()
set(_fcmp_juce804_probe [=[
#include <cstddef>
#include <initializer_list>
struct B { B(); B(const std::initializer_list<const short[2]>&); };
struct D : B { template <std::size_t n> D(const short l[n][2]) : B(l) {} };
int main() { return 0; }
]=])
check_cxx_source_compiles("${_fcmp_juce804_probe}" FCMP_CXX_ACCEPTS_JUCE804_LAYOUT_CTOR)
if(NOT FCMP_CXX_ACCEPTS_JUCE804_LAYOUT_CTOR)
  set(FCMP_JUCE804_WORKAROUND -fdelayed-template-parsing -Wno-delayed-template-parsing-in-cxx20)
  list(JOIN FCMP_JUCE804_WORKAROUND " " CMAKE_REQUIRED_FLAGS)
  string(APPEND CMAKE_REQUIRED_FLAGS " -Werror")
  check_cxx_source_compiles("${_fcmp_juce804_probe}" FCMP_CXX_DELAYED_TEMPLATES_ACCEPT_JUCE804)
  unset(CMAKE_REQUIRED_FLAGS)
  if(NOT FCMP_CXX_DELAYED_TEMPLATES_ACCEPT_JUCE804)
    message(FATAL_ERROR "FCompressor: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} rejects JUCE 8.0.4's "
                        "AudioPluginInstance layout constructor, even with -fdelayed-template-parsing")
  endif()
  foreach(_f IN LISTS FCMP_JUCE804_WORKAROUND)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${_f}>)
  endforeach()
  string(JOIN " " _fcmp_flags_text ${FCMP_JUCE804_WORKAROUND})
  message(STATUS "FCompressor: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} rejects JUCE 8.0.4's "
                 "AudioPluginInstance layout constructor: ${_fcmp_flags_text}")
endif()
