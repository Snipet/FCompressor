# cmake/FcmpPlatform.cmake: the platforms FCompressor builds on (ADR-92): macOS with AppleClang, and Linux with Clang;
# and the web (ADR-93): wasm32 through Emscripten's Clang, the FCOMPRESSOR_WEB configuration only.
# Included by CMakeLists.txt after project() and before the dependencies, so the compile options it adds reach every
# target made after it: ours, JUCE's module sources compiled inside them, and FunkGui's own tools.
#
# Defines
#   FCMP_PLATFORM         macos | linux | web
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
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(FCMP_PLATFORM web)
  set(FCMP_PLUGIN_FORMATS "")              # no plugin: the engine and the editor are wasm modules (cmake/FcmpWeb.cmake)
else()
  message(FATAL_ERROR "FCompressor builds on macOS and Linux (ADR-92) and for the web (ADR-93), not on "
                      "${CMAKE_SYSTEM_NAME}")
endif()
# The web configuration and the Emscripten toolchain go together (the `web` preset sets both).
if(FCOMPRESSOR_WEB AND NOT FCMP_PLATFORM STREQUAL "web")
  message(FATAL_ERROR "FCompressor: FCOMPRESSOR_WEB needs Emscripten's toolchain, but this build directory targets "
                      "${CMAKE_SYSTEM_NAME}: use a fresh build directory (the `web` preset)")
elseif(FCMP_PLATFORM STREQUAL "web" AND NOT FCOMPRESSOR_WEB)
  message(FATAL_ERROR "FCompressor: an Emscripten build is the web configuration: configure with -DFCOMPRESSOR_WEB=ON "
                      "(the `web` preset)")
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
  message(FATAL_ERROR "FCompressor needs Clang (the warning list and the lints are Clang's), not "
                      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER}. On Linux install clang and configure a fresh "
                      "build directory (clang is the default there unless CC/CXX or CMAKE_CXX_COMPILER say otherwise).")
endif()

# Linux, -flto (ADR-92). fcmp_lto's objects are LLVM bitcode, and GNU ar and ranlib can index a bitcode member only
# through an LLVM gold plugin of the compiler's own LLVM. A machine may have another one in binutils' plugin directory
# (GitHub's Ubuntu 24.04 image: Clang 18 beside LLVM 17's plugin): ar then prints "failed to create LTO module" per
# member and writes an archive without their symbols, and every link against it ends in undefined references. The
# compiler's own llvm-ar and llvm-ranlib read bitcode with no plugin, so every static library here is made with them
# (ours, JUCE's shared code, FunkGui's and bgfx's: they are all created after this file).
if(FCMP_PLATFORM STREQUAL "linux" AND FCOMPRESSOR_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  if(NOT CMAKE_CXX_COMPILER_AR OR NOT CMAKE_CXX_COMPILER_RANLIB)
    message(FATAL_ERROR "FCompressor: -flto on Linux needs the llvm-ar and llvm-ranlib of ${CMAKE_CXX_COMPILER} "
                        "(install the distribution's llvm package), or configure with -DFCOMPRESSOR_LTO=OFF")
  endif()
  set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}")
  set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}")
  message(STATUS "FCompressor: LTO archives with ${CMAKE_AR}")
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
