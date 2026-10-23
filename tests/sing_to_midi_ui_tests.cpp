#include "vocal/SingToMidiPanel.hpp"
#include "hum/core/project_file.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <iostream>
#include <numbers>
#undef emit
#include "support/FakeAudioCapture.hpp"
#include "support/FakeRecordingGuide.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("HumToSingTests"));
    QCoreApplication::setApplicationName(QStringLiteral("IsolatedVocal"));
    QTemporaryDir preferences;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    using namespace hum::app::vocal;
    int failures = 0;
    const auto expect = [&](bool value, const char* message) { if (!value) { ++failures; std::cerr << message << '\n'; } };
    hum::test::FakeAudioCapture microphone; microphone.device_format = {16'000, 1};
    hum::test::FakeRecordingGuide guide;
    SingToMidiPanel panel(nullptr, &microphone, &guide);
    HostContext host; host.origin = 3.37; host.bpm = 123; host.ppq = 960; host.track = 1;
    host.backing_notes = {{48,80,0,10,0},{60,80,4,2,0,true}};
    host.backing_voices = {hum::core::InstrumentVoice::strings,hum::core::InstrumentVoice::grand_piano};
    host.instrument = hum::core::InstrumentVoice::acoustic_guitar;
    panel.setHost([&] { return host; }, {}); panel.setOrigin(host.origin); panel.show();
    const auto button = [&](const char* name) { return panel.findChild<QPushButton*>(QString::fromLatin1(name)); };
    auto* record = button("vocalRecordButton"); auto* stop = button("vocalStopButton");
    auto* convert = button("vocalConvertButton"); auto* cancel = button("vocalCancelButton");
    auto* insert = button("vocalInsertButton"); auto* discard = button("vocalDiscardButton");
    expect(record && stop && convert && cancel && insert && discard, "all isolated panel actions exist");
    auto* preset = panel.findChild<QComboBox*>(QStringLiteral("vocalPreset")); preset->setCurrentIndex(0);
    auto* snap = panel.findChild<QCheckBox*>(QStringLiteral("vocalScaleSnap")); snap->setChecked(false);
    auto* quantize = panel.findChild<QComboBox*>(QStringLiteral("vocalTimeQuantize")); quantize->setCurrentIndex(0);
    expect(!convert->isEnabled() && !insert->isEnabled(), "no conversion or insertion without a take");
    QTest::mouseClick(record, Qt::LeftButton);
    expect(guide.active() && guide.plan.notes.size() == 1 && !convert->isEnabled(), "count-in snapshots audible backing without a take");
    microphone.emit(std::vector<float>(32'000,.9F));
    guide.advance(guide.plan.countInSeconds() - .01);
    expect(!convert->isEnabled(), "fourth beat has not started capture");
    QTest::mouseClick(stop, Qt::LeftButton);
    expect(!microphone.isCapturing() && !convert->isEnabled(), "stop during count-in stores nothing");
    QTest::mouseClick(record, Qt::LeftButton);
    microphone.emit(std::vector<float>(32'000,.9F));
    guide.advance(guide.plan.countInSeconds());
    expect(microphone.start_count == 2, "each take opens hardware once, never again on downbeat");
    std::vector<float> audio(32'000);
    for (std::size_t i = 3'200; i < 28'800; ++i) {
        const auto phase = 2 * std::numbers::pi * 277.182631 * static_cast<double>(i) / 16'000;
        audio[i] = static_cast<float>(.2 * std::sin(phase) + .08 * std::sin(2 * phase));
    }
    microphone.emit(audio); QTest::mouseClick(stop, Qt::LeftButton);
    expect(convert->isEnabled(), "stopped independent recording can convert");
    QTest::mouseClick(convert, Qt::LeftButton);
    QElapsedTimer wait; wait.start();
    while (cancel->isEnabled() && wait.elapsed() < 20'000) { QTest::qWait(20); }
    if (panel.previewNotes().empty()) { std::cerr << panel.findChild<QLabel*>(QStringLiteral("vocalStatus"))->text().toStdString() << '\n'; }
    expect(panel.previewNotes().size() == 1, "actual neural conversion creates a temporary preview");
    if (!panel.previewNotes().empty()) {
        expect(panel.previewNotes()[0].pitch == 61, "chromatic C-sharp is preserved with Scale Snap off");
        snap->setChecked(true); expect(panel.previewNotes()[0].pitch == 60, "preview scale correction works");
        snap->setChecked(false); expect(panel.previewNotes()[0].pitch == 61, "turning correction off restores detected pitch");
        quantize->setCurrentIndex(2);
        const auto grid = 30 / host.bpm;
        const auto beats = (panel.previewNotes()[0].start + host.origin) / grid;
        expect(std::abs(beats - std::round(beats)) < 1e-8, "preview quantization respects nonzero project origin");
        quantize->setCurrentIndex(0);
    }
    expect(!insert->isEnabled(), "no project mutation until an insertion adapter is connected");
    hum::core::NoteEditor editor;
    expect(editor.replaceAll({{60, 80, .1, .5, 0}}, {{{0}, 0, "Existing"}}, 3,
        {{}, {hum::core::TrackKind::chords, hum::core::InstrumentVoice::strings, true},
         {hum::core::TrackKind::melody, hum::core::InstrumentVoice::acoustic_guitar, false}}), "host fixture loads");
    const auto original_tracks = editor.tracks();
    panel.setHost([&] { return host; }, [&](const auto& notes, double origin, std::uint64_t generation) {
        if (generation != host.project_generation) { return QStringLiteral("Project changed"); }
        return PianoRollInsertAdapter::insert(editor, notes, origin, host.track) == hum::core::NoteEditResult::applied
            ? QString{} : QStringLiteral("Insert failed");
    });
    host.track = 2; // destination is read at insertion, not captured at conversion
    expect(editor.notes().size() == 1 && !editor.canUndo(), "preview and correction never mutate host history");
    const auto args = app.arguments();
    if (args.contains(QStringLiteral("--native-preview-check"))) {
        const auto shot = args.indexOf(QStringLiteral("--native-preview-check")) + 1;
        if (shot < args.size()) { expect(panel.grab().save(args[shot]), "native preview screenshot saved"); }
        auto* play = button("vocalPlayPreviewButton"); QTest::mouseClick(play, Qt::LeftButton);
        QTest::qWait(700);
        expect(play->text() == QStringLiteral("Stop Preview"), "native preview audition starts on local output");
        QTest::mouseClick(play, Qt::LeftButton);
        expect(play->text() == QStringLiteral("Play Preview"), "native preview audition stops");
    }
    const auto preview = panel.previewNotes();
    QTest::mouseClick(insert, Qt::LeftButton);
    expect(editor.notes().size() == 2 && editor.regions().back().track == 2, "insert appends to the currently selected track");
    expect(editor.notes()[0].pitch == 60 && editor.notes()[0].start_seconds == .1, "existing notes are untouched");
    expect(editor.tracks() == original_tracks, "track type, instruments and mute remain unchanged");
    expect(!insert->isEnabled(), "double insertion of the same preview is guarded");
    if (editor.notes().size() > 1 && !preview.empty()) {
        expect(std::abs(editor.notes()[1].start_seconds - host.origin - preview[0].start) < 1e-8, "insert retains captured time origin");
        const auto sequence = editor.exportSequence(host.bpm, host.ppq);
        expect(sequence && sequence->ticks_per_quarter == 960 && sequence->tempo_bpm == 123, "host export uses actual PPQ and tempo");
        if (sequence) {
            const auto expected = hum::core::MidiTimeline::secondsToTicks(host.origin + preview[0].start, host.bpm, host.ppq);
            expect(sequence->notes.back().start_tick == *expected, "inserted timing exports to correct project ticks");
        }
    }
    expect(editor.undo() && editor.notes().size() == 1 && editor.tracks() == original_tracks, "one undo removes only the inserted phrase");
    expect(editor.redo() && editor.notes().size() == 2, "one redo restores the full insertion");
    expect(PianoRollInsertAdapter::insert(editor, {{128, 0, .5, 96}}, 0, 2) != hum::core::NoteEditResult::applied
        && editor.notes().size() == 2, "invalid preview cannot mutate host state");
    QTest::mouseClick(convert, Qt::LeftButton); QTest::mouseClick(cancel, Qt::LeftButton);
    QTest::qWait(100); expect(panel.previewNotes().empty(), "cancelled worker cannot publish stale preview");
    expect(convert->isEnabled(), "cancel keeps captured audio for retry");
    QTest::mouseClick(discard, Qt::LeftButton);
    expect(panel.previewNotes().empty() && !convert->isEnabled(), "discard releases take and preview");
    QTest::mouseClick(record, Qt::LeftButton); guide.advance(guide.plan.countInSeconds()); microphone.fail(hum::core::ServiceStatus::device_disconnected);
    QTest::qWait(100); expect(!convert->isEnabled(), "disconnected capture cannot convert partial audio");
    QTest::mouseClick(record, Qt::LeftButton); guide.fail();
    expect(!guide.active() && !microphone.isCapturing(), "failed count-in output releases microphone and allows retry");
    QTest::mouseClick(record, Qt::LeftButton);
    panel.reject(); guide.advance(99); expect(!microphone.isCapturing(), "closing panel releases microphone");
    std::cout << "Sing to MIDI UI failures: " << failures << '\n'; return failures ? 1 : 0;
}
