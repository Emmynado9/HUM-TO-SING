#include "AiTrialPanel.hpp"
#include "hum/core/generation_trial.hpp"
#include "hum/core/wave_fixture.hpp"
#include "hum/vocal/neural_pitch.hpp"
#include "vocal/PianoRollInsertAdapter.hpp"
#include "vocal/VocalModelAssets.hpp"
#include "vocal/VocalPreviewWidget.hpp"
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <limits>

namespace hum::app {
namespace {
struct Imported {
    std::shared_ptr<const core::AudioRecording> audio;
    hum::vocal::ConversionResult result;
    QString error;
};
void writeFile(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Could not save trial files. Choose a writable folder.");
}
}
AiTrialPanel::AiTrialPanel(QWidget* parent, core::IAudioCapture* capture)
    : QDialog(parent), capture_(this), active_capture_(capture ? capture : &capture_),
      recorder_(*active_capture_, 10), audio_playback_(this), midi_playback_(this) {
    setObjectName("aiTrialPanel"); setWindowTitle("AI Trial — Voice to instrument");
    setWindowModality(Qt::ApplicationModal); resize(880, 780);
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll); scroll->setWidget(content); outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(content); layout->setContentsMargins(22, 18, 22, 18); layout->setSpacing(10);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    auto* title = new QLabel("AI Trial · Voice to instrument", this);
    title->setStyleSheet("font-size: 24px; font-weight: 600;"); layout->addWidget(title);
    auto* intro = new QLabel("Experimental · Personal, noncommercial use. Generate a creative interpretation in free Colab, then bring it back here. Nothing is uploaded automatically.", this);
    intro->setWordWrap(true); layout->addWidget(intro);
    auto* form = new QFormLayout;
    clips_ = new QComboBox(this); clips_->setObjectName("aiTrialClip"); clips_->setMinimumContentsLength(30); clips_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); form->addRow("Recorded voice", clips_);
    auto* controls = new QHBoxLayout;
    instrument_ = new QComboBox(this); instrument_->setObjectName("aiTrialInstrument");
    instrument_->addItem("Piano", "piano"); instrument_->addItem("Acoustic guitar", "acoustic-guitar"); instrument_->addItem("Flute", "flute");
    style_ = new QComboBox(this); style_->setObjectName("aiTrialStyle");
    for (const auto* value : {"neutral", "gentle", "bright", "melancholic", "energetic"}) style_->addItem(QString::fromLatin1(value), QString::fromLatin1(value));
    controls->addWidget(instrument_); controls->addWidget(new QLabel("Style", this)); controls->addWidget(style_);
    seed_ = new QSpinBox(this); seed_->setObjectName("aiTrialSeed"); seed_->setRange(0, std::numeric_limits<int>::max()); seed_->setValue(42);
    seed_->setToolTip("Keep the same seed for initial comparisons. New seeds may give different interpretations.");
    controls->addWidget(new QLabel("Seed", this)); controls->addWidget(seed_); form->addRow("Instrument", controls);
    auto* excerpt = new QHBoxLayout;
    offset_ = new QDoubleSpinBox(this); offset_->setObjectName("aiTrialOffset"); offset_->setRange(0, 30); offset_->setDecimals(3); offset_->setSuffix(" s");
    duration_ = new QDoubleSpinBox(this); duration_->setObjectName("aiTrialDuration"); duration_->setRange(.5, 10); duration_->setValue(8); duration_->setDecimals(3); duration_->setSuffix(" s");
    excerpt->addWidget(new QLabel("Start within clip", this)); excerpt->addWidget(offset_);
    excerpt->addWidget(new QLabel("Length (up to 10 s)", this)); excerpt->addWidget(duration_); form->addRow("Excerpt", excerpt); layout->addLayout(form);
    prompt_ = new QPlainTextEdit(this); prompt_->setObjectName("aiTrialPrompt"); prompt_->setReadOnly(true); prompt_->setMaximumHeight(68); prompt_->setAccessibleName("Automatic instrument prompt"); layout->addWidget(prompt_);
    auto button = [this](const char* text, const char* name) {
        auto* b = new QPushButton(QString::fromUtf8(text), this); b->setObjectName(QString::fromLatin1(name)); b->setAutoDefault(false);
        b->setMinimumHeight(34); b->setStyleSheet("QPushButton { padding: 5px 10px; border: 1px solid #b9c8d0; border-radius: 5px; background: white; color: #243843; } QPushButton:disabled { color: #909090; background: #eeeeee; }"); return b;
    };
    auto* mic_row = new QHBoxLayout;
    devices_ = new QComboBox(this); devices_->setObjectName("aiTrialMicrophone"); devices_->setAccessibleName("AI Trial microphone");
    refresh_devices_ = button("Refresh microphones", "aiTrialRefreshMicrophones");
    mic_row->addWidget(new QLabel("Microphone", this)); mic_row->addWidget(devices_, 1); mic_row->addWidget(refresh_devices_);
    layout->insertLayout(2, mic_row);
    auto* record_row = new QHBoxLayout;
    record_ = button("● Record voice", "aiTrialRecordVoice"); stop_record_ = button("Stop recording", "aiTrialStopRecording");
    play_voice_ = button("Listen to selected voice", "aiTrialPlayVoice");
    record_row->addWidget(record_); record_row->addWidget(stop_record_); record_row->addWidget(play_voice_); layout->insertLayout(3, record_row);
    recording_status_ = new QLabel("Record a new melody here (up to 10 seconds), or select a saved project voice below. Trial takes are temporary; save trial files to keep them.", this);
    recording_status_->setWordWrap(true); recording_status_->setMinimumHeight(44); layout->insertWidget(4, recording_status_);
    level_ = new QProgressBar(this); level_->setObjectName("aiTrialMicLevel"); level_->setRange(0, 100); level_->setValue(0); level_->setFormat("Microphone level %p%"); layout->insertWidget(5, level_);
    connect(record_, &QPushButton::clicked, this, [this] { recordVoice(); });
    connect(stop_record_, &QPushButton::clicked, this, [this] { finishVoice(); });
    connect(refresh_devices_, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(play_voice_, &QPushButton::clicked, this, [this] {
        const auto index = clips_->currentIndex(); if (index < 0) return;
        const auto& clip = project_.voice_clips[static_cast<std::size_t>(index)];
        std::vector<float> samples; samples.reserve(clip.samples.size());
        for (const auto sample : clip.samples) samples.push_back(sample / 32768.0F);
        stopPlayback(); audio_playback_.play(std::make_shared<core::AudioRecording>(core::AudioFormat{clip.sample_rate, 1}, std::move(samples)),
            [this](const QString& message, bool error) { if (error) status_->setText(message); });
    });
    auto* export_row = new QHBoxLayout;
    export_ = button("1. Save trial files…", "aiTrialExport"); folder_ = button("Show files", "aiTrialFolder");
    auto* colab = button("2. Upload & generate in Colab ↗", "aiTrialColab");
    export_row->addWidget(export_); export_row->addWidget(folder_); export_row->addWidget(colab); layout->addLayout(export_row);
    auto* instructions = new QLabel("Generation happens in your browser: open Colab, choose a free GPU, run setup, then run Upload → Generate → Download and select guide.wav + request.json. Watch progress there. Unzip the downloaded result and import generated.wav below. Importing your original hum does not generate an instrument.", this);
    instructions->setWordWrap(true); layout->addWidget(instructions);
    auto* import_row = new QHBoxLayout;
    import_ = button("3. Import Colab result WAV…", "aiTrialImport"); cancel_ = button("Cancel conversion", "aiTrialCancel");
    import_row->addWidget(import_); import_row->addWidget(cancel_); import_row->addStretch();
    origin_ = new QDoubleSpinBox(this); origin_->setObjectName("aiTrialOrigin"); origin_->setRange(0, 600); origin_->setDecimals(3); origin_->setSuffix(" s");
    import_row->addWidget(new QLabel("Insert at", this)); import_row->addWidget(origin_); layout->addLayout(import_row);
    progress_ = new QProgressBar(this); progress_->setToolTip("Local MIDI conversion only. Cloud generation and download progress appear in Colab."); progress_->setRange(0, 100); progress_->setValue(0); layout->addWidget(progress_);
    preview_ = new vocal::VocalPreviewWidget(this); preview_->setMinimumHeight(155); preview_->setAccessibleName("Generated instrument MIDI preview"); preview_->setToolTip("Detected notes from the generated audio. Add MIDI to edit them in the piano roll."); layout->addWidget(preview_, 1);
    destination_ = new QLabel(this); destination_->setWordWrap(true); destination_->setMinimumHeight(48); layout->addWidget(destination_);
    auto* result_row = new QHBoxLayout;
    play_audio_ = button("Play generated audio", "aiTrialPlayAudio"); play_midi_ = button("Play MIDI preview", "aiTrialPlayMidi");
    auto* stop = button("Stop preview", "aiTrialStop");
    result_row->addWidget(play_audio_); result_row->addWidget(play_midi_); result_row->addWidget(stop); layout->addLayout(result_row);
    status_ = new QLabel(this); status_->setObjectName("aiTrialStatus"); status_->setWordWrap(true); status_->setMinimumHeight(44); layout->addWidget(status_);
    auto* finish = new QHBoxLayout;
    add_audio_ = button("Add generated audio", "aiTrialAddAudio"); add_midi_ = button("Add MIDI to selected track", "aiTrialAddMidi"); auto* close = button("Close", "aiTrialClose");
    finish->addWidget(add_audio_); finish->addWidget(add_midi_); finish->addStretch(); finish->addWidget(close); layout->addLayout(finish);
    connect(close, &QPushButton::clicked, this, &AiTrialPanel::reject);
    connect(stop, &QPushButton::clicked, this, [this] { stopPlayback(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { cancel(); status_->setText("Conversion cancelled. Import the WAV again to retry."); });
    for (auto* combo : {clips_, instrument_, style_}) connect(combo, &QComboBox::currentIndexChanged, this, [this] { refreshPrompt(); });
    connect(offset_, &QDoubleSpinBox::valueChanged, this, [this] { refreshPrompt(); });
    connect(export_, &QPushButton::clicked, this, [this] {
        const auto parent = QFileDialog::getExistingDirectory(this, "Choose where to save this trial");
        if (parent.isEmpty()) return;
        const auto error = prepareInDirectory(parent); if (!error.isEmpty()) status_->setText(error);
    });
    connect(folder_, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(exported_directory_)); });
    connect(colab, &QPushButton::clicked, this, [this] {
        if (!exported_directory_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(exported_directory_));
        status_->setText("In Colab, run setup and Upload → Generate → Download. Select both saved files. Generation and download progress appear in the notebook, not in this local MIDI conversion bar.");
        if (!QDesktopServices::openUrl(QUrl("https://colab.research.google.com/github/Emmynado9/HUM-TO-SING/blob/main/experiments/audiocraft/voice_to_instrument_trial.ipynb")))
            status_->setText("Could not open your browser. Open the AudioCraft trial notebook from the repository in Colab.");
    });
    connect(import_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Import generated.wav from the unzipped Colab result", {}, "Wave audio (*.wav)");
        if (!path.isEmpty()) importGenerated(path);
    });
    connect(play_audio_, &QPushButton::clicked, this, [this] {
        stopPlayback(); audio_playback_.play(audio_, [this](const QString& message, bool error) { if (error) status_->setText(message); });
    });
    connect(play_midi_, &QPushButton::clicked, this, [this] {
        stopPlayback();
        const auto voice = track_ < project_.tracks.size() ? project_.tracks[track_].instrument : core::InstrumentVoice::grand_piano;
        midi_playback_.play(vocal::PianoRollInsertAdapter::timedNotes(notes_), [this](const QString& message, bool error) { if (error) status_->setText(message); },
            0, project_.tempo_bpm, false, std::vector<core::InstrumentVoice>(notes_.size(), voice));
    });
    connect(add_midi_, &QPushButton::clicked, this, [this] {
        if (busy_ || notes_.empty() || midi_added_ || !insert_midi_) return;
        if (std::any_of(notes_.begin(), notes_.end(), [this](const auto& n) { return origin_->value() + n.start + n.duration > 600; })) {
            status_->setText("The MIDI would extend past the 600-second project limit. Choose an earlier insertion point."); return;
        }
        stopPlayback(); const auto error = insert_midi_(notes_, origin_->value(), track_, project_generation_);
        if (error.isEmpty()) { midi_added_ = true; status_->setText("MIDI added as one region. Close this screen to edit it in the piano roll or undo the insertion."); }
        else status_->setText(error);
        updateControls();
    });
    connect(add_audio_, &QPushButton::clicked, this, [this] {
        if (busy_ || !audio_ || audio_added_ || !insert_audio_) return;
        auto clip = core::makeVoiceClip(*audio_, origin_->value());
        if (!clip) { status_->setText("Audio cannot fit at this insertion position."); return; }
        clip->name = "AI " + result_instrument_.toStdString(); stopPlayback();
        const auto error = insert_audio_(std::move(*clip), project_generation_);
        if (error.isEmpty()) { audio_added_ = true; status_->setText("Generated audio added to the Playlist. Save your project to keep it. Mute one version if comparing audio and MIDI together."); }
        else status_->setText(error);
        updateControls();
    });
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (recorder_.recording()) {
            level_->setValue(static_cast<int>(recorder_.peak() * 100));
            recording_status_->setText(QStringLiteral("Recording %1 / 10.0 s%2").arg(recorder_.elapsed(), 0, 'f', 1).arg(recorder_.clipped() ? " · Input clipping: sing more softly." : ""));
            if (recorder_.limitReached() || recorder_.error() != core::ServiceStatus::ok) finishVoice();
        }
        if (busy_ && progress_value_) progress_->setValue(static_cast<int>(progress_value_->load() * 100));
        preview_->setPlayhead(midi_playback_.isPlaying() ? midi_playback_.positionSeconds() : -1);
    }); timer_.start(50); refreshDevices();
    capture_.setDevicesChangedCallback([this] { if (!recorder_.recording() && !requesting_permission_) refreshDevices(); });
    updateControls();
}
AiTrialPanel::~AiTrialPanel() {
    capture_.setDevicesChangedCallback({});
    cancel();
    for (auto* child : children()) if (auto* watcher = dynamic_cast<QFutureWatcher<Imported>*>(child)) watcher->waitForFinished();
}
void AiTrialPanel::setHost(decltype(insert_midi_) midi, decltype(insert_audio_) audio) { insert_midi_ = std::move(midi); insert_audio_ = std::move(audio); }
void AiTrialPanel::setProject(core::ProjectDocument project, std::size_t track, double playhead, std::uint64_t generation) {
    cancel(); project_ = std::move(project); track_ = track; project_generation_ = generation; default_origin_ = playhead;
    audio_.reset(); notes_.clear(); audio_added_ = midi_added_ = false; exported_directory_.clear();
    { const QSignalBlocker blocker(clips_); clips_->clear();
      for (std::size_t i = 0; i < project_.voice_clips.size(); ++i) clips_->addItem(QStringLiteral("%1 · %2 (%3 s)").arg(i + 1).arg(QString::fromStdString(project_.voice_clips[i].name)).arg(project_.voice_clips[i].duration(), 0, 'f', 2)); }
    recording_status_->setText("Record a new melody here (up to 10 seconds), or select a saved project voice below. Trial takes are temporary; save trial files to keep them.");
    refreshDevices(); duration_->setValue(8);
    offset_->setValue(0); progress_->setValue(0); preview_->setNotes({}, {}, 0, project_.tempo_bpm);
    const auto voice = track_ < project_.tracks.size() ? project_.tracks[track_].instrument : core::InstrumentVoice::grand_piano;
    destination_->setText(QStringLiteral("MIDI preview and insertion use Track %1 · %2. Generated audio keeps the AI instrument sound. Review the transcription before adding it.")
        .arg(track_ + 1).arg(QString::fromLatin1(core::instrumentVoiceName(voice))));
    status_->setText(project_.voice_clips.empty() ? "No voice selected yet. Use Record voice above to record your melody here."
        : "Choose a voice clip and instrument. Save trial files to begin; your project is unchanged until you add a result.");
    refreshPrompt();
}
void AiTrialPanel::refreshDevices() {
    const auto selected = devices_->currentData(); devices_->clear();
    for (const auto& device : active_capture_->devices()) devices_->addItem(QString::fromStdString(device.name), QString::fromStdString(device.id));
    const auto index = devices_->findData(selected); if (index >= 0) devices_->setCurrentIndex(index);
    if (devices_->count() == 0) recording_status_->setText("No microphone found. Connect a microphone and click Refresh microphones.");
    updateControls();
}
void AiTrialPanel::recordVoice() {
    if (busy_ || recorder_.recording() || requesting_permission_) return;
    stopPlayback();
    if (active_capture_->permissionStatus() != core::PermissionStatus::granted) {
        requesting_permission_ = true; const auto generation = ++permission_generation_;
        updateControls(); QPointer<AiTrialPanel> safe(this);
        active_capture_->requestPermission([safe, generation](core::PermissionStatus permission) {
            if (!safe || safe->permission_generation_ != generation) return;
            safe->requesting_permission_ = false;
            if (permission == core::PermissionStatus::granted) safe->recordVoice();
            else safe->recording_status_->setText("Microphone access is denied. Allow HumToSing in System Settings → Privacy & Security → Microphone, then try again.");
            safe->updateControls();
        });
        return;
    }
    const auto result = recorder_.start(devices_->currentData().toString().toStdString());
    recording_status_->setText(result == core::ServiceStatus::ok ? "Recording… Sing or hum your melody. Stops automatically after 10 seconds."
        : "Could not start this microphone. Check the connection, refresh microphones and try again.");
    updateControls();
}
void AiTrialPanel::finishVoice() {
    if (!recorder_.recording()) return;
    recorder_.stop(); level_->setValue(0);
    const auto take = recorder_.audio();
    if (recorder_.error() != core::ServiceStatus::ok) recording_status_->setText("Microphone recording failed or disconnected. Your previous voice selection is unchanged; reconnect and try again.");
    else if (!take || take->duration() < .5) recording_status_->setText("That take was too short. Record at least half a second; your previous voice selection is unchanged.");
    else {
        const core::AudioRecording recording({take->sample_rate, 1}, take->samples);
        auto clip = core::makeVoiceClip(recording, default_origin_);
        if (!clip) recording_status_->setText("Could not prepare this take. Move the playhead earlier and try again.");
        else {
            clip->name = "AI Trial take " + std::to_string(project_.voice_clips.size() + 1);
            project_.voice_clips.push_back(std::move(*clip));
            const auto& saved = project_.voice_clips.back();
            clips_->addItem(QStringLiteral("%1 (%2 s) · temporary").arg(QString::fromStdString(saved.name)).arg(saved.duration(), 0, 'f', 2));
            clips_->setCurrentIndex(clips_->count() - 1); offset_->setValue(0); duration_->setValue(saved.duration());
            recording_status_->setText("Take selected. Listen to it, choose an instrument, then save trial files and open Colab to generate the instrument. This take has not changed your project.");
            exported_directory_.clear();
        }
    }
    updateControls();
}
void AiTrialPanel::refreshPrompt() {
    core::trial::Request request; request.instrument = instrument_->currentData().toString().toStdString();
    request.style = style_->currentData().toString().toStdString(); request.tempo = project_.tempo_bpm;
    prompt_->setPlainText(QString::fromStdString(core::trial::prompt(request)));
    if (!audio_ && !busy_) {
        const auto index = clips_->currentIndex();
        origin_->setValue(index >= 0 ? project_.voice_clips[static_cast<std::size_t>(index)].start_seconds + offset_->value() : default_origin_);
    }
    updateControls();
}
QString AiTrialPanel::prepareInDirectory(const QString& parent) {
    if (busy_ || recorder_.recording() || requesting_permission_) return "Finish recording or conversion first.";
    QString created;
    try {
        core::trial::Request request; request.instrument = instrument_->currentData().toString().toStdString();
        request.style = style_->currentData().toString().toStdString(); request.seed = static_cast<std::uint32_t>(seed_->value());
        const auto prepared = core::trial::prepare(project_, static_cast<std::size_t>(clips_->currentIndex()), request, offset_->value(), duration_->value());
        QDir directory(parent);
        const auto name = "hum-trial-" + instrument_->currentData().toString() + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        if (!directory.exists() || !directory.mkdir(name)) return "Could not create a trial folder here.";
        created = directory.absoluteFilePath(name);
        writeFile(created + "/guide.wav", QByteArray(reinterpret_cast<const char*>(prepared.wave.data()), static_cast<qsizetype>(prepared.wave.size())));
        writeFile(created + "/request.json", QByteArray::fromStdString(prepared.json));
        exported_directory_ = created;
        status_->setText(QStringLiteral("Saved %1 seconds to %2. Open Colab and upload guide.wav + request.json when asked. Nothing has been uploaded.").arg(prepared.request.duration, 0, 'f', 2).arg(created));
        updateControls(); return {};
    } catch (const std::exception& error) {
        if (!created.isEmpty()) QDir(created).removeRecursively();
        return QString::fromUtf8(error.what());
    }
}
void AiTrialPanel::importGenerated(const QString& path) {
    if (busy_ || recorder_.recording() || requesting_permission_) return;
    const QFileInfo file(path);
    if (!file.isFile() || file.size() > 16 * 1024 * 1024) { status_->setText("Choose a WAV file no larger than 16 MiB."); return; }
    cancel(); audio_.reset(); notes_.clear(); audio_added_ = midi_added_ = false;
    preview_->setNotes({}, {}, 0, project_.tempo_bpm); progress_->setValue(0); result_instrument_ = instrument_->currentText();
    busy_ = true; cancelled_ = std::make_shared<std::atomic_bool>(false); progress_value_ = std::make_shared<std::atomic<double>>(0);
    const auto generation = ++job_generation_; const auto cancelled = cancelled_; const auto progress = progress_value_;
    auto model = vocal::bundledModel(); const auto runtime = vocal::bundledRuntime();
    auto* watcher = new QFutureWatcher<Imported>(this);
    connect(watcher, &QFutureWatcher<Imported>::finished, this, [this, watcher, generation] {
        auto imported = watcher->result(); watcher->deleteLater();
        if (generation != job_generation_) return;
        busy_ = false; audio_ = std::move(imported.audio);
        if (imported.result.status == hum::vocal::Status::success) notes_ = std::move(imported.result.notes);
        preview_->setNotes(notes_, {}, 0, project_.tempo_bpm); progress_->setValue(100);
        if (!imported.error.isEmpty()) status_->setText(imported.error);
        else if (notes_.empty()) status_->setText("Audio imported, but no confident MIDI melody was found. You can listen or add the audio without MIDI.");
        else status_->setText(QStringLiteral("%1 notes detected locally. Compare generated audio with the MIDI preview; accuracy and AI quality are not yet verified.").arg(notes_.size()));
        updateControls();
    });
    watcher->setFuture(QtConcurrent::run([path, model = std::move(model), runtime, cancelled, progress] {
        Imported imported;
        try {
            auto loaded = core::loadWaveFixture(path.toStdString());
            if (!loaded) { imported.error = QString::fromStdString(loaded.error); return imported; }
            if (loaded.recording->durationSeconds() < .5 || loaded.recording->durationSeconds() > 10.001) {
                imported.error = "Import a generated excerpt between 0.5 and 10 seconds."; return imported;
            }
            imported.audio = std::make_shared<core::AudioRecording>(std::move(*loaded.recording));
            if (cancelled->load()) return imported;
            imported.result = hum::vocal::infer({imported.audio->format().sample_rate_hz, imported.audio->monoSamples()}, model, runtime,
                hum::vocal::Config::forPreset(hum::vocal::Preset::natural), [cancelled] { return cancelled->load(); }, [progress](double p) { progress->store(p); });
            if (imported.result.status == hum::vocal::Status::failed) imported.error = "Audio imported. MIDI conversion failed: " + QString::fromStdString(imported.result.error);
        } catch (const std::exception& error) { imported.error = QString::fromUtf8(error.what()); }
        return imported;
    }));
    status_->setText("Importing and converting on this Mac… No project changes yet."); updateControls();
}
void AiTrialPanel::stopPlayback() { audio_playback_.stop(); midi_playback_.stop(); }
void AiTrialPanel::cancel() {
    ++permission_generation_; requesting_permission_ = false; recorder_.discard();
    if (level_) level_->setValue(0);
    if (cancelled_) cancelled_->store(true);
    ++job_generation_; busy_ = false; stopPlayback(); updateControls();
}
void AiTrialPanel::reject() { cancel(); QDialog::reject(); }
void AiTrialPanel::updateControls() {
    if (!export_) return;
    const bool locked = busy_ || recorder_.recording() || requesting_permission_;
    devices_->setEnabled(!locked); refresh_devices_->setEnabled(!locked);
    record_->setEnabled(!locked && devices_->count() > 0); stop_record_->setEnabled(recorder_.recording());
    play_voice_->setEnabled(!locked && clips_->currentIndex() >= 0);
    for (auto* widget : {static_cast<QWidget*>(clips_), static_cast<QWidget*>(instrument_), static_cast<QWidget*>(style_), static_cast<QWidget*>(offset_), static_cast<QWidget*>(duration_), static_cast<QWidget*>(seed_), static_cast<QWidget*>(origin_)}) widget->setEnabled(!locked);
    export_->setEnabled(!locked && clips_->count() > 0); folder_->setEnabled(!exported_directory_.isEmpty());
    import_->setEnabled(!locked); cancel_->setEnabled(busy_);
    play_audio_->setEnabled(!locked && bool(audio_)); play_midi_->setEnabled(!locked && !notes_.empty());
    add_audio_->setEnabled(!locked && bool(audio_) && !audio_added_); add_midi_->setEnabled(!locked && !notes_.empty() && !midi_added_);
}
}
