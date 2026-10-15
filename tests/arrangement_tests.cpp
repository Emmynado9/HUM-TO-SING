#include "hum/core/note_editor.hpp"
#include "hum/core/project_file.hpp"
#include "hum/core/midi_file.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
int failures=0, checks=0;
void expect(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool same(const hum::core::TimedMidiNote& a, const hum::core::TimedMidiNote& b) {
    return a.pitch==b.pitch && a.velocity==b.velocity && a.channel==b.channel
        && a.start_seconds==b.start_seconds && a.duration_seconds==b.duration_seconds;
}
void transactions() {
    using namespace hum::core;
    NoteEditor editor;
    const std::vector<TimedMidiNote> original{{61,96,0.5,0.5,2},{64,72,1.25,0.25,2}};
    expect(editor.replaceAll(original), "legacy phrase loads into one canonical region");
    expect(editor.trackCount()==1 && editor.regions().size()==1, "one region references legacy notes");
    expect(editor.addTrack()==NoteEditResult::applied, "track addition is transactional");
    expect(editor.undo() && editor.trackCount()==1, "track addition undoes");
    expect(editor.redo() && editor.trackCount()==2, "track addition redoes");
    expect(editor.appendRegion({{72,85,0.25,1,0},{76,85,0.25,1,0}},4,1)==NoteEditResult::applied,
        "a phrase with overlapping MIDI events appends on the second lane");
    expect(editor.notes().size()==4 && editor.notes()[2].start_seconds==4.25,
        "append retains recorded relative timing and existing notes");
    editor.clearHistory();
    expect(editor.setScale(makeScaleDefinition(0,ScaleMode::major)), "C major selects");
    editor.setMagnetEnabled(true);
    expect(editor.moveRegion(0,0.125,1)==NoteEditResult::applied, "one move changes lane and start times");
    expect(editor.notes()[0].pitch==61 && editor.projectedNotes()[0].pitch==60,
        "region drag preserves canonical pitch under Magnet");
    expect(editor.notes()[0].start_seconds==0.625 && editor.notes()[1].start_seconds==1.375,
        "move preserves relative timing and internal rests");
    expect(editor.notes()[0].duration_seconds==0.5 && editor.notes()[0].velocity==96
        && editor.notes()[0].channel==2 && editor.regions()[0].track==1,
        "move changes only starts and arrangement lane");
    expect(editor.undo() && same(editor.notes()[0],original[0]) && editor.regions()[0].track==0,
        "one undo restores the complete region move");
    expect(!editor.canUndo(), "move produces one history entry");
    expect(editor.redo(), "region move redoes");
    expect(editor.duplicateRegion(0)==NoteEditResult::applied, "duplicate repeats a phrase");
    expect(editor.regions().size()==3 && editor.notes().size()==6 && editor.notes()[4].start_seconds==1.625,
        "duplicate starts immediately after the source region and retains its two notes");
    expect(editor.notes()[4].pitch==61 && editor.projectedNotes()[4].pitch==60,
        "duplicate uses canonical rather than snapped pitches");
    expect(editor.deleteRegion(0)==NoteEditResult::applied, "delete removes one region");
    expect(editor.notes().size()==4 && editor.regions()[0].note_indices==std::vector<std::size_t>({0,1})
        && editor.regions()[1].note_indices==std::vector<std::size_t>({2,3}), "delete remaps surviving membership");
    expect(editor.undo() && editor.notes().size()==6 && editor.regions().size()==3, "region delete undoes");
    expect(editor.select(0) && editor.deleteSelected()==NoteEditResult::applied,
        "piano-roll delete maintains membership");
    expect(validPlaylistRegions(editor.regions(),editor.notes().size(),editor.trackCount()),
        "note deletion leaves valid region references");
    expect(editor.add({65,99,0.75,0.2,3})==NoteEditResult::applied,
        "piano-roll addition is retained in a region");
    expect(validPlaylistRegions(editor.regions(),editor.notes().size(),editor.trackCount()),
        "note addition leaves valid region references");
    editor.setMagnetEnabled(false);
    expect(editor.notes().back().pitch==65 && editor.projectedNotes()[0].pitch==64,
        "Magnet OFF exposes canonical notes after arrangement edits");
    expect(editor.moveRegion(0,-100,0)==NoteEditResult::invalid_note,
        "negative note starts reject the complete group transaction");
    expect(editor.moveRegion(0,std::numeric_limits<double>::quiet_NaN(),0)==NoteEditResult::invalid_note,
        "nonfinite movement is rejected");
    expect(editor.moveRegion(99,0,0)==NoteEditResult::invalid_index, "invalid region is rejected");
    expect(editor.moveRegion(0,0,99)==NoteEditResult::invalid_index, "invalid track is rejected");
    expect(editor.appendRegion({},0,0)==NoteEditResult::invalid_note, "silence creates no region");
    for (std::size_t i=editor.trackCount(); i<16; ++i) { expect(editor.addTrack()==NoteEditResult::applied,"supported lane adds"); }
    expect(editor.addTrack()==NoteEditResult::invalid_index && editor.trackCount()==16, "lane count is bounded");
}
void persistenceAndExport() {
    using namespace hum::core;
    NoteEditor editor;
    expect(editor.replaceAll({{61,96,.5,.5,0}}),"fixture loads");
    expect(editor.addTrack()==NoteEditResult::applied,"fixture adds track");
    expect(editor.appendRegion({{67,90,.25,.5,0}},2,1)==NoteEditResult::applied,"second phrase appends");
    expect(editor.moveRegion(0,.125,1)==NoteEditResult::applied,"first phrase arranges");
    ProjectDocument project;
    project.canonical_notes=editor.notes(); project.playlist_regions=editor.regions();
    project.track_count=editor.trackCount(); project.magnet_enabled=true;
    const auto bytes=ProjectFileCodec::encode(project);
    const auto loaded=ProjectFileCodec::decode(bytes.value_or(""));
    expect(loaded && loaded.project->playlist_regions==project.playlist_regions
        && loaded.project->track_count==2,"project preserves region identity, membership and lanes");
    expect(loaded && same(loaded.project->canonical_notes[0],project.canonical_notes[0]),
        "save/load preserves canonical notes exactly");
    NoteEditor reopened;
    if (loaded) {
        expect(reopened.replaceAll(loaded.project->canonical_notes,loaded.project->playlist_regions,loaded.project->track_count),
            "saved arrangement loads into the canonical editor");
        reopened.setMagnetEnabled(true);
        const auto sequence=MidiTimeline::fromTimedNotes(reopened.projectedNotes(),120,480);
        expect(sequence && sequence->notes.size()==2 && sequence->notes[0].pitch==60
            && sequence->notes[0].start_tick==600 && sequence->notes[1].start_tick==2160,
            "MIDI uses audible Magnet pitches and arranged absolute timing");
        expect(sequence && StandardMidiFileExporter::serialize(*sequence).has_value(),
            "arranged notes serialize to Standard MIDI");
    }
    std::string legacy=*bytes;
    legacy.replace(legacy.find("PROJECT 2"),9,"PROJECT 1");
    legacy.erase(legacy.find("track_count")); legacy+="end\n";
    const auto old=ProjectFileCodec::decode(legacy);
    expect(old && old.project->schema_version==2 && old.project->track_count==1,
        "version 1 projects upgrade in memory without being overwritten");
    auto corrupt=project; corrupt.playlist_regions[1].note_indices={0};
    expect(!ProjectFileCodec::encode(corrupt),"duplicate ownership cannot be saved");
    corrupt=project; corrupt.playlist_regions[1].note_indices={999};
    expect(!ProjectFileCodec::encode(corrupt),"missing note reference cannot be saved");
    expect(!editor.replaceAll(project.canonical_notes,corrupt.playlist_regions,2),
        "invalid arrangement load preserves the existing editor");
}
}
int main() {
    transactions(); persistenceAndExport();
    if (failures) { std::cerr<<failures<<" arrangement checks failed.\n"; return 1; }
    std::cout<<"All arrangement tests passed ("<<checks<<" assertions).\n";
}
