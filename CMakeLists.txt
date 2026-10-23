cmake_minimum_required(VERSION 3.25)

project(HumToSing VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

option(HUM_BUILD_GUI "Build the Qt desktop application" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(cmake/NeuralVocal.cmake)

add_library(hum_core STATIC
    src/core/midi.cpp
    src/core/midi_file.cpp
    src/core/note_editor.cpp
    src/core/note_extraction.cpp
    src/core/voice_clip.cpp
    src/core/overdub.cpp
    src/core/pitch.cpp
    src/core/project_file.cpp
    src/core/recording.cpp
    src/core/scale.cpp
    src/core/simple_synth.cpp
    src/core/wave_fixture.cpp
)
add_library(HumToSing::core ALIAS hum_core)

if(HUM_VOCAL_BACKEND_AVAILABLE)
    add_executable(hum_vocal_fixture src/tools/VocalFixtureMain.cpp)
    target_link_libraries(hum_vocal_fixture PRIVATE hum_vocal HumToSing::core)
    target_compile_definitions(hum_vocal_fixture PRIVATE HUM_VOCAL_MODEL_FILE="${CMAKE_SOURCE_DIR}/resources/vocal/swift-f0.onnx")
    set_target_properties(hum_vocal_fixture PROPERTIES OUTPUT_NAME HumVocalFixture)
endif()

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
    add_executable(hum_overdub_tests tests/overdub_tests.cpp)
    target_include_directories(hum_overdub_tests PRIVATE src/app tests)
    target_link_libraries(hum_overdub_tests PRIVATE HumToSing::core)
    add_test(NAME hum_overdub_tests COMMAND hum_overdub_tests)
    set_tests_properties(hum_overdub_tests PROPERTIES LABELS "core;recording;playback")

    add_executable(hum_vocal_capture_tests tests/vocal_capture_tests.cpp src/app/vocal/IsolatedMicRecorder.cpp)
    target_include_directories(hum_vocal_capture_tests PRIVATE src/app tests)
    target_link_libraries(hum_vocal_capture_tests PRIVATE HumToSing::core)
    add_test(NAME hum_vocal_capture_tests COMMAND hum_vocal_capture_tests)
    set_tests_properties(hum_vocal_capture_tests PROPERTIES LABELS "core;vocal;recording")

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

    add_executable(hum_note_editor_tests tests/note_editor_tests.cpp)
    target_link_libraries(hum_note_editor_tests PRIVATE HumToSing::core)
    add_test(NAME hum_note_editor_tests COMMAND hum_note_editor_tests)
    set_tests_properties(hum_note_editor_tests PROPERTIES LABELS "core;note-editor;midi")

    add_executable(hum_scale_guidance_tests tests/scale_guidance_tests.cpp)
    target_link_libraries(hum_scale_guidance_tests PRIVATE HumToSing::core)
    add_test(NAME hum_scale_guidance_tests COMMAND hum_scale_guidance_tests)
    set_tests_properties(hum_scale_guidance_tests PROPERTIES LABELS "core;note-editor;scale;midi")

    add_executable(hum_arrangement_tests tests/arrangement_tests.cpp)
    target_link_libraries(hum_arrangement_tests PRIVATE HumToSing::core)
    add_test(NAME hum_arrangement_tests COMMAND hum_arrangement_tests)
    set_tests_properties(hum_arrangement_tests PROPERTIES LABELS "core;playlist;project;midi")

    add_executable(hum_project_file_tests tests/project_file_tests.cpp)
    target_link_libraries(hum_project_file_tests PRIVATE HumToSing::core)
    add_test(NAME hum_project_file_tests COMMAND hum_project_file_tests)
    set_tests_properties(hum_project_file_tests PROPERTIES LABELS "core;project")

    add_executable(hum_simple_synth_tests tests/simple_synth_tests.cpp)
    target_link_libraries(hum_simple_synth_tests PRIVATE HumToSing::core)
    add_test(NAME hum_simple_synth_tests COMMAND hum_simple_synth_tests)
    set_tests_properties(hum_simple_synth_tests PROPERTIES LABELS "core;playback")

    add_executable(hum_mvp_workflow_tests tests/mvp_workflow_tests.cpp)
    add_executable(hum_melody_stability_tests tests/melody_stability_tests.cpp)
    target_link_libraries(hum_melody_stability_tests PRIVATE HumToSing::core)
    add_test(NAME hum_melody_stability_tests COMMAND hum_melody_stability_tests)
    set_tests_properties(hum_melody_stability_tests PROPERTIES LABELS "core;pitch;note-extraction")
    target_include_directories(hum_mvp_workflow_tests PRIVATE tests)
    target_link_libraries(hum_mvp_workflow_tests PRIVATE HumToSing::core)
    add_test(NAME hum_mvp_workflow_tests COMMAND hum_mvp_workflow_tests)
    set_tests_properties(hum_mvp_workflow_tests PROPERTIES
        LABELS "core;integration;midi;playback;project;recording")
endif()

if(HUM_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Multimedia Widgets)
    if(BUILD_TESTING)
        find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    endif()

    qt_add_executable(hum_to_sing
        src/app/RecordingGuidePlayer.cpp
        src/app/vocal/IsolatedMicRecorder.cpp
        src/app/vocal/SingToMidiPanel.cpp
        src/app/vocal/VocalPreviewWidget.cpp
        src/app/vocal/PianoRollInsertAdapter.cpp
        src/app/EditorToolbar.cpp
        src/app/EditorToolbar.hpp
        src/app/ToolIcons.cpp
        src/app/ToolIcons.hpp
        src/app/AudioOutputFormat.hpp
        src/app/LevelMeter.cpp
        src/app/LevelMeter.hpp
        src/app/PianoRollWidget.cpp
        src/app/PianoRollWidget.hpp
        src/app/PlaylistWidget.cpp
        src/app/PlaylistWidget.hpp
        src/app/main.cpp
        src/app/MainWindow.cpp
        src/app/MainWindow.hpp
        src/app/PhraseConversionProcessor.cpp
        src/app/PhraseConversionProcessor.hpp
        src/app/ProjectFileStore.cpp
        src/app/ProjectFileStore.hpp
        src/app/QtAudioCapture.cpp
        src/app/QtAudioCapture.hpp
        src/app/QtAudioPlayback.cpp
        src/app/QtAudioPlayback.hpp
        src/app/QtMidiPlayback.cpp
        src/app/QtMidiPlayback.hpp
    )
    target_include_directories(hum_to_sing PRIVATE src/app)
    hum_bundle_vocal(hum_to_sing)
    target_link_libraries(hum_to_sing
        PRIVATE
            HumToSing::core
            hum_vocal
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
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
    if(APPLE)
        add_library(hum_mac_instruments STATIC src/audio/MacInstrumentRenderer.cpp)
        target_include_directories(hum_mac_instruments PUBLIC src/audio)
        target_link_libraries(hum_mac_instruments PUBLIC HumToSing::core "-framework AudioToolbox")
        target_link_libraries(hum_to_sing PRIVATE hum_mac_instruments)
        if(BUILD_TESTING)
            add_executable(hum_mac_instrument_tests tests/mac_instrument_tests.cpp)
            target_link_libraries(hum_mac_instrument_tests PRIVATE hum_mac_instruments)
            add_test(NAME hum_mac_instrument_tests COMMAND hum_mac_instrument_tests)
            set_tests_properties(hum_mac_instrument_tests PROPERTIES LABELS "audio;playback" TIMEOUT 60)
        endif()
        target_sources(hum_to_sing PRIVATE src/app/MacAudioInput.cpp src/app/MacAudioInput.hpp)
        target_link_libraries(hum_to_sing PRIVATE "-framework CoreAudio" "-framework AudioToolbox")
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            set(HUM_MACOS_MINIMUM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        else()
            set(HUM_MACOS_MINIMUM_VERSION "13.0")
        endif()
        set_target_properties(hum_to_sing PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
        )
    endif()

    install(TARGETS hum_to_sing
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    if(UNIX AND NOT APPLE)
        install(FILES cmake/HumToSing.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        )
    endif()
    install(FILES
        README.md
        docs/quick-start.md
        docs/project-files.md
        docs/mvp-beta-checklist.md
        docs/mvp-release-results.md
        docs/dependencies-and-licenses.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/HumToSing
    )

    if(BUILD_TESTING)
        get_target_property(HUM_APP_TEST_SOURCES hum_to_sing SOURCES)
        list(FILTER HUM_APP_TEST_SOURCES INCLUDE REGEX "\\.cpp$")
        list(FILTER HUM_APP_TEST_SOURCES EXCLUDE REGEX "(/main\\.cpp$|qrc_)")
        add_executable(hum_overdub_ui_tests tests/overdub_ui_tests.cpp ${HUM_APP_TEST_SOURCES})
        target_include_directories(hum_overdub_ui_tests PRIVATE src/app tests)
        target_link_libraries(hum_overdub_ui_tests PRIVATE HumToSing::core hum_vocal Qt6::Widgets Qt6::Test Qt6::Multimedia Qt6::Concurrent)
        if(APPLE)
            target_link_libraries(hum_overdub_ui_tests PRIVATE hum_mac_instruments "-framework CoreAudio" "-framework AudioToolbox")
        endif()
        hum_bundle_vocal(hum_overdub_ui_tests)
        add_test(NAME hum_overdub_ui_tests COMMAND hum_overdub_ui_tests)
        set_tests_properties(hum_overdub_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "gui;recording;playback" TIMEOUT 60)

        add_executable(hum_sing_to_midi_ui_tests tests/sing_to_midi_ui_tests.cpp
            src/app/RecordingGuidePlayer.cpp
            src/app/vocal/SingToMidiPanel.cpp src/app/vocal/IsolatedMicRecorder.cpp
            src/app/vocal/VocalPreviewWidget.cpp src/app/vocal/PianoRollInsertAdapter.cpp
            src/app/QtAudioCapture.cpp src/app/QtMidiPlayback.cpp)
        target_include_directories(hum_sing_to_midi_ui_tests PRIVATE src/app tests)
        target_link_libraries(hum_sing_to_midi_ui_tests PRIVATE HumToSing::core hum_vocal
            Qt6::Widgets Qt6::Test Qt6::Concurrent Qt6::Multimedia)
        if(APPLE)
            target_sources(hum_sing_to_midi_ui_tests PRIVATE src/app/MacAudioInput.cpp)
            target_link_libraries(hum_sing_to_midi_ui_tests PRIVATE hum_mac_instruments "-framework CoreAudio" "-framework AudioToolbox")
        endif()
        hum_bundle_vocal(hum_sing_to_midi_ui_tests)
        if(HUM_VOCAL_BACKEND_AVAILABLE)
            add_test(NAME hum_sing_to_midi_ui_tests COMMAND hum_sing_to_midi_ui_tests)
            set_tests_properties(hum_sing_to_midi_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "gui;vocal;neural" TIMEOUT 60)
            add_test(NAME hum_bundled_vocal_backend COMMAND hum_to_sing --vocal-backend-check)
            set_tests_properties(hum_bundled_vocal_backend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "gui;vocal;neural" TIMEOUT 30)
        endif()

        add_test(NAME hum_app_smoke COMMAND hum_to_sing --smoke-test)
        set_tests_properties(hum_app_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "gui;smoke"
            TIMEOUT 10
        )
        if(APPLE)
            set_tests_properties(hum_app_smoke PROPERTIES TIMEOUT 30)
        endif()

        add_executable(hum_piano_roll_ui_tests
            tests/piano_roll_ui_tests.cpp
            src/app/PianoRollWidget.cpp
            src/app/PianoRollWidget.hpp
        )
        target_include_directories(hum_piano_roll_ui_tests PRIVATE src/app)
        add_executable(hum_loop_selection_ui_tests tests/loop_selection_ui_tests.cpp src/app/PianoRollWidget.cpp src/app/PlaylistWidget.cpp)
        target_include_directories(hum_loop_selection_ui_tests PRIVATE src/app)
        target_link_libraries(hum_loop_selection_ui_tests PRIVATE HumToSing::core Qt6::Test Qt6::Widgets)
        add_test(NAME hum_loop_selection_ui_tests COMMAND hum_loop_selection_ui_tests)
        set_tests_properties(hum_loop_selection_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "gui;playlist;loop")
        add_executable(hum_playback_loop_tests tests/playback_loop_tests.cpp)
        target_include_directories(hum_playback_loop_tests PRIVATE src/app)
        target_link_libraries(hum_playback_loop_tests PRIVATE HumToSing::core Qt6::Core)
        add_test(NAME hum_playback_loop_tests COMMAND hum_playback_loop_tests)
        set_tests_properties(hum_playback_loop_tests PROPERTIES LABELS "playback;loop")
        target_link_libraries(hum_piano_roll_ui_tests
            PRIVATE
                HumToSing::core
                Qt6::Test
                Qt6::Widgets
        )
        add_test(NAME hum_piano_roll_ui_tests COMMAND hum_piano_roll_ui_tests)
        set_tests_properties(hum_piano_roll_ui_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "gui;note-editor"
            TIMEOUT 15
        )

        add_executable(hum_playlist_ui_tests
            tests/playlist_ui_tests.cpp
            src/app/PlaylistWidget.cpp
        )
        target_include_directories(hum_playlist_ui_tests PRIVATE src/app)
        target_link_libraries(hum_playlist_ui_tests
            PRIVATE HumToSing::core Qt6::Test Qt6::Widgets)
        add_test(NAME hum_playlist_ui_tests COMMAND hum_playlist_ui_tests)
        set_tests_properties(hum_playlist_ui_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "gui;playlist"
            TIMEOUT 15)

        add_executable(hum_audio_output_format_tests tests/audio_output_format_tests.cpp)
        target_include_directories(hum_audio_output_format_tests PRIVATE src/app)
        target_link_libraries(hum_audio_output_format_tests PRIVATE Qt6::Multimedia)
        add_test(NAME hum_audio_output_format_tests COMMAND hum_audio_output_format_tests)
        set_tests_properties(hum_audio_output_format_tests PROPERTIES LABELS "gui;playback")

        add_executable(hum_editor_toolbar_ui_tests tests/editor_toolbar_ui_tests.cpp
            src/app/EditorToolbar.cpp src/app/ToolIcons.cpp src/app/PianoRollWidget.cpp)
        target_include_directories(hum_editor_toolbar_ui_tests PRIVATE src/app)
        target_link_libraries(hum_editor_toolbar_ui_tests PRIVATE HumToSing::core Qt6::Test Qt6::Widgets)
        add_test(NAME hum_editor_toolbar_ui_tests COMMAND hum_editor_toolbar_ui_tests)
        set_tests_properties(hum_editor_toolbar_ui_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "gui;note-editor" TIMEOUT 15)

        add_executable(hum_project_store_tests
            tests/project_store_tests.cpp
            src/app/ProjectFileStore.cpp
            src/app/ProjectFileStore.hpp
        )
        target_include_directories(hum_project_store_tests PRIVATE src/app)
        target_link_libraries(hum_project_store_tests PRIVATE HumToSing::core Qt6::Core)
        add_test(NAME hum_project_store_tests COMMAND hum_project_store_tests)
        set_tests_properties(hum_project_store_tests PROPERTIES LABELS "gui;project")
    endif()
endif()

set(CPACK_PACKAGE_NAME "HumToSing")
set(CPACK_PACKAGE_VENDOR "Hum-to-MIDI Music Sketchpad")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Hum-to-MIDI Music Sketchpad maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Offline single-track hum-to-MIDI melody sketchpad")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "HumToSing")
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FixPackagePermissions.cmake")
set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FixPackageArtifactPermissions.cmake")
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libqt6core6t64 (>= 6.8), libqt6gui6 (>= 6.8), libqt6widgets6 (>= 6.8), libqt6multimedia6 (>= 6.8), qt6-qpa-plugins")
endif()
include(CPack)
