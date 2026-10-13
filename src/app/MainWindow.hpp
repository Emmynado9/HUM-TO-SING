#pragma once

#include <QMainWindow>

#include <memory>

class QCloseEvent;

namespace hum::app {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hum::app
