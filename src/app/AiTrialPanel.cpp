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
AiTrialPanel::AiTrialPanel(QWidget* parent) : QDialog(parent), audio_playback_(this), midi_playback_(this) {
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
    clips_ = new QComboBox(this); clips_->setObjectName("aiTrialClip"); form->addRow("Recorded voice", clips_);
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
    auto* export_row = new QHBoxLayout;
    export_ = button("1. Save trial files…", "aiTrialExport"); folder_ = button("Show files", "aiTrialFolder");
    auto* colab = button("2. Open free Colab trial", "aiTrialColab");
    export_row->addWidget(export_); export_row->addWidget(folder_); export_row->addWidget(colab); layout->addLayout(export_row);
    auto* instructions = new QLabel("In Colab: choose a free GPU, run the cells, and upload only guide.wav + request.json from your saved folder. Download and unzip the result, then import generated.wav below. Free GPU availability is not guaranteed.", this);
    instructions->setWordWrap(true); layout->addWidget(instructions);
    auto* import_row = new QHBoxLayout;
    import_ = button("3. Import generated WAV…", "aiTrialImport"); cancel_ = button("Cancel conversion", "aiTrialCancel");
    import_row->addWidget(import_); import_row->addWidget(cancel_); import_row->addStretch();
    origin_ = new QDoubleSpinBox(this); origin_->setObjectName("aiTrialOrigin"); origin_->setRange(0, 600); origin_->setDecimals(3); origin_->setSuffix(" s");
    import_row->addWidget(new QLabel("Insert at", this)); import_row->addWidget(origin_); layout->addLayout(import_row);
    progress_ = new QProgressBar(this); progress_->setRange(0, 100); progress_->setValue(0); layout->addWidget(progress_);
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
        if (busy_ && progress_value_) progress_->setValue(static_cast<int>(progress_value_->load() * 100));
        preview_->setPlayhead(midi_playback_.isPlaying() ? midi_playback_.positionSeconds() : -1);
    }); timer_.start(50); updateControls();
}
AiTrialPanel::~AiTrialPanel() {
    cancel();
    for (auto* child : children()) if (auto* watcher = dynamic_cast<QFutureWatcher<Imported>*>(child)) watcher->waitForFinished();
}
void AiTrialPanel::setHost(decltype(insert_midi_) midi, decltype(insert_audio_) audio) { insert_midi_ = std::move(midi); insert_audio_ = std::move(audio); }
void AiTrialPanel::setProject(core::ProjectDocument project, std::size_t track, double playhead, std::uint64_t generation) {
    cancel(); project_ = std::move(project); track_ = track; project_generation_ = generation; default_origin_ = playhead;
    audio_.reset(); notes_.clear(); audio_added_ = midi_added_ = false; exported_directory_.clear();
    { const QSignalBlocker blocker(clips_); clips_->clear();
      for (std::size_t i = 0; i < project_.voice_clips.size(); ++i) clips_->addItem(QStringLiteral("%1 · %2 (%3 s)").arg(i + 1).arg(QString::fromStdString(project_.voice_clips[i].name)).arg(project_.voice_clips[i].duration(), 0, 'f', 2)); }
    offset_->setValue(0); progress_->setValue(0); preview_->setNotes({}, {}, 0, project_.tempo_bpm);
    const auto voice = track_ < project_.tracks.size() ? project_.tracks[track_].instrument : core::InstrumentVoice::grand_piano;
    destination_->setText(QStringLiteral("MIDI preview and insertion use Track %1 · %2. Generated audio keeps the AI instrument sound. Review the transcription before adding it.")
        .arg(track_ + 1).arg(QString::fromLatin1(core::instrumentVoiceName(voice))));
    status_->setText(project_.voice_clips.empty() ? "No recorded voice clips yet. Close this screen and use Record voice in the Playlist, or import an existing generated WAV."
        : "Choose a voice clip and instrument. Save trial files to begin; your project is unchanged until you add a result.");
    refreshPrompt();
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
    if (busy_) return "Wait for conversion to finish.";
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
    if (busy_) return;
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
    if (cancelled_) cancelled_->store(true);
    ++job_generation_; busy_ = false; stopPlayback(); updateControls();
}
void AiTrialPanel::reject() { cancel(); QDialog::reject(); }
void AiTrialPanel::updateControls() {
    if (!export_) return;
    for (auto* widget : {static_cast<QWidget*>(clips_), static_cast<QWidget*>(instrument_), static_cast<QWidget*>(style_), static_cast<QWidget*>(offset_), static_cast<QWidget*>(duration_), static_cast<QWidget*>(seed_), static_cast<QWidget*>(origin_)}) widget->setEnabled(!busy_);
    export_->setEnabled(!busy_ && clips_->count() > 0); folder_->setEnabled(!exported_directory_.isEmpty());
    import_->setEnabled(!busy_); cancel_->setEnabled(busy_);
    play_audio_->setEnabled(!busy_ && bool(audio_)); play_midi_->setEnabled(!busy_ && !notes_.empty());
    add_audio_->setEnabled(!busy_ && bool(audio_) && !audio_added_); add_midi_->setEnabled(!busy_ && !notes_.empty() && !midi_added_);
}
}
