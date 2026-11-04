cmake_minimum_required(VERSION 3.22)

project(Murmur VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you have one (-DMURMUR_JUCE_PATH=...),
# otherwise download JUCE 8 automatically.
# ---------------------------------------------------------------------------
set(MURMUR_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(MURMUR_JUCE_PATH)
    add_subdirectory(${MURMUR_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MURMUR_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MURMUR_FORMATS AU)
endif()

juce_add_plugin(Murmur
    COMPANY_NAME "Kevin Kaye"
    COMPANY_WEBSITE ""
    BUNDLE_ID "uk.kevinkaye.murmur"
    PLUGIN_MANUFACTURER_CODE Kkay
    PLUGIN_CODE Murm
    FORMATS ${MURMUR_FORMATS}
    PRODUCT_NAME "Murmur"
    DESCRIPTION "Micro-looper and spatial effects with a musical clock"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay Reverb)

juce_add_binary_data(MurmurData SOURCES Resources/RobotoCondensed-Bold.ttf)

target_sources(Murmur PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PedalControl.cpp
    Source/Keeper.cpp)

target_include_directories(Murmur PRIVATE Source Source/engine)

target_compile_definitions(Murmur PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Murmur
    PRIVATE
        MurmurData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline engine test (no JUCE needed): renders test audio through every mode
# ---------------------------------------------------------------------------
option(MURMUR_BUILD_TESTS "Build the offline render test" ON)
if(MURMUR_BUILD_TESTS)
    add_executable(murmur_render_test Tests/RenderTest.cpp)
    target_include_directories(murmur_render_test PRIVATE Source/engine)

    add_executable(murmur_bench Tests/Benchmark.cpp)
    target_include_directories(murmur_bench PRIVATE Source/engine)

    # Plugin-level tests run the real processor and editor against a fake host
    add_executable(murmur_plugin_test Tests/PluginTest.cpp)
    target_include_directories(murmur_plugin_test PRIVATE Source Source/engine
        $<TARGET_PROPERTY:Murmur,INCLUDE_DIRECTORIES>)
    target_compile_definitions(murmur_plugin_test PRIVATE $<TARGET_PROPERTY:Murmur,COMPILE_DEFINITIONS>)
    target_compile_options(murmur_plugin_test PRIVATE $<TARGET_PROPERTY:Murmur,COMPILE_OPTIONS>)
    target_link_libraries(murmur_plugin_test PRIVATE Murmur)
endif()
