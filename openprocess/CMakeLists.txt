cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(openprocess VERSION 1.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(OP_COPY_AFTER_BUILD "Install the plug-ins into your user plug-in folders after building" OFF)
option(OP_BUILD_TESTS "Build the offline test tool (OpTest)" ON)
option(OP_BUILD_VST2 "Also build the VST2-compatible plug-in (uses the FST headers)" ON)

# JUCE and the VST2 headers live at the top of the bundle
set(BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT COMMAND juce_add_plugin)
    add_subdirectory("${BUNDLE_ROOT}/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
endif()

if(APPLE)
    set(OP_FORMATS AU VST3 Standalone)
elseif(UNIX)
    set(OP_FORMATS VST3 LV2 Standalone)
else()
    set(OP_FORMATS VST3 Standalone)
endif()
if(OP_BUILD_VST2)
    if(NOT TARGET juce_vst2_sdk)
        juce_set_vst2_sdk_path("${BUNDLE_ROOT}/third_party/vst2-fst")
    endif()
    list(APPEND OP_FORMATS VST)
endif()

set(OP_SOURCES Source/Bank.cpp Source/Engine.cpp Source/Model.cpp
               Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(OP_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_USE_MP3AUDIOFORMAT=1
            JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0 _USE_MATH_DEFINES=1)

# openprocess (stereo) and opensphere (the same workspace as an Ambisonics encoder) share the sources
function(op_add_plugin target code name desc spatial au_type)
    juce_add_plugin(${target}
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "FreakiePie"
        BUNDLE_ID "com.freakiepie.${target}"
        PLUGIN_MANUFACTURER_CODE Frkp
        PLUGIN_CODE ${code}
        FORMATS ${OP_FORMATS}
        PRODUCT_NAME "${name}"
        DESCRIPTION "${desc}"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE ${au_type}
        VST3_CATEGORIES Instrument Sampler
        VST2_CATEGORY kPlugCategSynth
        LV2URI "urn:freakiepie:${target}"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "${name} can capture audio input into its sound browser."
        COPY_PLUGIN_AFTER_BUILD ${OP_COPY_AFTER_BUILD})
    target_sources(${target} PRIVATE ${OP_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC ${OP_DEFS} OP_SPATIAL=${spatial})
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    if(OP_BUILD_TESTS)
        juce_add_console_app(${target}Test PRODUCT_NAME "${target}Test" VERSION ${PROJECT_VERSION})
        target_sources(${target}Test PRIVATE tests/OpTest.cpp ${OP_SOURCES})
        target_include_directories(${target}Test PRIVATE Source)
        target_compile_definitions(${target}Test PRIVATE ${OP_DEFS} OP_SPATIAL=${spatial} JucePlugin_Name="${name}"
            JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_Build_Standalone=0 JUCE_MODAL_LOOPS_PERMITTED=1 OP_TEST_BUILD=1
            JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
        target_link_libraries(${target}Test PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endif()
endfunction()

op_add_plugin(openprocess OpPr "openprocess"
    "Loop workspace after Oval's ovalprocess: browser, mix window, snapshots, timeline" 0 kAudioUnitType_MusicDevice)
op_add_plugin(opensphere OpSp "opensphere"
    "openprocess as an Ambisonics encoder: loops and live input placed and moved around you, 1st-7th order" 1 kAudioUnitType_MusicDevice)
