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
    // Independent historical fixtures: do not manufacture legacy bytes with the current encoder.
    const std::string legacy_header =
        "tempo_bpm 120\nticks_per_quarter 960\nroot_pitch_class 0\nscale_mode major\n"
        "magnet_enabled 1\nrhythm_snap_enabled 1\nrhythm_snap_seconds 0.3\n"
        "audio_storage omitted\nnote_count 2\nnote 61 96 0.625 0.5 0\nnote 67 90 2.25 0.5 0\n";
    const auto old=ProjectFileCodec::decode("HUM_TO_SING_PROJECT 1\n" + legacy_header + "end\n");
    expect(old && old.project->schema_version==kCurrentProjectSchemaVersion && old.project->track_count==1,
        "version 1 projects upgrade in memory without being overwritten");
    const auto v2=ProjectFileCodec::decode("HUM_TO_SING_PROJECT 2\n" + legacy_header
        + "track_count 2\nregion_count 2\nregion 1 \"First\" 1 0\nregion 0 \"Second\" 1 1\nend\n");
    expect(v2 && v2.project->track_count==2 && v2.project->playlist_regions.size()==2
        && v2.project->playlist_regions[0].track==1 && v2.project->ticks_per_quarter==960
        && v2.project->grid_division==0 && v2.project->rhythm_snap.interval_seconds==0.3
        && !v2.project->metronome_enabled && !v2.project->canonical_notes[0].muted,
        "version 2 preserves lanes and legacy timing with unmuted notes and metronome off");
    auto corrupt=project; corrupt.playlist_regions[1].note_indices={0};
    expect(!ProjectFileCodec::encode(corrupt),"duplicate ownership cannot be saved");
    corrupt=project; corrupt.playlist_regions[1].note_indices={999};
    expect(!ProjectFileCodec::encode(corrupt),"missing note reference cannot be saved");
    expect(!editor.replaceAll(project.canonical_notes,corrupt.playlist_regions,2),
        "invalid arrangement load preserves the existing editor");
}
void chordAndQuantise() {
    using namespace hum::core;
    const auto key = makeScaleDefinition(0, ScaleMode::major);
    const std::vector<TimedMidiNote> hummed{{61,96,0,.2,0},{60,96,.3,.2,0},{66,96,1,.4,0}};
    const auto smooth = makeDiatonicTriads(hummed, key, true);
    expect(smooth.size() == 6 && smooth[0].pitch == 60 && smooth[0].duration_seconds == 1
        && smooth[3].pitch == 65 && smooth[3].start_seconds == 1,
        "off-key roots snap down on ties, repeated roots merge and pauses connect");
    const auto detached = makeDiatonicTriads(hummed, key, false);
    expect(detached.size() == 9 && detached[0].duration_seconds == .2,
        "disabling smooth chords preserves hummed rests and rearticulations");
    expect((ScaleDefinition{0,{0,3,7}}).nearestPitch(66) == 67,
        "off-key root snaps upward when the upper scale pitch is closer");
    const auto degrees = parseChordDegrees("I, iv - V | vi 7");
    expect(degrees && *degrees == std::vector<std::uint8_t>{0,3,4,5,6}, "Roman and numeric progression input accepts separators and case");
    expect(!parseChordDegrees("I VIII") && !parseChordDegrees("I potato") && !parseChordDegrees(""),
        "malformed progressions fail without partially inserting notes");
    const auto progression = makeChordProgression({{0,3,4,0}}, key, 2);
    expect(progression.size() == 12 && progression[0].pitch == 48 && progression[3].pitch == 53
        && progression[6].pitch == 55 && progression[9].start_seconds == 6,
        "I IV V I builds C F G C at exact beat-based timing");
    expect(chordDegreeName(key,1) == "ii · D minor" && chordDegreeName(key,6) == "vii° · B dim",
        "Roman labels show diatonic minor and diminished qualities");
    NoteEditor instrument_editor;
    expect(instrument_editor.setTrackInstrument(0, InstrumentVoice::acoustic_guitar) == NoteEditResult::applied
        && instrument_editor.tracks()[0].instrument == InstrumentVoice::acoustic_guitar, "existing track instrument can change");
    expect(instrument_editor.undo() && instrument_editor.tracks()[0].instrument == InstrumentVoice::grand_piano,
        "instrument change is undoable");
    const int major_roots[]{0,2,4,5,7,9,11};
    const int major_thirds[]{4,3,3,4,4,3,3};
    const int major_fifths[]{7,7,7,7,7,7,6};
    const int minor_roots[]{0,2,3,5,7,8,10};
    const int minor_thirds[]{3,3,4,3,3,4,4};
    const int minor_fifths[]{7,6,7,7,7,7,7};
    for (const auto mode : {ScaleMode::major, ScaleMode::natural_minor}) {
        for (std::uint8_t key = 0; key < 12; ++key) {
            for (int degree = 0; degree < 7; ++degree) {
                const bool major = mode == ScaleMode::major;
                const auto pitch = static_cast<std::uint8_t>(48 + key + (major ? major_roots : minor_roots)[degree]);
                const std::vector<TimedMidiNote> root{{pitch, 93, .3, .7, 0}};
                const auto chord = makeDiatonicTriads(root, makeScaleDefinition(key, mode));
                expect(chord.size() == 3 && chord[0].pitch == pitch
                    && chord[1].pitch == pitch + (major ? major_thirds : minor_thirds)[degree]
                    && chord[2].pitch == pitch + (major ? major_fifths : minor_fifths)[degree],
                    "every degree forms the correct major/minor/diminished triad in all twelve keys");
                for (const auto& note : chord) {
                    expect(note.start_seconds == .3 && note.duration_seconds == .7 && note.velocity == 93,
                        "all chord voices retain the hummed root timing and velocity");
                }
            }
        }
    }
    const auto triads = makeDiatonicTriads({{{60, 96, .13, .4, 0}, {62, 96, .62, .4, 0}}},
        makeScaleDefinition(0, ScaleMode::major));
    expect(triads.size() == 6 && triads[0].pitch == 60 && triads[1].pitch == 64 && triads[2].pitch == 67
        && triads[3].pitch == 62 && triads[4].pitch == 65 && triads[5].pitch == 69,
        "hummed C and D roots form C-major and D-minor diatonic triads in C major");
    NoteEditor editor;
    expect(editor.replaceAll({{60, 96, .13, .4, 0}, {64, 96, .62, .4, 0}}), "quantise fixture loads");
    editor.selectAll();
    expect(editor.quantizeSelected(.25) == NoteEditResult::applied
        && editor.notes()[0].start_seconds == .25 && editor.notes()[1].start_seconds == .5,
        "quantise snaps every selected onset to its nearest grid line");
}
void editableTracks() {
    using namespace hum::core;
    NoteEditor editor;
    expect(editor.replaceAll({{60,96,0,1,0},{62,96,1,1,0,true},{64,96,2,1,0}},
        {{{0},0,"A"},{{1},1,"B"},{{2},2,"C"}},3), "three-track fixture loads");
    expect(editor.setTrackKind(1,TrackKind::chords)==NoteEditResult::applied, "track type changes");
    expect(editor.notes().size()==3 && editor.notes()[1].pitch==62, "type changes preserve canonical notes");
    expect(editor.setTrackInstrument(1,InstrumentVoice::strings)==NoteEditResult::applied, "track instrument changes");
    expect(editor.setTrackMuted(0,true)==NoteEditResult::applied, "track mute is undoable");
    expect(!editor.notes()[0].muted && editor.projectedNotes()[0].muted && editor.projectedNotes()[1].muted,
        "track mute overlays note mute without overwriting canonical flags");
    const auto sequence=MidiTimeline::fromTimedNotes(editor.projectedNotes(),120,480);
    expect(sequence && sequence->notes.size()==1 && sequence->notes[0].pitch==64,
        "MIDI projection excludes muted tracks and independently muted notes");
    const auto exported=editor.exportSequence(120,480);
    expect(exported && exported->notes.size()==1 && exported->notes[0].channel==2
        && StandardMidiFileExporter::serialize(*exported).has_value(), "export routes surviving notes after mute filtering without stale indices");
    editor.setMagnetEnabled(true);
    expect(editor.projectedNotes()[0].muted, "Magnet refresh preserves track mute");
    expect(editor.undo() && !editor.tracks()[0].muted && editor.projectedNotes()[1].muted,
        "undo track mute leaves individual note mute intact");
    expect(editor.redo() && editor.tracks()[0].muted, "redo restores track mute");
    ProjectDocument project;
    project.canonical_notes=editor.notes();project.playlist_regions=editor.regions();
    project.track_count=editor.trackCount();project.tracks=editor.tracks();
    const auto encoded=ProjectFileCodec::encode(project);
    const auto decoded=ProjectFileCodec::decode(encoded.value_or(""));
    expect(decoded && decoded.project->tracks==project.tracks && !decoded.project->canonical_notes[0].muted,
        "schema 5 persists track mute/type/instrument independently of note mute");
    const auto legacy=ProjectFileCodec::decode(
        "HUM_TO_SING_PROJECT 4\ntempo_bpm 120\nticks_per_quarter 480\nroot_pitch_class 0\n"
        "scale_mode major\nmagnet_enabled 0\nrhythm_snap_enabled 0\nrhythm_snap_seconds 0.125\n"
        "metronome_enabled 0\ngrid_division 16\naudio_storage omitted\nnote_count 0\n"
        "track_count 1\ntrack chords strings\nregion_count 0\nend\n");
    expect(legacy && !legacy.project->tracks[0].muted && legacy.project->tracks[0].kind==TrackKind::chords,
        "schema 4 migrates with unmuted tracks and retained types/instruments");
    auto invalid=*encoded;
    const auto flag=invalid.find("track melody grand_piano 1");
    invalid.replace(flag,std::string("track melody grand_piano 1").size(),"track melody grand_piano 2");
    expect(!ProjectFileCodec::decode(invalid), "invalid track mute flags reject the whole project");
    expect(editor.selectMany({0,1,2}), "cross-track selection can be constructed in core");
    expect(editor.deleteTrack(1)==NoteEditResult::applied && editor.trackCount()==2,
        "delete track removes its lane transactionally");
    expect(editor.notes().size()==2 && editor.notes()[1].pitch==64 && editor.trackForNote(1)==1
        && editor.regions()[1].note_indices==std::vector<std::size_t>{1}, "deletion remaps notes and later lanes");
    expect(editor.selectedIndices().empty(), "deletion clears stale selected indices");
    expect(editor.undo() && editor.trackCount()==3 && editor.notes()[1].pitch==62
        && editor.tracks()[1].instrument==InstrumentVoice::strings, "one undo restores track notes and settings");
    expect(editor.deleteTrack(9)==NoteEditResult::invalid_index, "invalid track deletion is rejected");
    NoteEditor empty;
    expect(empty.add({60,96,0,1,0},0)==NoteEditResult::applied, "paint into explicit empty track");
    expect(empty.deleteTrack(0)==NoteEditResult::applied && empty.trackCount()==1 && empty.notes().empty(),
        "deleting the last track leaves an empty recording destination");
    expect(empty.undo() && empty.notes().size()==1, "last-track deletion undoes");
    for (int i=1;i<16;++i) { static_cast<void>(empty.addTrack()); }
    expect(empty.add({72,96,1,1,0},15)==NoteEditResult::applied, "sixteenth lane accepts notes");
    const auto full=empty.exportSequence(120,480);
    expect(full && full->isValid() && StandardMidiFileExporter::serialize(*full).has_value(),
        "all sixteen lanes export within the MIDI channel bounds");
}

}
int main() {
    transactions(); persistenceAndExport(); chordAndQuantise(); editableTracks();
    if (failures) { std::cerr<<failures<<" arrangement checks failed.\n"; return 1; }
    std::cout<<"All arrangement tests passed ("<<checks<<" assertions).\n";
}
