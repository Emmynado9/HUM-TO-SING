#pragma once
#include <QIcon>
namespace hum::app {
enum class ToolIcon { paint, select, resize, erase, mute, slice, zoom, snap,
    microphone, piano, pitch, play, pause, stop, loop, loopRange, loopAll, exportMidi, addTrack, duplicate, undo, redo, metronome, speaker };
QIcon toolIcon(ToolIcon symbol, bool light = false);
}
