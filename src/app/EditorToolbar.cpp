#include "EditorToolbar.hpp"
#include "ToolIcons.hpp"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
namespace hum::app {
EditorToolbar::EditorToolbar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("pianoToolbar"));
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(6);
    auto* row=new QHBoxLayout();row_=row;row->setSpacing(8);layout->addLayout(row);
    tools_=new QButtonGroup(this);tools_->setExclusive(true);
    using Tool=PianoRollWidget::Tool;
    struct Entry {const char* label;Tool tool;ToolIcon icon;const char* help;};
    const std::array entries{
        Entry{"Paint",Tool::paint,ToolIcon::paint,"Click empty grid space to add a note."},
        Entry{"Select",Tool::select,ToolIcon::select,"Drag empty space to select notes. Shift-click extends selection. Drag notes to move."},
        Entry{"Resize",Tool::resize,ToolIcon::resize,"Drag a note to change its duration."},
        Entry{"Erase",Tool::erase,ToolIcon::erase,"Click a note to delete it or its selected group. Undo restores it."},
        Entry{"Mute",Tool::mute,ToolIcon::mute,"Click to mute or unmute notes in both playback and MIDI export."},
        Entry{"Slice",Tool::slice,ToolIcon::slice,"Click to split selected notes at that time."},
        Entry{"Zoom",Tool::zoom,ToolIcon::zoom,"Click to zoom in. Alt-click to zoom out."}};
    for(const auto& e:entries){
        auto* button=new QToolButton(this);button->setText(QString::fromLatin1(e.label));
        button->setObjectName(QStringLiteral("tool%1Button").arg(QString::fromLatin1(e.label)));
        button->setToolTip(QString::fromLatin1(e.help));button->setAccessibleName(button->text());
        button->setIcon(toolIcon(e.icon));button->setIconSize(QSize(28,28));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);button->setCheckable(true);
        button->setMinimumSize(76,65);tools_->addButton(button,static_cast<int>(e.tool));row->addWidget(button);
    }
    snap_=new QToolButton(this);snap_->setObjectName(QStringLiteral("toolSnapButton"));
    snap_->setText(QStringLiteral("Snap"));snap_->setAccessibleName(QStringLiteral("Rhythm snap"));
    snap_->setToolTip(QStringLiteral("Toggle time snapping to the selected rhythm interval. Scale Magnet is a separate pitch control."));
    snap_->setIcon(toolIcon(ToolIcon::snap));snap_->setIconSize(QSize(28,28));
    snap_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);snap_->setCheckable(true);snap_->setMinimumSize(76,65);
    row->addWidget(snap_);row->addStretch();
    description_=new QLabel(this);description_->setWordWrap(true);layout->addWidget(description_);
    setStyleSheet(QStringLiteral("QToolButton { background: #fff; color: #333; border: 1px solid #d7d7da; border-radius: 10px; padding: 6px; } QToolButton:hover { background: #edf2ee; } QToolButton:checked { background: #263c30; color: white; border-color: #263c30; } QToolButton:disabled { color: #999; background: #eee; } QLabel { color: #606067; font-size: 11px; }"));
    connect(tools_, &QButtonGroup::idClicked,this,[this](int id){
        const auto tool=static_cast<Tool>(id);setTool(tool);if(tool_changed_)tool_changed_(tool);
    });
    connect(snap_, &QToolButton::toggled,this,[this](bool enabled){if(snap_changed_)snap_changed_(enabled);});
    setTool(Tool::select);
}
void EditorToolbar::setTool(PianoRollWidget::Tool tool){
    if(auto* button=tools_->button(static_cast<int>(tool))){button->setChecked(true);
        description_->setText(QStringLiteral("%1 · %2").arg(button->text(),button->toolTip()));}
}
void EditorToolbar::setSnapEnabled(bool enabled){const QSignalBlocker blocker(snap_);snap_->setChecked(enabled);}
void EditorToolbar::onToolChanged(std::function<void(PianoRollWidget::Tool)> callback){tool_changed_=std::move(callback);}
void EditorToolbar::onSnapChanged(std::function<void(bool)> callback){snap_changed_=std::move(callback);}
void EditorToolbar::addTransport(QWidget* widget){row_->addWidget(widget);}
}
