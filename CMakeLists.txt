cmake_minimum_required(VERSION 3.22)

project(Spaces VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SPACES_BUILD_TESTS "Build the offline test runner" OFF)
set(SPACES_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of fetching it")

#==============================================================================
# JUCE
if(SPACES_JUCE_DIR)
    add_subdirectory(${SPACES_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#==============================================================================
# Plugin identity. The manufacturer and plugin codes are what hosts use to
# recognise saved instances, so don't change them once sessions exist.
set(SPACES_FORMATS VST3)
if(APPLE)
    list(APPEND SPACES_FORMATS AU)
endif()

juce_add_plugin(Spaces
    PRODUCT_NAME                "Spaces"
    COMPANY_NAME                "Gilly"
    BUNDLE_ID                   com.gilly.spaces
    PLUGIN_MANUFACTURER_CODE    Gily
    PLUGIN_CODE                 Spcs
    DESCRIPTION                 "Hear your mix in real spaces"
    FORMATS                     ${SPACES_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Reverb
    AU_MAIN_TYPE                kAudioUnitType_Effect)

#==============================================================================
# Everything except the plugin wrappers lives in SpacesCore, so the test
# runner compiles exactly the same code as the plugin.
add_library(SpacesCore INTERFACE)

target_sources(SpacesCore INTERFACE
    source/AssetCache.cpp
    source/dsp/WarmConvolution.cpp
    source/SpacePack.cpp
    source/PresetManager.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/ui/SpacesLookAndFeel.cpp
    source/ui/HeroView.cpp
    source/ui/Controls.cpp)

target_include_directories(SpacesCore INTERFACE source)

target_compile_definitions(SpacesCore INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    SPACES_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(SpacesCore INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(SpacesCore INTERFACE -Wall -Wextra -Wshadow -Wno-unused-parameter)
endif()

target_link_libraries(Spaces PRIVATE SpacesCore)

#==============================================================================
# Ship the space packs inside each plugin bundle (Contents/Resources/Packs).
# The plugin reads these with the same loader it uses for user-installed
# expansion packs in ~/Music/Spaces/Packs.
foreach(format IN LISTS SPACES_FORMATS)
    add_custom_command(TARGET Spaces_${format} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/spaces"
                "$<TARGET_FILE_DIR:Spaces_${format}>/../Resources/Packs"
        VERBATIM)
endforeach()

#==============================================================================
if(SPACES_BUILD_TESTS)
    juce_add_console_app(SpacesTests PRODUCT_NAME "SpacesTests")
    target_sources(SpacesTests PRIVATE tests/SpacesTests.cpp)
    target_compile_definitions(SpacesTests PRIVATE
        SPACES_TEST_PACKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/spaces"
        JucePlugin_Name="Spaces")
    target_link_libraries(SpacesTests PRIVATE SpacesCore)
endif()
