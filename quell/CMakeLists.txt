cmake_minimum_required(VERSION 3.22)
# macOS: one universal build for Apple silicon and Intel
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
project(Quell VERSION 1.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(QUELL_COPY_AFTER_BUILD "Install into the user plug-in folders after building" OFF)

option(QUELL_BUILD_TESTS "Build the offline test tool (QuellTest)" ON)
option(QUELL_BUILD_VST2 "Also build the VST2-compatible plug-in (uses the FST headers)" ON)

# JUCE and the VST2 headers live at the top of the bundle
set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT COMMAND juce_add_plugin)
    if(NOT EXISTS "${BUNDLE_ROOT}/JUCE/CMakeLists.txt")
        message(FATAL_ERROR "JUCE is missing - run: git submodule update --init")
    endif()
    add_subdirectory("${BUNDLE_ROOT}/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
endif()

# AU + VST3 + app on macOS, VST3 + app on Windows, VST3 + LV2 + app on Linux; VST2 everywhere
if(APPLE)
    set(QL_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(QL_FORMATS VST3 LV2 Standalone)
else()
    set(QL_FORMATS VST3 Standalone)
endif()
if(QUELL_BUILD_VST2)
    # FST: clean-room, GPL-licensed headers for the VST2 plug-in interface
    if(NOT TARGET juce_vst2_sdk)
        juce_set_vst2_sdk_path("${BUNDLE_ROOT}/third_party/vst2-fst")
    endif()
    list(APPEND QL_FORMATS VST)
endif()

juce_add_plugin(Quell
    VERSION 1.0.1
    COMPANY_NAME "FreakiePie"
    BUNDLE_ID "com.freakiepie.quell"
    PLUGIN_MANUFACTURER_CODE Frkp
    PLUGIN_CODE Qell
    FORMATS ${QL_FORMATS}
    PRODUCT_NAME "Quell"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    DESCRIPTION "Dynamic resonance suppressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx EQ Dynamics
    VST2_CATEGORY kPlugCategEffect
    LV2URI "urn:freakiepie:quell"
    COPY_PLUGIN_AFTER_BUILD ${QUELL_COPY_AFTER_BUILD})

target_sources(Quell PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(QL_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=0 _USE_MATH_DEFINES=1)
target_compile_definitions(Quell PUBLIC ${QL_DEFS})
target_link_libraries(Quell
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

if(QUELL_BUILD_TESTS)
juce_add_console_app(QuellTest PRODUCT_NAME "QuellTest")
target_sources(QuellTest PRIVATE tests/Test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(QuellTest PRIVATE ${QL_DEFS} JucePlugin_Name="Quell" JucePlugin_Build_Standalone=1
    JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=0 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
    JucePlugin_ManufacturerCode=0x46726b70 JucePlugin_PluginCode=0x51656c6c JucePlugin_VersionString="1.0.1"
    JucePlugin_Manufacturer="FreakiePie" JucePlugin_Desc="Quell" JucePlugin_VersionCode=0x10001 QUELL_TEST=1)
target_link_libraries(QuellTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
