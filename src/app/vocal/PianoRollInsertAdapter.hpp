#pragma once
#include "hum/core/note_editor.hpp"
#include "hum/vocal/transcription.hpp"

namespace hum::app::vocal {
struct HostContext {
    double origin{0}, bpm{120};
    std::uint16_t ppq{480};
    std::size_t track{0};
    std::uint64_t project_generation{0};
    core::InstrumentVoice instrument{core::InstrumentVoice::grand_piano};
    core::ScaleDefinition scale;
    bool magnet{false}, muted{false};
    std::vector<hum::vocal::NoteEvent> reference_notes;
};

class PianoRollInsertAdapter final {
public:
    [[nodiscard]] static std::vector<core::TimedMidiNote> timedNotes(
        const std::vector<hum::vocal::NoteEvent>& notes);
    [[nodiscard]] static core::NoteEditResult insert(core::NoteEditor& editor,
        const std::vector<hum::vocal::NoteEvent>& notes, double origin, std::size_t track);
};
}
