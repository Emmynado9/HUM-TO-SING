#include "PianoRollInsertAdapter.hpp"
#include <cmath>

namespace hum::app::vocal {
std::vector<core::TimedMidiNote> PianoRollInsertAdapter::timedNotes(
    const std::vector<hum::vocal::NoteEvent>& notes) {
    std::vector<core::TimedMidiNote> timed;
    if (notes.size() > 100'000) { return timed; }
    timed.reserve(notes.size());
    for (const auto& note : notes) {
        if (note.pitch < 0 || note.pitch > 127 || note.velocity < 1 || note.velocity > 127) { return {}; }
        core::TimedMidiNote event{static_cast<std::uint8_t>(note.pitch), static_cast<std::uint8_t>(note.velocity),
            note.start, note.duration, 0, false};
        if (!event.isValid()) { return {}; }
        timed.push_back(event);
    }
    return timed;
}
core::NoteEditResult PianoRollInsertAdapter::insert(core::NoteEditor& editor,
    const std::vector<hum::vocal::NoteEvent>& notes, double origin, std::size_t track) {
    if (!std::isfinite(origin) || origin < 0 || origin > 600 || track >= editor.trackCount()) {
        return core::NoteEditResult::invalid_index;
    }
    return editor.appendRegion(timedNotes(notes), origin, track);
}
}
