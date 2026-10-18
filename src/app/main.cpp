#include "MainWindow.hpp"
#include "QtAudioCapture.hpp"
#include "QtAudioPlayback.hpp"
#include "QtMidiPlayback.hpp"
#include "AudioOutputFormat.hpp"
#include "PianoRollWidget.hpp"
#include "PlaylistWidget.hpp"
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QToolButton>
#include <cmath>

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QDialog>
#include <QLabel>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QPermission>
#include <QDebug>
#include <utility>
#include <atomic>

namespace {
// Explicit hardware diagnostic only. Generates quiet tones; never opens a microphone.
int runPlaybackCheck(QApplication& application) {
    hum::app::QtMidiPlayback midi;
    hum::app::QtAudioPlayback original;
    int failures = 0;
    double paused_position = 0;
    auto check = [&](bool passed, const char* description) {
        qInfo() << (passed ? "PASS" : "FAIL") << description;
        if (!passed) { ++failures; }
    };
    auto status = [&](const QString& message, bool error) {
        qInfo() << "Output:" << message;
        if (error) { ++failures; }
    };
    QTimer::singleShot(100, &application, [&] {
        const auto device = QMediaDevices::defaultAudioOutput();
        const auto format = hum::app::chooseOutputFormat(device);
        qInfo() << "Device:" << device.description() << "preferred:" << device.preferredFormat();
        if (format) { qInfo() << "Negotiated:" << *format; }
        check(format.has_value(), "supported output format");
        midi.play({{69,45,0,3,0}}, status);
    });
    QTimer::singleShot(700, &application, [&] {
        check(midi.isPlaying() && midi.positionSeconds() > .1, "MIDI audio clock advances");
        midi.pause(); paused_position = midi.positionSeconds();
    });
    QTimer::singleShot(1100, &application, [&] {
        check(midi.isPaused() && std::abs(midi.positionSeconds() - paused_position) < .01,
            "pause holds the audio position");
        midi.seek(1.0);
        check(midi.isPaused() && std::abs(midi.positionSeconds() - 1.0) < .01,
            "seek preserves pause and changes position");
        midi.resume();
    });
    QTimer::singleShot(1800, &application, [&] {
        check(midi.isPlaying() && midi.positionSeconds() > 1.2, "resume advances from seek position");
        midi.stop();
        check(!midi.isPlaying() && !midi.isPaused(), "Stop releases playback");
        std::vector<float> samples(48000);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<float>(.025 * std::sin(6.28318530718 * 440 * static_cast<double>(i) / 48000));
        }
        original.play(std::make_shared<hum::core::AudioRecording>(
            hum::core::AudioFormat{48000,1}, std::move(samples)), status);
    });
    QTimer::singleShot(2400, &application, [&] {
        check(original.isPlaying(), "original-audio playback starts after resampling");
    });
    QTimer::singleShot(3400, &application, [&] {
        check(!original.isPlaying() && !original.isPreparing(), "original audio completes");
        original.stop();
        midi.play({}, status, 0.0, 120.0, true);
    });
    QTimer::singleShot(6100, &application, [&] {
        check(midi.isPlaying() && midi.positionSeconds() > 2.0, "metronome continues beyond one bar");
        midi.stop();
        check(!midi.isPlaying() && !midi.isPreparing(), "metronome stops cleanly");
        application.exit(failures == 0 ? 0 : 22);
    });
    return application.exec();
}
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("HumToSing"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("Hum-to-MIDI Music Sketchpad"));

    if (QCoreApplication::arguments().contains(QStringLiteral("--audio-playback-check"))) {
        return runPlaybackCheck(application);
    }
    const auto recording_check = QCoreApplication::arguments().contains(QStringLiteral("--audio-recording-check"));
    if (recording_check || QCoreApplication::arguments().contains(QStringLiteral("--audio-diagnostics"))) {
        hum::app::QtAudioCapture capture;
        std::atomic<std::size_t> samples{0};
        std::atomic<bool> failed{false};
        QTimer::singleShot(1000, &application, [&] {
            qInfo() << "Microphone permission:" << application.checkPermission(QMicrophonePermission{});
            const auto inputs = capture.devices();
            const auto outputs = QMediaDevices::audioOutputs();
            qInfo() << "Audio inputs:" << inputs.size() << "Audio outputs:" << outputs.size();
            for (const auto& output : outputs) {
                qInfo() << "Output:" << output.description() << "Preferred:" << output.preferredFormat();
                if (const auto format = hum::app::chooseOutputFormat(output)) { qInfo() << "Supported PCM:" << *format; }
            }
            for (const auto& device : inputs) {
                const auto format = capture.preferredFormat(device.id);
                qInfo() << "Input:" << QString::fromStdString(device.name) << "Default:" << device.is_default
                        << "Rate:" << (format ? format->sample_rate_hz : 0)
                        << "Channels:" << (format ? format->channel_count : 0);
            }
            if (!recording_check || inputs.empty()) { application.exit(inputs.empty() ? 10 : 0); return; }
            const auto format = capture.preferredFormat(inputs.front().id);
            if (!format || capture.start(inputs.front().id, *format,
                    [&](hum::core::AudioBufferView block) { samples.fetch_add(block.interleaved_samples.size()); },
                    [&](hum::core::ServiceStatus) { failed.store(true); }) != hum::core::ServiceStatus::ok) {
                application.exit(11); return;
            }
            QTimer::singleShot(1500, &application, [&] {
                capture.stop();
                qInfo() << "Captured samples in memory:" << samples.load() << "Audio written to disk: no";
                application.exit(samples.load() > 0 && !failed.load() ? 0 : 12);
            });
        });
        return application.exec();
    }

    hum::app::MainWindow window;
    window.show();

    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(250, &application, [&application, &window] {
            const auto complete_shell = window.isVisible()
                && window.findChild<QPushButton*>(QStringLiteral("openProjectButton"))
                && window.findChild<QPushButton*>(QStringLiteral("saveProjectButton"))
                && window.findChild<QPushButton*>(QStringLiteral("midiPlayButton"))
                && window.findChild<QWidget*>(QStringLiteral("playlist"))
                && window.findChild<QDoubleSpinBox*>(QStringLiteral("tempoSpin"));
            if (!complete_shell) { application.exit(2); return; }
            for (const auto& pair : {
                     std::pair{"openRecorderButton", "recordingDialog"},
                     std::pair{"openPianoRollButton", "pianoRollDialog"},
                     std::pair{"openPitchLabButton", "pitchLabDialog"}}) {
                auto* button = window.findChild<QPushButton*>(QString::fromLatin1(pair.first));
                auto* dialog = window.findChild<QDialog*>(QString::fromLatin1(pair.second));
                if (!button || !dialog || dialog->isVisible()) { application.exit(3); return; }
                button->click();
                if (!dialog->isVisible() || dialog->isModal()) { application.exit(4); return; }
                dialog->reject();
                button->click();
                if (!dialog->isVisible()) { application.exit(5); return; }
                dialog->reject();
            }
            auto* add_track = window.findChild<QPushButton*>(QStringLiteral("addTrackButton"));
            auto* undo = window.findChild<QPushButton*>(QStringLiteral("playlistUndoButton"));
            auto* redo = window.findChild<QPushButton*>(QStringLiteral("playlistRedoButton"));
            if (!add_track || !undo || !redo) { application.exit(6); return; }
            QTimer::singleShot(0, &window, [&] {
                auto* dialog = window.findChild<QDialog*>(QStringLiteral("createTrackDialog"));
                auto* type = dialog ? dialog->findChild<QComboBox*>(QStringLiteral("trackTypeCombo")) : nullptr;
                if (!type) { application.exit(23); return; }
                type->setCurrentIndex(1);
                dialog->accept();
            });
            add_track->click();
            auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("humToMidiTabs"));
            auto* playlist = dynamic_cast<hum::app::PlaylistWidget*>(window.findChild<QWidget*>(QStringLiteral("playlist")));
            auto* chord_key = window.findChild<QComboBox*>(QStringLiteral("chordKeyCombo"));
            auto* chord_scale = window.findChild<QComboBox*>(QStringLiteral("chordScaleCombo"));
            if (!tabs || !playlist || !chord_key || !chord_scale || tabs->currentIndex() != 1
                || playlist->selectedTrack() != 1 || chord_key->itemText(1) != QStringLiteral("C#/Db")) {
                application.exit(24); return;
            }
            tabs->setCurrentIndex(0);
            if (playlist->selectedTrack() != 0) { application.exit(25); return; }
            tabs->setCurrentIndex(1);
            if (playlist->selectedTrack() != 1) { application.exit(26); return; }
            chord_key->setCurrentIndex(6);
            chord_scale->setCurrentIndex(1);
            for (auto* combo : window.findChildren<QComboBox*>()) {
                if ((combo->accessibleName() == QStringLiteral("Root key") && combo->currentIndex() != 6)
                    || (combo->accessibleName() == QStringLiteral("Scale type") && combo->currentIndex() != 1)) {
                    application.exit(27); return;
                }
            }
            if (!undo->isEnabled()) { application.exit(7); return; }
            undo->click();
            if (!redo->isEnabled()) { application.exit(8); return; }
            redo->click();
            auto* mute_tool = window.findChild<QToolButton*>(QStringLiteral("toolMuteButton"));
            auto* select_tool = window.findChild<QToolButton*>(QStringLiteral("toolSelectButton"));
            auto* snap_tool = window.findChild<QToolButton*>(QStringLiteral("toolSnapButton"));
            auto* snap = window.findChild<QComboBox*>(QStringLiteral("rhythmSnapCombo"));
            auto* tempo = window.findChild<QDoubleSpinBox*>(QStringLiteral("tempoSpin"));
            auto* roll = dynamic_cast<hum::app::PianoRollWidget*>(window.findChild<QWidget*>(QStringLiteral("pianoRoll")));
            auto* metronome = window.findChild<QPushButton*>(QStringLiteral("metronomeButton"));
            auto* play = window.findChild<QPushButton*>(QStringLiteral("midiPlayButton"));
            auto* stop = window.findChild<QPushButton*>(QStringLiteral("midiStopButton"));
            auto* piano_dialog = window.findChild<QDialog*>(QStringLiteral("pianoRollDialog"));
            auto* piano_play = piano_dialog->findChild<QPushButton*>(QStringLiteral("pianoPlayButton"));
            auto* piano_pause = piano_dialog->findChild<QPushButton*>(QStringLiteral("pianoPauseButton"));
            auto* piano_stop = piano_dialog->findChild<QPushButton*>(QStringLiteral("pianoStopButton"));
            if (!piano_play || !piano_pause || !piano_stop || piano_play == play
                || piano_pause->isEnabled() || piano_play->isEnabled()) { application.exit(28); return; }
            if (!mute_tool || !select_tool || !snap_tool || !snap || !tempo || !roll || !metronome || !play || !stop) { application.exit(9); return; }
            mute_tool->click();
            if (roll->tool() != hum::app::PianoRollWidget::Tool::mute) { application.exit(13); return; }
            select_tool->click();
            snap->setCurrentIndex(snap->findData(16));
            tempo->setValue(60);
            if (!roll->rhythmSnap().enabled || std::abs(roll->rhythmSnap().interval_seconds - .25) > 1e-9) {
                application.exit(14); return;
            }
            snap_tool->click();
            if (roll->rhythmSnap().enabled || play->isEnabled()) { application.exit(15); return; }
            metronome->click();
            if (!play->isEnabled() || !piano_play->isEnabled()) { application.exit(16); return; }
            metronome->click();
            auto* record_metronome = window.findChild<QPushButton*>(QStringLiteral("recordMetronomeButton"));
            auto* record_tempo = window.findChild<QDoubleSpinBox*>(QStringLiteral("recordTempoSpin"));
            if (!record_metronome || !record_tempo || !window.findChild<QPushButton*>(QStringLiteral("testSpeakersButton"))) {
                application.exit(18);return;
            }
            record_metronome->click();
            if (!metronome->isChecked() || !play->isEnabled()) {application.exit(19);return;}
            record_tempo->setValue(96);
            if (tempo->value()!=96) {application.exit(20);return;}
            metronome->click();
            if (record_metronome->isChecked()) {application.exit(21);return;}
            stop->click();
            if (!roll->playheadViewportX()) { application.exit(17); return; }
            auto* progression = window.findChild<QLineEdit*>(QStringLiteral("chordProgressionEdit"));
            auto* add_progression = window.findChild<QPushButton*>(QStringLiteral("addProgressionButton"));
            auto* instrument = window.findChild<QComboBox*>(QStringLiteral("trackInstrumentCombo"));
            if (!progression || !add_progression || !instrument) { application.exit(29); return; }
            chord_key->setCurrentIndex(0); chord_scale->setCurrentIndex(0);
            progression->setText(QStringLiteral("I IV V I"));
            if (!add_progression->isEnabled()) { application.exit(30); return; }
            add_progression->click();
            if (!play->isEnabled() || !roll->noteViewportRect(11).isValid()) { application.exit(31); return; }
            instrument->setCurrentIndex(instrument->findData(static_cast<int>(hum::core::InstrumentVoice::acoustic_guitar)));
            undo->click();
            if (instrument->currentData().toInt() != static_cast<int>(hum::core::InstrumentVoice::grand_piano)) {
                application.exit(32); return;
            }
            progression->setText(QStringLiteral("I BAD"));
            if (add_progression->isEnabled()) { application.exit(33); return; }
            auto* loop_all = window.findChild<QPushButton*>(QStringLiteral("pianoLoopAllButton"));
            auto* playlist_loop = window.findChild<QPushButton*>(QStringLiteral("playlistLoopButton"));
            auto* piano_loop = window.findChild<QPushButton*>(QStringLiteral("pianoLoopButton"));
            auto* playlist_pause = window.findChild<QPushButton*>(QStringLiteral("midiPauseButton"));
            if (!loop_all || !playlist_loop || !piano_loop || !playlist_pause
                || !window.findChild<QWidget*>(QStringLiteral("pianoToolbar"))->isAncestorOf(piano_play)) {
                application.exit(34); return;
            }
            loop_all->click();
            if (!piano_loop->isChecked() || !playlist_loop->isChecked() || !roll->loopRange()
                || roll->loopRange() != playlist->loopRange()) { application.exit(35); return; }
            playlist_loop->click();
            if (piano_loop->isChecked() || roll->loopRange()) { application.exit(36); return; }
            application.exit(0);
        });
    }

    return application.exec();
}
