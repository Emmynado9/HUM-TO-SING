#include "MainWindow.hpp"
#include "PianoRollWidget.hpp"
#include "PlaylistWidget.hpp"
#include "ProjectFileStore.hpp"
#include "RecordingGuidePlayer.hpp"
#include "QtMidiPlayback.hpp"
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMenu>
#include <QScrollBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <set>
#undef emit
#include "support/FakeAudioCapture.hpp"
#include "support/FakeRecordingGuide.hpp"

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    std::cout << std::unitbuf;
    QTimer::singleShot(45'000,&app,[] { std::cerr << "Overdub UI check timed out\n"; std::exit(20); });
    QTemporaryDir temporary;
    QCoreApplication::setOrganizationName("HumToSingTests"); QCoreApplication::setApplicationName("Overdub");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    int failures = 0;
    const auto check = [&](bool ok,const char* message) { std::cout << (ok ? "PASS " : "FAIL ") << message << '\n'; if (!ok) { ++failures; } };
    if (app.arguments().contains("--native-guide-check")) {
        hum::app::RecordingGuidePlayer guide;
        const std::vector<hum::core::TimedMidiNote> backing{{60,70,1,2,0},{64,60,1.5,1,0}};
        auto plan = *hum::core::makeOverdubPlan(backing,{},1,240,false,1.5);
        hum::core::VoiceClip voice; voice.start_seconds = 1; voice.sample_rate = 8000;
        voice.samples.resize(16000);
        for (std::size_t i = 0; i < voice.samples.size(); ++i) voice.samples[i] = static_cast<std::int16_t>(2000 * std::sin(double(i) * 6.283185307179586 * 180 / 8000));
        plan.voice_clips = {voice};
        std::set<int> beats; int starts = 0; bool finished = false; QString error;
        QElapsedTimer clock; clock.start(); qint64 started_at = 0;
        guide.start(plan, {[&](double origin) {
            ++starts; started_at = clock.elapsed(); check(origin >= 1 && origin < 1.1,"native capture callback aligns to backing origin");
        }, [&](int beat,double) { if (beat) { beats.insert(beat); } },
        [&](const QString& message) { error = message; finished = true; }});
        while (!finished && clock.elapsed() < 15'000) { QTest::qWait(5); }
        check(error.isEmpty() && finished && starts == 1, "native recording guide completes once without output error");
        check(beats == std::set<int>{1,2,3,4} && started_at >= 1'000, "native audio clock provides four full beats before capture");
        if (!error.isEmpty()) { std::cerr << error.toStdString() << '\n'; }
        guide.start(plan, {[&](double) { ++starts; },{}, {}}); guide.stop(); QTest::qWait(600);
        check(starts == 1 && !guide.active(), "cancelled native render cannot restart recording");
        hum::app::QtMidiPlayback playback;
        QString voice_error;
        playback.play({}, [&](const QString& message, bool failed) { if (failed) voice_error = message; },
            1.25, 120, false, {}, std::nullopt, {voice});
        clock.restart();
        while (playback.isPreparing() && clock.elapsed() < 5000) QTest::qWait(10);
        check(voice_error.isEmpty() && playback.isPlaying() && std::abs(playback.phraseDurationSeconds() - 3) < .001,
            "native voice-only Playlist playback starts without MIDI and retains full clip timing");
        playback.pause(); playback.seek(1.8);
        check(playback.isPaused() && std::abs(playback.positionSeconds() - 1.8) < .001, "voice-only playback can pause and seek to an editing position");
        playback.resume(); clock.restart();
        while (playback.isPlaying() && clock.elapsed() < 5000) QTest::qWait(10);
        check(voice_error.isEmpty() && !playback.isPlaying() && playback.positionSeconds() >= 3,
            "native voice-only playback resumes and completes");
        if (!voice_error.isEmpty()) std::cerr << voice_error.toStdString() << '\n';
        return failures ? 1 : 0;
    }
    const auto path = temporary.filePath("overdub.humproj");
    hum::core::ProjectDocument fixture;
    fixture.canonical_notes = {{60,75,1,9,0},{64,75,1,9,0},{67,75,1,9,0}};
    fixture.track_count = 3; fixture.playlist_regions = {{{0,1},0,"Backing"},{{2},2,"Muted"}};
    fixture.tracks = {{hum::core::TrackKind::chords,hum::core::InstrumentVoice::acoustic_guitar,false},
        {hum::core::TrackKind::melody,hum::core::InstrumentVoice::strings,false},
        {hum::core::TrackKind::chords,hum::core::InstrumentVoice::grand_piano,true}};
    check(bool(hum::app::ProjectFileStore::save(path,fixture)),"overdub fixture saved");
    hum::test::FakeAudioCapture microphone; microphone.device_format = {48'000,1};
    hum::test::FakeRecordingGuide guide;
    hum::app::MainWindow window(nullptr,&microphone,&guide); window.show();
    const auto button = [&](const char* name) { return window.findChild<QPushButton*>(name); };
    auto* playlist = dynamic_cast<hum::app::PlaylistWidget*>(window.findChild<QWidget*>("playlist"));
    auto* roll = dynamic_cast<hum::app::PianoRollWidget*>(window.findChild<QWidget*>("pianoRoll"));
    QTimer file_driver; file_driver.setInterval(100);
    QObject::connect(&file_driver,&QTimer::timeout,&app,[&] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            std::cerr << message->text().toStdString() << '\n';
            if (auto* discard = message->button(QMessageBox::Discard)) { discard->click(); return; }
            std::exit(21);
        }
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { return; }
        file_driver.stop();
        dialog->setDirectory(temporary.path());
        QTimer::singleShot(250,dialog,[&,dialog] {
            dialog->findChild<QLineEdit*>("fileNameEdit")->setText(path);
            QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
        });
    });
    file_driver.start();
    button("openProjectButton")->click();
    std::cout << "Fixture loaded\n";
    playlist->selectTrack(1); button("openPianoRollButton")->click();
    QTest::mouseClick(roll->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(72 + static_cast<int>(roll->transform().timeToX(3.37)),8));
    button("openRecorderButton")->click();
    QPushButton* record = nullptr;
    for (auto* candidate : window.findChildren<QPushButton*>()) {
        if (candidate->text() == QStringLiteral("Record")) { record = candidate; break; }
    }
    if (!record) { return 22; }
    auto* stop = button("stopButton");
    const auto while_playing = app.arguments().contains("--native-workspace-check");
    if (while_playing) {
        button("midiPlayButton")->click();
        QElapsedTimer started; started.start();
        while (!button("midiPauseButton")->isEnabled() && started.elapsed() < 5'000) { QTest::qWait(10); }
        QTest::qWait(150);
        check(button("midiPauseButton")->isEnabled(), "existing arrangement is playing before Record is pressed");
    }
    record->click();
    check(guide.active() && guide.plan.notes.size() == 2 && guide.plan.voices.front() == hum::core::InstrumentVoice::acoustic_guitar,
        "Record includes audible backing lanes and their instruments, excludes muted lane");
    const auto origin = guide.plan.origin;
    check(while_playing ? origin > 3.37 && origin < 5 : std::abs(origin - 3.37) < .02,
        "recording freezes the current nonzero playhead, including active playback");
    microphone.emit(std::vector<float>(48'000,.9F)); guide.advance(1.99);
    check(stop->isEnabled() && !record->isEnabled() && window.findChild<QLabel*>("recordBeatLabel")->text().contains("4 / 4"),
        "fourth count-in beat remains cancellable and blocks a second Record");
    if (app.arguments().contains("--native-workspace-check")) {
        const auto index = app.arguments().indexOf("--native-workspace-check") + 1;
        if (index < app.arguments().size()) {
            check(window.findChild<QDialog*>("recordingDialog")->grab().save(app.arguments()[index]), "native count-in layout captured");
        }
    }
    stop->click();
    check(!microphone.isCapturing() && !guide.active() && record->isEnabled(),"Stop cancels count-in and releases hardware");
    record->click(); guide.fail();
    check(!microphone.isCapturing() && record->isEnabled(),"failed output never leaves recorder armed");
    record->click(); microphone.emit(std::vector<float>(96'000,.9F));
    guide.advance(guide.plan.countInSeconds());
    std::vector<float> tone(96'000);
    for (std::size_t i = 0; i < tone.size(); ++i) { tone[i] = static_cast<float>(.2 * std::sin(6.283185307179586 * 220 * static_cast<double>(i) / 48'000)); }
    microphone.emit(tone); guide.advance(guide.plan.countInSeconds()+2); stop->click();
    QElapsedTimer wait; wait.start();
    while (!button("saveProjectButton")->isEnabled() && wait.elapsed() < 15'000) { QTest::qWait(20); }
    button("saveProjectButton")->click();
    auto saved = hum::app::ProjectFileStore::load(path);
    check(saved && saved.project->canonical_notes.size() > 3, "captured overdub converts and appends a new region");
    if (saved && saved.project->canonical_notes.size() > 3) {
        check(saved.project->canonical_notes[0].start_seconds == 1 && saved.project->canonical_notes[0].duration_seconds == 9
            && saved.project->tracks == fixture.tracks, "existing notes, track kinds, instruments and mute survive overdub");
        const auto& region = saved.project->playlist_regions.back();
        const auto& note = saved.project->canonical_notes[region.note_indices.front()];
        check(region.track == 1 && note.start_seconds >= origin - .02 && note.start_seconds < origin + .2,
            "new melody lands at capture origin rather than lane end or count-in offset");
        check(note.duration_seconds > 1.5 && note.duration_seconds < 2.1, "stored take contains no count-in audio");
    }
    button("playlistUndoButton")->click(); button("saveProjectButton")->click();
    saved = hum::app::ProjectFileStore::load(path);
    check(saved && saved.project->canonical_notes.size() == 3,"one Undo removes only the overdub region");
    // The original Chords workflow expands hummed roots after capture.
    playlist->selectTrack(2); record->click(); guide.advance(guide.plan.countInSeconds());
    microphone.emit(tone); stop->click(); wait.restart();
    while (!button("saveProjectButton")->isEnabled() && wait.elapsed() < 15'000) { QTest::qWait(20); }
    button("saveProjectButton")->click(); saved = hum::app::ProjectFileStore::load(path);
    check(saved && saved.project->canonical_notes.size() >= 6 && saved.project->playlist_regions.back().track == 2,
        "chord-root overdub uses the same count-in and selected chord lane");
    record->click(); window.findChild<QDialog*>("recordingDialog")->reject(); guide.advance(99);
    check(!guide.active() && !microphone.isCapturing(),"closing recording controls cancels pending capture");
    button("openLyricsButton")->click();
    auto* lyrics = window.findChild<QPlainTextEdit*>("lyricsEditor");
    auto* lyrics_dialog = window.findChild<QDialog*>("lyricsDialog");
    const auto text = QString::fromUtf8("[Verse 1]\nCafé — my \"song\"\n\n[Chorus]\nSing with me");
    lyrics->setPlainText(text); lyrics->moveCursor(QTextCursor::End); QTest::keyClick(lyrics, Qt::Key_Space);
    check(lyrics->toPlainText() == text + " " && window.windowTitle().contains("*"), "lyrics accept normal spaces and mark the project unsaved");
    lyrics_dialog->reject(); button("openLyricsButton")->click();
    check(lyrics->toPlainText() == text + " ", "closing and reopening Lyrics retains writing");
    button("lyricsSaveButton")->click(); saved = hum::app::ProjectFileStore::load(path);
    check(saved && saved.project->lyrics == (text + " ").toStdString(), "Lyrics Save project persists Unicode text");
    const auto notes_before_voice = saved.project->canonical_notes.size();
    button("recordVoiceButton")->click();
    check(guide.active() && microphone.isCapturing(), "Playlist Record voice warms the microphone and starts count-in");
    microphone.emit(std::vector<float>(48000, .9F)); guide.advance(guide.plan.countInSeconds());
    const auto voice_origin = guide.plan.origin;
    microphone.emit(tone); button("recordVoiceButton")->click();
    button("saveProjectButton")->click(); saved = hum::app::ProjectFileStore::load(path);
    check(saved && saved.project->canonical_notes.size() == notes_before_voice && saved.project->voice_clips.size() == 1,
        "voice capture adds real audio without creating or changing MIDI notes");
    check(saved && saved.project->voice_clips.size() == 1 && saved.project->voice_clips[0].samples.size() == tone.size()
        && std::abs(saved.project->voice_clips[0].start_seconds - voice_origin) < .001,
        "voice audio excludes count-in and retains its Playlist origin");
    file_driver.start(); button("openProjectButton")->click();
    check(lyrics->toPlainText() == text + " " && playlist->voiceViewportRect(0).width() > 100,
        "reopening the project restores lyrics and the voice waveform lane");
    button("recordVoiceButton")->click();
    check(guide.plan.voice_clips.size() == 1, "subsequent overdubs include recorded voice backing");
    button("recordVoiceButton")->click();
    playlist->verticalScrollBar()->setValue(playlist->verticalScrollBar()->maximum());
    auto edit_voice = [&](const QString& action) {
        QTimer::singleShot(100, &app, [action] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                for (auto* item : menu->actions()) if (item->text() == action) {
                    menu->setActiveAction(item); QTest::keyClick(menu, Qt::Key_Return); return;
                }
            }
        });
        QTest::mouseClick(playlist->viewport(), Qt::RightButton, Qt::NoModifier,
            QPoint(30, static_cast<int>(playlist->voiceViewportRect(0).center().y())));
    };
    edit_voice("Mute voice"); button("saveProjectButton")->click(); saved = hum::app::ProjectFileStore::load(path);
    check(saved && saved.project->voice_clips[0].muted, "voice lane mute is editable and saved");
    if (while_playing) {
        window.grab().save("build/voice-playlist.png");
        lyrics_dialog->grab().save("build/lyrics-notepad.png");
    }
    edit_voice("Delete voice recording"); button("saveProjectButton")->click(); saved = hum::app::ProjectFileStore::load(path);
    check(saved && saved.project->voice_clips.empty() && saved.project->lyrics == (text + " ").toStdString()
        && saved.project->canonical_notes.size() == notes_before_voice, "deleting voice preserves MIDI and lyrics");
    std::cout << "Overdub UI failures: " << failures << '\n'; return failures ? 1 : 0;
}
