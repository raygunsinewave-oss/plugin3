cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(MysteryBox VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MYSTERYBOX_BUILD_TESTS "Build the DSP / processor unit tests" ON)
set(MYSTERYBOX_JUCE_DIR "" CACHE PATH "Use a local JUCE 8 checkout instead of fetching one")

# ---------------------------------------------------------------- JUCE
if(MYSTERYBOX_JUCE_DIR)
    add_subdirectory(${MYSTERYBOX_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# ---------------------------------------------------------------- Embedded resources
juce_add_binary_data(MysteryBoxData SOURCES
    resources/fonts/Orbitron.ttf
    resources/fonts/JetBrainsMono-Regular.ttf
    resources/factory_presets.json)

# ---------------------------------------------------------------- Plugin
set(MYSTERYBOX_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MYSTERYBOX_FORMATS AU)
endif()

juce_add_plugin(MysteryBox
    PRODUCT_NAME                "Mystery Box"
    COMPANY_NAME                "Raygun"
    COMPANY_WEBSITE             "https://example.com/raygun"
    BUNDLE_ID                   "com.raygun.mysterybox"
    PLUGIN_MANUFACTURER_CODE    Rygn
    PLUGIN_CODE                 MyBx
    FORMATS                     ${MYSTERYBOX_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    DESCRIPTION                 "6-oscillator cyberpunk synthesizer")

set(MYSTERYBOX_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/Sections.cpp
    src/ui/FxPage.cpp
    src/ui/Fonts.cpp
    src/ui/SystemPages.cpp
    src/ui/PresetsPage.cpp
    src/ui/XdPage.cpp
    src/games/GamesA.cpp
    src/games/GamesB.cpp
    src/games/GamesC.cpp
    src/params/Parameters.cpp
    src/state/Assets.cpp
    src/state/PresetManager.cpp
    src/dsp/Effects.cpp
    src/fx/FxRack.cpp)

target_sources(MysteryBox PRIVATE ${MYSTERYBOX_SOURCES})

set(MYSTERYBOX_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    MYSTERYBOX_VERSION="${PROJECT_VERSION}")

target_compile_definitions(MysteryBox PUBLIC ${MYSTERYBOX_DEFINITIONS})

set(MYSTERYBOX_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_opengl
    MysteryBoxData)

target_link_libraries(MysteryBox
    PRIVATE ${MYSTERYBOX_MODULES}
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- Tests
if(MYSTERYBOX_BUILD_TESTS)
    juce_add_console_app(MysteryBoxTests PRODUCT_NAME "MysteryBoxTests")
    target_sources(MysteryBoxTests PRIVATE tests/TestMain.cpp tests/DspTests.cpp tests/ProcessorTests.cpp tests/PresetTests.cpp ${MYSTERYBOX_SOURCES})
    target_compile_definitions(MysteryBoxTests PRIVATE ${MYSTERYBOX_DEFINITIONS})
    target_link_libraries(MysteryBoxTests
        PRIVATE ${MYSTERYBOX_MODULES}
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
    if(MSVC)
        target_link_options(MysteryBoxTests PRIVATE /STACK:16777216)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(MysteryBoxTests PRIVATE -Wno-format-zero-length -Wno-format-security)
    endif()
    enable_testing()
    add_test(NAME MysteryBoxTests COMMAND MysteryBoxTests)
endif()
