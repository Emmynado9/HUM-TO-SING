#include "AiTrialPanel.hpp"
#include "hum/core/generation_trial.hpp"
#include "vocal/PianoRollInsertAdapter.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <iostream>
#undef emit
#include "support/FakeAudioCapture.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv); std::cout << std::unitbuf;
    QTimer::singleShot(45'000, &app, [] { std::cerr << "AI Trial UI timed out\n"; std::exit(20); });
    int failures = 0;
    auto check = [&](bool value, const char* message) { std::cout << (value ? "PASS " : "FAIL ") << message << '\n'; if (!value) ++failures; };
    QTemporaryDir temporary;
    hum::core::ProjectDocument project;
    project.tempo_bpm = 95; project.lyrics = "These lyrics must not be exported";
    project.canonical_notes = {{60, 90, 0, 1, 0}}; project.playlist_regions = {{{0}, 0, "Existing"}};
    hum::core::VoiceClip voice; voice.start_seconds = 4.25; voice.sample_rate = 16000; voice.samples.resize(32000);
    for (std::size_t i = 0; i < voice.samples.size(); ++i) voice.samples[i] = static_cast<std::int16_t>(9000 * std::sin(6.283185307179586 * 220 * double(i) / 16000));
    project.voice_clips.push_back(voice);
    hum::core::NoteEditor editor; static_cast<void>(editor.replaceAll(project.canonical_notes, project.playlist_regions, 1));
    hum::test::FakeAudioCapture capture; capture.device_format = {16000, 1};
    hum::app::AiTrialPanel panel(nullptr, &capture);
    int inserted = 0, audio_added = 0; bool reject_host = false;
    panel.setHost([&](const auto& notes, double at, std::size_t track, std::uint64_t generation) -> QString {
        if (reject_host) return "The project changed.";
        check(generation == 7 && track == 0, "MIDI insert uses frozen project and destination track");
        const auto result = hum::app::vocal::PianoRollInsertAdapter::insert(editor, notes, at, track);
        if (result != hum::core::NoteEditResult::applied) return "Insertion failed";
        ++inserted; return {};
    }, [&](hum::core::VoiceClip clip, std::uint64_t generation) -> QString {
        if (reject_host) return "The project changed.";
        check(generation == 7 && clip.start_seconds == 4.25 && clip.samples.size() == 32000,
              "generated audio keeps its samples and chosen placement");
        project.voice_clips.push_back(std::move(clip)); ++audio_added; return {};
    });
    const auto button = [&](const char* name) { return panel.findChild<QPushButton*>(name); };
    const auto wait = [&] { QElapsedTimer clock; clock.start(); while (panel.busy() && clock.elapsed() < 15'000) QTest::qWait(10); check(!panel.busy(), "background import finishes"); };
    panel.setProject({}, 0, 2, 7); panel.show();
    check(!button("aiTrialExport")->isEnabled() && button("aiTrialImport")->isEnabled(), "empty projects guide users to record while allowing existing generated WAV import");
    button("aiTrialRecordVoice")->click();
    check(capture.isCapturing() && !button("aiTrialExport")->isEnabled() && !button("aiTrialImport")->isEnabled(), "AI Trial owns a recorder and locks export/import during capture");
    capture.emit(std::vector<float>(1600, .2F)); button("aiTrialStopRecording")->click();
    check(panel.findChild<QComboBox*>("aiTrialClip")->count() == 0, "short take does not create a source clip");
    button("aiTrialRecordVoice")->click();
    std::vector<float> take(160000); for (std::size_t i = 0; i < take.size(); ++i) take[i] = .25F * std::sin(6.283185307179586 * 220 * double(i) / 16000);
    capture.emit(take); QTest::qWait(100);
    check(!capture.isCapturing() && panel.findChild<QComboBox*>("aiTrialClip")->count() == 1
        && button("aiTrialPlayVoice")->isEnabled(), "ten-second take automatically stops and becomes the selected playable source");
    check(panel.prepareInDirectory(temporary.path()).isEmpty() && editor.notes().size() == 1 && audio_added == 0,
        "new AI Trial recording exports without touching the project or Hum-to-MIDI notes");
    button("aiTrialRecordVoice")->click(); capture.fail(hum::core::ServiceStatus::device_disconnected); QTest::qWait(100);
    check(!capture.isCapturing() && panel.findChild<QComboBox*>("aiTrialClip")->count() == 1, "microphone disconnection preserves the previous take");
    button("aiTrialRecordVoice")->click(); capture.emit(take); panel.reject(); QTest::qWait(100);
    check(!capture.isCapturing() && panel.findChild<QComboBox*>("aiTrialClip")->count() == 1, "closing stops capture without accepting a partial take");
    capture.permission = hum::core::PermissionStatus::denied; panel.show(); button("aiTrialRecordVoice")->click();
    check(!capture.isCapturing() && button("aiTrialRecordVoice")->isEnabled(), "permission denial leaves recording retryable");
    capture.permission = hum::core::PermissionStatus::granted;
    panel.setProject(project, 0, 2, 7);
    panel.findChild<QComboBox*>("aiTrialInstrument")->setCurrentIndex(2);
    panel.findChild<QComboBox*>("aiTrialStyle")->setCurrentIndex(1);
    check(panel.findChild<QPlainTextEdit*>("aiTrialPrompt")->toPlainText().contains("Solo flute")
        && panel.findChild<QPlainTextEdit*>("aiTrialPrompt")->toPlainText().contains("gentle")
        && panel.findChild<QPlainTextEdit*>("aiTrialPrompt")->toPlainText().contains("95 BPM"), "instrument, style and project tempo build the automatic prompt");
    check(panel.prepareInDirectory(temporary.path()).isEmpty(), "trial files export locally from in-memory voice clip");
    const auto exported = panel.exportedDirectory();
    QFile request_file(exported + "/request.json"); request_file.open(QIODevice::ReadOnly); const auto bytes = request_file.readAll();
    const auto request = QJsonDocument::fromJson(bytes).object();
    check(request["duration_seconds"].toDouble() == 2 && request["instrument"].toString() == "flute"
        && !bytes.contains("lyrics"), "export contains only the selected excerpt request, not project lyrics");
    check(panel.prepareInDirectory(temporary.path()).isEmpty() && exported != panel.exportedDirectory(), "another export creates a fresh folder without overwriting a previous trial");
    panel.findChild<QDoubleSpinBox*>("aiTrialOffset")->setValue(3);
    check(!panel.prepareInDirectory(temporary.path()).isEmpty(), "invalid excerpt offsets fail safely");
    panel.findChild<QDoubleSpinBox*>("aiTrialOffset")->setValue(0);
    panel.importGenerated(exported + "/guide.wav");
    check(panel.busy() && !button("aiTrialAddMidi")->isEnabled() && !button("aiTrialExport")->isEnabled(), "import and conversion lock dependent controls without changing project notes");
    wait();
    check(!panel.notes().empty() && editor.notes().size() == 1 && audio_added == 0, "synthetic generated audio produces a temporary note preview only");
    check(button("aiTrialPlayAudio")->isEnabled() && button("aiTrialPlayMidi")->isEnabled(), "generated audio and MIDI are separately auditionable");
    if (app.arguments().contains("--native-screenshot")) {
        QTest::qWait(100);
        check(panel.grab().save("build/ai-trial-screen.png"), "native AI Trial layout captured");
        button("aiTrialPlayAudio")->click(); QTest::qWait(300); button("aiTrialStop")->click();
        button("aiTrialPlayMidi")->click(); QTest::qWait(300); button("aiTrialStop")->click();
    }
    panel.findChild<QDoubleSpinBox*>("aiTrialOrigin")->setValue(599);
    button("aiTrialAddMidi")->click();
    check(inserted == 0 && panel.findChild<QLabel*>("aiTrialStatus")->text().contains("600"), "out-of-range MIDI placement is rejected before project mutation");
    panel.findChild<QDoubleSpinBox*>("aiTrialOrigin")->setValue(4.25);
    reject_host = true; button("aiTrialAddMidi")->click(); button("aiTrialAddAudio")->click();
    check(inserted == 0 && audio_added == 0 && button("aiTrialAddMidi")->isEnabled(), "host rejection keeps the preview retryable without inserting stale results");
    reject_host = false; button("aiTrialAddMidi")->click(); button("aiTrialAddMidi")->click();
    check(inserted == 1 && editor.notes().size() > 1 && !button("aiTrialAddMidi")->isEnabled(), "MIDI inserts once as an undoable canonical region");
    check(editor.notes().back().start_seconds >= 4.25 && editor.notes().front().start_seconds == 0, "generated MIDI uses insertion origin and preserves existing notes");
    static_cast<void>(editor.undo()); check(editor.notes().size() == 1, "one Undo removes only the generated MIDI region");
    button("aiTrialAddAudio")->click(); button("aiTrialAddAudio")->click();
    check(audio_added == 1 && !button("aiTrialAddAudio")->isEnabled(), "generated audio can be added once independently of MIDI");
    panel.importGenerated(exported + "/guide.wav"); button("aiTrialCancel")->click(); QTest::qWait(400);
    check(!panel.busy() && panel.notes().empty() && !button("aiTrialAddMidi")->isEnabled(), "cancelling discards late conversion completion");
    panel.importGenerated(exported + "/guide.wav"); panel.reject(); QTest::qWait(400);
    check(!panel.busy() && panel.notes().empty(), "closing cancels conversion and cannot insert late results");
    QFile invalid(temporary.filePath("invalid.wav")); invalid.open(QIODevice::WriteOnly); invalid.write("not a WAV"); invalid.close();
    panel.show(); panel.importGenerated(invalid.fileName()); wait();
    check(panel.notes().empty() && !button("aiTrialAddAudio")->isEnabled(), "malformed WAV leaves no addable result");
    QFile long_wave(temporary.filePath("long.wav"));
    const auto wav = hum::core::trial::pcmWave(std::vector<std::int16_t>(160000, 8000), 16000);
    QByteArray oversized(reinterpret_cast<const char*>(wav.data()), static_cast<qsizetype>(wav.size()));
    // A valid 10-second file with its header sample rate changed to 8 kHz becomes 20 seconds.
    oversized[24] = char(0x40); oversized[25] = char(0x1f); oversized[28] = char(0x80); oversized[29] = char(0x3e);
    long_wave.open(QIODevice::WriteOnly); long_wave.write(oversized); long_wave.close();
    panel.importGenerated(long_wave.fileName()); wait();
    check(!button("aiTrialAddAudio")->isEnabled() && panel.findChild<QLabel*>("aiTrialStatus")->text().contains("10 seconds"), "overlong generated audio is rejected");
    std::cout << "AI Trial failures: " << failures << '\n'; return failures ? 1 : 0;
}
