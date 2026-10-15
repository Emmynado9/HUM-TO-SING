#include "MainWindow.hpp"

#include "LevelMeter.hpp"
#include "PianoRollWidget.hpp"
#include "PlaylistWidget.hpp"
#include "PhraseConversionProcessor.hpp"
#include "PitchLabProcessor.hpp"
#include "ProjectFileStore.hpp"
#include "QtAudioCapture.hpp"
#include "QtAudioPlayback.hpp"
#include "QtMidiPlayback.hpp"
#include "hum/core/midi_file.hpp"
#include "hum/core/note_editor.hpp"
#include "hum/core/note_extraction.hpp"
#include "hum/core/pitch.hpp"
#include "hum/core/project_file.hpp"
#include "hum/core/recording.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
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
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
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
    explicit Impl(MainWindow* window)
        : window_(window),
          capture_(window),
          recorder_(capture_, 30.0, 0.98F),
          playback_(window),
          midi_playback_(window),
          pitch_processor_(window),
          phrase_processor_(window),
          pitch_smoother_(5) {
        buildUi();
        wireEvents();
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
        monitor_.stop();
        capture_.setDevicesChangedCallback({});
        playback_.stop();
        midi_playback_.stop();
        if (piano_roll_) {
            piano_roll_->setPlayheadSeconds(std::nullopt);
        }
        if (playlist_) {
            playlist_->setPlayheadSeconds(std::nullopt);
        }
        pitch_processor_.cancel();
        phrase_processor_.cancel();
        recorder_.shutdown();
    }

    [[nodiscard]] bool requestClose() {
        if (!confirmDiscardChanges(QStringLiteral("close the application"))) {
            return false;
        }
        shutdown();
        return true;
    }

private:
    void buildUi() {
        window_->setWindowTitle(QStringLiteral("Hum-to-MIDI Music Sketchpad — Piano Roll"));
        window_->setMinimumSize(1'180, 820);
        window_->resize(1'460, 980);

        auto* root = new QWidget(window_);
        root->setObjectName(QStringLiteral("root"));
        auto* shell = new QHBoxLayout(root);
        shell->setContentsMargins(18, 18, 18, 18);
        shell->setSpacing(22);

        auto* rail = new QFrame(root);
        rail->setObjectName(QStringLiteral("rail"));
        rail->setFixedWidth(88);
        auto* rail_layout = new QVBoxLayout(rail);
        rail_layout->setContentsMargins(14, 22, 14, 22);
        rail_layout->setSpacing(18);
        auto* logo = new QLabel(QStringLiteral("H."), rail);
        logo->setObjectName(QStringLiteral("logo"));
        logo->setAlignment(Qt::AlignCenter);
        rail_layout->addWidget(logo);
        rail_layout->addSpacing(34);
        for (const auto& label : {QStringLiteral("●"), QStringLiteral("E"), QStringLiteral("P"), QStringLiteral("X")}) {
            auto* item = new QLabel(label, rail);
            item->setObjectName(label == QStringLiteral("●")
                ? QStringLiteral("railActive") : QStringLiteral("railItem"));
            item->setAlignment(Qt::AlignCenter);
            item->setToolTip(label == QStringLiteral("●")
                ? QStringLiteral("Record") : QStringLiteral("Planned workflow section"));
            rail_layout->addWidget(item);
        }
        rail_layout->addStretch();
        auto* local = new QLabel(QStringLiteral("LOCAL\nONLY"), rail);
        local->setObjectName(QStringLiteral("localBadge"));
        local->setAlignment(Qt::AlignCenter);
        rail_layout->addWidget(local);
        shell->addWidget(rail);

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
            "Open editable notes and settings. Original recordings are not stored in project files."));
        save_project_button_->setToolTip(QStringLiteral(
            "Save canonical editable notes and settings. Session audio remains in memory only."));
        header->addWidget(project_name_label_, 0, Qt::AlignTop);
        header->addWidget(open_project_button_, 0, Qt::AlignTop);
        header->addWidget(save_project_button_, 0, Qt::AlignTop);
        auto* privacy = new QLabel(QStringLiteral("●  IN MEMORY · NEVER UPLOADED"), root);
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
        record_button_ = new QPushButton(QStringLiteral("●  Record"), recorder_card);
        record_button_->setObjectName(QStringLiteral("primaryButton"));
        stop_button_ = new QPushButton(QStringLiteral("■  Stop"), recorder_card);
        stop_button_->setObjectName(QStringLiteral("stopButton"));
        play_button_ = new QPushButton(QStringLiteral("▶  Play original"), recorder_card);
        play_button_->setObjectName(QStringLiteral("secondaryButton"));
        controls->addStretch();
        controls->addWidget(record_button_);
        controls->addWidget(stop_button_);
        controls->addWidget(play_button_);
        controls->addStretch();
        recorder_layout->addLayout(controls);

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

        pitch_dialog_ = new QDialog(window_);
        pitch_dialog_->setObjectName(QStringLiteral("pitchLabDialog"));
        pitch_dialog_->setWindowTitle(QStringLiteral("Pitch Lab — Local YIN diagnostics"));
        pitch_dialog_->resize(660, 790);
        auto* pitch_card = new QFrame(pitch_dialog_);
        pitch_card->setObjectName(QStringLiteral("card"));
        auto* pitch_layout = new QVBoxLayout(pitch_card);
        pitch_layout->setContentsMargins(22, 20, 22, 20);
        pitch_layout->setSpacing(11);
        auto* pitch_title = new QLabel(QStringLiteral("Pitch Lab"), pitch_card);
        pitch_title->setObjectName(QStringLiteral("cardTitle"));
        auto* pitch_copy = new QLabel(
            QStringLiteral("Single-note YIN diagnostic · A4 = 440 Hz"), pitch_card);
        pitch_copy->setObjectName(QStringLiteral("footnote"));
        pitch_layout->addWidget(pitch_title);
        pitch_layout->addWidget(pitch_copy);

        pitch_state_label_ = new QLabel(QStringLiteral("NO RELIABLE PITCH"), pitch_card);
        pitch_state_label_->setObjectName(QStringLiteral("pitchState"));
        pitch_state_label_->setAlignment(Qt::AlignCenter);
        pitch_note_label_ = new QLabel(QStringLiteral("—"), pitch_card);
        pitch_note_label_->setObjectName(QStringLiteral("pitchNote"));
        pitch_note_label_->setAlignment(Qt::AlignCenter);
        pitch_layout->addWidget(pitch_state_label_);
        pitch_layout->addWidget(pitch_note_label_);

        auto* pitch_metrics = new QHBoxLayout();
        pitch_frequency_value_ = addMetric(pitch_metrics, QStringLiteral("FREQUENCY"));
        pitch_cents_value_ = addMetric(pitch_metrics, QStringLiteral("CENTS FROM NOTE"));
        pitch_quality_value_ = addMetric(
            pitch_metrics, QStringLiteral("VOICING QUALITY"));
        pitch_layout->addLayout(pitch_metrics);

        auto* settings_label = new QLabel(QStringLiteral("ANALYSIS SETTINGS"), pitch_card);
        settings_label->setObjectName(QStringLiteral("sectionLabel"));
        pitch_layout->addWidget(settings_label);
        auto* settings = new QGridLayout();
        settings->setHorizontalSpacing(8);
        settings->setVerticalSpacing(7);
        frame_size_combo_ = new QComboBox(pitch_card);
        for (const auto size : {2'048, 4'096, 8'192}) {
            frame_size_combo_->addItem(QString::number(size), size);
        }
        frame_size_combo_->setCurrentIndex(1);
        hop_size_combo_ = new QComboBox(pitch_card);
        for (const auto size : {128, 256, 512, 1'024}) {
            hop_size_combo_->addItem(QString::number(size), size);
        }
        hop_size_combo_->setCurrentIndex(2);
        gate_spin_ = new QDoubleSpinBox(pitch_card);
        gate_spin_->setRange(0.0, 0.20);
        gate_spin_->setDecimals(3);
        gate_spin_->setSingleStep(0.005);
        gate_spin_->setValue(0.010);
        gate_spin_->setSuffix(QStringLiteral(" RMS"));
        minimum_pitch_spin_ = new QSpinBox(pitch_card);
        minimum_pitch_spin_->setRange(50, 500);
        minimum_pitch_spin_->setValue(80);
        minimum_pitch_spin_->setSuffix(QStringLiteral(" Hz"));
        maximum_pitch_spin_ = new QSpinBox(pitch_card);
        maximum_pitch_spin_->setRange(200, 2'000);
        maximum_pitch_spin_->setValue(1'000);
        maximum_pitch_spin_->setSuffix(QStringLiteral(" Hz"));
        addSetting(settings, 0, QStringLiteral("Frame"), frame_size_combo_);
        addSetting(settings, 1, QStringLiteral("Hop"), hop_size_combo_);
        addSetting(settings, 2, QStringLiteral("Input gate"), gate_spin_);
        addSetting(settings, 3, QStringLiteral("Minimum"), minimum_pitch_spin_);
        addSetting(settings, 4, QStringLiteral("Maximum"), maximum_pitch_spin_);
        pitch_layout->addLayout(settings);

        pitch_detail_label_ = new QLabel(
            QStringLiteral("Start recording and sustain one note."), pitch_card);
        pitch_detail_label_->setObjectName(QStringLiteral("pitchDetail"));
        pitch_detail_label_->setWordWrap(true);
        latency_label_ = new QLabel(
            QStringLiteral("Window delay depends on the selected device sample rate; UI refresh ≤ 33 ms."),
            pitch_card);
        latency_label_->setObjectName(QStringLiteral("footnote"));
        latency_label_->setWordWrap(true);
        replay_analysis_button_ = new QPushButton(
            QStringLiteral("Analyze saved take"), pitch_card);
        replay_analysis_button_->setObjectName(QStringLiteral("secondaryButton"));
        pitch_layout->addWidget(pitch_detail_label_);
        pitch_layout->addWidget(latency_label_);
        pitch_layout->addWidget(replay_analysis_button_);
        new QVBoxLayout(pitch_dialog_);
        pitch_dialog_->layout()->addWidget(pitch_card);

        auto* phrase_card = new QFrame(recording_content);
        phrase_card->setObjectName(QStringLiteral("card"));
        auto* phrase_layout = new QVBoxLayout(phrase_card);
        phrase_layout->setContentsMargins(22, 18, 22, 18);
        phrase_layout->setSpacing(8);
        auto* phrase_title = new QLabel(QStringLiteral("Phrase → MIDI"), phrase_card);
        phrase_title->setObjectName(QStringLiteral("cardTitle"));
        phrase_status_label_ = new QLabel(
            QStringLiteral("Stop a recording to begin post-recording conversion."), phrase_card);
        phrase_status_label_->setObjectName(QStringLiteral("pitchDetail"));
        phrase_status_label_->setWordWrap(true);
        phrase_progress_ = new QProgressBar(phrase_card);
        phrase_progress_->setRange(0, 100);
        phrase_progress_->setValue(0);
        phrase_progress_->setTextVisible(true);
        auto* phrase_controls = new QHBoxLayout();
        convert_button_ = new QPushButton(QStringLiteral("Add saved take again"), phrase_card);
        convert_button_->setToolTip(QStringLiteral("Convert the last in-memory take and append a region on the selected track. Existing regions are retained."));
        cancel_conversion_button_ = new QPushButton(QStringLiteral("Cancel"), phrase_card);
        export_button_ = new QPushButton(QStringLiteral("Export MIDI…"), phrase_card);
        convert_button_->setObjectName(QStringLiteral("secondaryButton"));
        cancel_conversion_button_->setObjectName(QStringLiteral("secondaryButton"));
        export_button_->setObjectName(QStringLiteral("primaryButton"));
        phrase_controls->addWidget(convert_button_);
        phrase_controls->addWidget(cancel_conversion_button_);
        phrase_controls->addStretch();
        phrase_controls->addWidget(export_button_);
        auto* phrase_note = new QLabel(
            QStringLiteral("New takes append regions. Original audio and raw analysis are retained for the latest take only; projects save notes, not recordings."),
            phrase_card);
        phrase_note->setWordWrap(true);
        phrase_note->setObjectName(QStringLiteral("footnote"));
        phrase_layout->addWidget(phrase_title);
        phrase_layout->addWidget(phrase_status_label_);
        phrase_layout->addWidget(phrase_progress_);
        phrase_layout->addLayout(phrase_controls);
        phrase_layout->addWidget(phrase_note);
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
        auto* piano_title = new QLabel(QStringLiteral("Melody workspace"), piano_card);
        piano_title->setObjectName(QStringLiteral("cardTitle"));
        editor_status_label_ = new QLabel(
            QStringLiteral("Record a melody, or double-click the grid to add your first note."),
            piano_card);
        editor_status_label_->setObjectName(QStringLiteral("footnote"));
        editor_status_label_->setWordWrap(true);
        piano_title_box->addWidget(piano_title);
        piano_title_box->addWidget(editor_status_label_);
        piano_heading->addLayout(piano_title_box, 1);

        undo_button_ = new QPushButton(QStringLiteral("Undo"), piano_card);
        redo_button_ = new QPushButton(QStringLiteral("Redo"), piano_card);
        add_note_button_ = new QPushButton(QStringLiteral("+ Add note"), piano_card);
        delete_note_button_ = new QPushButton(QStringLiteral("Delete"), piano_card);
        for (auto* button : {undo_button_, redo_button_, add_note_button_, delete_note_button_}) {
            button->setObjectName(QStringLiteral("secondaryButton"));
            piano_heading->addWidget(button);
        }
        midi_play_button_ = new QPushButton(QStringLiteral("▶  Play melody"), piano_card);
        midi_play_button_->setObjectName(QStringLiteral("midiPlayButton"));
        midi_play_button_->setToolTip(QStringLiteral(
            "Play the visible pitches with the built-in offline soft-tone instrument."));
        piano_heading->addWidget(midi_play_button_);
        piano_layout->addLayout(piano_heading);

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
        rhythm_snap_combo_->addItem(QStringLiteral("Off · keep captured timing"), 0.0);
        rhythm_snap_combo_->addItem(QStringLiteral("¼ beat · 0.125 s"), 0.125);
        rhythm_snap_combo_->addItem(QStringLiteral("½ beat · 0.250 s"), 0.250);
        rhythm_snap_combo_->addItem(QStringLiteral("1 beat · 0.500 s"), 0.500);
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

        piano_roll_ = new PianoRollWidget(note_editor_, piano_card);
        piano_roll_->setObjectName(QStringLiteral("pianoRoll"));
        piano_roll_->setMinimumHeight(280);
        piano_layout->addWidget(piano_roll_, 1);
        auto* piano_window_layout = new QVBoxLayout(piano_dialog_);
        piano_window_layout->addWidget(piano_card);

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
        open_recorder_button_ = featureButton(QStringLiteral("●  Hum → MIDI"), QStringLiteral("openRecorderButton"), recording_dialog_);
        featureButton(QStringLiteral("▥  Piano roll"), QStringLiteral("openPianoRollButton"), piano_dialog_);
        featureButton(QStringLiteral("Pitch Lab"), QStringLiteral("openPitchLabButton"), pitch_dialog_);
        feature_toolbar->addStretch();
        feature_toolbar->addWidget(midi_play_button_);
        feature_toolbar->addWidget(export_button_);
        page->addLayout(feature_toolbar);

        auto* playlist_card = new QFrame(root);
        playlist_card->setObjectName(QStringLiteral("card"));
        auto* playlist_layout = new QVBoxLayout(playlist_card);
        playlist_layout->setContentsMargins(20, 18, 20, 20);
        playlist_layout->setSpacing(14);
        auto* playlist_heading = new QHBoxLayout();
        auto* playlist_title = new QLabel(QStringLiteral("Playlist"), playlist_card);
        playlist_title->setObjectName(QStringLiteral("cardTitle"));
        playlist_heading->addWidget(playlist_title);
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
        playlist_undo_button_ = playlistButton(QStringLiteral("Undo"), QStringLiteral("playlistUndoButton"));
        playlist_redo_button_ = playlistButton(QStringLiteral("Redo"), QStringLiteral("playlistRedoButton"));
        playlist_layout->addLayout(playlist_heading);
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
        playlist_->setObjectName(QStringLiteral("playlist"));
        playlist_->setMinimumHeight(360);
        playlist_layout->addWidget(playlist_, 1);
        page->addWidget(playlist_card, 1);
        shell->addLayout(page, 1);
        window_->setCentralWidget(root);

        window_->setStyleSheet(QStringLiteral(R"(
            #root { background: #f4f4f5; color: #0b0b0b; }
            QDialog { background: #f4f4f5; }
            #rail { background: #080808; border-radius: 22px; }
            #logo { color: white; font-size: 30px; font-weight: 900; }
            #railItem { color: #a6a6a6; font-size: 17px; padding: 13px; }
            #railActive { background: white; color: black; border-radius: 20px; font-size: 18px; padding: 10px; }
            #localBadge { color: #a9a9a9; font-size: 9px; font-weight: 700; }
            #eyebrow { color: #777; font-size: 11px; font-weight: 800; letter-spacing: 2px; }
            #pageTitle { color: #080808; font-size: 29px; font-weight: 800; }
            #subtitle { color: #666; font-size: 14px; }
            #privacyBadge { background: white; border: 1px solid #ddd; border-radius: 17px; padding: 9px 15px; color: #333; font-size: 10px; font-weight: 700; }
            #card { background: #ffffff; border: 1px solid #e4e4e4; border-radius: 18px; }
            #darkCard { background: #080808; border-radius: 18px; }
            #sectionLabel { color: #777; font-size: 11px; font-weight: 800; letter-spacing: 1px; }
            #fieldTitle { color: #222; font-size: 13px; font-weight: 700; }
            #cardTitle { color: #080808; font-size: 20px; font-weight: 800; }
            #pitchState { color: #777; font-size: 10px; font-weight: 900; letter-spacing: 1px; padding-top: 5px; }
            #pitchState[reliable="true"] { color: #18723a; }
            #pitchNote { color: #080808; font-size: 54px; font-weight: 900; }
            #pitchDetail { background: #f3f3f4; border-radius: 9px; padding: 9px; color: #555; font-size: 11px; }
            #statusPanel { background: #f3f3f4; border-radius: 10px; padding: 13px; color: #333; }
            #statusPanel[error="true"] { background: #fff0f0; color: #9c2020; }
            #duration { color: #080808; font-size: 37px; font-weight: 800; padding: 12px; }
            #clipWarning { color: #c52c2c; font-size: 11px; font-weight: 800; }
            QComboBox, QSpinBox, QDoubleSpinBox { background: #f3f3f4; border: 1px solid #ddd; border-radius: 8px; padding: 7px 9px; min-height: 18px; }
            QPushButton { border-radius: 8px; padding: 10px 16px; font-weight: 700; }
            #primaryButton { background: #080808; color: white; border: 1px solid #080808; }
            #stopButton { background: #e34848; color: white; border: 1px solid #e34848; }
            #secondaryButton { background: white; color: #080808; border: 1px solid #cfcfcf; }
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
            #openRecorderButton, #openPianoRollButton, #openPitchLabButton,
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

    void addSetting(
        QGridLayout* layout,
        const int row,
        const QString& name,
        QWidget* editor) {
        auto* label = new QLabel(name, window_);
        label->setObjectName(QStringLiteral("fieldTitle"));
        layout->addWidget(label, row, 0);
        layout->addWidget(editor, row, 1);
    }

    [[nodiscard]] core::PitchDetectorConfig pitchConfig() const {
        core::PitchDetectorConfig config;
        if (frame_size_combo_) {
            config.frame_size = static_cast<std::size_t>(
                frame_size_combo_->currentData().toULongLong());
            config.hop_size = static_cast<std::size_t>(
                hop_size_combo_->currentData().toULongLong());
            config.input_gate_rms = static_cast<float>(gate_spin_->value());
            config.minimum_frequency_hz = minimum_pitch_spin_->value();
            config.maximum_frequency_hz = maximum_pitch_spin_->value();
        }
        return config;
    }

    void updatePitchLatency(
        const std::uint32_t sample_rate_hz = 0,
        const double worker_milliseconds = -1.0) {
        if (sample_rate_hz == 0) {
            latency_label_->setText(QStringLiteral(
                "Window delay depends on the selected device sample rate; UI refresh ≤ 33 ms."));
            return;
        }
        const auto config = pitchConfig();
        const auto window_ms = 1'000.0 * static_cast<double>(config.frame_size) / sample_rate_hz;
        const auto hop_ms = 1'000.0 * static_cast<double>(config.hop_size) / sample_rate_hz;
        auto text = QStringLiteral(
            "Detector window %1 ms · hop %2 ms · UI refresh ≤ 33 ms")
            .arg(window_ms, 0, 'f', 1)
            .arg(hop_ms, 0, 'f', 1);
        if (worker_milliseconds >= 0.0) {
            text += QStringLiteral(" · last worker %1 ms").arg(worker_milliseconds, 0, 'f', 2);
        }
        latency_label_->setText(text);
    }

    void setPitchReliableProperty(const bool reliable) {
        pitch_state_label_->setProperty("reliable", reliable);
        pitch_state_label_->style()->unpolish(pitch_state_label_);
        pitch_state_label_->style()->polish(pitch_state_label_);
    }

    void showNoReliablePitch(const QString& detail) {
        setPitchReliableProperty(false);
        pitch_state_label_->setText(QStringLiteral("NO RELIABLE PITCH"));
        pitch_note_label_->setText(QStringLiteral("—"));
        pitch_frequency_value_->setText(QStringLiteral("—"));
        pitch_cents_value_->setText(QStringLiteral("—"));
        pitch_quality_value_->setText(QStringLiteral("—"));
        pitch_detail_label_->setText(detail);
    }

    void presentPitch(
        const core::DisplayPitchEstimate& display,
        const double worker_milliseconds,
        const bool replayed = false) {
        updatePitchLatency(
            recorder_.currentFormat().value_or(core::AudioFormat{}).sample_rate_hz,
            worker_milliseconds);
        if (!display.reliable) {
            showNoReliablePitch(QStringLiteral("%1 · RMS %2%3")
                .arg(QString::fromLatin1(core::pitchStatusText(display.raw.status)))
                .arg(display.raw.rms_level, 0, 'f', 3)
                .arg(replayed ? QStringLiteral(" · replay") : QString{}));
            pitch_quality_value_->setText(
                QStringLiteral("%1%").arg(display.raw.voicing_quality * 100.0, 0, 'f', 0));
            return;
        }

        setPitchReliableProperty(true);
        pitch_state_label_->setText(QStringLiteral("RELIABLE PITCH"));
        pitch_note_label_->setText(QString::fromStdString(display.note_name));
        pitch_frequency_value_->setText(
            QStringLiteral("%1 Hz").arg(display.frequency_hz, 0, 'f', 2));
        const auto sign = display.cents_deviation >= 0.0
            ? QStringLiteral("+") : QStringLiteral("−");
        pitch_cents_value_->setText(QStringLiteral("%1%2")
            .arg(sign)
            .arg(std::abs(display.cents_deviation), 0, 'f', 1));
        pitch_quality_value_->setText(
            QStringLiteral("%1%").arg(display.raw.voicing_quality * 100.0, 0, 'f', 0));
        pitch_detail_label_->setText(QStringLiteral("Raw %1 Hz · RMS %2%3")
            .arg(display.raw.frequency_hz, 0, 'f', 2)
            .arg(display.raw.rms_level, 0, 'f', 3)
            .arg(replayed ? QStringLiteral(" · replayed take") : QString{}));
    }

    void resetPitchAnalysis(const bool continue_from_latest) {
        pitch_processor_.cancel();
        pitch_smoother_ = core::PitchSmoother(pitchConfig().smoothing_window);
        next_pitch_frame_ = 0;
        if (continue_from_latest) {
            const auto metrics = recorder_.pollMetrics();
            const auto frame_size = static_cast<std::uint64_t>(pitchConfig().frame_size);
            if (metrics.frame_count > frame_size) {
                next_pitch_frame_ = metrics.frame_count - frame_size;
            }
        }
        showNoReliablePitch(QStringLiteral("Waiting for a stable voiced analysis window."));
        if (const auto format = recorder_.currentFormat()) {
            updatePitchLatency(format->sample_rate_hz);
        } else {
            updatePitchLatency();
        }
    }

    void schedulePitchAnalysis(const core::RecordingMetrics& metrics) {
        if (pitch_processor_.isBusy()) {
            return;
        }
        const auto format = recorder_.currentFormat();
        if (!format) {
            return;
        }
        const auto config = pitchConfig();
        updatePitchLatency(format->sample_rate_hz);
        if (!config.isValid(format->sample_rate_hz)) {
            showNoReliablePitch(QStringLiteral(
                "These settings cannot cover the selected range at this sample rate. Increase Frame or raise Minimum."));
            return;
        }
        const auto frame_size = static_cast<std::uint64_t>(config.frame_size);
        const auto hop_size = static_cast<std::uint64_t>(config.hop_size);
        if (metrics.frame_count < frame_size) {
            showNoReliablePitch(QStringLiteral("Waiting for the first complete analysis window."));
            return;
        }
        if (next_pitch_frame_ + frame_size > metrics.frame_count) {
            return;
        }

        constexpr std::uint64_t maximum_batch = 4;
        const auto ready = 1U + (metrics.frame_count - next_pitch_frame_ - frame_size) / hop_size;
        if (ready > maximum_batch) {
            const auto latest_start = metrics.frame_count - frame_size;
            const auto retained_span = (maximum_batch - 1U) * hop_size;
            next_pitch_frame_ = latest_start > retained_span
                ? latest_start - retained_span : 0;
            pitch_smoother_.reset();
        }

        std::vector<PitchFrameJob> jobs;
        jobs.reserve(static_cast<std::size_t>(maximum_batch));
        auto cursor = next_pitch_frame_;
        while (jobs.size() < maximum_batch && cursor + frame_size <= metrics.frame_count) {
            auto samples = recorder_.copyMonoFrames(cursor, config.frame_size);
            if (!samples) {
                break;
            }
            jobs.push_back({cursor, std::move(*samples)});
            cursor += hop_size;
        }
        if (jobs.empty()) {
            return;
        }
        if (pitch_processor_.analyze(
                std::move(jobs), format->sample_rate_hz, config,
                [this, sample_rate = format->sample_rate_hz](LivePitchBatchResult result) {
                    std::optional<core::DisplayPitchEstimate> latest;
                    for (const auto& raw : result.estimates) {
                        latest = pitch_smoother_.process(raw, pitchConfig().reference_a4_hz);
                    }
                    if (latest) {
                        presentPitch(*latest, result.processing_milliseconds);
                    } else {
                        updatePitchLatency(sample_rate, result.processing_milliseconds);
                    }
                    updateUi();
                })) {
            next_pitch_frame_ = cursor;
        }
    }

    void replaySavedTake() {
        const auto recording = recorder_.recording();
        if (!recording) {
            showNoReliablePitch(QStringLiteral("There is no saved take to analyze."));
            return;
        }
        const auto config = pitchConfig();
        if (!config.isValid(recording->format().sample_rate_hz)) {
            showNoReliablePitch(QStringLiteral(
                "These settings are invalid for the saved take's sample rate."));
            return;
        }
        pitch_smoother_.reset();
        showNoReliablePitch(QStringLiteral("Analyzing the saved in-memory take on a worker…"));
        if (!pitch_processor_.replay(
                recording, config, [this, recording](ReplayPitchResult result) {
                    if (result.estimates.empty()) {
                        showNoReliablePitch(QStringLiteral(
                            "The saved take is shorter than one analysis window."));
                    } else {
                        presentPitch(
                            result.estimates.back(), result.processing_milliseconds, true);
                    }
                    updatePitchLatency(
                        recording->format().sample_rate_hz, result.processing_milliseconds);
                    updateUi();
                })) {
            showNoReliablePitch(QStringLiteral("Pitch analysis is already running."));
        }
        updateUi();
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

    void beginPhraseConversion() {
        const auto recording = recorder_.recording();
        if (!recording || recording->sampleCount() == 0) {
            phrase_status_label_->setText(QStringLiteral(
                "No captured samples are available for conversion."));
            updateUi();
            return;
        }
        if (phrase_processor_.isBusy()) {
            return;
        }
        stopMelodyPlayback(QStringLiteral(
            "Melody playback stopped while the recording is re-transcribed."));
        phrase_progress_->setValue(0);
        phrase_status_label_->setText(QStringLiteral(
            "Analyzing the saved audio after recording… 0%"));
        const auto conversion_track = playlist_->selectedTrack();
        double conversion_offset = 0.0;
        for (const auto& region : note_editor_.regions()) {
            if (region.track == conversion_track) {
                for (const auto index : region.note_indices) {
                    const auto& note = note_editor_.notes()[index];
                    conversion_offset = std::max(conversion_offset, note.start_seconds + note.duration_seconds);
                }
            }
        }
        if (!phrase_processor_.convert(
                recording,
                pitchConfig(),
                core::NoteExtractionConfig{},
                tempo_spin_->value(),
                core::kDefaultTicksPerQuarter,
                [this, conversion_track, conversion_offset](PhraseWorkerResult result) {
                    phrase_progress_->setValue(100);
                    last_conversion_ = std::move(result.conversion);
                    if (last_conversion_->status == core::PhraseConversionStatus::success) {
                        const auto added = note_editor_.appendRegion(last_conversion_->notes,
                            conversion_offset, conversion_track);
                        if (added != core::NoteEditResult::applied) {
                            phrase_status_label_->setText(QStringLiteral("The converted phrase could not be added safely. Existing regions are unchanged."));
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
                    } else if (last_conversion_->status == core::PhraseConversionStatus::empty) {
                        phrase_status_label_->setText(QStringLiteral("No notes found; existing Playlist regions are retained: %1")
                            .arg(QString::fromStdString(last_conversion_->message)));
                    } else {
                        phrase_status_label_->setText(QStringLiteral("%1: %2")
                            .arg(QString::fromLatin1(core::phraseConversionStatusText(
                                last_conversion_->status)))
                            .arg(QString::fromStdString(last_conversion_->message)));
                    }
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
        workspace_message_label_->setText(phrase_status_label_->text());
        updateUi();
    }

    [[nodiscard]] core::ProjectDocument currentProjectDocument() const {
        core::ProjectDocument project;
        project.tempo_bpm = tempo_spin_->value();
        project.ticks_per_quarter = core::kDefaultTicksPerQuarter;
        project.root_pitch_class = static_cast<std::uint8_t>(
            root_pitch_combo_->currentData().toUInt());
        project.scale_mode = selectedScaleMode();
        project.magnet_enabled = note_editor_.magnetEnabled();
        project.rhythm_snap = piano_roll_->rhythmSnap();
        project.canonical_notes = note_editor_.notes();
        project.playlist_regions = note_editor_.regions();
        project.track_count = note_editor_.trackCount();
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
            "Project saved safely: %1. Editable notes and settings are stored; the original recording remains session-only and is not embedded.")
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
            "Project saved safely: %1. Original audio is not embedded.").arg(project_path_));
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
            QStringLiteral("Save your editable notes and settings before you %1?\n\n"
                "Project files do not include the in-memory original recording.").arg(action),
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
        pitch_processor_.cancel();
        phrase_processor_.cancel();
        applying_project_ = true;
        const auto& project = *loaded.project;
        {
            const QSignalBlocker root_blocker(root_pitch_combo_);
            const QSignalBlocker scale_blocker(scale_mode_combo_);
            const QSignalBlocker magnet_blocker(magnet_button_);
            const QSignalBlocker snap_blocker(rhythm_snap_combo_);
            const QSignalBlocker tempo_blocker(tempo_spin_);
            root_pitch_combo_->setCurrentIndex(root_pitch_combo_->findData(project.root_pitch_class));
            scale_mode_combo_->setCurrentIndex(scale_mode_combo_->findData(
                static_cast<int>(project.scale_mode)));
            magnet_button_->setChecked(project.magnet_enabled);
            auto snap_index = 0;
            if (project.rhythm_snap.enabled) {
                snap_index = rhythm_snap_combo_->findData(project.rhythm_snap.interval_seconds);
                if (snap_index < 0) {
                    rhythm_snap_combo_->addItem(
                        QStringLiteral("Custom · %1 s").arg(
                            project.rhythm_snap.interval_seconds, 0, 'f', 3),
                        project.rhythm_snap.interval_seconds);
                    snap_index = rhythm_snap_combo_->count() - 1;
                }
            }
            rhythm_snap_combo_->setCurrentIndex(snap_index);
            tempo_spin_->setValue(project.tempo_bpm);
        }
        static_cast<void>(note_editor_.setScale(core::makeScaleDefinition(
            project.root_pitch_class, project.scale_mode)));
        note_editor_.setMagnetEnabled(project.magnet_enabled);
        static_cast<void>(note_editor_.replaceAll(project.canonical_notes,
            project.playlist_regions, project.track_count));
        piano_roll_->setRhythmSnap(project.rhythm_snap);
        playlist_->setRhythmSnap(project.rhythm_snap);
        piano_roll_->refresh(true);
        static_cast<void>(recorder_.clearRecording());
        updateMetadata();
        last_conversion_.reset();
        project_path_ = source;
        dirty_ = false;
        applying_project_ = false;
        phrase_progress_->setValue(0);
        phrase_status_label_->setText(QStringLiteral(
            "Project loaded: %1. Its canonical notes and settings are editable. Original audio is omitted by the project format, so Play original is unavailable.")
            .arg(source));
        updateScaleGuidanceUi();
        updateEditorUi();
        updateWindowTitle();
        updateUi();
        workspace_message_label_->setText(phrase_status_label_->text());
    }

    void stopMelodyPlayback(const QString& message = {}) {
        const auto was_active = midi_playback_.isPlaying() || midi_playback_.isPreparing();
        midi_playback_.stop();
        piano_roll_->setPlayheadSeconds(std::nullopt);
        playlist_->setPlayheadSeconds(std::nullopt);
        if (was_active && !message.isEmpty()) {
            editor_status_label_->setText(message);
            workspace_message_label_->setText(message);
        }
    }

    void toggleMidiPlayback() {
        if (midi_playback_.isPlaying() || midi_playback_.isPreparing()) {
            stopMelodyPlayback(QStringLiteral("Melody playback stopped. No notes remain sounding."));
            updateUi();
            return;
        }
        if (note_editor_.projectedNotes().empty()) {
            editor_status_label_->setText(QStringLiteral(
                "Add or convert at least one note before playing the melody."));
            return;
        }
        playback_.stop();
        midi_playback_.play(note_editor_.projectedNotes(),
            [this](const QString& message, const bool error) {
                editor_status_label_->setText(message);
                workspace_message_label_->setText(message);
                if (!midi_playback_.isPlaying() && !midi_playback_.isPreparing()) {
                    piano_roll_->setPlayheadSeconds(std::nullopt);
                    playlist_->setPlayheadSeconds(std::nullopt);
                }
                if (error) {
                    setStatus(message, true);
                }
                updateUi();
            });
        piano_roll_->setPlayheadSeconds(0.0);
        playlist_->setPlayheadSeconds(0.0);
        updateUi();
    }

    void exportMidi() {
        if (note_editor_.notes().empty()) {
            phrase_status_label_->setText(QStringLiteral(
                "Add or convert at least one valid note before exporting."));
            return;
        }
        const auto sequence = core::MidiTimeline::fromTimedNotes(
            note_editor_.projectedNotes(), tempo_spin_->value(), core::kDefaultTicksPerQuarter);
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
        QObject::connect(record_button_, &QPushButton::clicked, window_, [this] { startRecording(); });
        QObject::connect(stop_button_, &QPushButton::clicked, window_, [this] { stopRecording(); });
        QObject::connect(play_button_, &QPushButton::clicked, window_, [this] { togglePlayback(); });
        QObject::connect(replay_analysis_button_, &QPushButton::clicked, window_,
            [this] { replaySavedTake(); });
        QObject::connect(convert_button_, &QPushButton::clicked, window_,
            [this] { beginPhraseConversion(); });
        QObject::connect(cancel_conversion_button_, &QPushButton::clicked, window_,
            [this] { cancelPhraseConversion(); });
        QObject::connect(export_button_, &QPushButton::clicked, window_,
            [this] { exportMidi(); });
        QObject::connect(midi_play_button_, &QPushButton::clicked, window_,
            [this] { toggleMidiPlayback(); });
        QObject::connect(undo_button_, &QPushButton::clicked, window_,
            [this] { piano_roll_->undo(); });
        QObject::connect(redo_button_, &QPushButton::clicked, window_,
            [this] { piano_roll_->redo(); });
        QObject::connect(playlist_undo_button_, &QPushButton::clicked, window_, [this] { piano_roll_->undo(); });
        QObject::connect(playlist_redo_button_, &QPushButton::clicked, window_, [this] { piano_roll_->redo(); });
        QObject::connect(duplicate_region_button_, &QPushButton::clicked, window_, [this] { playlist_->duplicateSelected(); });
        QObject::connect(delete_region_button_, &QPushButton::clicked, window_, [this] { playlist_->deleteSelected(); });
        QObject::connect(add_track_button_, &QPushButton::clicked, window_, [this] {
            if (note_editor_.addTrack() == core::NoteEditResult::applied) {
                markDirty(); updateEditorUi(); updateUi();
            }
        });
        QObject::connect(add_note_button_, &QPushButton::clicked, window_,
            [this] {
                showFeature(piano_dialog_);
                piano_roll_->addNoteAtViewportCenter();
            });
        QObject::connect(delete_note_button_, &QPushButton::clicked, window_,
            [this] { piano_roll_->deleteSelected(); });
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
        QObject::connect(rhythm_snap_combo_, &QComboBox::currentIndexChanged, window_,
            [this](const int index) {
                const auto interval = rhythm_snap_combo_->itemData(index).toDouble();
                piano_roll_->setRhythmSnap({index > 0, interval > 0.0 ? interval : 0.125});
                playlist_->setRhythmSnap(piano_roll_->rhythmSnap());
                editor_status_label_->setText(index == 0
                    ? QStringLiteral("Rhythm snap is off; captured timing is preserved.")
                    : QStringLiteral("Rhythm snap affects time only. It is separate from Magnet pitch guidance."));
                markDirty();
            });
        QObject::connect(tempo_spin_, &QDoubleSpinBox::valueChanged, window_,
            [this](const double value) {
                playlist_->setTempo(value);
                markDirty();
            });
        playlist_->setOpenEditorCallback([this] {
            showFeature(piano_dialog_);
            piano_roll_->refresh(true);
            piano_roll_->setFocus();
        });
        playlist_->setDocumentChangedCallback([this] {
            stopMelodyPlayback(QStringLiteral("Playback stopped because the arrangement changed."));
            piano_roll_->refresh(); markDirty(); updateEditorUi(); updateUi();
        });
        playlist_->setSelectionChangedCallback([this] { updateEditorUi(); updateUi(); });
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
        QObject::connect(frame_size_combo_, &QComboBox::currentIndexChanged, window_,
            [this](const int) { resetPitchAnalysis(true); });
        QObject::connect(hop_size_combo_, &QComboBox::currentIndexChanged, window_,
            [this](const int) { resetPitchAnalysis(true); });
        QObject::connect(gate_spin_, &QDoubleSpinBox::valueChanged, window_,
            [this](const double) { resetPitchAnalysis(true); });
        QObject::connect(minimum_pitch_spin_, &QSpinBox::valueChanged, window_,
            [this](const int value) {
                maximum_pitch_spin_->setMinimum(value + 1);
                resetPitchAnalysis(true);
            });
        QObject::connect(maximum_pitch_spin_, &QSpinBox::valueChanged, window_,
            [this](const int value) {
                minimum_pitch_spin_->setMaximum(value - 1);
                resetPitchAnalysis(true);
            });
        QObject::connect(&monitor_, &QTimer::timeout, window_, [this] { poll(); });
        capture_.setDevicesChangedCallback([this] {
            QMetaObject::invokeMethod(window_, [this] { refreshDevices(); }, Qt::QueuedConnection);
        });
    }

    void refreshDevices() {
        const auto previous_id = device_combo_->currentData().toString();
        device_combo_->clear();
        for (const auto& device : capture_.devices()) {
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

        const auto permission = capture_.permissionStatus();
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
        capture_.requestPermission([this](const core::PermissionStatus) {
            QMetaObject::invokeMethod(window_, [this] {
                permission_button_->setEnabled(true);
                refreshDevices();
            }, Qt::QueuedConnection);
        });
    }

    void startRecording() {
        stopMelodyPlayback();
        playback_.stop();
        if (recorder_.state() == core::RecordingState::error) {
            static_cast<void>(recorder_.acknowledgeError());
        }
        if (device_combo_->currentIndex() < 0) {
            setStatus(errorMessage(core::ServiceStatus::unavailable), true);
            return;
        }
        const auto result = recorder_.start(device_combo_->currentData().toString().toStdString());
        switch (result) {
        case core::StartRecordingResult::started:
            phrase_processor_.cancel();
            clearPhraseConversion(QStringLiteral(
                "Recording in progress. After Stop, conversion adds a new region; existing regions are retained."), false);
            resetPitchAnalysis(false);
            level_meter_->setLevel(0.0F, false);
            clip_label_->setVisible(false);
            setStatus(QStringLiteral("Recording… hum or sing one monophonic phrase."), false);
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
        updateUi();
    }

    void stopRecording() {
        if (recorder_.stop()) {
            presentCompletedRecording(core::RecordingStopReason::user);
        }
        updateUi();
    }

    void presentCompletedRecording(const core::RecordingStopReason reason) {
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
        if (recording && recording->sampleCount() > 0) {
            beginPhraseConversion();
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
        playback_.play(recorder_.recording(), [this](const QString& message, const bool error) {
            setStatus(message, error);
            updateUi();
        });
        updateUi();
    }

    void poll() {
        if (const auto event = recorder_.service()) {
            if (*event == core::RecordingStopReason::device_failure) {
                setStatus(errorMessage(recorder_.lastError()), true);
                updateMetadata();
                if (recorder_.recording() && recorder_.recording()->sampleCount() > 0) {
                    beginPhraseConversion();
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
            schedulePitchAnalysis(metrics);
        } else {
            displayed_level_ = 0.0F;
        }
        if (phrase_processor_.isBusy()) {
            const auto percent = static_cast<int>(std::lround(
                std::clamp(phrase_processor_.progress(), 0.0, 1.0) * 100.0));
            phrase_progress_->setValue(percent);
            phrase_status_label_->setText(QStringLiteral(
                "Analyzing the saved audio after recording… %1%").arg(percent));
            workspace_message_label_->setText(phrase_status_label_->text());
        }
        if (midi_playback_.isPlaying()) {
            piano_roll_->setPlayheadSeconds(midi_playback_.positionSeconds(), true);
            playlist_->setPlayheadSeconds(midi_playback_.positionSeconds(), true);
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
        const auto enabled = note_editor_.magnetEnabled();
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
        playlist_status_label_->setText(QStringLiteral(
            "%1 track(s) · %2 region(s) · %3 notes · New takes go to Track %4. Drag regions to arrange; double-click to correct notes. %5 · Magnet %6.")
            .arg(note_editor_.trackCount()).arg(note_editor_.regions().size())
            .arg(note_editor_.notes().size()).arg(playlist_->selectedTrack() + 1)
            .arg(selectedScaleName()).arg(note_editor_.magnetEnabled() ? QStringLiteral("ON") : QStringLiteral("OFF")));
        undo_button_->setEnabled(note_editor_.canUndo());
        redo_button_->setEnabled(note_editor_.canRedo());
        undo_button_->setText(note_editor_.canUndo()
            ? QStringLiteral("Undo %1").arg(QString::fromStdString(note_editor_.undoLabel()).toLower())
            : QStringLiteral("Undo"));
        redo_button_->setText(note_editor_.canRedo()
            ? QStringLiteral("Redo %1").arg(QString::fromStdString(note_editor_.redoLabel()).toLower())
            : QStringLiteral("Redo"));
        delete_note_button_->setEnabled(note_editor_.selectedIndex().has_value());

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
        const auto recording_active = state == core::RecordingState::recording
            || state == core::RecordingState::starting
            || state == core::RecordingState::stopping;
        const auto can_record = !recording_active
            && state != core::RecordingState::closed
            && capture_.permissionStatus() == core::PermissionStatus::granted
            && device_combo_->count() > 0;
        open_recorder_button_->setText(recording_active
            ? QStringLiteral("●  Recording… open controls") : QStringLiteral("●  Hum → MIDI"));
        record_button_->setEnabled(can_record);
        stop_button_->setEnabled(state == core::RecordingState::recording);
        device_combo_->setEnabled(!recording_active);
        refresh_button_->setEnabled(!recording_active);
        permission_button_->setEnabled(!recording_active);
        open_project_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        save_project_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());

        const auto recording = recorder_.recording();
        const auto has_audio = recording && recording->sampleCount() > 0;
        play_button_->setEnabled(!recording_active && has_audio);
        play_button_->setText(playback_.isPlaying() || playback_.isPreparing()
            ? QStringLiteral("■  Stop playback") : QStringLiteral("▶  Play original"));
        replay_analysis_button_->setEnabled(
            !recording_active && has_audio && !pitch_processor_.isBusy());
        convert_button_->setEnabled(
            !recording_active && has_audio && !phrase_processor_.isBusy());
        cancel_conversion_button_->setEnabled(phrase_processor_.isBusy());
        export_button_->setEnabled(
            !recording_active && !phrase_processor_.isBusy()
            && !note_editor_.notes().empty());
        midi_play_button_->setEnabled(
            !recording_active && !phrase_processor_.isBusy()
            && !note_editor_.notes().empty());
        midi_play_button_->setText(midi_playback_.isPlaying() || midi_playback_.isPreparing()
            ? QStringLiteral("■  Stop melody") : QStringLiteral("▶  Play melody"));
        add_note_button_->setEnabled(!recording_active && !phrase_processor_.isBusy());
        delete_note_button_->setEnabled(
            !recording_active && !phrase_processor_.isBusy()
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

    MainWindow* window_;
    QtAudioCapture capture_;
    core::RecordingController recorder_;
    QtAudioPlayback playback_;
    QtMidiPlayback midi_playback_;
    PitchLabProcessor pitch_processor_;
    PhraseConversionProcessor phrase_processor_;
    core::PitchSmoother pitch_smoother_;
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
    QLabel* pitch_state_label_{nullptr};
    QLabel* pitch_note_label_{nullptr};
    QLabel* pitch_frequency_value_{nullptr};
    QLabel* pitch_cents_value_{nullptr};
    QLabel* pitch_quality_value_{nullptr};
    QLabel* pitch_detail_label_{nullptr};
    QLabel* latency_label_{nullptr};
    QComboBox* frame_size_combo_{nullptr};
    QComboBox* hop_size_combo_{nullptr};
    QDoubleSpinBox* gate_spin_{nullptr};
    QSpinBox* minimum_pitch_spin_{nullptr};
    QSpinBox* maximum_pitch_spin_{nullptr};
    QPushButton* replay_analysis_button_{nullptr};
    QLabel* phrase_status_label_{nullptr};
    QProgressBar* phrase_progress_{nullptr};
    QPushButton* convert_button_{nullptr};
    QPushButton* cancel_conversion_button_{nullptr};
    QPushButton* export_button_{nullptr};
    QPushButton* midi_play_button_{nullptr};
    PianoRollWidget* piano_roll_{nullptr};
    PlaylistWidget* playlist_{nullptr};
    QDialog* recording_dialog_{nullptr};
    QDialog* piano_dialog_{nullptr};
    QDialog* pitch_dialog_{nullptr};
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
    QComboBox* root_pitch_combo_{nullptr};
    QComboBox* scale_mode_combo_{nullptr};
    QPushButton* magnet_button_{nullptr};
    QLabel* scale_guidance_label_{nullptr};
    QComboBox* rhythm_snap_combo_{nullptr};
    QDoubleSpinBox* tempo_spin_{nullptr};
    QSlider* horizontal_zoom_slider_{nullptr};
    QSlider* vertical_zoom_slider_{nullptr};
    std::optional<core::PhraseConversionResult> last_conversion_;
    std::uint64_t next_pitch_frame_{0};
    float displayed_level_{0.0F};
    QString project_path_;
    bool dirty_{false};
    bool applying_project_{false};
    bool closed_{false};
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      impl_(std::make_unique<Impl>(this)) {
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
