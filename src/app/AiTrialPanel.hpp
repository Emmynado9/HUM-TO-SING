#pragma once
#include "hum/core/project_file.hpp"
#include "hum/vocal/transcription.hpp"
#include "QtAudioPlayback.hpp"
#include "QtMidiPlayback.hpp"
#include <QDialog>
#include <QTimer>
#include <atomic>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QProgressBar;
namespace hum::app {
namespace vocal { class VocalPreviewWidget; }
class AiTrialPanel final : public QDialog {
public:
    explicit AiTrialPanel(QWidget* parent = nullptr);
    ~AiTrialPanel() override;
    void setProject(core::ProjectDocument project, std::size_t track, double playhead, std::uint64_t generation);
    void setHost(std::function<QString(const std::vector<hum::vocal::NoteEvent>&, double, std::size_t, std::uint64_t)> insert_midi,
        std::function<QString(core::VoiceClip, std::uint64_t)> insert_audio);
    // These are also used by the UI's file dialogs; return errors without modal dialogs.
    [[nodiscard]] QString prepareInDirectory(const QString& parent);
    void importGenerated(const QString& path);
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] const std::vector<hum::vocal::NoteEvent>& notes() const { return notes_; }
    [[nodiscard]] QString exportedDirectory() const { return exported_directory_; }
    void reject() override;
private:
    void refreshPrompt();
    void updateControls();
    void cancel();
    void stopPlayback();
    core::ProjectDocument project_;
    std::size_t track_{0};
    std::uint64_t project_generation_{0}, job_generation_{0};
    double default_origin_{0};
    QComboBox *clips_{}, *instrument_{}, *style_{};
    QDoubleSpinBox *offset_{}, *duration_{}, *origin_{};
    QSpinBox* seed_{};
    QPlainTextEdit* prompt_{};
    QLabel *status_{}, *destination_{};
    QPushButton *export_{}, *folder_{}, *import_{}, *cancel_{}, *play_audio_{}, *play_midi_{}, *add_audio_{}, *add_midi_{};
    QProgressBar* progress_{};
    vocal::VocalPreviewWidget* preview_{};
    QString exported_directory_, result_instrument_;
    bool busy_{false}, audio_added_{false}, midi_added_{false};
    std::shared_ptr<const core::AudioRecording> audio_;
    std::vector<hum::vocal::NoteEvent> notes_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::shared_ptr<std::atomic<double>> progress_value_;
    QtAudioPlayback audio_playback_;
    QtMidiPlayback midi_playback_;
    QTimer timer_;
    std::function<QString(const std::vector<hum::vocal::NoteEvent>&, double, std::size_t, std::uint64_t)> insert_midi_;
    std::function<QString(core::VoiceClip, std::uint64_t)> insert_audio_;
};
}
