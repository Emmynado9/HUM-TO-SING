#pragma once
#include "PianoRollWidget.hpp"
#include <QWidget>
#include <functional>
class QButtonGroup;
class QToolButton;
class QLabel;
class QHBoxLayout;
namespace hum::app {
class EditorToolbar final : public QWidget {
public:
    explicit EditorToolbar(QWidget* parent=nullptr);
    void setTool(PianoRollWidget::Tool tool);
    void setSnapEnabled(bool enabled);
    void onToolChanged(std::function<void(PianoRollWidget::Tool)> callback);
    void onSnapChanged(std::function<void(bool)> callback);
    void addTransport(QWidget* widget);
private:
    QHBoxLayout* row_{nullptr};
    QButtonGroup* tools_;
    QToolButton* snap_;
    QLabel* description_;
    std::function<void(PianoRollWidget::Tool)> tool_changed_;
    std::function<void(bool)> snap_changed_;
};
}
