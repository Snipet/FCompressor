# cmake/FcmpPlugin.cmake: the FCompressor plugin target (03 §2.7-§2.8; 02 §1.9). Every JUCE configuration.
#
# - Sources: FCMP_PLUGIN_SOURCES + FCMP_EDITOR_SOURCES + the one CreateEditor*.cpp FcmpSources.cmake chose, plus the
#   GPU editor sources in the GPU configuration. Our files get FCMP_WARNING_FLAGS (with -Werror) per file; JUCE's module
#   TUs in the same targets never do (03 §2.2).
# - FunkGui is linked PRIVATE only: core + presets in every JUCE configuration, gpu in the GPU one. A PUBLIC link would
#   compile FunkGui's sources (.mm included) into every format wrapper, so configure fails if one appears (K2 #26e).
# - Product identity comes from cmake/FcmpSources.cmake (the same values as the generated FcmpProduct.h).
include_guard(GLOBAL)

set(FCOMPRESSOR_COMPANY_WEBSITE "" CACHE STRING "Shown by hosts (VST3 manifest, AU)")
set(FCOMPRESSOR_COMPANY_EMAIL   "" CACHE STRING "Shown by hosts (VST3 manifest)")

juce_add_plugin(FCompressor
    COMPANY_NAME                  "${FCMP_COMPANY_NAME}"
    COMPANY_COPYRIGHT             "${FCMP_COMPANY_COPYRIGHT}"
    COMPANY_WEBSITE               "${FCOMPRESSOR_COMPANY_WEBSITE}"
    COMPANY_EMAIL                 "${FCOMPRESSOR_COMPANY_EMAIL}"
    PLUGIN_MANUFACTURER_CODE      ${FCMP_MANUFACTURER_CODE}
    PLUGIN_CODE                   ${FCMP_PLUGIN_CODE}
    FORMATS                       AU VST3 Standalone
    PRODUCT_NAME                  "${FCMP_PRODUCT_NAME}"
    VERSION                       ${PROJECT_VERSION}
    BUNDLE_ID                     ${FCMP_BUNDLE_ID}
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              FALSE
    NEEDS_MIDI_OUTPUT             FALSE
    IS_MIDI_EFFECT                FALSE
    VST3_CATEGORIES               Fx Dynamics
    AU_MAIN_TYPE                  kAudioUnitType_Effect
    # The Standalone records from the device the user picks; without the usage text macOS kills it on first input
    # access. The matching entitlement is Resources/FCompressor.entitlements (Scripts/release.sh signs with it).
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "FCompressor processes the audio input you choose."
    HARDENED_RUNTIME_ENABLED      TRUE
    HARDENED_RUNTIME_OPTIONS      "com.apple.security.device.audio-input"
    # JUCE does not emit LSMinimumSystemVersion; merge it from the variable the compiler uses (HR :183-210).
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict><key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string></dict></plist>"
    COPY_PLUGIN_AFTER_BUILD       ${FCOMPRESSOR_INSTALL_AFTER_BUILD})

set(_fcmp_plugin_own ${FCMP_PLUGIN_SOURCES} ${FCMP_EDITOR_SOURCES} ${FCMP_CREATE_EDITOR})
if(NOT FCOMPRESSOR_HEADLESS)
  list(APPEND _fcmp_plugin_own ${FCMP_EDITOR_GPU_SOURCES})
endif()
target_sources(FCompressor PRIVATE ${_fcmp_plugin_own})
fcmp_warn_sources(${_fcmp_plugin_own})
target_include_directories(FCompressor PRIVATE ${FCMP_SOURCE_ROOT} ${FCMP_GENERATED_DIR})

# JUCE configuration: PUBLIC, so the format wrappers compile JUCE with the same settings as the shared code.
target_compile_definitions(FCompressor PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(FCMP_GPU_EDITOR)
  target_compile_definitions(FCompressor PRIVATE FCOMPRESSOR_GPU_EDITOR=1)
endif()

target_link_libraries(FCompressor
    PRIVATE fcdsp fcmp_flags juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(FCOMPRESSOR_LTO)          # -flto and the Xcode weak-reference workaround, Release only (JUCEHelperTargets.cmake:140-150)
  target_link_libraries(FCompressor PUBLIC juce::juce_recommended_lto_flags)
endif()

# FunkGui (02 §1.9).
target_link_libraries(FCompressor PRIVATE FunkGui::core FunkGui::presets)
funkgui_configure_product(FCompressor PRODUCT ${FCMP_PRODUCT_NAME} OBJC_PREFIX ${FCMP_OBJC_PREFIX}
                          ENV_PREFIX ${FCMP_ENV_PREFIX} PREFS_FOLDER ${FCMP_PREFS_FOLDER})
if(NOT FCOMPRESSOR_HEADLESS)
  target_link_libraries(FCompressor PRIVATE FunkGui::gpu)
  funkgui_compile_shaders(FCompressor)
  funkgui_add_font(FCompressor)      # font licence + bgfx/bx/bimg/bgfx.cmake licences as bundle resources (02 §1.6)
endif()

# GlueCompressor's guard against codesign's "resource fork, Finder information, or similar detritus not allowed" (B §4).
# Ninja runs PRE_BUILD as PRE_LINK; the first build has no bundle yet, hence "|| true".
foreach(_fmt AU VST3)
  if(TARGET FCompressor_${_fmt})
    add_custom_command(TARGET FCompressor_${_fmt} PRE_BUILD
        COMMAND /bin/sh -c "xattr -cr \"$<TARGET_BUNDLE_DIR:FCompressor_${_fmt}>\" 2>/dev/null || true"
        VERBATIM)
  endif()
endforeach()

# PRIVATE-link assertion (K2 #26e). On a static library a PRIVATE dependency is recorded as $<LINK_ONLY:...>, which
# carries no usage requirements (no INTERFACE_SOURCES) to the wrappers; anything else naming FunkGui is a PUBLIC or
# INTERFACE link and fails the configure.
get_target_property(_fcmp_ill FCompressor INTERFACE_LINK_LIBRARIES)
if(_fcmp_ill)
  foreach(_l IN LISTS _fcmp_ill)
    if(_l MATCHES "FunkGui|FunkPresets" AND NOT _l MATCHES "^\\$<LINK_ONLY:")
      message(FATAL_ERROR "FCompressor: FunkGui must be linked PRIVATE (K2 #26e); INTERFACE_LINK_LIBRARIES has '${_l}'")
    endif()
  endforeach()
endif()
