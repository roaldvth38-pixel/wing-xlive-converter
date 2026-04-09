cmake_minimum_required(VERSION 3.22)

project(JuiceVST3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(JUCE_FETCH "Download JUCE during configure" ON)

if(WIN32 AND NOT DEFINED JUCE_TARGET_ARCHITECTURE)
    if(CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")
        set(JUCE_TARGET_ARCHITECTURE "i386" CACHE STRING "JUCE target architecture")
    elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
        set(JUCE_TARGET_ARCHITECTURE "aarch64" CACHE STRING "JUCE target architecture")
    else()
        set(JUCE_TARGET_ARCHITECTURE "x86_64" CACHE STRING "JUCE target architecture")
    endif()
endif()

if(JUCE_FETCH)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.5
    )
    FetchContent_MakeAvailable(JUCE)
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

juce_add_plugin(Juice
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.juice
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Juc1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Juice"
)

juce_generate_juce_header(Juice)

target_sources(Juice
    PRIVATE
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/PluginProcessor.cpp
        src/PluginProcessor.h
)

target_compile_definitions(Juice
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(Juice
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
