cmake_minimum_required(VERSION 3.25)

project(HumToSing VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

option(HUM_BUILD_GUI "Build the Qt desktop application" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(hum_core STATIC
    src/core/midi.cpp
    src/core/midi_file.cpp
    src/core/note_extraction.cpp
    src/core/pitch.cpp
    src/core/recording.cpp
    src/core/scale.cpp
    src/core/wave_fixture.cpp
)
add_library(HumToSing::core ALIAS hum_core)

add_executable(hum_pitch_fixture src/tools/PitchFixtureMain.cpp)
target_link_libraries(hum_pitch_fixture PRIVATE HumToSing::core)
set_target_properties(hum_pitch_fixture PROPERTIES OUTPUT_NAME "HumPitchFixture")

target_include_directories(hum_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(hum_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(hum_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(BUILD_TESTING)
    add_executable(hum_core_tests tests/core_tests.cpp)
    target_link_libraries(hum_core_tests PRIVATE HumToSing::core)
    add_test(NAME hum_core_tests COMMAND hum_core_tests)
    set_tests_properties(hum_core_tests PROPERTIES LABELS "core")

    add_executable(hum_recording_tests tests/recording_tests.cpp)
    target_include_directories(hum_recording_tests PRIVATE tests)
    target_link_libraries(hum_recording_tests PRIVATE HumToSing::core)
    add_test(NAME hum_recording_tests COMMAND hum_recording_tests)
    set_tests_properties(hum_recording_tests PROPERTIES LABELS "core;recording")

    add_executable(hum_pitch_tests tests/pitch_tests.cpp)
    target_link_libraries(hum_pitch_tests PRIVATE HumToSing::core)
    add_test(NAME hum_pitch_tests COMMAND hum_pitch_tests)
    set_tests_properties(hum_pitch_tests PROPERTIES LABELS "core;pitch")

    add_executable(hum_note_extraction_tests tests/note_extraction_tests.cpp)
    target_link_libraries(hum_note_extraction_tests PRIVATE HumToSing::core)
    add_test(NAME hum_note_extraction_tests COMMAND hum_note_extraction_tests)
    set_tests_properties(hum_note_extraction_tests PROPERTIES LABELS "core;note-extraction;midi")
endif()

if(HUM_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Multimedia Widgets)

    add_executable(hum_to_sing
        src/app/LevelMeter.cpp
        src/app/LevelMeter.hpp
        src/app/main.cpp
        src/app/MainWindow.cpp
        src/app/MainWindow.hpp
        src/app/PitchLabProcessor.cpp
        src/app/PitchLabProcessor.hpp
        src/app/PhraseConversionProcessor.cpp
        src/app/PhraseConversionProcessor.hpp
        src/app/QtAudioCapture.cpp
        src/app/QtAudioCapture.hpp
        src/app/QtAudioPlayback.cpp
        src/app/QtAudioPlayback.hpp
    )
    target_link_libraries(hum_to_sing
        PRIVATE
            HumToSing::core
            Qt6::Concurrent
            Qt6::Multimedia
            Qt6::Widgets
    )
    set_target_properties(hum_to_sing PROPERTIES
        OUTPUT_NAME "HumToSing"
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Hum-to-MIDI Music Sketchpad"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hum-to-sing.sketchpad"
    )
    if(APPLE)
        set_target_properties(hum_to_sing PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
        )
    endif()

    install(TARGETS hum_to_sing
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(BUILD_TESTING)
        add_test(NAME hum_app_smoke COMMAND hum_to_sing --smoke-test)
        set_tests_properties(hum_app_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "gui;smoke"
            TIMEOUT 10
        )
    endif()
endif()

set(CPACK_PACKAGE_NAME "HumToSing")
set(CPACK_PACKAGE_VENDOR "Hum-to-MIDI Music Sketchpad")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
