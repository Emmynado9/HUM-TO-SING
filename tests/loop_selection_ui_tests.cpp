#include "PianoRollWidget.hpp"
#include "PlaylistWidget.hpp"
#include <QApplication>
#include <QTest>
#include <iostream>
int main(int argc,char**argv) {
    QApplication app(argc,argv);
    int failures=0;
    auto check=[&](bool ok,const char* text){if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';}};
    hum::core::NoteEditor editor;
    check(editor.replaceAll({{60,96,.5,1,0}}),"fixture loads");
    hum::app::PianoRollWidget roll(editor);
    hum::app::PlaylistWidget playlist(editor);
    roll.resize(900,400);playlist.resize(900,300);roll.show();playlist.show();roll.refresh(true);
    QApplication::processEvents();
    std::optional<hum::core::LoopRange> selected;
    auto changed=[&](hum::core::LoopRange range){selected=range;roll.setLoopRange(range,true);playlist.setLoopRange(range,true);};
    roll.setLoopChangedCallback(changed);playlist.setLoopChangedCallback(changed);
    roll.setRhythmSnap({true,.25});playlist.setRhythmSnap({true,.25});
    roll.setLoopTool(true);
    auto rect=roll.noteViewportRect(0);
    QTest::mousePress(roll.viewport(),Qt::LeftButton,Qt::NoModifier,rect.bottomRight().toPoint());
    QTest::mouseRelease(roll.viewport(),Qt::LeftButton,Qt::NoModifier,rect.topLeft().toPoint());
    check(selected && *selected==hum::core::LoopRange{.5,1.5},"reverse piano drag selects a snapped loop without needing mouse-move delivery");
    check(playlist.loopRange()==selected && !editor.canUndo(),"loop selection shares time without mutating notes/history");
    QTest::mousePress(roll.viewport(),Qt::LeftButton,Qt::NoModifier,rect.topLeft().toPoint());
    QTest::keyClick(&roll,Qt::Key_Escape);
    QTest::mouseRelease(roll.viewport(),Qt::LeftButton,Qt::NoModifier,rect.bottomRight().toPoint());
    check(roll.loopRange()==selected && !editor.canUndo(),"Escape cancels only the pending loop gesture");
    QTest::mouseDClick(roll.viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.right()+30,rect.top()+8));
    check(editor.notes().size()==1 && !editor.canUndo(),"double clicking in loop mode cannot insert a note");
    playlist.setLoopTool(true);
    rect=playlist.regionViewportRect(0);
    QTest::mousePress(playlist.viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.left(),10));
    QTest::mouseRelease(playlist.viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rect.right(),10));
    check(selected && *selected==hum::core::LoopRange{.5,1.5},"Playlist ruler drag selects the same musical time range");
    check(editor.notes()[0].start_seconds==.5 && editor.notes()[0].duration_seconds==1 && !editor.canUndo(),
        "loop gestures never move or resize canonical notes");
    int opened=0;playlist.setOpenEditorCallback([&]{++opened;});
    QTest::mouseDClick(playlist.viewport(),Qt::LeftButton,Qt::NoModifier,rect.center().toPoint());
    check(opened==0,"loop-mode double click cannot open a region for editing");
    check(!roll.viewport()->grab().isNull() && !playlist.viewport()->grab().isNull(),"both views paint highlighted loop ranges");
    return failures?1:0;
}
