#include "MainWindow.hpp"
#include "AiTrialPanel.hpp"

#include "LevelMeter.hpp"
#include "EditorToolbar.hpp"
#include "ToolIcons.hpp"
#include "AudioOutputFormat.hpp"
#include <QAudioDevice>
#include <QMediaDevices>
#include "PianoRollWidget.hpp"
#include "PlaylistWidget.hpp"
#include "PhraseConversionProcessor.hpp"
#include "ProjectFileStore.hpp"
#include "QtAudioCapture.hpp"
#include "QtAudioPlayback.hpp"
#include "QtMidiPlayback.hpp"
#include "vocal/SingToMidiPanel.hpp"
#include "ArmedCapture.hpp"
#include "RecordingGuidePlayer.hpp"
#include "hum/core/midi_file.hpp"
#include "hum/core/note_editor.hpp"
#include "hum/core/note_extraction.hpp"
#include "hum/core/pitch.hpp"
#include "hum/core/project_file.hpp"
#include "hum/core/recording.hpp"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QProgressBar>
#include <QSlider>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QTabWidget>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hum::app {

namespace {

[[nodiscard]] QString durationText(const double seconds, const double maximum_seconds) {
    const auto tenths = static_cast<int>(std::floor(std::max(seconds, 0.0) * 10.0));
    const auto maximum_tenths = static_cast<int>(std::floor(maximum_seconds * 10.0));
    return QStringLiteral("%1:%2.%3 / %4:%5")
        .arg(tenths / 600, 2, 10, QLatin1Char('0'))
        .arg((tenths / 10) % 60, 2, 10, QLatin1Char('0'))
        .arg(tenths % 10)
        .arg(maximum_tenths / 600, 2, 10, QLatin1Char('0'))
        .arg((maximum_tenths / 10) % 60, 2, 10, QLatin1Char('0'));
}

[[nodiscard]] QString channelText(const std::uint16_t channels) {
    if (channels == 1) {
        return QStringLiteral("1 · Mono");
    }
    if (channels == 2) {
        return QStringLiteral("2 · Stereo");
    }
    return QStringLiteral("%1 channels").arg(channels);
}

[[nodiscard]] QString errorMessage(const core::ServiceStatus error) {
    switch (error) {
    case core::ServiceStatus::permission_required:
        return QStringLiteral("Microphone permission is required. Choose Enable microphone to continue.");
    case core::ServiceStatus::permission_denied:
        return QStringLiteral("Microphone access is denied. Enable it in system privacy settings, then choose Retry access.");
    case core::ServiceStatus::device_disconnected:
        return QStringLiteral("The selected microphone was disconnected. Reconnect it or select another device, then retry.");
    case core::ServiceStatus::unavailable:
        return QStringLiteral("No usable microphone is available. Connect a device and choose Refresh devices.");
    case core::ServiceStatus::invalid_input:
        return QStringLiteral("The microphone changed to an unsupported format. Refresh devices and try again.");
    case core::ServiceStatus::io_error:
        return QStringLiteral("The audio system stopped the recording. Check the selected device and try again.");
    case core::ServiceStatus::busy:
        return QStringLiteral("A recording session is already active.");
    case core::ServiceStatus::ok:
        return {};
    }
    return QStringLiteral("The microphone could not be opened.");
}

} // namespace

class MainWindow::Impl final {
public:
    explicit Impl(MainWindow* window, core::IAudioCapture* capture, IRecordingGuide* guide)
        : window_(window),
          capture_(window),
          capture_source_(capture ? *capture : capture_),
          armed_capture_(capture_source_), recorder_(armed_capture_, 30.0, 0.98F),
          guide_player_(window), guide_(guide ? guide : &guide_player_),
          playback_(window),
          midi_playback_(window),
          cue_playback_(window),
          audition_playback_(window),
          phrase_processor_(window) {
        note_editor_.setMagnetEnabled(false);
        buildUi();
        wireEvents();
        for (QWidget* host : {static_cast<QWidget*>(window_), static_cast<QWidget*>(piano_dialog_)}) {
            auto* space = new QShortcut(QKeySequence(Qt::Key_Space), host);
            space->setContext(Qt::WindowShortcut);
            space->setAutoRepeat(false);
            QObject::connect(space, &QShortcut::activated, window_, [this] { toggleMidiPlayback(); });
        }
        updateScaleGuidanceUi();
        updateEditorUi();
        refreshDevices();
        updateUi();
        updateWindowTitle();
        monitor_.start(33);
    }

    ~Impl() {
        shutdown();
    }

    void shutdown() noexcept {
        if (closed_) {
            return;
        }
        closed_ = true;
        guide_->stop(); armed_capture_.stop();
        if (sing_panel_) { sing_panel_->reject(); }
        if (ai_trial_panel_) { ai_trial_panel_->reject(); }
        monitor_.stop();
        capture_.setDevicesChangedCallback({});
        playback_.stop();
        midi_playback_.stop();
        audition_playback_.stop();
        cue_playback_.stop();
        if (piano_roll_) {
            piano_roll_->setPlayheadSeconds(std::nullopt);
        }
        if (playlist_) {
            playlist_->setPlayheadSeconds(std::nullopt);
        }
        phrase_processor_.cancel();
        recorder_.shutdown();
    }

    [[nodiscard]] bool requestClose() {
        if (guide_->active() || recorder_.state() == core::RecordingState::recording) { stopRecording(); }
        if (!confirmDiscardChanges(QStringLiteral("close the application"))) {
            return false;
        }
        shutdown();
        return true;
    }

private:
    [[nodiscard]] vocal::HostContext vocalContext() const {
        vocal::HostContext context;
        context.origin = midi_playback_.isPlaying() || midi_playback_.isPaused() ? midi_playback_.positionSeconds() : playback_start_seconds_; context.bpm = tempo_spin_->value();
        context.ppq = ticks_per_quarter_; context.track = playlist_->selectedTrack();
        context.project_generation = vocal_project_generation_;
        context.instrument = note_editor_.tracks()[context.track].instrument;
        context.muted = note_editor_.tracks()[context.track].muted;
        context.voice_clips = voice_clips_;
        context.recording_click = metronome_button_->isChecked();
        context.backing_notes = note_editor_.projectedNotes();
        context.backing_voices.resize(context.backing_notes.size(), core::InstrumentVoice::grand_piano);
        for (std::size_t i = 0; i < context.backing_notes.size(); ++i) {
            if (const auto track = note_editor_.trackForNote(i)) {
                context.backing_voices[i] = note_editor_.tracks()[*track].instrument;
            }
        }
        context.scale = note_editor_.scale(); context.magnet = note_editor_.magnetEnabled();
        for (std::size_t index = 0; index < note_editor_.notes().size(); ++index) {
            if (note_editor_.trackForNote(index) != context.track) { continue; }
            const auto& note = note_editor_.projectedNotes()[index];
            if (note.start_seconds + note.duration_seconds < context.origin || note.start_seconds > context.origin + 30) { continue; }
            context.reference_notes.push_back({note.pitch, note.start_seconds, note.duration_seconds, note.velocity});
            if (context.reference_notes.size() >= 2'000) { break; }
        }
        return context;
    }

    QString insertVocalPreview(const std::vector<hum::vocal::NoteEvent>& notes,
        double origin, std::uint64_t generation) {
        if (closed_ || generation != vocal_project_generation_) {
            return QStringLiteral("The project changed. Record a new take for the current project.");
        }
        const auto inserted = vocal::PianoRollInsertAdapter::insert(note_editor_, notes, origin, playlist_->selectedTrack());
        if (inserted != core::NoteEditResult::applied) {
            return QStringLiteral("The phrase could not be inserted safely. Existing notes are unchanged.");
        }
        stopMelodyPlayback(); markDirty(); piano_roll_->refresh(true); updateEditorUi(); updateUi();
        return {};
    }

    void buildUi() {
        window_->setWindowTitle(QStringLiteral("Hum-to-MIDI Music Sketchpad — Piano Roll"));
        window_->setMinimumSize(1'180, 820);
        window_->resize(1'460, 980);

        auto* root = new QWidget(window_);
        root->setObjectName(QStringLiteral("root"));
        auto* shell = new QHBoxLayout(root);
        shell->setContentsMargins(18, 18, 18, 18);
        shell->setSpacing(22);

        auto* page = new QVBoxLayout();
        page->setSpacing(18);
        auto* header = new QHBoxLayout();
        auto* heading_box = new QVBoxLayout();
        auto* eyebrow = new QLabel(QStringLiteral("HUM · EDIT · PLAY · EXPORT"), root);
        eyebrow->setObjectName(QStringLiteral("eyebrow"));
        auto* title = new QLabel(QStringLiteral("Shape your hummed melody"), root);
        title->setObjectName(QStringLiteral("pageTitle"));
        auto* subtitle = new QLabel(
            QStringLiteral("Record locally, correct notes, hear the built-in instrument, then save or export."), root);
        subtitle->setObjectName(QStringLiteral("subtitle"));
        heading_box->addWidget(eyebrow);
        heading_box->addWidget(title);
        heading_box->addWidget(subtitle);
        header->addLayout(heading_box, 1);
        project_name_label_ = new QLabel(QStringLiteral("Untitled project"), root);
        project_name_label_->setObjectName(QStringLiteral("footnote"));
        project_name_label_->setAlignment(Qt::AlignCenter);
        open_project_button_ = new QPushButton(QStringLiteral("Open project…"), root);
        save_project_button_ = new QPushButton(QStringLiteral("Save project…"), root);
        open_project_button_->setObjectName(QStringLiteral("openProjectButton"));
        save_project_button_->setObjectName(QStringLiteral("saveProjectButton"));
        open_project_button_->setToolTip(QStringLiteral(
            "Open notes, settings, lyrics and Playlist voice recordings."));
        save_project_button_->setToolTip(QStringLiteral(
            "Save canonical editable notes and settings. Session audio remains in memory only."));
        header->addWidget(project_name_label_, 0, Qt::AlignTop);
        header->addWidget(open_project_button_, 0, Qt::AlignTop);
        header->addWidget(save_project_button_, 0, Qt::AlignTop);
        auto* privacy = new QLabel(QStringLiteral("●  LOCAL ONLY · NEVER UPLOADED"), root);
        privacy->setObjectName(QStringLiteral("privacyBadge"));
        privacy->setAlignment(Qt::AlignCenter);
        header->addWidget(privacy, 0, Qt::AlignTop);
        page->addLayout(header);

        recording_dialog_ = new QDialog(window_);
        recording_dialog_->setObjectName(QStringLiteral("recordingDialog"));
        recording_dialog_->setWindowTitle(QStringLiteral("Hum → MIDI — Record and convert"));
        recording_dialog_->resize(760, 840);
        auto* recording_content = new QWidget(recording_dialog_);
        auto* recording_layout = new QVBoxLayout(recording_content);
        recording_layout->setSpacing(18);
        auto* recorder_card = new QFrame(recording_content);
        recorder_card->setObjectName(QStringLiteral("card"));
        auto* recorder_layout = new QVBoxLayout(recorder_card);
        recorder_layout->setContentsMargins(24, 22, 24, 24);
        recorder_layout->setSpacing(16);
        auto* input_title = new QLabel(QStringLiteral("MICROPHONE INPUT"), recorder_card);
        input_title->setObjectName(QStringLiteral("sectionLabel"));
        recorder_layout->addWidget(input_title);

        auto* device_row = new QHBoxLayout();
        device_combo_ = new QComboBox(recorder_card);
        device_combo_->setAccessibleName(QStringLiteral("Microphone device"));
        refresh_button_ = new QPushButton(QStringLiteral("Refresh devices"), recorder_card);
        permission_button_ = new QPushButton(QStringLiteral("Enable microphone"), recorder_card);
        permission_button_->setObjectName(QStringLiteral("secondaryButton"));
        refresh_button_->setObjectName(QStringLiteral("secondaryButton"));
        device_row->addWidget(device_combo_, 1);
        device_row->addWidget(refresh_button_);
        device_row->addWidget(permission_button_);
        recorder_layout->addLayout(device_row);

        status_label_ = new QLabel(recorder_card);
        status_label_->setObjectName(QStringLiteral("statusPanel"));
        status_label_->setWordWrap(true);
        status_label_->setAccessibleName(QStringLiteral("Recording status"));
        recorder_layout->addWidget(status_label_);

        auto* level_heading = new QHBoxLayout();
        auto* level_title = new QLabel(QStringLiteral("Input level"), recorder_card);
        level_title->setObjectName(QStringLiteral("fieldTitle"));
        clip_label_ = new QLabel(QStringLiteral("CLIPPING — move back from the microphone"), recorder_card);
        clip_label_->setObjectName(QStringLiteral("clipWarning"));
        clip_label_->hide();
        level_heading->addWidget(level_title);
        level_heading->addStretch();
        level_heading->addWidget(clip_label_);
        recorder_layout->addLayout(level_heading);
        level_meter_ = new LevelMeter(recorder_card);
        recorder_layout->addWidget(level_meter_);

        duration_label_ = new QLabel(durationText(0.0, recorder_.maximumDurationSeconds()), recorder_card);
        duration_label_->setObjectName(QStringLiteral("duration"));
        duration_label_->setAlignment(Qt::AlignCenter);
        recorder_layout->addWidget(duration_label_);

        auto* controls = new QHBoxLayout();
        record_button_ = new QPushButton(QStringLiteral("Record"), recorder_card);
        record_button_->setObjectName(QStringLiteral("primaryButton"));
        stop_button_ = new QPushButton(QStringLiteral("Stop"), recorder_card);
        stop_button_->setObjectName(QStringLiteral("stopButton"));
        play_button_ = new QPushButton(QStringLiteral("Play original"), recorder_card);
        play_button_->setObjectName(QStringLiteral("secondaryButton"));
        controls->addStretch();
        controls->addWidget(record_button_);
        controls->addWidget(stop_button_);
        controls->addWidget(play_button_);
        record_button_->setIcon(toolIcon(ToolIcon::microphone, true));
        stop_button_->setIcon(toolIcon(ToolIcon::stop, true));
        play_button_->setIcon(toolIcon(ToolIcon::play));
        controls->addStretch();
        recorder_layout->addLayout(controls);
        auto* record_beat_row = new QHBoxLayout();
        record_metronome_button_ = new QPushButton(QStringLiteral("Metronome OFF"), recorder_card);
        record_metronome_button_->setObjectName(QStringLiteral("recordMetronomeButton"));
        record_metronome_button_->setCheckable(true);
        record_metronome_button_->setIcon(toolIcon(ToolIcon::metronome));
        record_metronome_button_->setToolTip(QStringLiteral("Always count four beats before recording. Turn this on to continue the click during the take."));
        record_tempo_spin_ = new QDoubleSpinBox(recorder_card);
        record_tempo_spin_->setObjectName(QStringLiteral("recordTempoSpin"));
        record_tempo_spin_->setRange(20,400);record_tempo_spin_->setDecimals(1);
        record_tempo_spin_->setValue(120);record_tempo_spin_->setSuffix(QStringLiteral(" BPM"));
        record_tempo_spin_->setAccessibleName(QStringLiteral("Recording metronome tempo"));
        record_beat_label_ = new QLabel(QStringLiteral("4-beat count-in · then record with tracks"), recorder_card);
        record_beat_label_->setObjectName(QStringLiteral("recordBeatLabel"));
        record_beat_label_->setWordWrap(true);
        record_beat_row->addWidget(record_metronome_button_);
        record_beat_row->addWidget(record_tempo_spin_);
        record_beat_row->addWidget(record_beat_label_,1);
        recorder_layout->addLayout(record_beat_row);
        auto* headphones = new QLabel(QStringLiteral("Use headphones to keep the backing tracks and count-in out of the microphone."), recorder_card);
        headphones->setObjectName(QStringLiteral("footnote"));headphones->setWordWrap(true);
        recorder_layout->addWidget(headphones);

        auto* metadata = new QHBoxLayout();
        sample_rate_value_ = addMetric(metadata, QStringLiteral("SAMPLE RATE"));
        channels_value_ = addMetric(metadata, QStringLiteral("CHANNELS"));
        samples_value_ = addMetric(metadata, QStringLiteral("CAPTURED SAMPLES"));
        recorder_layout->addLayout(metadata);
        auto* limit = new QLabel(
            QStringLiteral("Safety limit: 30 seconds. Recording stops automatically at the limit."),
            recorder_card);
        limit->setObjectName(QStringLiteral("footnote"));
        limit->setAlignment(Qt::AlignCenter);
        recorder_layout->addWidget(limit);
        recording_layout->addWidget(recorder_card);

        auto* phrase_card = new QFrame(recording_content);
        phrase_card->setObjectName(QStringLiteral("card"));
        auto* phrase_layout = new QVBoxLayout(phrase_card);
        phrase_layout->setContentsMargins(22, 18, 22, 18);
        phrase_layout->setSpacing(8);
        conversion_tabs_ = new QTabWidget(phrase_card);
        auto* conversion_tabs = conversion_tabs_;
        conversion_tabs->setObjectName(QStringLiteral("humToMidiTabs"));
        auto* melody_tab = new QWidget(conversion_tabs);
        auto* melody_tab_layout = new QVBoxLayout(melody_tab);
        melody_tab_layout->setContentsMargins(0, 8, 0, 0);
        auto* phrase_title = new QLabel(QStringLiteral("Melody → MIDI"), melody_tab);
        phrase_title->setObjectName(QStringLiteral("cardTitle"));
        phrase_status_label_ = new QLabel(
            QStringLiteral("Stop a recording to begin post-recording conversion."), melody_tab);
        phrase_status_label_->setObjectName(QStringLiteral("pitchDetail"));
        phrase_status_label_->setWordWrap(true);
        phrase_progress_ = new QProgressBar(melody_tab);
        phrase_progress_->setRange(0, 100);
        phrase_progress_->setValue(0);
        phrase_progress_->setTextVisible(true);
        auto* phrase_controls = new QHBoxLayout();
        convert_button_ = new QPushButton(QStringLiteral("Add saved take again"), melody_tab);
        convert_button_->setToolTip(QStringLiteral("Convert the last in-memory take and append a region on the selected track. Existing regions are retained."));
        cancel_conversion_button_ = new QPushButton(QStringLiteral("Cancel"), melody_tab);
        export_button_ = new QPushButton(QStringLiteral("Export MIDI…"), melody_tab);
        convert_button_->setObjectName(QStringLiteral("secondaryButton"));
        cancel_conversion_button_->setObjectName(QStringLiteral("secondaryButton"));
        export_button_->setObjectName(QStringLiteral("primaryButton"));
        phrase_controls->addWidget(convert_button_);
        phrase_controls->addWidget(cancel_conversion_button_);
        phrase_controls->addStretch();
        phrase_controls->addWidget(export_button_);
        auto* phrase_note = new QLabel(
            QStringLiteral("New takes append regions. Hum → MIDI keeps only the latest source take in memory. Use Record voice in the Playlist to save vocal audio with the project."),
            melody_tab);
        phrase_note->setWordWrap(true);
        phrase_note->setObjectName(QStringLiteral("footnote"));
        melody_tab_layout->addWidget(phrase_title);
        melody_tab_layout->addWidget(phrase_status_label_);
        melody_tab_layout->addWidget(phrase_progress_);
        melody_tab_layout->addLayout(phrase_controls);
        melody_tab_layout->addWidget(phrase_note);
        conversion_tabs->addTab(melody_tab, QStringLiteral("Melody"));
        melody_snap_check_ = new QCheckBox(QStringLiteral("Snap melody to key — nearest scale note (Magnet)"), melody_tab);
        melody_snap_check_->setObjectName(QStringLiteral("melodySnapCheck"));
        melody_tab->layout()->addWidget(melody_snap_check_);
        auto* chords_tab = new QWidget(conversion_tabs);
        auto* chords_layout = new QVBoxLayout(chords_tab);
        chords_layout->setContentsMargins(0, 8, 0, 0);
        auto* chords_title = new QLabel(QStringLiteral("Chord progression → MIDI"), chords_tab);
        chords_title->setObjectName(QStringLiteral("cardTitle"));
        auto* chords_copy = new QLabel(QStringLiteral(
            "Choose the song key and scale, then hum one root note per chord. Each root becomes a three-note chord in that scale (C → C, E, G in C major). Nearby pitches snap to the scale. A Chords track is selected or created automatically. Audio stays on this Mac."), chords_tab);
        chords_copy->setWordWrap(true);
        auto* chord_key_row = new QHBoxLayout();
        chord_key_combo_ = new QComboBox(chords_tab);
        chord_key_combo_->setObjectName(QStringLiteral("chordKeyCombo"));
        for (std::uint8_t pitch = 0; pitch < 12; ++pitch) { chord_key_combo_->addItem(QString::fromStdString(std::string(core::pitchClassName(pitch))), pitch); }
        chord_scale_combo_ = new QComboBox(chords_tab);
        chord_scale_combo_->setObjectName(QStringLiteral("chordScaleCombo"));
        chord_scale_combo_->addItem(QStringLiteral("Major"), static_cast<int>(core::ScaleMode::major));
        chord_scale_combo_->addItem(QStringLiteral("Natural minor"), static_cast<int>(core::ScaleMode::natural_minor));
        chord_key_row->addWidget(new QLabel(QStringLiteral("Song key"), chords_tab));
        chord_key_row->addWidget(chord_key_combo_);
        chord_key_row->addWidget(chord_scale_combo_);
        chord_key_row->addStretch();
        chord_convert_button_ = new QPushButton(QStringLiteral("Convert saved hum to chords"), chords_tab);
        chord_convert_button_->setObjectName(QStringLiteral("chordConvertButton"));
        chord_status_label_ = new QLabel(QStringLiteral("Hum the roots, then press Stop to create your chord progression."), chords_tab);
        chord_status_label_->setWordWrap(true);
        chord_status_label_->setObjectName(QStringLiteral("pitchDetail"));
        chords_layout->addWidget(chords_title);
        chords_layout->addWidget(chords_copy);
        chords_layout->addLayout(chord_key_row);
        chords_layout->addWidget(chord_convert_button_);
        chords_layout->addWidget(chord_status_label_);
        chord_cancel_button_ = new QPushButton(QStringLiteral("Cancel conversion"), chords_tab);
        chords_layout->addWidget(chord_cancel_button_);
        connect_chords_check_ = new QCheckBox(QStringLiteral("Smooth chords: hold each chord until the next root"), chords_tab);
        connect_chords_check_->setObjectName(QStringLiteral("connectChordsCheck"));
        connect_chords_check_->setChecked(true);
        connect_chords_check_->setToolTip(QStringLiteral("Joins repeated roots and fills pauses between roots. Turn off to keep hummed rests."));
        chords_layout->addWidget(connect_chords_check_);
        auto* snap_hint = new QLabel(QStringLiteral("Off-key roots snap to the nearest scale note above or below. Equal distances choose the lower note."), chords_tab);
        snap_hint->setWordWrap(true);
        chords_layout->addWidget(snap_hint);
        auto* progression_title = new QLabel(QStringLiteral("Build a progression with chord numbers"), chords_tab);
        progression_title->setObjectName(QStringLiteral("cardTitle"));
        chords_layout->addWidget(progression_title);
        auto* degree_grid = new QGridLayout();
        for (std::size_t degree = 0; degree < degree_buttons_.size(); ++degree) {
            auto* button = new QPushButton(chords_tab);
            degree_buttons_[degree] = button;
            button->setObjectName(QStringLiteral("chordDegree%1").arg(degree + 1));
            degree_grid->addWidget(button, static_cast<int>(degree / 3), static_cast<int>(degree % 3));
            QObject::connect(button, &QPushButton::clicked, window_, [this, degree] {
                constexpr std::array<const char*, 7> tokens{"I", "II", "III", "IV", "V", "VI", "VII"};
                const auto text = progression_edit_->text().trimmed();
                progression_edit_->setText(text + (text.isEmpty() ? QString{} : QStringLiteral(" "))
                    + QString::fromLatin1(tokens[degree]));
            });
        }
        chords_layout->addLayout(degree_grid);
        progression_edit_ = new QLineEdit(chords_tab);
        progression_edit_->setObjectName(QStringLiteral("chordProgressionEdit"));
        progression_edit_->setPlaceholderText(QStringLiteral("I IV V I  ·  or 1 4 5 1"));
        progression_edit_->setMaxLength(4096);
        chords_layout->addWidget(progression_edit_);
        progression_preview_ = new QLabel(chords_tab);
        progression_preview_->setWordWrap(true);
        chords_layout->addWidget(progression_preview_);
        auto* progression_controls = new QHBoxLayout();
        chord_beats_spin_ = new QSpinBox(chords_tab);
        chord_beats_spin_->setObjectName(QStringLiteral("chordBeatsSpin"));
        chord_beats_spin_->setRange(1, 16);
        chord_beats_spin_->setValue(4);
        chord_beats_spin_->setSuffix(QStringLiteral(" beats / chord"));
        progression_controls->addWidget(chord_beats_spin_);
        progression_add_button_ = new QPushButton(QStringLiteral("Add progression to Playlist"), chords_tab);
        progression_add_button_->setObjectName(QStringLiteral("addProgressionButton"));
        progression_controls->addWidget(progression_add_button_);
        chords_layout->addLayout(progression_controls);
        conversion_tabs->addTab(chords_tab, QStringLiteral("Chord progression"));
        phrase_layout->addWidget(conversion_tabs);
        recording_layout->addWidget(phrase_card);
        auto* recording_scroll = new QScrollArea(recording_dialog_);
        recording_scroll->setWidgetResizable(true);
        recording_scroll->setFrameShape(QFrame::NoFrame);
        recording_scroll->setWidget(recording_content);
        auto* recording_window_layout = new QVBoxLayout(recording_dialog_);
        recording_window_layout->addWidget(recording_scroll);

        piano_dialog_ = new QDialog(window_);
        piano_dialog_->setObjectName(QStringLiteral("pianoRollDialog"));
        piano_dialog_->setWindowTitle(QStringLiteral("Piano roll — Correct your notes"));
        piano_dialog_->resize(1240, 660);

        auto* piano_card = new QFrame(piano_dialog_);
        piano_card->setObjectName(QStringLiteral("card"));
        auto* piano_layout = new QVBoxLayout(piano_card);
        piano_layout->setContentsMargins(18, 16, 18, 18);
        piano_layout->setSpacing(9);
        auto* piano_heading = new QHBoxLayout();
        auto* piano_title_box = new QVBoxLayout();
        piano_title_ = new QLabel(QStringLiteral("Piano roll"), piano_card);
        piano_title_->setObjectName(QStringLiteral("cardTitle"));
        editor_status_label_ = new QLabel(
            QStringLiteral("Record a melody, or double-click the grid to add your first note."),
            piano_card);
        editor_status_label_->setObjectName(QStringLiteral("footnote"));
        editor_status_label_->setWordWrap(true);
        piano_title_box->addWidget(piano_title_);
        piano_title_box->addWidget(editor_status_label_);
        piano_heading->addLayout(piano_title_box, 1);

        auto* sing_button = new QPushButton(QStringLiteral("Sing to MIDI"), piano_card);
        sing_button->setObjectName(QStringLiteral("singToMidiButton"));
        sing_button->setToolTip(QStringLiteral("Record and preview with the separate local vocal engine."));
        piano_heading->addWidget(sing_button);
        QObject::connect(sing_button, &QPushButton::clicked, window_, [this] {
            if (recorder_.state() == core::RecordingState::recording || guide_->active() || phrase_processor_.isBusy()) {
                editor_status_label_->setText(QStringLiteral("Finish the current Hum → MIDI take before opening Sing to MIDI."));
                return;
            }
            playback_.stop(); cue_playback_.stop();
            if (vocalContext().origin > 570) {
                editor_status_label_->setText(QStringLiteral("Place the playhead before 570 seconds to leave room for a vocal take.")); return;
            }
            if (!sing_panel_) {
                sing_panel_ = new vocal::SingToMidiPanel(piano_dialog_);
                sing_panel_->setHost([this] { return vocalContext(); },
                    [this](const auto& notes, double origin, std::uint64_t generation) {
                        return insertVocalPreview(notes, origin, generation);
                    }, [this] { stopMelodyPlayback(); playback_.stop(); cue_playback_.stop(); },
                    [this](double position) { playback_start_seconds_ = position; showTransportPosition(position); });
            }
            sing_panel_->setOrigin(vocalContext().origin);
            sing_panel_->show(); sing_panel_->raise(); sing_panel_->activateWindow();
        });

        undo_button_ = new QPushButton(QStringLiteral("Undo"), piano_card);
        redo_button_ = new QPushButton(QStringLiteral("Redo"), piano_card);
        add_note_button_ = new QPushButton(QStringLiteral("+ Add note"), piano_card);
        delete_note_button_ = new QPushButton(QStringLiteral("Delete"), piano_card);
        quantize_button_ = new QPushButton(QStringLiteral("Quantise"), piano_card);
        quantize_button_->setObjectName(QStringLiteral("quantizeButton"));
        quantize_button_->setToolTip(QStringLiteral("Snap every selected note onset to the nearest selected rhythmic grid line."));
        for (auto* button : {undo_button_, redo_button_, add_note_button_, delete_note_button_, quantize_button_}) {
            button->setObjectName(QStringLiteral("secondaryButton"));
            piano_heading->addWidget(button);
        }
        add_note_button_->setObjectName(QStringLiteral("addNoteButton"));
        piano_play_button_ = new QPushButton(QStringLiteral("Play"), piano_card);
        piano_play_button_->setObjectName(QStringLiteral("pianoPlayButton"));
        piano_pause_button_ = new QPushButton(QStringLiteral("Pause"), piano_card);
        piano_pause_button_->setObjectName(QStringLiteral("pianoPauseButton"));
        piano_stop_button_ = new QPushButton(QStringLiteral("Stop"), piano_card);
        piano_stop_button_->setObjectName(QStringLiteral("pianoStopButton"));
        piano_position_ = new QLabel(QStringLiteral("0.00 s"), piano_card);
        auto* piano_transport = new QHBoxLayout();
        piano_transport->addWidget(piano_position_);
        piano_transport->addWidget(new QLabel(QStringLiteral("Space: play / pause · Plays the Playlist arrangement"), piano_card));
        piano_transport->addStretch();
        track_instrument_combo_ = new QComboBox(piano_card);
        track_instrument_combo_->setObjectName(QStringLiteral("trackInstrumentCombo"));
        for (const auto voice : {core::InstrumentVoice::grand_piano, core::InstrumentVoice::acoustic_guitar,
                 core::InstrumentVoice::electric_piano, core::InstrumentVoice::electric_guitar, core::InstrumentVoice::strings}) {
            track_instrument_combo_->addItem(QString::fromLatin1(core::instrumentVoiceName(voice)), static_cast<int>(voice));
        }
        track_instrument_combo_->setToolTip(QStringLiteral("Instrument for the selected Playlist track and clicked piano keys."));
        piano_transport->addWidget(new QLabel(QStringLiteral("Track instrument"), piano_card));
        piano_transport->addWidget(track_instrument_combo_);
        show_other_tracks_ = new QCheckBox(QStringLiteral("Show other tracks"), piano_card);
        show_other_tracks_->setObjectName(QStringLiteral("showOtherTracksCheck"));
        show_other_tracks_->setToolTip(QStringLiteral("Show other tracks as grey, read-only reference notes."));
        piano_transport->addWidget(show_other_tracks_);
        piano_layout->addLayout(piano_heading);
        piano_layout->addLayout(piano_transport);

        auto* scale_tools = new QHBoxLayout();
        scale_tools->setSpacing(8);
        auto* key_label = new QLabel(QStringLiteral("Key"), piano_card);
        key_label->setObjectName(QStringLiteral("fieldTitle"));
        root_pitch_combo_ = new QComboBox(piano_card);
        root_pitch_combo_->setAccessibleName(QStringLiteral("Root key"));
        for (std::uint8_t root = 0; root < 12; ++root) {
            root_pitch_combo_->addItem(
                QString::fromStdString(std::string(core::pitchClassName(root))), root);
        }
        auto* scale_label = new QLabel(QStringLiteral("Scale"), piano_card);
        scale_label->setObjectName(QStringLiteral("fieldTitle"));
        scale_mode_combo_ = new QComboBox(piano_card);
        scale_mode_combo_->setAccessibleName(QStringLiteral("Scale type"));
        scale_mode_combo_->addItem(QStringLiteral("Major"),
            static_cast<int>(core::ScaleMode::major));
        scale_mode_combo_->addItem(QStringLiteral("Natural minor"),
            static_cast<int>(core::ScaleMode::natural_minor));
        magnet_button_ = new QPushButton(QStringLiteral("Magnet OFF"), piano_card);
        magnet_button_->setObjectName(QStringLiteral("magnetButton"));
        magnet_button_->setCheckable(true);
        magnet_button_->setAccessibleName(QStringLiteral("Scale Magnet"));
        magnet_button_->setToolTip(QStringLiteral(
            "Shows and exports the closest note in the selected scale without replacing your original pitch."));
        scale_tools->addWidget(key_label);
        scale_tools->addWidget(root_pitch_combo_);
        scale_tools->addWidget(scale_label);
        scale_tools->addWidget(scale_mode_combo_);
        scale_tools->addWidget(magnet_button_);
        scale_tools->addStretch();
        piano_layout->addLayout(scale_tools);

        scale_guidance_label_ = new QLabel(
            QStringLiteral("C Major guides pitch only. Key selection does not transpose your melody. Magnet is OFF."),
            piano_card);
        scale_guidance_label_->setObjectName(QStringLiteral("scaleGuidance"));
        scale_guidance_label_->setWordWrap(true);
        piano_layout->addWidget(scale_guidance_label_);

        auto* piano_tools = new QHBoxLayout();
        piano_tools->setSpacing(8);
        auto* snap_label = new QLabel(QStringLiteral("Rhythm snap"), piano_card);
        snap_label->setObjectName(QStringLiteral("fieldTitle"));
        rhythm_snap_combo_ = new QComboBox(piano_card);
        rhythm_snap_combo_->setAccessibleName(QStringLiteral("Optional rhythmic timing snap"));
        rhythm_snap_combo_->setToolTip(QStringLiteral(
            "Rounds note timing to a rhythmic interval. This does not move pitches into a key or scale."));
        rhythm_snap_combo_->setObjectName(QStringLiteral("rhythmSnapCombo"));
        rhythm_snap_combo_->addItem(QStringLiteral("Off · keep captured timing"), 0);
        for (const auto division : {4, 8, 16, 32, 64}) {
            rhythm_snap_combo_->addItem(QStringLiteral("1/%1 note").arg(division), division);
        }
        auto* tempo_label = new QLabel(QStringLiteral("Tempo"), piano_card);
        tempo_label->setObjectName(QStringLiteral("fieldTitle"));
        tempo_spin_ = new QDoubleSpinBox(piano_card);
        tempo_spin_->setObjectName(QStringLiteral("tempoSpin"));
        tempo_spin_->setAccessibleName(QStringLiteral("Project and MIDI export tempo"));
        tempo_spin_->setRange(20.0, 400.0);
        tempo_spin_->setDecimals(1);
        tempo_spin_->setSingleStep(1.0);
        tempo_spin_->setValue(core::kDefaultTempoBpm);
        tempo_spin_->setSuffix(QStringLiteral(" BPM"));
        tempo_spin_->setToolTip(QStringLiteral(
            "Sets the playlist bar guide and MIDI tempo. It does not quantize or retime the melody."));
        auto* horizontal_zoom_label = new QLabel(QStringLiteral("Time zoom"), piano_card);
        horizontal_zoom_label->setObjectName(QStringLiteral("fieldTitle"));
        horizontal_zoom_slider_ = new QSlider(Qt::Horizontal, piano_card);
        horizontal_zoom_slider_->setRange(50, 520);
        horizontal_zoom_slider_->setValue(160);
        horizontal_zoom_slider_->setFixedWidth(120);
        horizontal_zoom_slider_->setAccessibleName(QStringLiteral("Horizontal time zoom"));
        auto* vertical_zoom_label = new QLabel(QStringLiteral("Pitch zoom"), piano_card);
        vertical_zoom_label->setObjectName(QStringLiteral("fieldTitle"));
        vertical_zoom_slider_ = new QSlider(Qt::Horizontal, piano_card);
        vertical_zoom_slider_->setRange(10, 34);
        vertical_zoom_slider_->setValue(20);
        vertical_zoom_slider_->setFixedWidth(90);
        vertical_zoom_slider_->setAccessibleName(QStringLiteral("Vertical pitch zoom"));
        piano_tools->addWidget(snap_label);
        piano_tools->addWidget(rhythm_snap_combo_);
        piano_tools->addWidget(tempo_label);
        piano_tools->addWidget(tempo_spin_);
        piano_tools->addStretch();
        piano_tools->addWidget(horizontal_zoom_label);
        piano_tools->addWidget(horizontal_zoom_slider_);
        piano_tools->addWidget(vertical_zoom_label);
        piano_tools->addWidget(vertical_zoom_slider_);
        piano_layout->addLayout(piano_tools);

        editor_toolbar_ = new EditorToolbar(piano_card);
        editor_toolbar_->addTransport(buildTransport(piano_card, piano_play_button_, piano_pause_button_, piano_stop_button_, 1));
        piano_layout->addWidget(editor_toolbar_);

        piano_roll_ = new PianoRollWidget(note_editor_, piano_card);
        piano_roll_->setObjectName(QStringLiteral("pianoRoll"));
        piano_roll_->setMinimumHeight(280);
        piano_layout->addWidget(piano_roll_, 1);
        auto* piano_window_layout = new QVBoxLayout(piano_dialog_);
        piano_window_layout->addWidget(piano_card);

        lyrics_dialog_ = new QDialog(window_);
        lyrics_dialog_->setObjectName(QStringLiteral("lyricsDialog"));
        lyrics_dialog_->setWindowTitle(QStringLiteral("Lyrics — Songwriting notepad"));
        lyrics_dialog_->resize(620, 660);
        auto* lyrics_layout = new QVBoxLayout(lyrics_dialog_);
        auto* lyrics_hint = new QLabel(QStringLiteral("Write while your song plays. Lyrics are saved with your project."), lyrics_dialog_);
        lyrics_hint->setWordWrap(true); lyrics_layout->addWidget(lyrics_hint);
        lyrics_edit_ = new QPlainTextEdit(lyrics_dialog_);
        lyrics_edit_->setObjectName(QStringLiteral("lyricsEditor"));
        lyrics_edit_->setAccessibleName(QStringLiteral("Song lyrics"));
        lyrics_edit_->setPlaceholderText(QStringLiteral("Song title\n\n[Verse 1]\nWrite your lyrics here…\n\n[Chorus]\n"));
        lyrics_edit_->setStyleSheet(QStringLiteral("QPlainTextEdit { background: white; color: #202020; padding: 16px; font-size: 17px; }"));
        lyrics_layout->addWidget(lyrics_edit_, 1);
        auto* lyrics_save = new QPushButton(QStringLiteral("Save project"), lyrics_dialog_);
        lyrics_save->setObjectName(QStringLiteral("lyricsSaveButton")); lyrics_layout->addWidget(lyrics_save);
        QObject::connect(lyrics_edit_, &QPlainTextEdit::textChanged, window_, [this] { markDirty(); });
        QObject::connect(lyrics_save, &QPushButton::clicked, window_, [this] { static_cast<void>(saveProject()); });
        auto* lyrics_shortcut = new QShortcut(QKeySequence::Save, lyrics_dialog_);
        QObject::connect(lyrics_shortcut, &QShortcut::activated, window_, [this] { static_cast<void>(saveProject()); });
        auto* feature_toolbar = new QHBoxLayout();
        auto featureButton = [this, root, feature_toolbar](const QString& text,
            const QString& name, QDialog* dialog) {
            auto* button = new QPushButton(text, root);
            button->setObjectName(name);
            button->setToolTip(QStringLiteral("Open this screen above the Playlist. Closing it keeps your work."));
            feature_toolbar->addWidget(button);
            QObject::connect(button, &QPushButton::clicked, window_, [this, dialog] {
                showFeature(dialog);
            });
            return button;
        };
        open_recorder_button_ = featureButton(QStringLiteral("Hum → MIDI"), QStringLiteral("openRecorderButton"), recording_dialog_);
        featureButton(QStringLiteral("Piano roll"), QStringLiteral("openPianoRollButton"), piano_dialog_);
        featureButton(QStringLiteral("Lyrics"), QStringLiteral("openLyricsButton"), lyrics_dialog_);
        ai_trial_button_ = new QPushButton(QStringLiteral("AI Trial"), root);
        ai_trial_button_->setObjectName(QStringLiteral("openAiTrialButton"));
        ai_trial_button_->setToolTip(QStringLiteral("Prepare a voice-to-instrument trial, then preview and add a generated result."));
        feature_toolbar->addWidget(ai_trial_button_);
        QObject::connect(ai_trial_button_, &QPushButton::clicked, window_, [this] {
            if (guide_->active() || recorder_.state() == core::RecordingState::recording || phrase_processor_.isBusy()) return;
            const auto origin = midi_playback_.isPlaying() || midi_playback_.isPaused() ? midi_playback_.positionSeconds() : playback_start_seconds_;
            stopMelodyPlayback(); playback_.stop(); cue_playback_.stop(); audition_playback_.stop();
            if (!ai_trial_panel_) {
                ai_trial_panel_ = new AiTrialPanel(window_);
                ai_trial_panel_->setHost(
                    [this](const std::vector<hum::vocal::NoteEvent>& notes, double at, std::size_t track, std::uint64_t generation) -> QString {
                        if (closed_ || generation != vocal_project_generation_ || track >= note_editor_.trackCount())
                            return QStringLiteral("The destination project changed. Reopen AI Trial for the current song.");
                        const auto result = vocal::PianoRollInsertAdapter::insert(note_editor_, notes, at, track);
                        if (result != core::NoteEditResult::applied) return QStringLiteral("The MIDI could not be inserted safely. Existing notes are unchanged.");
                        markDirty(); piano_roll_->refresh(true); updateEditorUi(); updateUi(); return {};
                    },
                    [this](core::VoiceClip clip, std::uint64_t generation) -> QString {
                        if (closed_ || generation != vocal_project_generation_)
                            return QStringLiteral("The destination project changed. Reopen AI Trial for the current song.");
                        auto candidate = voice_clips_; candidate.push_back(std::move(clip));
                        if (!core::validVoiceClips(candidate)) return QStringLiteral("The project has reached its voice clip or audio storage limit.");
                        voice_clips_ = std::move(candidate); markDirty(); playlist_->refresh(); updateEditorUi(); updateUi(); return {};
                    });
            }
            ai_trial_panel_->setProject(currentProjectDocument(), playlist_->selectedTrack(), origin, vocal_project_generation_);
            ai_trial_panel_->show(); ai_trial_panel_->raise(); ai_trial_panel_->activateWindow();
        });
        feature_toolbar->addStretch();
        midi_play_button_ = new QPushButton(QStringLiteral("Play"), root);
        midi_play_button_->setObjectName(QStringLiteral("midiPlayButton"));
        midi_pause_button_ = new QPushButton(QStringLiteral("Pause"), root);
        midi_pause_button_->setObjectName(QStringLiteral("midiPauseButton"));
        feature_toolbar->addWidget(export_button_);
        open_recorder_button_->setIcon(toolIcon(ToolIcon::microphone, true));
        window_->findChild<QPushButton*>(QStringLiteral("openPianoRollButton"))->setIcon(toolIcon(ToolIcon::piano));
        midi_play_button_->setIcon(toolIcon(ToolIcon::play, true));
        export_button_->setIcon(toolIcon(ToolIcon::exportMidi, true));
        page->addLayout(feature_toolbar);
        auto* transport = new QHBoxLayout();
        midi_stop_button_ = new QPushButton(QStringLiteral("Stop / return to start"), root);
        midi_stop_button_->setObjectName(QStringLiteral("midiStopButton"));
        metronome_button_ = new QPushButton(QStringLiteral("Metronome"), root);
        metronome_button_->setObjectName(QStringLiteral("metronomeButton"));
        metronome_button_->setCheckable(true);
        metronome_button_->setToolTip(QStringLiteral("Add a local click during playback. With no notes, Play runs a repeating 4/4 click."));
        transport_position_ = new QLabel(QStringLiteral("0.00 s"), root);
        transport_position_->setObjectName(QStringLiteral("transportPosition"));
        transport->addWidget(metronome_button_);
        transport->addWidget(tempo_label);
        transport->addWidget(tempo_spin_);
        transport->addWidget(transport_position_);
        transport->addStretch();
        transport->addWidget(new QLabel(QStringLiteral("Click either timeline to play from that point"), root));
        midi_stop_button_->setIcon(toolIcon(ToolIcon::stop, true));
        metronome_button_->setIcon(toolIcon(ToolIcon::metronome));
        page->addLayout(transport);
        auto* speaker_row = new QHBoxLayout();
        output_label_ = new QLabel(root);
        output_label_->setObjectName(QStringLiteral("outputDeviceLabel"));
        speaker_test_button_ = new QPushButton(QStringLiteral("Test speakers"), root);
        speaker_test_button_->setObjectName(QStringLiteral("testSpeakersButton"));
        speaker_test_button_->setIcon(toolIcon(ToolIcon::speaker));
        speaker_row->addWidget(output_label_);
        speaker_row->addWidget(speaker_test_button_);
        speaker_row->addStretch();
        page->addLayout(speaker_row);
        refreshOutputLabel();

        auto* playlist_card = new QFrame(root);
        playlist_card->setObjectName(QStringLiteral("card"));
        auto* playlist_layout = new QVBoxLayout(playlist_card);
        playlist_layout->setContentsMargins(20, 18, 20, 20);
        playlist_layout->setSpacing(14);
        auto* playlist_heading = new QHBoxLayout();
        auto* playlist_title = new QLabel(QStringLiteral("Playlist"), playlist_card);
        playlist_title->setObjectName(QStringLiteral("cardTitle"));
        playlist_heading->addWidget(playlist_title);
        playlist_heading->addWidget(buildTransport(playlist_card, midi_play_button_, midi_pause_button_, midi_stop_button_, 0));
        playlist_heading->addStretch();
        auto playlistButton = [playlist_card, playlist_heading](const QString& text, const QString& name) {
            auto* button = new QPushButton(text, playlist_card);
            button->setObjectName(name);
            playlist_heading->addWidget(button);
            return button;
        };
        add_track_button_ = playlistButton(QStringLiteral("+ Track"), QStringLiteral("addTrackButton"));
        duplicate_region_button_ = playlistButton(QStringLiteral("Duplicate region"), QStringLiteral("duplicateRegionButton"));
        delete_region_button_ = playlistButton(QStringLiteral("Delete region"), QStringLiteral("deleteRegionButton"));
        voice_record_button_ = playlistButton(QStringLiteral("Record voice"), QStringLiteral("recordVoiceButton"));
        voice_record_button_->setIcon(toolIcon(ToolIcon::microphone));
        voice_record_button_->setToolTip(QStringLiteral("Record actual vocal audio after four beats, with the Playlist playing. Maximum 30 seconds per take."));
        QObject::connect(voice_record_button_, &QPushButton::clicked, window_, [this] {
            if (guide_->active() || recorder_.state() == core::RecordingState::recording) stopRecording();
            else { showFeature(recording_dialog_); startRecording(true); }
        });
        playlist_undo_button_ = playlistButton(QStringLiteral("Undo"), QStringLiteral("playlistUndoButton"));
        playlist_redo_button_ = playlistButton(QStringLiteral("Redo"), QStringLiteral("playlistRedoButton"));
        add_track_button_->setIcon(toolIcon(ToolIcon::addTrack));
        duplicate_region_button_->setIcon(toolIcon(ToolIcon::duplicate));
        delete_region_button_->setIcon(toolIcon(ToolIcon::erase));
        playlist_undo_button_->setIcon(toolIcon(ToolIcon::undo));
        playlist_redo_button_->setIcon(toolIcon(ToolIcon::redo));
        playlist_layout->addLayout(playlist_heading);
        auto* track_controls = new QHBoxLayout();
        selected_track_label_ = new QLabel(playlist_card);
        selected_track_label_->setObjectName(QStringLiteral("selectedTrackLabel"));
        track_controls->addWidget(selected_track_label_);
        playlist_kind_combo_ = new QComboBox(playlist_card);
        playlist_kind_combo_->setObjectName(QStringLiteral("playlistTrackTypeCombo"));
        playlist_kind_combo_->addItem(QStringLiteral("Melody"), static_cast<int>(core::TrackKind::melody));
        playlist_kind_combo_->addItem(QStringLiteral("Chords"), static_cast<int>(core::TrackKind::chords));
        playlist_kind_combo_->setToolTip(QStringLiteral("Type for new recordings on this track. Existing notes stay unchanged."));
        track_controls->addWidget(playlist_kind_combo_);
        playlist_instrument_combo_ = new QComboBox(playlist_card);
        playlist_instrument_combo_->setObjectName(QStringLiteral("playlistTrackInstrumentCombo"));
        for (int i = 0; i < track_instrument_combo_->count(); ++i) {
            playlist_instrument_combo_->addItem(track_instrument_combo_->itemText(i), track_instrument_combo_->itemData(i));
        }
        track_controls->addWidget(playlist_instrument_combo_);
        mute_track_button_ = new QPushButton(QStringLiteral("Mute track"), playlist_card);
        mute_track_button_->setCheckable(true);
        mute_track_button_->setObjectName(QStringLiteral("muteTrackButton"));
        mute_track_button_->setToolTip(QStringLiteral("Mute this track in playback and MIDI export. Individual note mutes are preserved."));
        track_controls->addWidget(mute_track_button_);
        delete_track_button_ = new QPushButton(QStringLiteral("Delete track"), playlist_card);
        delete_track_button_->setObjectName(QStringLiteral("deleteTrackButton"));
        delete_track_button_->setToolTip(QStringLiteral("Delete this track and its regions. Undo restores everything."));
        track_controls->addWidget(delete_track_button_);
        track_controls->addStretch();
        playlist_layout->addLayout(track_controls);
        playlist_status_label_ = new QLabel(playlist_card);
        playlist_status_label_->setObjectName(QStringLiteral("footnote"));
        playlist_status_label_->setWordWrap(true);
        playlist_layout->addWidget(playlist_status_label_);
        workspace_message_label_ = new QLabel(playlist_card);
        workspace_message_label_->setObjectName(QStringLiteral("workspaceMessage"));
        workspace_message_label_->setWordWrap(true);
        workspace_message_label_->setAccessibleName(QStringLiteral("Workspace status"));
        playlist_layout->addWidget(workspace_message_label_);
        playlist_ = new PlaylistWidget(note_editor_, playlist_card);
        playlist_->setVoiceClips(&voice_clips_, [this](std::size_t index, bool remove) {
            if (index >= voice_clips_.size()) return;
            stopMelodyPlayback();
            if (remove) voice_clips_.erase(voice_clips_.begin() + static_cast<std::ptrdiff_t>(index));
            else voice_clips_[index].muted = !voice_clips_[index].muted;
            markDirty(); playlist_->refresh(); updateUi();
        });
        playlist_->setObjectName(QStringLiteral("playlist"));
        playlist_->setMinimumHeight(360);
        playlist_layout->addWidget(playlist_, 1);
        page->addWidget(playlist_card, 1);
        shell->addLayout(page, 1);
        window_->setCentralWidget(root);

        window_->setStyleSheet(QStringLiteral(R"(
            #root { background: #f4f4f5; color: #0b0b0b; }
            QDialog { background: #f4f4f5; }
            #eyebrow { color: #777; font-size: 11px; font-weight: 800; letter-spacing: 2px; }
            #pageTitle { color: #080808; font-size: 29px; font-weight: 800; }
            #subtitle { color: #666; font-size: 14px; }
            #privacyBadge { background: white; border: 1px solid #ddd; border-radius: 17px; padding: 9px 15px; color: #333; font-size: 10px; font-weight: 700; }
            #card { background: #ffffff; border: 1px solid #e4e4e4; border-radius: 18px; }
            #darkCard { background: #080808; border-radius: 18px; }
            #sectionLabel { color: #777; font-size: 11px; font-weight: 800; letter-spacing: 1px; }
            #fieldTitle { color: #222; font-size: 13px; font-weight: 700; }
            #cardTitle { color: #080808; font-size: 20px; font-weight: 800; }
            #statusPanel { background: #f3f3f4; border-radius: 10px; padding: 13px; color: #333; }
            #statusPanel[error="true"] { background: #fff0f0; color: #9c2020; }
            #duration { color: #080808; font-size: 37px; font-weight: 800; padding: 12px; }
            #clipWarning { color: #c52c2c; font-size: 11px; font-weight: 800; }
            QComboBox, QSpinBox, QDoubleSpinBox { background: #f3f3f4; border: 1px solid #ddd; border-radius: 8px; padding: 7px 9px; min-height: 18px; }
            QPushButton { border-radius: 8px; padding: 10px 16px; font-weight: 700; }
            #recordMetronomeButton, #metronomeButton, #testSpeakersButton, #midiStopButton { background: white; color: #080808; border: 1px solid #cfcfcf; }
            #recordMetronomeButton:checked, #metronomeButton:checked { background: #263c30; color: white; border-color: #263c30; }
            #primaryButton { background: #080808; color: white; border: 1px solid #080808; }
            #stopButton { background: #e34848; color: white; border: 1px solid #e34848; }
            #secondaryButton, #addNoteButton, #muteTrackButton, #deleteTrackButton { background: white; color: #080808; border: 1px solid #cfcfcf; }
            #muteTrackButton:checked { background: #263c30; color: white; border-color: #263c30; }
            #openProjectButton, #saveProjectButton { background: white; color: #080808; border: 1px solid #cfcfcf; }
            #midiPlayButton { background: #080808; color: white; border: 1px solid #080808; }
            #magnetButton { background: white; color: #080808; border: 1px solid #8f9690; }
            #magnetButton:checked { background: #31483a; color: white; border-color: #31483a; }
            QPushButton:disabled { background: #e8e8e8; color: #999; border-color: #ddd; }
            #metricCard { background: #f4f4f5; border-radius: 10px; }
            #metricLabel { color: #888; font-size: 9px; font-weight: 800; }
            #metricValue { color: #0b0b0b; font-size: 15px; font-weight: 800; }
            #footnote { color: #888; font-size: 11px; }
            #workspaceMessage { background: #f3f3f4; color: #555; border-radius: 7px; padding: 8px 10px; font-size: 11px; }
            #scaleGuidance { background: #eef3ef; color: #334238; border-radius: 7px; padding: 7px 10px; font-size: 11px; }
            #workflowActive { background: #080808; color: white; border-radius: 9px; padding: 6px; font-weight: 800; }
            #workflowNumber { background: #ededed; color: #777; border-radius: 9px; padding: 6px; font-weight: 800; }
            #workflowTitle { color: #111; font-size: 14px; font-weight: 800; }
            #workflowCopy { color: #888; font-size: 11px; }
            #darkTitle { color: white; font-size: 17px; font-weight: 800; }
            #darkCopy { color: #c7c7c7; font-size: 12px; }
            QProgressBar { background: #ededed; border: 0; border-radius: 5px; height: 10px; text-align: center; color: #555; }
            QProgressBar::chunk { background: #080808; border-radius: 5px; }
            QAbstractScrollArea { background: white; border: 1px solid #dedee1; border-radius: 9px; }
            #openRecorderButton, #openPianoRollButton, #openLyricsButton, #openAiTrialButton, #recordVoiceButton, #lyricsSaveButton,
            #addTrackButton, #duplicateRegionButton, #deleteRegionButton,
            #playlistUndoButton, #playlistRedoButton {
                background: white; color: #080808; border: 1px solid #cfcfcf;
            }
            #openRecorderButton { background: #080808; color: white; border-color: #080808; }
            #primaryButton:disabled, #stopButton:disabled, #secondaryButton:disabled,
            #midiPlayButton:disabled, #openProjectButton:disabled, #saveProjectButton:disabled,
            #addTrackButton:disabled, #duplicateRegionButton:disabled, #deleteRegionButton:disabled,
            #playlistUndoButton:disabled, #playlistRedoButton:disabled {
                background: #e8e8e8; color: #999; border-color: #ddd;
            }
        )"));
    }

    void showFeature(QDialog* dialog) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }

    QLabel* addMetric(QHBoxLayout* row, const QString& name) {
        auto* card = new QFrame(window_);
        card->setObjectName(QStringLiteral("metricCard"));
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 10, 12, 10);
        auto* label = new QLabel(name, card);
        label->setObjectName(QStringLiteral("metricLabel"));
        auto* value = new QLabel(QStringLiteral("—"), card);
        value->setObjectName(QStringLiteral("metricValue"));
        layout->addWidget(label);
        layout->addWidget(value);
        row->addWidget(card, 1);
        return value;
    }

    [[nodiscard]] core::PitchDetectorConfig pitchConfig() const {
        core::PitchDetectorConfig config;
        config.minimum_frequency_hz = 55.0;
        config.input_gate_rms = 0.004F;
        // Keep a comparable window/hop duration on higher-rate microphones.
        const auto rate = recorder_.currentFormat().value_or(core::AudioFormat{48'000, 1}).sample_rate_hz;
        while (!config.isValid(rate) && config.frame_size < 32'768) {
            config.frame_size *= 2;
            config.hop_size *= 2;
        }
        return config;
    }

    void clearPhraseConversion(const QString& message, const bool clear_editor) {
        last_conversion_.reset();
        phrase_progress_->setValue(0);
        phrase_status_label_->setText(message);
        if (clear_editor) {
            static_cast<void>(note_editor_.replaceAll({}));
            piano_roll_->refresh(true);
            updateEditorUi();
        }
    }

    QWidget* buildTransport(QWidget* parent, QPushButton* play, QPushButton* pause, QPushButton* stop, std::size_t index) {
        auto* panel = new QWidget(parent);
        panel->setObjectName(index == 0 ? QStringLiteral("playlistTransport") : QStringLiteral("pianoTransport"));
        auto* row = new QHBoxLayout(panel); row->setContentsMargins(0,0,0,0); row->setSpacing(5);
        auto configure = [&](QPushButton* button, ToolIcon icon, const QString& name) {
            button->setText({}); button->setAccessibleName(name); button->setToolTip(name);
            button->setIcon(toolIcon(icon)); button->setIconSize(QSize(22,22));
            button->setFixedSize(40,40);
            button->setStyleSheet(QStringLiteral("QPushButton {padding: 5px; min-width: 0; min-height: 0; background: white; border: 1px solid #cfd6d0; border-radius: 8px;} QPushButton:hover {background: #eaf2eb;} QPushButton:checked {background: #327aa8; border: 2px solid #245f84;} QPushButton:disabled {background: #eeeeef;}"));
            row->addWidget(button);
        };
        configure(play,ToolIcon::play,QStringLiteral("Play / continue (Space)"));
        configure(pause,ToolIcon::pause,QStringLiteral("Pause (Space)"));
        configure(stop,ToolIcon::stop,QStringLiteral("Stop and return to start"));
        const auto prefix = index == 0 ? QStringLiteral("playlist") : QStringLiteral("piano");
        range_buttons_[index] = new QPushButton(panel);
        range_buttons_[index]->setCheckable(true);
        range_buttons_[index]->setObjectName(prefix + QStringLiteral("SelectLoopButton"));
        configure(range_buttons_[index],ToolIcon::loopRange,QStringLiteral("Select loop area: drag across the grid or ruler. Escape cancels. Changing the area stops playback."));
        loop_buttons_[index] = new QPushButton(panel);
        loop_buttons_[index]->setCheckable(true);
        loop_buttons_[index]->setObjectName(prefix + QStringLiteral("LoopButton"));
        configure(loop_buttons_[index],ToolIcon::loop,QStringLiteral("Enable / disable repeating the selected area (or everything). Press Play after changing."));
        all_loop_buttons_[index] = new QPushButton(panel);
        all_loop_buttons_[index]->setObjectName(prefix + QStringLiteral("LoopAllButton"));
        configure(all_loop_buttons_[index],ToolIcon::loopAll,QStringLiteral("Loop all: clear the selected area and repeat the entire Playlist"));
        return panel;
    }

    std::optional<core::LoopRange> activeLoopRange() const {
        if (!loop_enabled_) { return std::nullopt; }
        if (loop_range_) { return loop_range_; }
        double end = 0;
        for (const auto& note : note_editor_.notes()) { end = std::max(end, note.start_seconds + note.duration_seconds); }
        for (const auto& clip : voice_clips_) end = std::max(end, clip.start_seconds + clip.duration());
        if (end <= 0 && metronome_button_->isChecked()) { end = 240.0 / tempo_spin_->value(); }
        const core::LoopRange range{0, end};
        return range.isValid() ? std::optional{range} : std::nullopt;
    }

    void setLoopTool(bool enabled) {
        loop_tool_ = enabled;
        playlist_->setLoopTool(enabled); piano_roll_->setLoopTool(enabled);
        updateUi();
    }

    void updateProgressionUi() {
        const auto& scale = note_editor_.scale();
        for (std::uint8_t degree = 0; degree < 7; ++degree) {
            degree_buttons_[degree]->setText(QString::fromStdString(core::chordDegreeName(scale, degree)));
        }
        const auto degrees = core::parseChordDegrees(progression_edit_->text().toStdString());
        if (!degrees) {
            progression_preview_->setText(QStringLiteral("Choose chords above or enter I–VII / 1–7, separated by spaces. Chord quality follows the selected key."));
            return;
        }
        QStringList names;
        for (const auto degree : *degrees) { names << QString::fromStdString(core::chordDegreeName(scale, degree)); }
        progression_preview_->setText(names.join(QStringLiteral(" → ")));
    }

    void addChordProgression() {
        const auto degrees = core::parseChordDegrees(progression_edit_->text().toStdString());
        if (!degrees) { return; }
        const auto notes = core::makeChordProgression(*degrees, note_editor_.scale(),
            chord_beats_spin_->value() * 60.0 / tempo_spin_->value());
        if (notes.empty()) { chord_status_label_->setText(QStringLiteral("This progression is too long. Reduce the chord count or beats per chord.")); return; }
        if (!ensureConversionTrack(true)) { updateUi(); return; }
        const auto track = playlist_->selectedTrack();
        double offset = 0.0;
        for (const auto& region : note_editor_.regions()) {
            if (region.track == track) {
                for (const auto i : region.note_indices) {
                    offset = std::max(offset, note_editor_.notes()[i].start_seconds + note_editor_.notes()[i].duration_seconds);
                }
            }
        }
        stopMelodyPlayback();
        if (note_editor_.appendRegion(notes, offset, track) != core::NoteEditResult::applied) {
            chord_status_label_->setText(QStringLiteral("The progression could not be added. Existing notes are unchanged.")); return;
        }
        markDirty(); piano_roll_->refresh(true); updateEditorUi(); updateUi();
        chord_status_label_->setText(QStringLiteral("Added %1 chords to Track %2. Open Piano roll to edit, choose Piano or Guitar, and play.")
            .arg(degrees->size()).arg(track + 1));
        workspace_message_label_->setText(chord_status_label_->text());
    }

    bool ensureConversionTrack(const bool make_chords) {
        const auto kind = make_chords ? core::TrackKind::chords : core::TrackKind::melody;
        if (note_editor_.tracks()[playlist_->selectedTrack()].kind == kind) { return true; }
        for (std::size_t i = 0; i < note_editor_.trackCount(); ++i) {
            if (note_editor_.tracks()[i].kind == kind) { playlist_->selectTrack(i); return true; }
        }
        if (note_editor_.addTrack({kind, core::InstrumentVoice::grand_piano}) != core::NoteEditResult::applied) {
            phrase_status_label_->setText(QStringLiteral("The Playlist has reached its track limit. Select a track of the requested type."));
            chord_status_label_->setText(phrase_status_label_->text());
            return false;
        }
        playlist_->selectTrack(note_editor_.trackCount() - 1);
        markDirty();
        updateEditorUi();
        return true;
    }

    void beginPhraseConversion(const bool make_chords = false) {
        const auto recording = recorder_.recording();
        if (!recording || recording->sampleCount() == 0) {
            phrase_status_label_->setText(QStringLiteral(
                "No captured samples are available for conversion."));
            chord_status_label_->setText(phrase_status_label_->text());
            updateUi();
            return;
        }
        if (phrase_processor_.isBusy()) {
            return;
        }
        if (!ensureConversionTrack(make_chords)) { updateUi(); return; }
        const auto conversion_scale = note_editor_.scale();
        const auto conversion_scale_name = selectedScaleName();
        const auto connect_roots = connect_chords_check_->isChecked();
        stopMelodyPlayback(QStringLiteral(
            "Melody playback stopped while the recording is re-transcribed."));
        phrase_progress_->setValue(0);
        phrase_status_label_->setText(make_chords
            ? QStringLiteral("Finding hummed chord roots… 0%")
            : QStringLiteral("Analyzing the saved audio after recording… 0%"));
        const auto conversion_track = playlist_->selectedTrack();
        const auto conversion_offset = recorded_origin_;
        if (!phrase_processor_.convert(
                recording,
                pitchConfig(),
                core::NoteExtractionConfig{},
                tempo_spin_->value(),
                ticks_per_quarter_,
                [this, conversion_track, conversion_offset, make_chords, conversion_scale, conversion_scale_name, connect_roots](PhraseWorkerResult result) {
                    phrase_progress_->setValue(100);
                    last_conversion_ = std::move(result.conversion);
                    if (last_conversion_->status == core::PhraseConversionStatus::success) {
                        auto notes = last_conversion_->notes;
                        if (make_chords) {
                            notes = core::makeDiatonicTriads(notes, conversion_scale, connect_roots);
                            if (notes.empty()) {
                                chord_status_label_->setText(QStringLiteral("No valid chord roots were found in the selected key."));
                                updateUi(); return;
                            }
                        }
                        const auto added = note_editor_.appendRegion(notes,
                            conversion_offset, conversion_track);
                        if (added != core::NoteEditResult::applied) {
                            phrase_status_label_->setText(QStringLiteral("The converted phrase could not be added safely. Existing regions are unchanged."));
                            chord_status_label_->setText(phrase_status_label_->text());
                            workspace_message_label_->setText(phrase_status_label_->text());
                            updateEditorUi(); updateUi(); return;
                        }
                        piano_roll_->refresh(true);
                        markDirty();
                        phrase_status_label_->setText(QStringLiteral(
                            "%1 detected note(s) · %2 raw frames · %3 ms. A new region is ready in the Playlist.")
                            .arg(last_conversion_->notes.size())
                            .arg(last_conversion_->raw_pitch_frames.size())
                            .arg(result.processing_milliseconds, 0, 'f', 1));
                        if (make_chords) {
                            phrase_status_label_->setText(QStringLiteral(
                                "%1 hummed root(s) became %2 chord note(s) in %3. Open Piano roll to edit and play them.")
                                .arg(last_conversion_->notes.size()).arg(notes.size())
                                .arg(conversion_scale_name));
                        }
                    } else if (last_conversion_->status == core::PhraseConversionStatus::empty) {
                        phrase_status_label_->setText(QStringLiteral("No notes found; existing Playlist regions are retained: %1")
                            .arg(QString::fromStdString(last_conversion_->message)));
                    } else {
                        phrase_status_label_->setText(QStringLiteral("%1: %2")
                            .arg(QString::fromLatin1(core::phraseConversionStatusText(
                                last_conversion_->status)))
                            .arg(QString::fromStdString(last_conversion_->message)));
                    }
                    chord_status_label_->setText(phrase_status_label_->text());
                    updateEditorUi();
                    updateUi();
                    workspace_message_label_->setText(phrase_status_label_->text());
                })) {
            phrase_status_label_->setText(QStringLiteral(
                "A phrase conversion is already running."));
        }
        updateUi();
    }

    void cancelPhraseConversion() {
        if (!phrase_processor_.isBusy()) {
            return;
        }
        phrase_processor_.cancel();
        phrase_status_label_->setText(QStringLiteral(
            "Conversion cancelled. The original recording remains in memory."));
        chord_status_label_->setText(phrase_status_label_->text());
        workspace_message_label_->setText(phrase_status_label_->text());
        updateUi();
    }

    [[nodiscard]] core::ProjectDocument currentProjectDocument() const {
        core::ProjectDocument project;
        project.tempo_bpm = tempo_spin_->value();
        project.ticks_per_quarter = ticks_per_quarter_;
        project.root_pitch_class = static_cast<std::uint8_t>(
            root_pitch_combo_->currentData().toUInt());
        project.scale_mode = selectedScaleMode();
        project.magnet_enabled = note_editor_.magnetEnabled();
        project.rhythm_snap = piano_roll_->rhythmSnap();
        project.metronome_enabled = metronome_button_->isChecked();
        project.grid_division = rhythm_snap_combo_->currentData().toUInt();
        project.canonical_notes = note_editor_.notes();
        project.playlist_regions = note_editor_.regions();
        project.track_count = note_editor_.trackCount();
        project.tracks = note_editor_.tracks();
        project.lyrics = lyrics_edit_->toPlainText().toUtf8().toStdString();
        project.voice_clips = voice_clips_;
        return project;
    }

    void markDirty() {
        if (applying_project_) {
            return;
        }
        dirty_ = true;
        updateWindowTitle();
    }

    void updateWindowTitle() {
        const auto name = project_path_.isEmpty()
            ? QStringLiteral("Untitled project") : QFileInfo(project_path_).fileName();
        if (project_name_label_) {
            project_name_label_->setText(QStringLiteral("%1%2")
                .arg(name)
                .arg(dirty_ ? QStringLiteral(" · Unsaved") : QString{}));
        }
        window_->setWindowTitle(QStringLiteral("%1%2 — Hum-to-MIDI Music Sketchpad")
            .arg(name)
            .arg(dirty_ ? QStringLiteral(" *") : QString{}));
    }

    [[nodiscard]] bool saveProjectAs() {
        auto destination = QFileDialog::getSaveFileName(
            window_,
            QStringLiteral("Save Hum-to-MIDI project"),
            project_path_.isEmpty() ? QStringLiteral("untitled.humproj") : project_path_,
            QStringLiteral("Hum-to-MIDI projects (*.humproj)"));
        if (destination.isEmpty()) {
            return false;
        }
        if (!destination.endsWith(QStringLiteral(".humproj"), Qt::CaseInsensitive)) {
            destination += QStringLiteral(".humproj");
        }
        const auto result = ProjectFileStore::save(destination, currentProjectDocument());
        if (!result) {
            QMessageBox::critical(window_, QStringLiteral("Project save failed"), result.message);
            phrase_status_label_->setText(result.message);
            return false;
        }
        project_path_ = destination;
        dirty_ = false;
        updateWindowTitle();
        phrase_status_label_->setText(QStringLiteral(
            "Project saved safely: %1. Notes, settings, lyrics and Playlist voice recordings are stored.")
            .arg(destination));
        workspace_message_label_->setText(phrase_status_label_->text());
        return true;
    }

    [[nodiscard]] bool saveProject() {
        if (project_path_.isEmpty()) {
            return saveProjectAs();
        }
        const auto result = ProjectFileStore::save(project_path_, currentProjectDocument());
        if (!result) {
            QMessageBox::critical(window_, QStringLiteral("Project save failed"), result.message);
            phrase_status_label_->setText(result.message);
            return false;
        }
        dirty_ = false;
        updateWindowTitle();
        phrase_status_label_->setText(QStringLiteral(
            "Project saved safely: %1. Lyrics and Playlist voice audio are included.").arg(project_path_));
        workspace_message_label_->setText(phrase_status_label_->text());
        return true;
    }

    [[nodiscard]] bool confirmDiscardChanges(const QString& action) {
        if (!dirty_) {
            return true;
        }
        const auto choice = QMessageBox::warning(
            window_,
            QStringLiteral("Unsaved project changes"),
            QStringLiteral("Save your notes, lyrics and Playlist voice recordings before you %1?").arg(action),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Save) {
            return saveProject();
        }
        return choice == QMessageBox::Discard;
    }

    void loadProject() {
        if (!confirmDiscardChanges(QStringLiteral("open another project"))) {
            return;
        }
        const auto source = QFileDialog::getOpenFileName(
            window_,
            QStringLiteral("Open Hum-to-MIDI project"),
            {},
            QStringLiteral("Hum-to-MIDI projects (*.humproj)"));
        if (source.isEmpty()) {
            return;
        }
        const auto loaded = ProjectFileStore::load(source);
        if (!loaded) {
            const auto title = loaded.status == ProjectStoreStatus::unsupported_version
                ? QStringLiteral("Unsupported project version")
                : QStringLiteral("Project could not be opened");
            QMessageBox::critical(window_, title, loaded.message);
            phrase_status_label_->setText(loaded.message);
            return;
        }

        stopMelodyPlayback();
        playback_.stop();
        cue_playback_.stop();
        phrase_processor_.cancel();
        if (sing_panel_) { sing_panel_->reject(); }
        if (ai_trial_panel_) { ai_trial_panel_->reject(); }
        guide_->stop(); static_cast<void>(recorder_.stop()); armed_capture_.stop();
        ++vocal_project_generation_;
        applying_project_ = true;
        const auto& project = *loaded.project;
        voice_clips_ = project.voice_clips;
        { const QSignalBlocker blocker(lyrics_edit_); lyrics_edit_->setPlainText(QString::fromStdString(project.lyrics)); }
        {
            const QSignalBlocker root_blocker(root_pitch_combo_);
            const QSignalBlocker scale_blocker(scale_mode_combo_);
            const QSignalBlocker magnet_blocker(magnet_button_);
            const QSignalBlocker snap_blocker(rhythm_snap_combo_);
            const QSignalBlocker tempo_blocker(tempo_spin_);
            const QSignalBlocker metronome_blocker(metronome_button_);
            metronome_button_->setChecked(project.metronome_enabled);
            ticks_per_quarter_ = project.ticks_per_quarter;
            root_pitch_combo_->setCurrentIndex(root_pitch_combo_->findData(project.root_pitch_class));
            scale_mode_combo_->setCurrentIndex(scale_mode_combo_->findData(
                static_cast<int>(project.scale_mode)));
            magnet_button_->setChecked(project.magnet_enabled);
            auto snap_index = 0;
            if (project.rhythm_snap.enabled) {
                snap_index = project.grid_division > 0
                    ? rhythm_snap_combo_->findData(project.grid_division) : -1;
                if (snap_index < 0) {
                    rhythm_snap_combo_->addItem(
                        QStringLiteral("Custom · %1 s").arg(
                            project.rhythm_snap.interval_seconds, 0, 'f', 3),
                        0);
                    snap_index = rhythm_snap_combo_->count() - 1;
                    rhythm_snap_combo_->setItemData(snap_index,
                        project.rhythm_snap.interval_seconds, Qt::UserRole + 1);
                }
            }
            rhythm_snap_combo_->setCurrentIndex(snap_index);
            tempo_spin_->setValue(project.tempo_bpm);
        }
        static_cast<void>(note_editor_.setScale(core::makeScaleDefinition(
            project.root_pitch_class, project.scale_mode)));
        note_editor_.setMagnetEnabled(project.magnet_enabled);
        static_cast<void>(note_editor_.replaceAll(project.canonical_notes,
            project.playlist_regions, project.track_count, project.tracks));
        piano_roll_->setTempo(project.tempo_bpm);
        piano_roll_->setRhythmSnap(project.rhythm_snap);
        playlist_->setRhythmSnap(project.rhythm_snap);
        editor_toolbar_->setSnapEnabled(project.rhythm_snap.enabled);
        if (project.rhythm_snap.enabled) { last_snap_index_ = rhythm_snap_combo_->currentIndex(); }
        { const QSignalBlocker blocker(record_tempo_spin_); record_tempo_spin_->setValue(project.tempo_bpm); }
        piano_roll_->refresh(true);
        static_cast<void>(recorder_.clearRecording()); recorded_origin_ = 0;
        updateMetadata();
        last_conversion_.reset();
        project_path_ = source;
        playback_start_seconds_ = 0.0; showTransportPosition(0.0);
        loop_range_.reset(); loop_enabled_ = false; loop_tool_ = false;
        playlist_->setLoopTool(false); piano_roll_->setLoopTool(false);
        dirty_ = false;
        applying_project_ = false;
        phrase_progress_->setValue(0);
        phrase_status_label_->setText(QStringLiteral(
            "Project loaded: %1. Notes, lyrics and Playlist voice recordings are restored. Hum → MIDI source takes are session-only.")
            .arg(source));
        updateScaleGuidanceUi();
        updateEditorUi();
        updateWindowTitle();
        updateUi();
        workspace_message_label_->setText(phrase_status_label_->text());
    }

    void showTransportPosition(double seconds) {
        transport_position_->setText(QStringLiteral("%1 s").arg(seconds, 0, 'f', 2));
        piano_position_->setText(transport_position_->text());
        piano_roll_->setPlayheadSeconds(seconds);
        playlist_->setPlayheadSeconds(seconds);
    }

    void stopMelodyPlayback(const QString& message = {}, const bool reset_position = false) {
        audition_playback_.stop();
        const auto was_active = midi_playback_.isPlaying() || midi_playback_.isPreparing()
            || midi_playback_.isPaused();
        if (reset_position) { playback_start_seconds_ = 0.0; }
        else if (midi_playback_.isPlaying() || midi_playback_.isPaused()) {
            playback_start_seconds_ = midi_playback_.positionSeconds();
        }
        midi_playback_.stop();
        showTransportPosition(playback_start_seconds_);
        if (was_active && !message.isEmpty()) {
            editor_status_label_->setText(message);
            workspace_message_label_->setText(message);
        }
    }

    void toggleMidiPlayback() {
        if (recorder_.state() == core::RecordingState::recording || guide_->active() || phrase_processor_.isBusy()) { return; }
        if (midi_playback_.isPreparing()) {
            stopMelodyPlayback();
        } else if (midi_playback_.isPlaying()) {
            midi_playback_.pause();
            showTransportPosition(midi_playback_.positionSeconds());
        } else if (midi_playback_.isPaused()) {
            midi_playback_.resume();
        } else {
            if (note_editor_.projectedNotes().empty() && voice_clips_.empty() && !metronome_button_->isChecked()) {
                return;
            }
            playback_.stop();
            cue_playback_.stop();
            std::vector<core::InstrumentVoice> voices(note_editor_.notes().size(), core::InstrumentVoice::grand_piano);
            for (const auto& region : note_editor_.regions()) {
                for (const auto index : region.note_indices) { voices[index] = note_editor_.tracks()[region.track].instrument; }
            }
            midi_playback_.play(note_editor_.projectedNotes(),
                [this](const QString& message, const bool error) {
                    editor_status_label_->setText(message);
                    workspace_message_label_->setText(message);
                    showTransportPosition(midi_playback_.positionSeconds());
                    if (error) { setStatus(message, true); }
                    updateUi();
                }, playback_start_seconds_, tempo_spin_->value(), metronome_button_->isChecked(), std::move(voices), activeLoopRange(), voice_clips_);
        }
        updateUi();
    }

    void applyRhythmSnap() {
        if (rhythm_snap_combo_->currentIndex() > 0) { last_snap_index_ = rhythm_snap_combo_->currentIndex(); }
        const auto division = rhythm_snap_combo_->currentData().toUInt();
        const auto custom = rhythm_snap_combo_->currentData(Qt::UserRole + 1).toDouble();
        const auto interval = division > 0 ? 240.0 / (tempo_spin_->value() * division)
            : (custom > 0.0 ? custom : 0.125);
        piano_roll_->setRhythmSnap({rhythm_snap_combo_->currentIndex() > 0, interval});
        playlist_->setRhythmSnap(piano_roll_->rhythmSnap());
        editor_toolbar_->setSnapEnabled(piano_roll_->rhythmSnap().enabled);
    }

    void exportMidi() {
        if (note_editor_.notes().empty()) {
            phrase_status_label_->setText(QStringLiteral(
                "Add or convert at least one valid note before exporting."));
            return;
        }
        auto sequence = note_editor_.exportSequence(tempo_spin_->value(), ticks_per_quarter_);
        if (!sequence) {
            phrase_status_label_->setText(QStringLiteral(
                "The edited notes could not be represented safely as MIDI."));
            return;
        }
        auto destination = QFileDialog::getSaveFileName(
            window_,
            QStringLiteral("Export MIDI"),
            QStringLiteral("hummed-phrase.mid"),
            QStringLiteral("MIDI files (*.mid *.midi)"));
        if (destination.isEmpty()) {
            return;
        }
        if (!destination.endsWith(QStringLiteral(".mid"), Qt::CaseInsensitive)
            && !destination.endsWith(QStringLiteral(".midi"), Qt::CaseInsensitive)) {
            destination += QStringLiteral(".mid");
        }
        core::StandardMidiFileExporter exporter;
        const auto status = exporter.exportFile(
            *sequence,
            std::filesystem::path(destination.toStdU16String()));
        if (status == core::ServiceStatus::ok) {
            phrase_status_label_->setText(note_editor_.magnetEnabled()
                ? QStringLiteral("MIDI note instructions saved at %1 BPM with the audible/visible %2 Magnet pitches: %3. This file contains no instrument audio; canonical pitches are retained.")
                    .arg(tempo_spin_->value(), 0, 'f', 1)
                    .arg(selectedScaleName())
                    .arg(destination)
                : QStringLiteral("MIDI note instructions saved at %1 BPM with the audible/visible canonical pitches: %2. This file contains no built-in instrument audio.")
                    .arg(tempo_spin_->value(), 0, 'f', 1)
                    .arg(destination));
        } else {
            phrase_status_label_->setText(QStringLiteral(
                "MIDI export failed. Check the destination and try again."));
        }
        workspace_message_label_->setText(phrase_status_label_->text());
    }

    void wireEvents() {
        QObject::connect(open_project_button_, &QPushButton::clicked, window_, [this] { loadProject(); });
        QObject::connect(save_project_button_, &QPushButton::clicked, window_, [this] {
            static_cast<void>(saveProject());
        });
        QObject::connect(recording_dialog_, &QDialog::finished, window_, [this] {
            if (guide_->active() || recorder_.state() == core::RecordingState::recording) { stopRecording(); }
        });
        QObject::connect(record_button_, &QPushButton::clicked, window_, [this] { startRecording(); });
        QObject::connect(stop_button_, &QPushButton::clicked, window_, [this] { stopRecording(); });
        QObject::connect(play_button_, &QPushButton::clicked, window_, [this] { togglePlayback(); });
        QObject::connect(convert_button_, &QPushButton::clicked, window_,
            [this] { beginPhraseConversion(conversion_tabs_->currentIndex() == 1); });
        QObject::connect(cancel_conversion_button_, &QPushButton::clicked, window_,
            [this] { cancelPhraseConversion(); });
        QObject::connect(export_button_, &QPushButton::clicked, window_,
            [this] { exportMidi(); });
        QObject::connect(midi_play_button_, &QPushButton::clicked, window_,
            [this] { toggleMidiPlayback(); });
        QObject::connect(midi_pause_button_, &QPushButton::clicked, window_, [this] {
            if (midi_playback_.isPlaying()) { toggleMidiPlayback(); }
        });
        for (std::size_t i = 0; i < 2; ++i) {
            QObject::connect(range_buttons_[i], &QPushButton::toggled, window_, [this](bool enabled) {
                setLoopTool(enabled);
                if (enabled) { workspace_message_label_->setText(QStringLiteral("Drag across the grid or ruler to choose a loop area. Escape cancels the drag.")); }
            });
            QObject::connect(loop_buttons_[i], &QPushButton::toggled, window_, [this](bool enabled) {
                stopMelodyPlayback(); loop_enabled_ = enabled; updateUi();
                workspace_message_label_->setText(enabled ? QStringLiteral("Loop enabled. Press Play to repeat.") : QStringLiteral("Loop disabled."));
            });
            QObject::connect(all_loop_buttons_[i], &QPushButton::clicked, window_, [this] {
                stopMelodyPlayback(); loop_range_.reset(); loop_enabled_ = true; setLoopTool(false);
                workspace_message_label_->setText(QStringLiteral("Loop all enabled. Press Play to repeat the entire Playlist."));
            });
        }
        auto loop_changed = [this](core::LoopRange range) {
            stopMelodyPlayback(); loop_range_ = range; loop_enabled_ = true;
            playback_start_seconds_ = range.start_seconds; showTransportPosition(playback_start_seconds_);
            setLoopTool(false);
            workspace_message_label_->setText(QStringLiteral("Loop %1–%2 s selected. Press Play to repeat it.")
                .arg(range.start_seconds,0,'f',2).arg(range.end_seconds,0,'f',2));
        };
        piano_roll_->setLoopChangedCallback(loop_changed); playlist_->setLoopChangedCallback(loop_changed);
        QObject::connect(piano_play_button_, &QPushButton::clicked, window_, [this] { toggleMidiPlayback(); });
        QObject::connect(piano_pause_button_, &QPushButton::clicked, window_, [this] {
            if (midi_playback_.isPlaying()) { toggleMidiPlayback(); }
        });
        QObject::connect(piano_stop_button_, &QPushButton::clicked, window_, [this] {
            stopMelodyPlayback(QStringLiteral("Playback stopped."), true); updateUi();
        });
        QObject::connect(undo_button_, &QPushButton::clicked, window_,
            [this] { piano_roll_->undo(); });
        QObject::connect(redo_button_, &QPushButton::clicked, window_,
            [this] { piano_roll_->redo(); });
        QObject::connect(playlist_undo_button_, &QPushButton::clicked, window_, [this] { piano_roll_->undo(); });
        QObject::connect(playlist_redo_button_, &QPushButton::clicked, window_, [this] { piano_roll_->redo(); });
        QObject::connect(duplicate_region_button_, &QPushButton::clicked, window_, [this] { playlist_->duplicateSelected(); });
        QObject::connect(delete_region_button_, &QPushButton::clicked, window_, [this] { playlist_->deleteSelected(); });
        QObject::connect(add_track_button_, &QPushButton::clicked, window_, [this] {
            QDialog dialog(window_);
            dialog.setObjectName(QStringLiteral("createTrackDialog"));
            dialog.setWindowTitle(QStringLiteral("Create Playlist track"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* type = new QComboBox(&dialog);
            type->setObjectName(QStringLiteral("trackTypeCombo"));
            type->addItem(QStringLiteral("Melody"), static_cast<int>(core::TrackKind::melody));
            type->addItem(QStringLiteral("Chord progression"), static_cast<int>(core::TrackKind::chords));
            auto* instrument = new QComboBox(&dialog);
            for (const auto voice : {core::InstrumentVoice::grand_piano, core::InstrumentVoice::electric_piano,
                     core::InstrumentVoice::acoustic_guitar, core::InstrumentVoice::electric_guitar,
                     core::InstrumentVoice::strings}) {
                instrument->addItem(QString::fromLatin1(core::instrumentVoiceName(voice)), static_cast<int>(voice));
            }
            layout->addWidget(new QLabel(QStringLiteral("Track type"), &dialog));
            layout->addWidget(type);
            layout->addWidget(new QLabel(QStringLiteral("Instrument"), &dialog));
            layout->addWidget(instrument);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            if (dialog.exec() != QDialog::Accepted) { return; }
            const auto track = core::TrackDefinition{
                static_cast<core::TrackKind>(type->currentData().toInt()),
                static_cast<core::InstrumentVoice>(instrument->currentData().toInt())};
            if (note_editor_.addTrack(track) == core::NoteEditResult::applied) {
                playlist_->selectTrack(note_editor_.trackCount() - 1);
                markDirty(); updateEditorUi(); updateUi();
            }
        });
        QObject::connect(chord_convert_button_, &QPushButton::clicked, window_, [this] {
            beginPhraseConversion(true);
        });
        QObject::connect(chord_cancel_button_, &QPushButton::clicked, window_, [this] { cancelPhraseConversion(); });
        QObject::connect(melody_snap_check_, &QCheckBox::toggled, window_, [this](bool enabled) {
            magnet_button_->setChecked(enabled);
        });
        QObject::connect(progression_edit_, &QLineEdit::textChanged, window_, [this] { updateProgressionUi(); updateUi(); });
        QObject::connect(chord_beats_spin_, &QSpinBox::valueChanged, window_, [this] { updateProgressionUi(); });
        QObject::connect(progression_add_button_, &QPushButton::clicked, window_, [this] { addChordProgression(); });
        auto track_changed = [this](core::NoteEditResult result) {
            if (result != core::NoteEditResult::applied) { return; }
            stopMelodyPlayback(); markDirty(); playlist_->refresh(); piano_roll_->refresh(); updateEditorUi(); updateUi();
        };
        for (auto* combo : {track_instrument_combo_, playlist_instrument_combo_}) {
            QObject::connect(combo, &QComboBox::currentIndexChanged, window_, [this, combo, track_changed] {
                track_changed(note_editor_.setTrackInstrument(playlist_->selectedTrack(),
                    static_cast<core::InstrumentVoice>(combo->currentData().toInt())));
            });
        }
        QObject::connect(playlist_kind_combo_, &QComboBox::currentIndexChanged, window_, [this, track_changed] {
            track_changed(note_editor_.setTrackKind(playlist_->selectedTrack(),
                static_cast<core::TrackKind>(playlist_kind_combo_->currentData().toInt())));
        });
        QObject::connect(mute_track_button_, &QPushButton::toggled, window_, [this, track_changed](bool muted) {
            track_changed(note_editor_.setTrackMuted(playlist_->selectedTrack(), muted));
        });
        QObject::connect(delete_track_button_, &QPushButton::clicked, window_, [this, track_changed] {
            const auto track = playlist_->selectedTrack();
            track_changed(note_editor_.deleteTrack(track));
            playlist_->selectTrack(std::min(track, note_editor_.trackCount() - 1));
            workspace_message_label_->setText(QStringLiteral("Track deleted. Undo restores the track and its notes."));
        });
        QObject::connect(show_other_tracks_, &QCheckBox::toggled, window_, [this](bool show) {
            piano_roll_->setShowOtherTracks(show);
        });
        piano_roll_->setAuditionCallback([this](std::uint8_t pitch) {
            if (recorder_.state() == core::RecordingState::recording || guide_->active() || phrase_processor_.isBusy()) { return; }
            const auto voice = note_editor_.tracks()[playlist_->selectedTrack()].instrument;
            audition_playback_.play({{pitch, 90, 0.0, .8, 0}}, [this](const QString& text, bool error) {
                if (error) { editor_status_label_->setText(text); }
            }, 0, tempo_spin_->value(), false, {voice});
        });
        QObject::connect(chord_key_combo_, &QComboBox::currentIndexChanged, window_, [this](int index) {
            root_pitch_combo_->setCurrentIndex(index);
        });
        QObject::connect(chord_scale_combo_, &QComboBox::currentIndexChanged, window_, [this](int index) {
            scale_mode_combo_->setCurrentIndex(index);
        });
        QObject::connect(conversion_tabs_, &QTabWidget::currentChanged, window_, [this](int index) {
            const auto kind = index == 1 ? core::TrackKind::chords : core::TrackKind::melody;
            if (note_editor_.tracks()[playlist_->selectedTrack()].kind == kind) { return; }
            for (std::size_t i = 0; i < note_editor_.trackCount(); ++i) {
                if (note_editor_.tracks()[i].kind == kind) { playlist_->selectTrack(i); break; }
            }
        });
        QObject::connect(add_note_button_, &QPushButton::clicked, window_,
            [this] {
                showFeature(piano_dialog_);
                piano_roll_->addNoteAtViewportCenter();
            });
        QObject::connect(delete_note_button_, &QPushButton::clicked, window_,
            [this] { piano_roll_->deleteSelected(); });
        QObject::connect(quantize_button_, &QPushButton::clicked, window_, [this] {
            const auto snap = piano_roll_->rhythmSnap();
            const auto interval = snap.enabled ? snap.interval_seconds : 60.0 / tempo_spin_->value();
            if (note_editor_.quantizeSelected(interval) == core::NoteEditResult::applied) {
                stopMelodyPlayback(QStringLiteral("Playback stopped because selected notes were quantised."));
                piano_roll_->refresh(); markDirty(); updateEditorUi(); updateUi();
            }
        });
        QObject::connect(root_pitch_combo_, &QComboBox::currentIndexChanged, window_,
            [this](const int) { applyScaleSettings(); });
        QObject::connect(scale_mode_combo_, &QComboBox::currentIndexChanged, window_,
            [this](const int) { applyScaleSettings(); });
        QObject::connect(magnet_button_, &QPushButton::toggled, window_,
            [this](const bool enabled) {
                stopMelodyPlayback(QStringLiteral(
                    "Melody playback stopped because the audible pitch projection changed."));
                note_editor_.setMagnetEnabled(enabled);
                piano_roll_->refresh();
                updateScaleGuidanceUi();
                updateEditorUi();
                markDirty();
                updateUi();
            });
        QObject::connect(horizontal_zoom_slider_, &QSlider::valueChanged, window_,
            [this](const int value) {
                piano_roll_->setHorizontalZoom(value);
                playlist_->setHorizontalZoom(value);
            });
        QObject::connect(vertical_zoom_slider_, &QSlider::valueChanged, window_,
            [this](const int value) { piano_roll_->setVerticalZoom(value); });
        editor_toolbar_->onToolChanged([this](PianoRollWidget::Tool tool) { setLoopTool(false); piano_roll_->setTool(tool); });
        editor_toolbar_->onSnapChanged([this](bool enabled) {
            rhythm_snap_combo_->setCurrentIndex(enabled ? last_snap_index_ : 0);
        });
        QObject::connect(record_metronome_button_, &QPushButton::toggled, window_, [this](bool enabled) {
            metronome_button_->setChecked(enabled);
        });
        QObject::connect(record_tempo_spin_, &QDoubleSpinBox::valueChanged, window_, [this](double bpm) {
            tempo_spin_->setValue(bpm);
        });
        QObject::connect(speaker_test_button_, &QPushButton::clicked, window_, [this] {
            stopMelodyPlayback();playback_.stop();cue_playback_.stop();refreshOutputLabel();
            cue_playback_.play({{69,48,0,.4,0},{72,48,.55,.4,0}}, [this](const QString& message,bool error) {
                setStatus(error ? message : QStringLiteral("Speaker test: %1").arg(message), error);
                updateUi();
            });
        });
        auto* outputs = new QMediaDevices(window_);
        QObject::connect(outputs, &QMediaDevices::audioOutputsChanged, window_, [this] { refreshOutputLabel(); });
        QObject::connect(midi_stop_button_, &QPushButton::clicked, window_, [this] {
            stopMelodyPlayback(QStringLiteral("Playback stopped."), true); cue_playback_.stop(); updateUi();
        });
        QObject::connect(metronome_button_, &QPushButton::toggled, window_, [this] {
            stopMelodyPlayback();
            cue_playback_.stop();
            record_beat_label_->setText(QStringLiteral("4-beat count-in · then record with tracks"));
            markDirty(); updateUi();
        });
        QObject::connect(rhythm_snap_combo_, &QComboBox::currentIndexChanged, window_,
            [this](const int) { applyRhythmSnap(); markDirty(); });
        QObject::connect(tempo_spin_, &QDoubleSpinBox::valueChanged, window_,
            [this](const double value) {
                stopMelodyPlayback(QStringLiteral("Tempo changed. Press Play to hear it."));
                playlist_->setTempo(value);
                piano_roll_->setTempo(value);
                { const QSignalBlocker blocker(record_tempo_spin_); record_tempo_spin_->setValue(value); }
                applyRhythmSnap();
                markDirty(); updateUi();
            });
        const auto seek = [this](double seconds) {
            if (midi_playback_.isPreparing()) {
                stopMelodyPlayback();
            }
            if (midi_playback_.isPlaying() || midi_playback_.isPaused()) {
                midi_playback_.seek(seconds);
                seconds = midi_playback_.positionSeconds();
            }
            playback_start_seconds_ = seconds;
            showTransportPosition(seconds);
        };
        playlist_->setSeekCallback(seek);
        piano_roll_->setSeekCallback(seek);
        playlist_->setOpenEditorCallback([this] {
            showFeature(piano_dialog_);
            piano_roll_->refresh(true);
            piano_roll_->setFocus();
        });
        playlist_->setDocumentChangedCallback([this] {
            stopMelodyPlayback(QStringLiteral("Playback stopped because the arrangement changed."));
            piano_roll_->refresh(); markDirty(); updateEditorUi(); updateUi();
        });
        playlist_->setSelectionChangedCallback([this] {
            const QSignalBlocker blocker(conversion_tabs_);
            conversion_tabs_->setCurrentIndex(note_editor_.tracks()[playlist_->selectedTrack()].kind == core::TrackKind::chords ? 1 : 0);
            updateEditorUi(); updateUi();
        });
        piano_roll_->setDocumentChangedCallback([this] {
            stopMelodyPlayback(QStringLiteral(
                "Melody playback stopped because a note changed."));
            markDirty();
            updateEditorUi();
            updateUi();
        });
        piano_roll_->setSelectionChangedCallback([this] { updateEditorUi(); });
        piano_roll_->setStatusCallback([this](const QString& message) {
            editor_status_label_->setText(message);
        });
        QObject::connect(refresh_button_, &QPushButton::clicked, window_, [this] {
            if (recorder_.state() == core::RecordingState::error) {
                static_cast<void>(recorder_.acknowledgeError());
            }
            refreshDevices();
        });
        QObject::connect(permission_button_, &QPushButton::clicked, window_, [this] { requestPermission(); });
        QObject::connect(&monitor_, &QTimer::timeout, window_, [this] { poll(); });
        capture_.setDevicesChangedCallback([this] {
            QMetaObject::invokeMethod(window_, [this] { refreshDevices(); }, Qt::QueuedConnection);
        });
    }

    void refreshDevices() {
        const auto previous_id = device_combo_->currentData().toString();
        device_combo_->clear();
        for (const auto& device : capture_source_.devices()) {
            const auto label = QString::fromStdString(device.name)
                + (device.is_default ? QStringLiteral(" · Default") : QString{});
            device_combo_->addItem(label, QString::fromStdString(device.id));
        }
        if (!previous_id.isEmpty()) {
            const auto index = device_combo_->findData(previous_id);
            if (index >= 0) {
                device_combo_->setCurrentIndex(index);
            }
        }

        const auto permission = capture_source_.permissionStatus();
        permission_button_->setVisible(permission != core::PermissionStatus::granted);
        permission_button_->setText(permission == core::PermissionStatus::denied
            ? QStringLiteral("Retry access") : QStringLiteral("Enable microphone"));
        if (permission == core::PermissionStatus::denied) {
            setStatus(errorMessage(core::ServiceStatus::permission_denied), true);
        } else if (permission == core::PermissionStatus::unknown) {
            setStatus(errorMessage(core::ServiceStatus::permission_required), false);
        } else if (device_combo_->count() == 0) {
            setStatus(errorMessage(core::ServiceStatus::unavailable), true);
        } else if (recorder_.state() != core::RecordingState::recording) {
            setStatus(QStringLiteral("Ready. Choose a microphone, then record one clear vocal phrase."), false);
        }
        updateUi();
    }

    void requestPermission() {
        permission_button_->setEnabled(false);
        setStatus(QStringLiteral("Waiting for microphone permission…"), false);
        capture_source_.requestPermission([this](const core::PermissionStatus) {
            QMetaObject::invokeMethod(window_, [this] {
                permission_button_->setEnabled(true);
                refreshDevices();
            }, Qt::QueuedConnection);
        });
    }

    void refreshOutputLabel() {
        const auto device = QMediaDevices::defaultAudioOutput();
        const auto format = chooseOutputFormat(device);
        output_label_->setText(format ? QStringLiteral("Speakers: %1 · %2 Hz").arg(device.description()).arg(format->sampleRate())
            : QStringLiteral("No supported speaker output. Select an output in macOS Sound settings."));
    }

    void stopRecordingClick() {
        guide_->stop(); armed_capture_.stop(); cue_playback_.stop();
        record_beat_label_->setText(QStringLiteral("4-beat count-in · then record with tracks"));
    }

    void startRecording(bool voice = false) {
        if (guide_->active() || recorder_.state() == core::RecordingState::recording || phrase_processor_.isBusy()) { return; }
        if (device_combo_->currentIndex() < 0) { setStatus(errorMessage(core::ServiceStatus::unavailable), true); return; }
        if (recorder_.state() == core::RecordingState::error) { static_cast<void>(recorder_.acknowledgeError()); }
        if (voice && voice_clips_.size() >= 16) { setStatus(QStringLiteral("The project already has 16 voice takes. Delete a take before recording another."), true); return; }
        voice_recording_ = voice;
        recording_dialog_->setWindowTitle(voice ? QStringLiteral("Record voice — Playlist audio") : QStringLiteral("Hum → MIDI — Record and convert"));
        if (!voice && !ensureConversionTrack(conversion_tabs_->currentIndex() == 1)) { updateUi(); return; }
        const auto context = vocalContext();
        auto plan = core::makeOverdubPlan(context.backing_notes, context.backing_voices,
            context.origin, context.bpm, context.recording_click);
        if (!plan) { setStatus(QStringLiteral("Place the playhead before 570 seconds to leave room for a take."), true); return; }
        const auto armed = armed_capture_.arm(device_combo_->currentData().toString().toStdString());
        if (armed != core::ServiceStatus::ok) { setStatus(errorMessage(armed), true); return; }
        stopMelodyPlayback(); playback_.stop(); cue_playback_.stop();
        setStatus(QStringLiteral("Preparing four count-in beats… Recording starts after beat 4."), false);
        record_beat_label_->setText(QStringLiteral("Preparing count-in…"));
        plan->voice_clips = voice_clips_;
        guide_->start(*plan, {
            [this](double origin) { beginRecordingCapture(origin); },
            [this](int beat, double position) {
                playback_start_seconds_ = position; showTransportPosition(position);
                record_beat_label_->setText(beat > 0 ? QStringLiteral("Count in: %1 / 4 — get ready…").arg(beat)
                    : QStringLiteral("Recording with tracks · %1 s").arg(position, 0, 'f', 2));
            },
            [this](const QString& error) {
                stopRecording(); if (!error.isEmpty()) { setStatus(error, true); }
            }});
        updateUi();
    }

    void beginRecordingCapture(double origin) {
        const auto result = recorder_.start(device_combo_->currentData().toString().toStdString());
        switch (result) {
        case core::StartRecordingResult::started:
            recorded_origin_ = origin;
            phrase_processor_.cancel();
            clearPhraseConversion(QStringLiteral(
                "Recording in progress. After Stop, conversion adds a new region; existing regions are retained."), false);
            level_meter_->setLevel(0.0F, false);
            clip_label_->setVisible(false);
            setStatus(voice_recording_ ? QStringLiteral("Recording your voice with the Playlist… Press Stop to add the audio clip.")
                : QStringLiteral("Recording with tracks… hum a melody or chord roots, then press Stop."), false);
            break;
        case core::StartRecordingResult::permission_required:
            setStatus(errorMessage(core::ServiceStatus::permission_required), false);
            break;
        case core::StartRecordingResult::permission_denied:
            setStatus(errorMessage(core::ServiceStatus::permission_denied), true);
            break;
        case core::StartRecordingResult::device_unavailable:
            setStatus(errorMessage(core::ServiceStatus::unavailable), true);
            break;
        case core::StartRecordingResult::invalid_format:
            setStatus(errorMessage(core::ServiceStatus::invalid_input), true);
            break;
        case core::StartRecordingResult::source_error:
            setStatus(errorMessage(core::ServiceStatus::io_error), true);
            break;
        case core::StartRecordingResult::busy:
            setStatus(errorMessage(core::ServiceStatus::busy), true);
            break;
        case core::StartRecordingResult::closed:
            setStatus(QStringLiteral("The recorder is closing."), true);
            break;
        }
        if (result != core::StartRecordingResult::started) { stopRecordingClick(); }
        updateUi();
    }

    void stopRecording() {
        const auto counting = guide_->active() && recorder_.state() != core::RecordingState::recording;
        stopRecordingClick();
        if (counting) { setStatus(QStringLiteral("Count-in cancelled. No new take was recorded."), false); }
        if (recorder_.stop()) {
            presentCompletedRecording(core::RecordingStopReason::user);
        }
        updateUi();
    }

    void presentCompletedRecording(const core::RecordingStopReason reason) {
        stopRecordingClick();
        const auto recording = recorder_.recording();
        updateMetadata();
        level_meter_->setLevel(0.0F, recording && recording->peakLevel() >= 0.98F);
        if (!recording || recording->sampleCount() == 0) {
            setStatus(QStringLiteral("No audio samples arrived. Check the microphone and try again."), true);
        } else if (recording->durationSeconds() < 0.15) {
            setStatus(QStringLiteral("That take is very short. Record a brief sustained phrase for reliable analysis later."), false);
        } else if (recording->isSilent()) {
            setStatus(QStringLiteral("The take contains little or no audible input. Check the level meter and microphone selection."), false);
        } else if (reason == core::RecordingStopReason::duration_limit) {
            setStatus(QStringLiteral("The 30-second safety limit was reached. Your bounded recording is ready to play."), false);
        } else {
            setStatus(QStringLiteral("Recording ready. Play the original phrase to verify the capture."), false);
        }
        if (voice_recording_ && recording && recording->sampleCount() > 0) {
            auto clip = core::makeVoiceClip(*recording, recorded_origin_);
            if (clip) {
                clip->name = "Voice " + std::to_string(voice_clips_.size() + 1);
                auto candidate = voice_clips_; candidate.push_back(std::move(*clip));
                if (core::validVoiceClips(candidate)) {
                    voice_clips_ = std::move(candidate); markDirty(); playlist_->refresh();
                    setStatus(QStringLiteral("Voice added to Playlist. Save the project to keep the audio. Right-click its lane to mute or delete."), false);
                } else setStatus(QStringLiteral("Voice storage is full. The latest take is still available through Play original."), true);
            } else setStatus(QStringLiteral("This take could not be added as voice audio."), true);
            voice_recording_ = false;
            return;
        }
        if (recording && recording->sampleCount() > 0) {
            beginPhraseConversion(conversion_tabs_->currentIndex() == 1);
        }
    }

    void togglePlayback() {
        if (playback_.isPlaying() || playback_.isPreparing()) {
            playback_.stop();
            setStatus(QStringLiteral("Playback stopped. Your recording remains in memory."), false);
            updateUi();
            return;
        }
        stopMelodyPlayback();
        cue_playback_.stop();
        playback_.play(recorder_.recording(), [this](const QString& message, const bool error) {
            setStatus(message, error);
            updateUi();
        });
        updateUi();
    }

    void poll() {
        if (guide_->active() && armed_capture_.error() != core::ServiceStatus::ok) {
            const auto error = armed_capture_.error(); stopRecording(); setStatus(errorMessage(error), true);
        }
        if (const auto event = recorder_.service()) {
            stopRecordingClick();
            if (*event == core::RecordingStopReason::device_failure) {
                const bool was_voice = voice_recording_;
                if (was_voice) { presentCompletedRecording(*event); }
                setStatus(errorMessage(recorder_.lastError()), true);
                updateMetadata();
                if (!was_voice && recorder_.recording() && recorder_.recording()->sampleCount() > 0) {
                    beginPhraseConversion(conversion_tabs_->currentIndex() == 1);
                }
            } else {
                presentCompletedRecording(*event);
            }
        }

        const auto metrics = recorder_.pollMetrics();
        if (recorder_.state() == core::RecordingState::recording) {
            displayed_level_ = std::max(metrics.recent_peak, displayed_level_ * 0.72F);
            level_meter_->setLevel(displayed_level_, metrics.clipped);
            clip_label_->setVisible(metrics.clipped);
            duration_label_->setText(durationText(metrics.duration_seconds, recorder_.maximumDurationSeconds()));
            samples_value_->setText(QString::number(metrics.sample_count));
        } else {
            displayed_level_ = 0.0F;
        }
        if (phrase_processor_.isBusy()) {
            const auto percent = static_cast<int>(std::lround(
                std::clamp(phrase_processor_.progress(), 0.0, 1.0) * 100.0));
            phrase_progress_->setValue(percent);
            phrase_status_label_->setText(QStringLiteral(
                "Analyzing the saved audio after recording… %1%").arg(percent));
            chord_status_label_->setText(phrase_status_label_->text());
            workspace_message_label_->setText(phrase_status_label_->text());
        }
        if (midi_playback_.isPlaying()) {
            piano_roll_->setPlayheadSeconds(midi_playback_.positionSeconds(), true);
            playlist_->setPlayheadSeconds(midi_playback_.positionSeconds(), true);
            transport_position_->setText(QStringLiteral("%1 s").arg(midi_playback_.positionSeconds(), 0, 'f', 2));
            piano_position_->setText(transport_position_->text());
        }
        updateUi();
    }

    void updateMetadata() {
        const auto recording = recorder_.recording();
        if (!recording) {
            clip_label_->hide();
            sample_rate_value_->setText(QStringLiteral("—"));
            channels_value_->setText(QStringLiteral("—"));
            samples_value_->setText(QStringLiteral("—"));
            duration_label_->setText(durationText(0.0, recorder_.maximumDurationSeconds()));
            return;
        }
        sample_rate_value_->setText(QStringLiteral("%1 Hz").arg(recording->format().sample_rate_hz));
        channels_value_->setText(channelText(recording->format().channel_count));
        samples_value_->setText(QString::number(recording->sampleCount()));
        duration_label_->setText(durationText(recording->durationSeconds(), recorder_.maximumDurationSeconds()));
        clip_label_->setVisible(recording->peakLevel() >= 0.98F);
    }

    [[nodiscard]] core::ScaleMode selectedScaleMode() const noexcept {
        return static_cast<core::ScaleMode>(scale_mode_combo_->currentData().toInt());
    }

    [[nodiscard]] QString selectedScaleName() const {
        return QStringLiteral("%1 %2")
            .arg(root_pitch_combo_->currentText())
            .arg(QString::fromStdString(std::string(core::scaleModeName(selectedScaleMode()))));
    }

    void applyScaleSettings() {
        stopMelodyPlayback(QStringLiteral(
            "Melody playback stopped because the key or scale changed."));
        const auto root = static_cast<std::uint8_t>(root_pitch_combo_->currentData().toUInt());
        static_cast<void>(note_editor_.setScale(
            core::makeScaleDefinition(root, selectedScaleMode())));
        piano_roll_->refresh();
        updateScaleGuidanceUi();
        updateEditorUi();
        markDirty();
        updateUi();
    }

    void updateScaleGuidanceUi() {
        updateProgressionUi();
        const QSignalBlocker magnet_blocker(magnet_button_);
        const QSignalBlocker key_blocker(chord_key_combo_);
        const QSignalBlocker scale_blocker(chord_scale_combo_);
        chord_key_combo_->setCurrentIndex(root_pitch_combo_->currentIndex());
        chord_scale_combo_->setCurrentIndex(scale_mode_combo_->currentIndex());
        const auto enabled = note_editor_.magnetEnabled();
        { const QSignalBlocker blocker(melody_snap_check_); melody_snap_check_->setChecked(enabled); }
        magnet_button_->setChecked(enabled);
        magnet_button_->setText(enabled
            ? QStringLiteral("Magnet ON") : QStringLiteral("Magnet OFF"));
        scale_guidance_label_->setText(enabled
            ? QStringLiteral("%1 Magnet view is ON. Closest scale pitches are shown and exported; your original/manual pitches remain underneath. Key selection does not transpose the melody.")
                .arg(selectedScaleName())
            : QStringLiteral("%1 guides pitch only. Key selection does not transpose the melody. Magnet is OFF, so original/manual pitches are shown and exported.")
                .arg(selectedScaleName()));
    }

    void updateEditorUi() {
        playlist_->setTempo(tempo_spin_->value());
        playlist_->refresh();
        const auto track = playlist_->selectedTrack();
        const auto& definition = note_editor_.tracks()[track];
        piano_roll_->setActiveTrack(track);
        piano_dialog_->setWindowTitle(QStringLiteral("Piano roll — Track %1 · %2")
            .arg(track + 1).arg(QString::fromLatin1(core::trackKindName(definition.kind))));
        piano_title_->setText(piano_dialog_->windowTitle());
        selected_track_label_->setText(QStringLiteral("Track %1").arg(track + 1));
        for (auto* combo : {track_instrument_combo_, playlist_instrument_combo_}) {
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(combo->findData(static_cast<int>(definition.instrument)));
        }
        { const QSignalBlocker blocker(playlist_kind_combo_);
          playlist_kind_combo_->setCurrentIndex(playlist_kind_combo_->findData(static_cast<int>(definition.kind))); }
        { const QSignalBlocker blocker(mute_track_button_);
          mute_track_button_->setChecked(definition.muted);
          mute_track_button_->setText(definition.muted ? QStringLiteral("Unmute track") : QStringLiteral("Mute track")); }
        { const QSignalBlocker blocker(conversion_tabs_);
          conversion_tabs_->setCurrentIndex(definition.kind == core::TrackKind::chords ? 1 : 0); }
        playlist_status_label_->setText(QStringLiteral(
            "%1 track(s) · %2 region(s) · %3 notes · New takes go to Track %4. Drag regions to arrange; double-click to correct notes. %5 · Magnet %6.")
            .arg(note_editor_.trackCount()).arg(note_editor_.regions().size())
            .arg(note_editor_.notes().size()).arg(playlist_->selectedTrack() + 1)
            .arg(selectedScaleName()).arg(note_editor_.magnetEnabled() ? QStringLiteral("ON") : QStringLiteral("OFF")));
        undo_button_->setEnabled(note_editor_.canUndo());
        redo_button_->setEnabled(note_editor_.canRedo());
        undo_button_->setToolTip(note_editor_.canUndo()
            ? QStringLiteral("Undo %1").arg(QString::fromStdString(note_editor_.undoLabel()).toLower())
            : QStringLiteral("Undo"));
        redo_button_->setToolTip(note_editor_.canRedo()
            ? QStringLiteral("Redo %1").arg(QString::fromStdString(note_editor_.redoLabel()).toLower())
            : QStringLiteral("Redo"));
        delete_note_button_->setEnabled(note_editor_.selectedIndex().has_value());
        quantize_button_->setEnabled(note_editor_.selectedIndex().has_value());

        const auto* selected = note_editor_.selectedNote();
        if (selected) {
            const auto visible_pitch = note_editor_.projectedPitch(selected->pitch);
            const auto pitch_guidance = note_editor_.pitchIsInScale(selected->pitch)
                ? QStringLiteral("IN %1").arg(selectedScaleName())
                : (note_editor_.magnetEnabled()
                    ? QStringLiteral("original %1 → Magnet %2 in %3; original retained")
                        .arg(QString::fromStdString(core::midiNoteName(selected->pitch)))
                        .arg(QString::fromStdString(core::midiNoteName(visible_pitch)))
                        .arg(selectedScaleName())
                    : QStringLiteral("OUTSIDE %1; Magnet OFF preserves it")
                        .arg(selectedScaleName()));
            editor_status_label_->setText(QStringLiteral(
                "Selected %1 · %2 · starts %3 s · lasts %4 s · velocity %5.")
                .arg(QString::fromStdString(core::midiNoteName(visible_pitch)))
                .arg(pitch_guidance)
                .arg(selected->start_seconds, 0, 'f', 3)
                .arg(selected->duration_seconds, 0, 'f', 3)
                .arg(selected->velocity));
        } else if (note_editor_.notes().empty()) {
            editor_status_label_->setText(QStringLiteral(
                "No melody notes yet. Record a phrase or double-click the grid to add one."));
        } else {
            const auto overlaps = note_editor_.overlapCount();
            const auto outside = static_cast<std::size_t>(std::count_if(
                note_editor_.notes().cbegin(), note_editor_.notes().cend(),
                [this](const core::TimedMidiNote& note) {
                    return !note_editor_.pitchIsInScale(note.pitch);
                }));
            editor_status_label_->setText(QStringLiteral(
                "%1 editable note(s) · %2 outside %3%4. Click a block to select it; double-click empty space to add.")
                .arg(note_editor_.notes().size())
                .arg(outside)
                .arg(selectedScaleName())
                .arg(overlaps > 0
                    ? QStringLiteral(" · %1 preserved overlap(s)").arg(overlaps)
                    : QString{}));
        }
    }

    void updateUi() {
        const auto state = recorder_.state();
        const auto recording_active = guide_->active() || state == core::RecordingState::recording
            || state == core::RecordingState::starting
            || state == core::RecordingState::stopping;
        const auto can_record = !recording_active && !phrase_processor_.isBusy()
            && state != core::RecordingState::closed
            && capture_source_.permissionStatus() == core::PermissionStatus::granted
            && device_combo_->count() > 0;
        open_recorder_button_->setText(recording_active
            ? QStringLiteral("●  Recording… open controls") : QStringLiteral("Hum → MIDI"));
        record_button_->setEnabled(can_record);
        ai_trial_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        stop_button_->setEnabled(recording_active);
        voice_record_button_->setText(recording_active ? QStringLiteral("Stop recording") : QStringLiteral("Record voice"));
        voice_record_button_->setEnabled(recording_active || (!phrase_processor_.isBusy() && state != core::RecordingState::closed));
        device_combo_->setEnabled(!recording_active);
        refresh_button_->setEnabled(!recording_active);
        permission_button_->setEnabled(!recording_active);
        open_project_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        save_project_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());

        const auto recording = recorder_.recording();
        const auto has_audio = recording && recording->sampleCount() > 0;
        play_button_->setEnabled(!recording_active && has_audio);
        play_button_->setText(playback_.isPlaying() || playback_.isPreparing()
            ? QStringLiteral("Stop playback") : QStringLiteral("Play original"));
        convert_button_->setEnabled(
            !recording_active && has_audio && !phrase_processor_.isBusy());
        cancel_conversion_button_->setEnabled(phrase_processor_.isBusy());
        chord_convert_button_->setEnabled(convert_button_->isEnabled());
        chord_cancel_button_->setEnabled(phrase_processor_.isBusy());
        conversion_tabs_->tabBar()->setEnabled(!recording_active && !phrase_processor_.isBusy());
        chord_key_combo_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        chord_scale_combo_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        connect_chords_check_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        melody_snap_check_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        progression_edit_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        chord_beats_spin_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        progression_add_button_->setEnabled(!recording_active && !phrase_processor_.isBusy()
            && core::parseChordDegrees(progression_edit_->text().toStdString()).has_value());
        for (QWidget* control : {static_cast<QWidget*>(track_instrument_combo_), static_cast<QWidget*>(playlist_instrument_combo_),
                 static_cast<QWidget*>(playlist_kind_combo_), static_cast<QWidget*>(mute_track_button_), static_cast<QWidget*>(delete_track_button_)}) {
            control->setEnabled(!recording_active && !phrase_processor_.isBusy());
        }
        for (auto* button : degree_buttons_) { button->setEnabled(!recording_active && !phrase_processor_.isBusy()); }
        export_button_->setEnabled(
            !recording_active && !phrase_processor_.isBusy()
            && !note_editor_.notes().empty());
        midi_play_button_->setEnabled(!recording_active && !phrase_processor_.isBusy() && !midi_playback_.isPlaying()
            && (!note_editor_.notes().empty() || !voice_clips_.empty() || metronome_button_->isChecked()));
        const auto play_label = midi_playback_.isPreparing() ? QStringLiteral("Cancel preparation")
            : midi_playback_.isPaused() ? QStringLiteral("Continue playback (Space)") : QStringLiteral("Play (Space)");
        for (auto* button : {midi_play_button_, piano_play_button_}) {
            button->setText({}); button->setAccessibleName(play_label); button->setToolTip(play_label);
            button->setIcon(toolIcon(midi_playback_.isPreparing() ? ToolIcon::stop : ToolIcon::play));
        }
        midi_stop_button_->setEnabled(!recording_active);
        midi_pause_button_->setEnabled(midi_playback_.isPlaying() && !recording_active);
        piano_play_button_->setEnabled(midi_play_button_->isEnabled() && !midi_playback_.isPlaying());
        piano_pause_button_->setEnabled(midi_playback_.isPlaying() && !recording_active);
        piano_stop_button_->setEnabled(!recording_active);
        metronome_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        metronome_button_->setText(metronome_button_->isChecked()
            ? QStringLiteral("Metronome ON") : QStringLiteral("Metronome OFF"));
        editor_toolbar_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        for (std::size_t i = 0; i < 2; ++i) {
            const QSignalBlocker range_blocker(range_buttons_[i]), loop_blocker(loop_buttons_[i]);
            range_buttons_[i]->setChecked(loop_tool_); loop_buttons_[i]->setChecked(loop_enabled_);
            for (auto* button : {range_buttons_[i], loop_buttons_[i], all_loop_buttons_[i]}) {
                button->setEnabled(!recording_active && !phrase_processor_.isBusy());
            }
        }
        const auto display_range = loop_range_ ? loop_range_ : activeLoopRange();
        playlist_->setLoopRange(display_range, loop_enabled_); piano_roll_->setLoopRange(display_range, loop_enabled_);
        { const QSignalBlocker blocker(record_metronome_button_);
          record_metronome_button_->setChecked(metronome_button_->isChecked()); }
        record_metronome_button_->setText(metronome_button_->isChecked()
            ? QStringLiteral("Metronome ON") : QStringLiteral("Metronome OFF"));
        record_metronome_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        record_tempo_spin_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        speaker_test_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        add_note_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        delete_note_button_->setEnabled(
            !recording_active && !phrase_processor_.isBusy()
            && note_editor_.selectedIndex().has_value());
        quantize_button_->setEnabled(!recording_active && !phrase_processor_.isBusy()
            && note_editor_.selectedIndex().has_value());
        undo_button_->setEnabled(
            !recording_active && !phrase_processor_.isBusy() && note_editor_.canUndo());
        redo_button_->setEnabled(
            !recording_active && !phrase_processor_.isBusy() && note_editor_.canRedo());
        piano_roll_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        const auto can_arrange = !recording_active && !phrase_processor_.isBusy();
        playlist_->setEnabled(can_arrange);
        add_track_button_->setEnabled(can_arrange && note_editor_.trackCount() < 16);
        duplicate_region_button_->setEnabled(can_arrange && playlist_->selectedRegion().has_value());
        delete_region_button_->setEnabled(can_arrange && playlist_->selectedRegion().has_value());
        playlist_undo_button_->setEnabled(can_arrange && note_editor_.canUndo());
        playlist_redo_button_->setEnabled(can_arrange && note_editor_.canRedo());
        root_pitch_combo_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        scale_mode_combo_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        magnet_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        rhythm_snap_combo_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        tempo_spin_->setEnabled(!recording_active && !phrase_processor_.isBusy());
    }

    void setStatus(const QString& text, const bool error) {
        if (workspace_message_label_) { workspace_message_label_->setText(text); }
        status_label_->setText(text);
        status_label_->setProperty("error", error);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
    }

    std::vector<core::VoiceClip> voice_clips_;
    bool voice_recording_{false};
    QPushButton* voice_record_button_{nullptr};
    QDialog* lyrics_dialog_{nullptr};
    QPlainTextEdit* lyrics_edit_{nullptr};
    MainWindow* window_;
    QtAudioCapture capture_;
    core::IAudioCapture& capture_source_;
    ArmedCapture armed_capture_;
    core::RecordingController recorder_;
    RecordingGuidePlayer guide_player_;
    IRecordingGuide* guide_;
    double recorded_origin_{0};
    QtAudioPlayback playback_;
    QtMidiPlayback midi_playback_;
    QtMidiPlayback cue_playback_;
    QtMidiPlayback audition_playback_;
    PhraseConversionProcessor phrase_processor_;
    core::NoteEditor note_editor_;
    QTimer monitor_;
    QLabel* project_name_label_{nullptr};
    QPushButton* open_project_button_{nullptr};
    QPushButton* save_project_button_{nullptr};
    QComboBox* device_combo_{nullptr};
    QPushButton* refresh_button_{nullptr};
    QPushButton* permission_button_{nullptr};
    QLabel* status_label_{nullptr};
    LevelMeter* level_meter_{nullptr};
    QLabel* clip_label_{nullptr};
    QLabel* duration_label_{nullptr};
    QPushButton* record_button_{nullptr};
    QPushButton* stop_button_{nullptr};
    QPushButton* play_button_{nullptr};
    QLabel* sample_rate_value_{nullptr};
    QLabel* channels_value_{nullptr};
    QLabel* samples_value_{nullptr};
    QLabel* phrase_status_label_{nullptr};
    QProgressBar* phrase_progress_{nullptr};
    QPushButton* convert_button_{nullptr};
    QPushButton* cancel_conversion_button_{nullptr};
    QPushButton* export_button_{nullptr};
    QComboBox* chord_key_combo_{nullptr};
    QComboBox* chord_scale_combo_{nullptr};
    QComboBox* track_instrument_combo_{nullptr};
    QComboBox* playlist_instrument_combo_{nullptr};
    QComboBox* playlist_kind_combo_{nullptr};
    QPushButton* mute_track_button_{nullptr};
    QPushButton* delete_track_button_{nullptr};
    QLabel* selected_track_label_{nullptr};
    QLabel* piano_title_{nullptr};
    QCheckBox* show_other_tracks_{nullptr};
    QCheckBox* connect_chords_check_{nullptr};
    QCheckBox* melody_snap_check_{nullptr};
    QLineEdit* progression_edit_{nullptr};
    QLabel* progression_preview_{nullptr};
    QSpinBox* chord_beats_spin_{nullptr};
    QPushButton* progression_add_button_{nullptr};
    std::array<QPushButton*, 7> degree_buttons_{};
    QTabWidget* conversion_tabs_{nullptr};
    QPushButton* chord_cancel_button_{nullptr};
    QPushButton* piano_play_button_{nullptr};
    QPushButton* piano_pause_button_{nullptr};
    QPushButton* piano_stop_button_{nullptr};
    QLabel* piano_position_{nullptr};
    QPushButton* chord_convert_button_{nullptr};
    QLabel* chord_status_label_{nullptr};
    QPushButton* midi_play_button_{nullptr};
    QPushButton* midi_pause_button_{nullptr};
    std::array<QPushButton*,2> range_buttons_{}, loop_buttons_{}, all_loop_buttons_{};
    std::optional<core::LoopRange> loop_range_;
    bool loop_enabled_{false}, loop_tool_{false};
    QPushButton* midi_stop_button_{nullptr};
    QPushButton* metronome_button_{nullptr};
    QLabel* transport_position_{nullptr};
    int last_snap_index_{3};
    EditorToolbar* editor_toolbar_{nullptr};
    QPushButton* record_metronome_button_{nullptr};
    QDoubleSpinBox* record_tempo_spin_{nullptr};
    QLabel* record_beat_label_{nullptr};
    QLabel* output_label_{nullptr};
    QPushButton* speaker_test_button_{nullptr};
    AiTrialPanel* ai_trial_panel_{nullptr};
    QPushButton* ai_trial_button_{nullptr};
    vocal::SingToMidiPanel* sing_panel_{nullptr};
    std::uint64_t vocal_project_generation_{0};
    double playback_start_seconds_{0.0};
    std::uint16_t ticks_per_quarter_{core::kDefaultTicksPerQuarter};
    PianoRollWidget* piano_roll_{nullptr};
    PlaylistWidget* playlist_{nullptr};
    QDialog* recording_dialog_{nullptr};
    QDialog* piano_dialog_{nullptr};
    QLabel* playlist_status_label_{nullptr};
    QLabel* workspace_message_label_{nullptr};
    QPushButton* open_recorder_button_{nullptr};
    QPushButton* add_track_button_{nullptr};
    QPushButton* duplicate_region_button_{nullptr};
    QPushButton* delete_region_button_{nullptr};
    QPushButton* playlist_undo_button_{nullptr};
    QPushButton* playlist_redo_button_{nullptr};
    QLabel* editor_status_label_{nullptr};
    QPushButton* undo_button_{nullptr};
    QPushButton* redo_button_{nullptr};
    QPushButton* add_note_button_{nullptr};
    QPushButton* delete_note_button_{nullptr};
    QPushButton* quantize_button_{nullptr};
    QComboBox* root_pitch_combo_{nullptr};
    QComboBox* scale_mode_combo_{nullptr};
    QPushButton* magnet_button_{nullptr};
    QLabel* scale_guidance_label_{nullptr};
    QComboBox* rhythm_snap_combo_{nullptr};
    QDoubleSpinBox* tempo_spin_{nullptr};
    QSlider* horizontal_zoom_slider_{nullptr};
    QSlider* vertical_zoom_slider_{nullptr};
    std::optional<core::PhraseConversionResult> last_conversion_;
    float displayed_level_{0.0F};
    QString project_path_;
    bool dirty_{false};
    bool applying_project_{false};
    bool closed_{false};
};

MainWindow::MainWindow(QWidget* parent, core::IAudioCapture* capture, IRecordingGuide* guide)
    : QMainWindow(parent),
      impl_(std::make_unique<Impl>(this, capture, guide)) {
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    if (impl_->requestClose()) {
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace hum::app
