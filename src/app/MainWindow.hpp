#pragma once

#include <QMainWindow>

#include <memory>

class QCloseEvent;

namespace hum::core { class IAudioCapture; }
namespace hum::app {
class IRecordingGuide;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr, core::IAudioCapture* capture = nullptr, IRecordingGuide* guide = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hum::app
