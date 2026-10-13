#include "MainWindow.hpp"

#include "LevelMeter.hpp"
#include "PhraseConversionProcessor.hpp"
#include "PitchLabProcessor.hpp"
#include "QtAudioCapture.hpp"
#include "QtAudioPlayback.hpp"
#include "hum/core/midi_file.hpp"
#include "hum/core/note_extraction.hpp"
#include "hum/core/pitch.hpp"
#include "hum/core/recording.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
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
    explicit Impl(MainWindow* window)
        : window_(window),
          capture_(window),
          recorder_(capture_, 30.0, 0.98F),
          playback_(window),
          pitch_processor_(window),
          phrase_processor_(window),
          pitch_smoother_(5) {
        buildUi();
        wireEvents();
        refreshDevices();
        updateUi();
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
        pitch_processor_.cancel();
        phrase_processor_.cancel();
        recorder_.shutdown();
    }

private:
    void buildUi() {
        window_->setWindowTitle(QStringLiteral("Hum-to-MIDI Music Sketchpad — Phrase to MIDI"));
        window_->setMinimumSize(1'120, 760);
        window_->resize(1'320, 900);

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
        auto* eyebrow = new QLabel(QStringLiteral("HUM · INSPECT · EXPORT"), root);
        eyebrow->setObjectName(QStringLiteral("eyebrow"));
        auto* title = new QLabel(QStringLiteral("Turn one hummed phrase into MIDI"), root);
        title->setObjectName(QStringLiteral("pageTitle"));
        auto* subtitle = new QLabel(
            QStringLiteral("Record locally, inspect conservative note events, then export a standard MIDI file."), root);
        subtitle->setObjectName(QStringLiteral("subtitle"));
        heading_box->addWidget(eyebrow);
        heading_box->addWidget(title);
        heading_box->addWidget(subtitle);
        header->addLayout(heading_box, 1);
        auto* privacy = new QLabel(QStringLiteral("●  IN MEMORY · NEVER UPLOADED"), root);
        privacy->setObjectName(QStringLiteral("privacyBadge"));
        privacy->setAlignment(Qt::AlignCenter);
        header->addWidget(privacy, 0, Qt::AlignTop);
        page->addLayout(header);

        auto* columns = new QHBoxLayout();
        columns->setSpacing(18);
        auto* recorder_card = new QFrame(root);
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
        columns->addWidget(recorder_card, 2);

        auto* side = new QVBoxLayout();
        side->setSpacing(18);
        auto* pitch_card = new QFrame(root);
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
        side->addWidget(pitch_card, 2);

        auto* phrase_card = new QFrame(root);
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
        phrase_table_ = new QTableWidget(0, 4, phrase_card);
        phrase_table_->setHorizontalHeaderLabels({
            QStringLiteral("Note"), QStringLiteral("Start"),
            QStringLiteral("Duration"), QStringLiteral("Velocity")});
        phrase_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        phrase_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        phrase_table_->verticalHeader()->setVisible(false);
        phrase_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        phrase_table_->horizontalHeader()->setStretchLastSection(true);
        phrase_table_->setMinimumHeight(130);
        auto* phrase_controls = new QHBoxLayout();
        convert_button_ = new QPushButton(QStringLiteral("Convert again"), phrase_card);
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
            QStringLiteral("Original seconds retained · 120 BPM · 480 TPQ · fixed velocity 96"),
            phrase_card);
        phrase_note->setObjectName(QStringLiteral("footnote"));
        phrase_layout->addWidget(phrase_title);
        phrase_layout->addWidget(phrase_status_label_);
        phrase_layout->addWidget(phrase_progress_);
        phrase_layout->addWidget(phrase_table_);
        phrase_layout->addLayout(phrase_controls);
        phrase_layout->addWidget(phrase_note);
        side->addWidget(phrase_card, 1);
        columns->addLayout(side, 2);

        page->addLayout(columns, 1);
        shell->addLayout(page, 1);
        window_->setCentralWidget(root);

        window_->setStyleSheet(QStringLiteral(R"(
            #root { background: #f4f4f5; color: #0b0b0b; }
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
            QPushButton:disabled { background: #e8e8e8; color: #999; border-color: #ddd; }
            #metricCard { background: #f4f4f5; border-radius: 10px; }
            #metricLabel { color: #888; font-size: 9px; font-weight: 800; }
            #metricValue { color: #0b0b0b; font-size: 15px; font-weight: 800; }
            #footnote { color: #888; font-size: 11px; }
            #workflowActive { background: #080808; color: white; border-radius: 9px; padding: 6px; font-weight: 800; }
            #workflowNumber { background: #ededed; color: #777; border-radius: 9px; padding: 6px; font-weight: 800; }
            #workflowTitle { color: #111; font-size: 14px; font-weight: 800; }
            #workflowCopy { color: #888; font-size: 11px; }
            #darkTitle { color: white; font-size: 17px; font-weight: 800; }
            #darkCopy { color: #c7c7c7; font-size: 12px; }
            QProgressBar { background: #ededed; border: 0; border-radius: 5px; height: 10px; text-align: center; color: #555; }
            QProgressBar::chunk { background: #080808; border-radius: 5px; }
            QTableWidget { background: #fafafa; border: 1px solid #e1e1e1; border-radius: 8px; gridline-color: #ededed; }
            QHeaderView::section { background: #f0f0f0; border: 0; padding: 5px; font-weight: 700; }
        )"));
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

    void clearPhraseConversion(const QString& message) {
        last_conversion_.reset();
        phrase_table_->setRowCount(0);
        phrase_progress_->setValue(0);
        phrase_status_label_->setText(message);
    }

    void beginPhraseConversion() {
        const auto recording = recorder_.recording();
        if (!recording || recording->sampleCount() == 0) {
            clearPhraseConversion(QStringLiteral(
                "No captured samples are available for conversion."));
            updateUi();
            return;
        }
        if (phrase_processor_.isBusy()) {
            return;
        }
        clearPhraseConversion(QStringLiteral(
            "Analyzing the saved audio after recording… 0%"));
        if (!phrase_processor_.convert(
                recording,
                pitchConfig(),
                core::NoteExtractionConfig{},
                core::kDefaultTempoBpm,
                core::kDefaultTicksPerQuarter,
                [this](PhraseWorkerResult result) {
                    phrase_progress_->setValue(100);
                    last_conversion_ = std::move(result.conversion);
                    phrase_table_->setRowCount(static_cast<int>(last_conversion_->notes.size()));
                    for (std::size_t row = 0; row < last_conversion_->notes.size(); ++row) {
                        const auto& note = last_conversion_->notes[row];
                        const auto values = std::array<QString, 4>{
                            QString::fromStdString(core::midiNoteName(note.pitch)),
                            QStringLiteral("%1 s").arg(note.start_seconds, 0, 'f', 3),
                            QStringLiteral("%1 s").arg(note.duration_seconds, 0, 'f', 3),
                            QString::number(note.velocity),
                        };
                        for (int column = 0; column < static_cast<int>(values.size()); ++column) {
                            auto* item = new QTableWidgetItem(values[static_cast<std::size_t>(column)]);
                            item->setTextAlignment(Qt::AlignCenter);
                            phrase_table_->setItem(static_cast<int>(row), column, item);
                        }
                    }

                    if (last_conversion_->status == core::PhraseConversionStatus::success) {
                        phrase_status_label_->setText(QStringLiteral(
                            "%1 note event(s) · %2 raw frames · %3 ms processing. Original timing retained.")
                            .arg(last_conversion_->notes.size())
                            .arg(last_conversion_->raw_pitch_frames.size())
                            .arg(result.processing_milliseconds, 0, 'f', 1));
                    } else {
                        phrase_status_label_->setText(QStringLiteral("%1: %2")
                            .arg(QString::fromLatin1(core::phraseConversionStatusText(
                                last_conversion_->status)))
                            .arg(QString::fromStdString(last_conversion_->message)));
                    }
                    updateUi();
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
        updateUi();
    }

    void exportMidi() {
        if (!last_conversion_ || !last_conversion_->hasNotes()) {
            phrase_status_label_->setText(QStringLiteral(
                "Convert a phrase with at least one reliable note before exporting."));
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
            last_conversion_->midi_sequence,
            std::filesystem::path(destination.toStdU16String()));
        phrase_status_label_->setText(status == core::ServiceStatus::ok
            ? QStringLiteral("MIDI file saved locally: %1").arg(destination)
            : QStringLiteral("MIDI export failed. Check the destination and try again."));
    }

    void wireEvents() {
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
                "Recording in progress. Conversion begins after Stop."));
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
        }
        updateUi();
    }

    void updateMetadata() {
        const auto recording = recorder_.recording();
        if (!recording) {
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

    void updateUi() {
        const auto state = recorder_.state();
        const auto recording_active = state == core::RecordingState::recording
            || state == core::RecordingState::starting
            || state == core::RecordingState::stopping;
        const auto can_record = !recording_active
            && state != core::RecordingState::closed
            && capture_.permissionStatus() == core::PermissionStatus::granted
            && device_combo_->count() > 0;
        record_button_->setEnabled(can_record);
        stop_button_->setEnabled(state == core::RecordingState::recording);
        device_combo_->setEnabled(!recording_active);
        refresh_button_->setEnabled(!recording_active);
        permission_button_->setEnabled(!recording_active);

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
            && last_conversion_ && last_conversion_->hasNotes());
    }

    void setStatus(const QString& text, const bool error) {
        status_label_->setText(text);
        status_label_->setProperty("error", error);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
    }

    MainWindow* window_;
    QtAudioCapture capture_;
    core::RecordingController recorder_;
    QtAudioPlayback playback_;
    PitchLabProcessor pitch_processor_;
    PhraseConversionProcessor phrase_processor_;
    core::PitchSmoother pitch_smoother_;
    QTimer monitor_;
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
    QTableWidget* phrase_table_{nullptr};
    QPushButton* convert_button_{nullptr};
    QPushButton* cancel_conversion_button_{nullptr};
    QPushButton* export_button_{nullptr};
    std::optional<core::PhraseConversionResult> last_conversion_;
    std::uint64_t next_pitch_frame_{0};
    float displayed_level_{0.0F};
    bool closed_{false};
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      impl_(std::make_unique<Impl>(this)) {
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    impl_->shutdown();
    QMainWindow::closeEvent(event);
}

} // namespace hum::app
