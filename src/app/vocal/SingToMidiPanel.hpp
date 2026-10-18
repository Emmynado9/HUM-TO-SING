#pragma once
#include "IsolatedMicRecorder.hpp"
#include "QtAudioCapture.hpp"
#include "QtMidiPlayback.hpp"
#include "PianoRollInsertAdapter.hpp"
#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>

class QComboBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QCloseEvent;
class QCheckBox;

namespace hum::app::vocal {
class VocalPreviewWidget;
class SingToMidiPanel final : public QDialog {
public:
    explicit SingToMidiPanel(QWidget* parent = nullptr, core::IAudioCapture* capture = nullptr);
    ~SingToMidiPanel() override;
    void setOrigin(double seconds);
    void setHost(std::function<HostContext()> context,
        std::function<QString(const std::vector<hum::vocal::NoteEvent>&, double, std::uint64_t)> insert);
    [[nodiscard]] const std::vector<hum::vocal::NoteEvent>& previewNotes() const { return preview_notes_; }
    void reject() override;
private:
    void refreshDevices();
    void record();
    void tick();
    void updateControls();
    void reportCaptureError(core::ServiceStatus error);
    void convert();
    void updatePreview(bool resegment);
    void cancelConversion();
    void discard();
    void playPreview();
    [[nodiscard]] hum::vocal::Config config() const;
    QtAudioCapture capture_;
    core::IAudioCapture* active_capture_;
    IsolatedMicRecorder recorder_;
    QtMidiPlayback preview_playback_;
    QTimer timer_;
    QComboBox* devices_{};
    QPushButton *record_{}, *stop_{}, *discard_{}, *convert_{};
    QLabel *status_{}, *elapsed_{}, *origin_label_{};
    QProgressBar* level_{};
    double origin_{0};
    bool requesting_permission_{false};
    std::uint64_t permission_generation_{0};
    QComboBox *preset_{}, *quantize_{};
    QCheckBox* scale_snap_{};
    QPushButton *cancel_{}, *play_{}, *insert_{};
    QProgressBar* progress_{};
    VocalPreviewWidget* preview_{};
    std::function<HostContext()> context_;
    std::function<QString(const std::vector<hum::vocal::NoteEvent>&, double, std::uint64_t)> insert_callback_;
    HostContext take_context_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::shared_ptr<std::atomic<double>> progress_value_;
    std::uint64_t conversion_generation_{0};
    bool converting_{false}, inserted_{false};
    hum::vocal::ConversionResult result_;
    std::vector<hum::vocal::NoteEvent> preview_notes_;
};
}
