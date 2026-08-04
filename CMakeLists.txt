cmake_minimum_required(VERSION 3.22)

project(MixBalancerVST3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(JUCE_FETCH "Download JUCE during configure" ON)
option(DOLBY740_CI_ONLY "Configure only Dolby740 target" OFF)

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

if(NOT DOLBY740_CI_ONLY)

juce_add_plugin(MixBalancer
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.mixbalancer
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Mxb1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Mix Balancer"
)

juce_generate_juce_header(MixBalancer)

target_sources(MixBalancer
    PRIVATE
        src/MixBalanceBus.cpp
        src/MixBalanceBus.h
    src/mixbalancer/MixBalancerEditor.cpp
    src/mixbalancer/MixBalancerEditor.h
    src/mixbalancer/MixBalancerProcessor.cpp
    src/mixbalancer/MixBalancerProcessor.h
)

target_compile_definitions(MixBalancer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(MixBalancer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_add_plugin(DrumBusUltimate
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.drumbusultimate
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Dbu1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DrumBusUltimate"
)

juce_generate_juce_header(DrumBusUltimate)

target_sources(DrumBusUltimate
    PRIVATE
        src/drumbus/DrumBusEditor.cpp
        src/drumbus/DrumBusEditor.h
        src/drumbus/DrumBusProcessor.cpp
        src/drumbus/DrumBusProcessor.h
)

target_compile_definitions(DrumBusUltimate
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(DrumBusUltimate
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------
# TapeChannel — VST3 tape machine channel strip (schematic Fig 4-8, Ch.1)
# -----------------------------------------------------------------------
juce_add_plugin(TapeChannel
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.tapechannel
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Tch1
    FORMATS VST3 Standalone
    PRODUCT_NAME "TapeChannel"
)

juce_generate_juce_header(TapeChannel)

target_sources(TapeChannel
    PRIVATE
        src/tapechannel/TapeChannelProcessor.cpp
        src/tapechannel/TapeChannelProcessor.h
        src/tapechannel/TapeChannelEditor.cpp
        src/tapechannel/TapeChannelEditor.h
)

target_compile_definitions(TapeChannel
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(TapeChannel
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------
# SyncMultitrackTape - synchronized tape saturation with global wow/flutter
# -----------------------------------------------------------------------
juce_add_plugin(SyncMultitrackTape
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.syncmultitracktape
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Smt1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Synchronized Multitrack Tape"
)

juce_generate_juce_header(SyncMultitrackTape)

target_sources(SyncMultitrackTape
    PRIVATE
        src/synctape/SyncTapeGlobalState.cpp
        src/synctape/SyncTapeGlobalState.h
        src/synctape/SyncTapeGlobalWindow.cpp
        src/synctape/SyncTapeGlobalWindow.h
        src/synctape/SyncTapeProcessor.cpp
        src/synctape/SyncTapeProcessor.h
        src/synctape/SyncTapeEditor.cpp
        src/synctape/SyncTapeEditor.h
)

target_compile_definitions(SyncMultitrackTape
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(SyncMultitrackTape
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_add_plugin(MasterBusUltimate
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.masterbusultimate
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Mbu1
    FORMATS VST3 Standalone
    PRODUCT_NAME "MasterBusUltimate"
)

juce_generate_juce_header(MasterBusUltimate)

target_sources(MasterBusUltimate
    PRIVATE
        src/masterbus/MasterBusProcessor.cpp
        src/masterbus/MasterBusProcessor.h
        src/masterbus/MasterBusEditor.cpp
        src/masterbus/MasterBusEditor.h
)

target_compile_definitions(MasterBusUltimate
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(MasterBusUltimate
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------
# AutoAlign — FFT cross-correlation track alignment (à la SoundRadix Auto-Align)
# -----------------------------------------------------------------------
juce_add_plugin(AutoAlign
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.autoalign
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Aal1
    FORMATS VST3 Standalone
    PRODUCT_NAME "AutoAlign"
)

juce_generate_juce_header(AutoAlign)

target_sources(AutoAlign
    PRIVATE
        src/autoalign/AutoAlignGroupManager.cpp
        src/autoalign/AutoAlignGroupManager.h
        src/autoalign/AutoAlignProcessor.cpp
        src/autoalign/AutoAlignProcessor.h
        src/autoalign/AutoAlignEditor.cpp
        src/autoalign/AutoAlignEditor.h
            src/autoalign/AutoAlignGroupListView.cpp
            src/autoalign/AutoAlignGroupListView.h
)

target_compile_definitions(AutoAlign
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(AutoAlign
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------
# BassChannelStrip - CLA-bass-inspired all-in-one bass processing strip
# -----------------------------------------------------------------------
juce_add_plugin(BassChannelStrip
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.basschannelstrip
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE Bcs1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Bass Channel Strip"
)

juce_generate_juce_header(BassChannelStrip)

target_sources(BassChannelStrip
    PRIVATE
        src/basschannelstrip/BassChannelStripProcessor.cpp
        src/basschannelstrip/BassChannelStripProcessor.h
        src/basschannelstrip/BassChannelStripEditor.cpp
        src/basschannelstrip/BassChannelStripEditor.h
)

target_compile_definitions(BassChannelStrip
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(BassChannelStrip
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

endif()

# -----------------------------------------------------------------------
# Dolby740 - Dolby 740 inspired dual-mono spectral processor
# -----------------------------------------------------------------------
juce_add_plugin(Dolby740
    COMPANY_NAME "Audio Plugin Creator"
    BUNDLE_ID com.audioplugincreator.dolby740
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Apc1
    PLUGIN_CODE D740
    FORMATS VST3
    PRODUCT_NAME "Dolby 740"
)

juce_generate_juce_header(Dolby740)

target_sources(Dolby740
    PRIVATE
    src/dolby740/Dolby740Processor.cpp
    src/dolby740/Dolby740Processor.h
    src/dolby740/Dolby740Editor.cpp
    src/dolby740/Dolby740Editor.h
)

target_compile_definitions(Dolby740
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(Dolby740
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
