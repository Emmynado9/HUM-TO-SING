#include "EditorToolbar.hpp"
#include "hum/core/midi.hpp"
#include <QApplication>
#include <QToolButton>
#include <QTest>
#include <iostream>
int main(int argc,char** argv){
    QApplication app(argc,argv);int failures=0;
    auto expect=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}};
    using namespace hum;
    core::NoteEditor editor;static_cast<void>(editor.replaceAll({{61,90,.5,1,0}}));
    app::PianoRollWidget roll(editor);roll.resize(800,400);roll.show();roll.focusNotes();
    app::EditorToolbar toolbar;toolbar.show();
    toolbar.onToolChanged([&](auto tool){roll.setTool(tool);});
    bool snap=false;toolbar.onSnapChanged([&](bool enabled){snap=enabled;roll.setRhythmSnap({enabled,.125});});
    QApplication::processEvents();
    const auto buttons=toolbar.findChildren<QToolButton*>();
    expect(buttons.size()==8,"Toolbar must provide all eight requested icon buttons");
    for(auto* b:buttons)expect(!b->icon().isNull()&&!b->text().isEmpty()&&!b->toolTip().isEmpty(),"Tools must have a visible icon, label and help");
    auto* mute=toolbar.findChild<QToolButton*>("toolMuteButton");
    auto* select=toolbar.findChild<QToolButton*>("toolSelectButton");
    auto* snapButton=toolbar.findChild<QToolButton*>("toolSnapButton");
    mute->click();expect(roll.tool()==app::PianoRollWidget::Tool::mute&&mute->isChecked()&&!select->isChecked(),"Tool clicks must change the actual editor and selection highlight");
    QTest::mouseClick(roll.viewport(),Qt::LeftButton,Qt::NoModifier,roll.noteViewportRect(0).center().toPoint());
    const auto midi=core::MidiTimeline::fromTimedNotes(editor.projectedNotes());
    expect(editor.notes()[0].muted&&midi&&midi->notes.empty(),"Mute icon must affect actual note playback/export data");
    snapButton->click();expect(snap&&mute->isChecked()&&roll.tool()==app::PianoRollWidget::Tool::mute,"Snap toggles independently of the active tool");
    toolbar.setSnapEnabled(false);expect(!snapButton->isChecked()&&snap,"Programmatic snap synchronization must not emit an edit");
    select->click();expect(roll.tool()==app::PianoRollWidget::Tool::select&&!mute->isChecked(),"Select icon restores selection tool");
    std::cout<<"Toolbar regressions: "<<failures<<" failure(s)\n";return failures?1:0;
}
