#include "SingToMidiPanel.hpp"
#include "VocalPreviewWidget.hpp"
#include "VocalModelAssets.hpp"
#include "hum/vocal/neural_pitch.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace hum::app::vocal {
SingToMidiPanel::SingToMidiPanel(QWidget* parent, core::IAudioCapture* capture)
    : QDialog(parent), capture_(this), active_capture_(capture ? capture : &capture_),
      recorder_(*active_capture_), preview_playback_(this) {
    setObjectName(QStringLiteral("singToMidiPanel"));
    setWindowTitle(QStringLiteral("Sing to MIDI — Piano Roll"));
    setWindowModality(Qt::ApplicationModal);
    resize(880, 690);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(22, 18, 22, 18); layout->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("Sing to MIDI"), this);
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 600;")); layout->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("A separate local neural vocal engine. Record → convert → preview → insert."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    origin_label_ = new QLabel(this); origin_label_->setWordWrap(true); layout->addWidget(origin_label_);
    auto* input = new QHBoxLayout;
    devices_ = new QComboBox(this); devices_->setObjectName(QStringLiteral("vocalInputDevice"));
    devices_->setAccessibleName(QStringLiteral("Sing to MIDI microphone"));
    auto* refresh = new QPushButton(QStringLiteral("Refresh microphones"), this);
    input->addWidget(devices_, 1); input->addWidget(refresh); layout->addLayout(input);
    level_ = new QProgressBar(this); level_->setRange(0, 100); level_->setTextVisible(false);
    level_->setFixedHeight(9); level_->setAccessibleName(QStringLiteral("Vocal input level")); layout->addWidget(level_);
    elapsed_ = new QLabel(QStringLiteral("0.0 / 30.0 seconds"), this); layout->addWidget(elapsed_);
    auto* actions = new QHBoxLayout;
    record_ = new QPushButton(QStringLiteral("Record"), this); record_->setObjectName(QStringLiteral("vocalRecordButton"));
    stop_ = new QPushButton(QStringLiteral("Stop recording"), this); stop_->setObjectName(QStringLiteral("vocalStopButton"));
    convert_ = new QPushButton(QStringLiteral("Convert to MIDI"), this); convert_->setObjectName(QStringLiteral("vocalConvertButton"));
    cancel_ = new QPushButton(QStringLiteral("Cancel conversion"), this); cancel_->setObjectName(QStringLiteral("vocalCancelButton"));
    for (auto* button : {record_, stop_, convert_, cancel_}) { actions->addWidget(button); }
    layout->addLayout(actions);
    auto* settings = new QHBoxLayout;
    preset_ = new QComboBox(this); preset_->setObjectName(QStringLiteral("vocalPreset"));
    preset_->addItems({QStringLiteral("Natural"), QStringLiteral("Clean"), QStringLiteral("Tight")});
    preset_->setToolTip(QStringLiteral("Natural preserves quick changes. Clean steadies the contour. Tight requires longer pitch changes. Timing changes only when you select a quantize grid."));
    scale_snap_ = new QCheckBox(QStringLiteral("Scale Snap"), this); scale_snap_->setObjectName(QStringLiteral("vocalScaleSnap"));
    scale_snap_->setToolTip(QStringLiteral("Preview the closest notes in the current project key and scale. Turn off to restore this take's original detected pitches."));
    quantize_ = new QComboBox(this); quantize_->setObjectName(QStringLiteral("vocalTimeQuantize"));
    quantize_->addItem(QStringLiteral("Off"), 0);
    for (int value : {4, 8, 16}) { quantize_->addItem(QStringLiteral("1/%1").arg(value), value); }
    auto* bends = new QCheckBox(QStringLiteral("Pitch bends unavailable"), this); bends->setEnabled(false);
    bends->setToolTip(QStringLiteral("This project supports notes with fixed pitches. Slides and vibrato are approximated without pitch-bend automation."));
    settings->addWidget(new QLabel(QStringLiteral("Preset"), this)); settings->addWidget(preset_);
    settings->addWidget(scale_snap_); settings->addWidget(new QLabel(QStringLiteral("Time quantize"), this));
    settings->addWidget(quantize_); settings->addStretch(); settings->addWidget(bends); layout->addLayout(settings);
    QSettings preferences;
    preset_->setCurrentIndex(std::clamp(preferences.value(QStringLiteral("singToMidi/preset"), 0).toInt(), 0, 2));
    scale_snap_->setChecked(preferences.value(QStringLiteral("singToMidi/scaleSnap"), false).toBool());
    quantize_->setCurrentIndex(std::clamp(preferences.value(QStringLiteral("singToMidi/quantize"), 0).toInt(), 0, 3));
    progress_ = new QProgressBar(this); progress_->setRange(0, 100); progress_->setValue(0); layout->addWidget(progress_);
    preview_ = new VocalPreviewWidget(this); layout->addWidget(preview_, 1);
    status_ = new QLabel(QStringLiteral("Choose a microphone. Your take stays on this Mac."), this);
    status_->setObjectName(QStringLiteral("vocalStatus")); status_->setWordWrap(true); status_->setMinimumHeight(46); layout->addWidget(status_);
    auto* finish = new QHBoxLayout;
    play_ = new QPushButton(QStringLiteral("Play Preview"), this); play_->setObjectName(QStringLiteral("vocalPlayPreviewButton"));
    insert_ = new QPushButton(QStringLiteral("Insert to Selected Track"), this); insert_->setObjectName(QStringLiteral("vocalInsertButton"));
    discard_ = new QPushButton(QStringLiteral("Discard"), this); discard_->setObjectName(QStringLiteral("vocalDiscardButton"));
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    for (auto* button : {play_, insert_, discard_, close}) { finish->addWidget(button); }
    layout->addLayout(finish);
    for (auto* button : {record_, stop_, convert_, cancel_, play_, insert_, discard_, close, refresh}) {
        button->setMinimumHeight(32); button->setAutoDefault(false);
        button->setStyleSheet(QStringLiteral("QPushButton { padding: 5px 12px; border: 1px solid #b9c8d0; border-radius: 5px; background: #f7fafb; color: #243843; } QPushButton:disabled { color: #8c989f; background: #f1f3f4; } QPushButton:hover:enabled { background: #e1f0f5; }"));
    }
    connect(close, &QPushButton::clicked, this, &SingToMidiPanel::reject);
    connect(refresh, &QPushButton::clicked, this, [this] { if (!recorder_.recording() && !requesting_permission_) { refreshDevices(); } });
    connect(record_, &QPushButton::clicked, this, [this] { record(); });
    connect(stop_, &QPushButton::clicked, this, [this] {
        recorder_.stop();
        status_->setText(recorder_.audio() ? QStringLiteral("Take captured. Convert it to preview notes before insertion.")
            : QStringLiteral("No audio arrived. Check the microphone and record again.")); tick();
    });
    connect(discard_, &QPushButton::clicked, this, [this] { discard(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        cancelConversion(); status_->setText(QStringLiteral("Conversion cancelled. The take is available to try again.")); updateControls();
    });
    connect(convert_, &QPushButton::clicked, this, [this] { convert(); });
    connect(play_, &QPushButton::clicked, this, [this] { playPreview(); });
    connect(insert_, &QPushButton::clicked, this, [this] {
        if (!insert_callback_ || preview_notes_.empty() || converting_ || inserted_) { return; }
        preview_playback_.stop();
        const auto error = insert_callback_(preview_notes_, origin_, take_context_.project_generation);
        if (!error.isEmpty()) { status_->setText(error); return; }
        inserted_ = true;
        status_->setText(QStringLiteral("Inserted %1 notes as one new region. Close this panel to edit, play or undo the insertion.").arg(preview_notes_.size()));
        updateControls();
    });
    const auto settings_changed = [this] {
        QSettings preferences;
        preferences.setValue(QStringLiteral("singToMidi/preset"), preset_->currentIndex());
        preferences.setValue(QStringLiteral("singToMidi/scaleSnap"), scale_snap_->isChecked());
        preferences.setValue(QStringLiteral("singToMidi/quantize"), quantize_->currentIndex());
        updatePreview(true);
    };
    connect(preset_, &QComboBox::currentIndexChanged, this, settings_changed);
    connect(scale_snap_, &QCheckBox::toggled, this, settings_changed);
    connect(quantize_, &QComboBox::currentIndexChanged, this, settings_changed);
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
    capture_.setDevicesChangedCallback([this] { if (!recorder_.recording()) { refreshDevices(); } });
    timer_.start(50); setOrigin(0); refreshDevices(); updateControls();
}
SingToMidiPanel::~SingToMidiPanel() {
    capture_.setDevicesChangedCallback({}); cancelConversion(); recorder_.discard(); preview_playback_.stop();
    for (auto* child : children()) {
        if (auto* watcher = dynamic_cast<QFutureWatcher<hum::vocal::ConversionResult>*>(child)) { watcher->waitForFinished(); }
    }
}
void SingToMidiPanel::setHost(std::function<HostContext()> context,
    std::function<QString(const std::vector<hum::vocal::NoteEvent>&, double, std::uint64_t)> insert) {
    context_ = std::move(context); insert_callback_ = std::move(insert); updateControls();
}
void SingToMidiPanel::setOrigin(double seconds) {
    origin_ = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 570.0) : 0;
    origin_label_->setText(QStringLiteral("Recording origin: %1 s · first audio sample aligns here · up to 30 seconds per take.").arg(origin_, 0, 'f', 2));
}
void SingToMidiPanel::reject() { discard(); QDialog::reject(); }
void SingToMidiPanel::discard() {
    ++permission_generation_; requesting_permission_ = false; cancelConversion();
    recorder_.discard(); preview_playback_.stop(); result_ = {}; preview_notes_.clear(); inserted_ = false;
    preview_->setNotes({}, {}, origin_, 120); progress_->setValue(0);
    status_->setText(QStringLiteral("Take discarded. Existing notes are unchanged.")); tick();
}
void SingToMidiPanel::cancelConversion() {
    ++conversion_generation_; if (cancelled_) { cancelled_->store(true); }
    converting_ = false;
}
void SingToMidiPanel::refreshDevices() {
    const auto selected = devices_->currentData().toString(); devices_->clear();
    for (const auto& device : active_capture_->devices()) { devices_->addItem(QString::fromStdString(device.name), QString::fromStdString(device.id)); }
    const auto index = devices_->findData(selected); if (index >= 0) { devices_->setCurrentIndex(index); }
    updateControls();
}
void SingToMidiPanel::record() {
    if (requesting_permission_ || recorder_.recording() || converting_) { return; }
    if (active_capture_->permissionStatus() != core::PermissionStatus::granted) {
        requesting_permission_ = true; const auto generation = ++permission_generation_;
        updateControls(); QPointer<SingToMidiPanel> safe(this);
        active_capture_->requestPermission([safe, generation](core::PermissionStatus permission) {
            if (!safe || safe->permission_generation_ != generation) { return; }
            safe->requesting_permission_ = false;
            if (permission == core::PermissionStatus::granted) { safe->record(); }
            else { safe->reportCaptureError(core::ServiceStatus::permission_denied); safe->updateControls(); }
        }); return;
    }
    preview_playback_.stop();
    const auto error = recorder_.start(devices_->currentData().toString().toStdString());
    if (error != core::ServiceStatus::ok) { reportCaptureError(error); }
    else {
        take_context_ = context_ ? context_() : HostContext{};
        if (context_) { setOrigin(take_context_.origin); }
        result_ = {}; preview_notes_.clear(); inserted_ = false; preview_->setNotes({}, {}, origin_, take_context_.bpm);
        status_->setText(QStringLiteral("Recording locally… Sing one melody, then press Stop."));
    }
    updateControls();
}
hum::vocal::Config SingToMidiPanel::config() const {
    return hum::vocal::Config::forPreset(static_cast<hum::vocal::Preset>(preset_->currentIndex()));
}
void SingToMidiPanel::convert() {
    const auto audio = recorder_.audio();
    if (!audio || recorder_.recording() || converting_ || audio->duration() < .15) { return; }
    preview_playback_.stop(); result_ = {}; preview_notes_.clear(); inserted_ = false;
    preview_->setNotes({}, {}, origin_, take_context_.bpm);
    auto model = bundledModel();
    if (model.empty()) { status_->setText(QStringLiteral("The bundled vocal model is missing. Reinstall the complete app.")); return; }
    const auto runtime = bundledRuntime();
    converting_ = true; const auto generation = ++conversion_generation_;
    cancelled_ = std::make_shared<std::atomic_bool>(false); progress_value_ = std::make_shared<std::atomic<double>>(0);
    auto cancelled = cancelled_; auto progress = progress_value_; const auto settings = config();
    auto* watcher = new QFutureWatcher<hum::vocal::ConversionResult>(this);
    connect(watcher, &QFutureWatcher<hum::vocal::ConversionResult>::finished, this, [this, watcher, generation] {
        auto result = watcher->result(); watcher->deleteLater();
        if (generation != conversion_generation_) { return; }
        converting_ = false; result_ = std::move(result); progress_->setValue(100);
        if (result_.status == hum::vocal::Status::failed) { status_->setText(QString::fromStdString(result_.error)); }
        else if (result_.status == hum::vocal::Status::cancelled) { status_->setText(QStringLiteral("Conversion cancelled. The take is still available.")); }
        else { updatePreview(false); }
        updateControls();
    });
    watcher->setFuture(QtConcurrent::run([audio, model = std::move(model), runtime, settings, cancelled, progress] {
        return hum::vocal::infer(*audio, model, runtime, settings,
            [cancelled] { return cancelled->load(); }, [progress](double value) { progress->store(value); });
    }));
    status_->setText(QStringLiteral("Converting on this Mac… The project is unchanged.")); updateControls();
}
void SingToMidiPanel::updatePreview(bool resegment) {
    if (converting_ || result_.frames.empty()) { return; }
    preview_playback_.stop();
    try {
        if (resegment && recorder_.audio()) {
            const auto warnings = result_.warnings;
            result_ = hum::vocal::transcribe(result_.frames, recorder_.audio()->duration(), config());
            result_.warnings = warnings;
        }
        const auto host = context_ ? context_() : take_context_;
        if (host.project_generation != take_context_.project_generation) { discard(); status_->setText(QStringLiteral("The project changed. Record a new take for this project.")); return; }
        hum::vocal::Correction correction; correction.bpm = host.bpm; correction.origin = origin_;
        correction.quantize_division = quantize_->currentData().toInt(); correction.scale_snap = scale_snap_->isChecked();
        correction.scale_pitch_classes.clear();
        for (const auto interval : host.scale.intervals) { correction.scale_pitch_classes.push_back((host.scale.root_pitch_class + interval) % 12); }
        preview_notes_ = hum::vocal::correct(result_.notes, correction);
        preview_->setNotes(preview_notes_, host.reference_notes, origin_, host.bpm);
        auto message = preview_notes_.empty() ? QStringLiteral("No confident melody found. Try a clear single voice closer to the microphone.")
            : QStringLiteral("%1 notes ready for Track %2 · %3. Adjust the preview or insert when ready.")
                .arg(preview_notes_.size()).arg(host.track + 1).arg(QString::fromLatin1(core::instrumentVoiceName(host.instrument)));
        if (host.magnet) { message += QStringLiteral(" Project Magnet is ON and will also affect arrangement playback/export."); }
        if (host.muted) { message += QStringLiteral(" This track is muted; preview audition still plays."); }
        for (const auto& warning : result_.warnings) { message += QLatin1Char(' ') + QString::fromStdString(warning); }
        status_->setText(message);
    } catch (const std::exception& error) { preview_notes_.clear(); status_->setText(QString::fromUtf8(error.what())); }
    updateControls();
}
void SingToMidiPanel::playPreview() {
    if (preview_playback_.isPlaying() || preview_playback_.isPreparing()) { preview_playback_.stop(); updateControls(); return; }
    if (preview_notes_.empty() || converting_ || recorder_.recording()) { return; }
    const auto host = context_ ? context_() : take_context_;
    preview_playback_.play(PianoRollInsertAdapter::timedNotes(preview_notes_), [this](const QString& message, bool error) {
        if (error) { status_->setText(message); } updateControls();
    }, 0, host.bpm, false, std::vector<core::InstrumentVoice>(preview_notes_.size(), host.instrument));
    updateControls();
}
void SingToMidiPanel::tick() {
    const auto was_recording = recorder_.recording(); recorder_.poll();
    if (was_recording && !recorder_.recording()) {
        if (recorder_.error() != core::ServiceStatus::ok) { reportCaptureError(recorder_.error()); }
        else { status_->setText(QStringLiteral("30-second take captured. Ready to convert.")); }
    }
    elapsed_->setText(QStringLiteral("%1 / 30.0 seconds%2").arg(recorder_.elapsed(), 0, 'f', 1)
        .arg(recorder_.clipped() ? QStringLiteral(" · Clipping: move back from the microphone") : QString{}));
    level_->setValue(static_cast<int>(std::clamp(recorder_.peak(), 0.0F, 1.0F) * 100));
    if (converting_ && progress_value_) { progress_->setValue(static_cast<int>(progress_value_->load() * 100)); }
    preview_->setPlayhead(preview_playback_.isPlaying() ? preview_playback_.positionSeconds() : -1);
    updateControls();
}
void SingToMidiPanel::updateControls() {
    const auto active = recorder_.recording() || requesting_permission_;
    devices_->setEnabled(!active && !converting_); record_->setEnabled(!active && !converting_ && devices_->count() > 0);
    stop_->setEnabled(recorder_.recording()); discard_->setEnabled(active || converting_ || bool(recorder_.audio()));
    convert_->setEnabled(!active && !converting_ && recorder_.audio() && recorder_.audio()->duration() >= .15);
    cancel_->setEnabled(converting_);
    preset_->setEnabled(!active && !converting_ && !inserted_); scale_snap_->setEnabled(!active && !converting_ && !inserted_);
    quantize_->setEnabled(!active && !converting_ && !inserted_);
    play_->setEnabled(!active && !converting_ && !preview_notes_.empty());
    play_->setText(preview_playback_.isPlaying() || preview_playback_.isPreparing() ? QStringLiteral("Stop Preview") : QStringLiteral("Play Preview"));
    insert_->setEnabled(!active && !converting_ && !inserted_ && !preview_notes_.empty() && bool(insert_callback_));
}
void SingToMidiPanel::reportCaptureError(core::ServiceStatus error) {
    switch (error) {
    case core::ServiceStatus::permission_denied:
    case core::ServiceStatus::permission_required:
        status_->setText(QStringLiteral("Allow Microphone access for HumToSing in System Settings → Privacy & Security, then try Record again.")); break;
    case core::ServiceStatus::device_disconnected:
        status_->setText(QStringLiteral("Microphone disconnected. The incomplete take was discarded. Reconnect it and record again.")); break;
    default:
        status_->setText(QStringLiteral("The microphone could not supply usable audio. Refresh microphones and try again.")); break;
    }
}
}
