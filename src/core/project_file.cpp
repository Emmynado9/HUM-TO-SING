#include "hum/core/project_file.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace hum::core {

namespace {

constexpr std::size_t kMaximumProjectNotes = 100'000;

[[nodiscard]] const char* modeToken(const ScaleMode mode) noexcept {
    return mode == ScaleMode::major ? "major" : "natural_minor";
}

[[nodiscard]] std::optional<ScaleMode> parseMode(const std::string& token) noexcept {
    if (token == "major") {
        return ScaleMode::major;
    }
    if (token == "natural_minor") {
        return ScaleMode::natural_minor;
    }
    return std::nullopt;
}

[[nodiscard]] const char* trackKindToken(const TrackKind kind) noexcept {
    return kind == TrackKind::chords ? "chords" : "melody";
}
[[nodiscard]] std::optional<TrackKind> parseTrackKind(const std::string& token) noexcept {
    if (token == "melody") { return TrackKind::melody; }
    if (token == "chords") { return TrackKind::chords; }
    return std::nullopt;
}
[[nodiscard]] const char* instrumentToken(const InstrumentVoice instrument) noexcept {
    switch (instrument) {
    case InstrumentVoice::grand_piano: return "grand_piano";
    case InstrumentVoice::electric_piano: return "electric_piano";
    case InstrumentVoice::acoustic_guitar: return "acoustic_guitar";
    case InstrumentVoice::electric_guitar: return "electric_guitar";
    case InstrumentVoice::strings: return "strings";
    }
    return "grand_piano";
}
[[nodiscard]] std::optional<InstrumentVoice> parseInstrument(const std::string& token) noexcept {
    if (token == "grand_piano") return InstrumentVoice::grand_piano;
    if (token == "electric_piano") return InstrumentVoice::electric_piano;
    if (token == "acoustic_guitar") return InstrumentVoice::acoustic_guitar;
    if (token == "electric_guitar") return InstrumentVoice::electric_guitar;
    if (token == "strings") return InstrumentVoice::strings;
    return std::nullopt;
}

[[nodiscard]] bool readKey(std::istringstream& input, const char* expected) {
    std::string key;
    return static_cast<bool>(input >> key) && key == expected;
}

[[nodiscard]] ProjectDecodeResult corrupt(std::string message) {
    return {ProjectDecodeStatus::corrupt, std::nullopt, std::move(message)};
}

} // namespace

bool ProjectDocument::isValid() const noexcept {
    constexpr std::uint16_t kMaximumMetricDivision = 0x7fff;
    return schema_version == kCurrentProjectSchemaVersion
        && std::isfinite(tempo_bpm) && tempo_bpm >= 20.0 && tempo_bpm <= 400.0
        && ticks_per_quarter > 0 && ticks_per_quarter <= kMaximumMetricDivision
        && root_pitch_class < 12
        && (scale_mode == ScaleMode::major || scale_mode == ScaleMode::natural_minor)
        && rhythm_snap.isValid()
        && (grid_division == 0 || grid_division == 4 || grid_division == 8
            || grid_division == 16 || grid_division == 32 || grid_division == 64)
        && lyrics.size() <= 1024U * 1024U && validVoiceClips(voice_clips)
        && canonical_notes.size() <= kMaximumProjectNotes
        && track_count > 0 && track_count <= 16
        && (tracks.empty() || (tracks.size() == track_count
            && std::all_of(tracks.cbegin(), tracks.cend(), validTrackDefinition)))
        && (playlist_regions.empty()
            || validPlaylistRegions(playlist_regions, canonical_notes.size(), track_count))
        && std::all_of(canonical_notes.cbegin(), canonical_notes.cend(), [](const auto& note) {
            return note.isValid();
        });
}

std::optional<std::string> ProjectFileCodec::encode(const ProjectDocument& project) {
    if (!project.isValid()) {
        return std::nullopt;
    }

    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "HUM_TO_SING_PROJECT " << project.schema_version << '\n'
           << "tempo_bpm " << project.tempo_bpm << '\n'
           << "ticks_per_quarter " << project.ticks_per_quarter << '\n'
           << "root_pitch_class " << static_cast<unsigned>(project.root_pitch_class) << '\n'
           << "scale_mode " << modeToken(project.scale_mode) << '\n'
           << "magnet_enabled " << (project.magnet_enabled ? 1 : 0) << '\n'
           << "rhythm_snap_enabled " << (project.rhythm_snap.enabled ? 1 : 0) << '\n'
           << "rhythm_snap_seconds " << project.rhythm_snap.interval_seconds << '\n'
           << "metronome_enabled " << (project.metronome_enabled ? 1 : 0) << '\n'
           << "grid_division " << project.grid_division << '\n'
           << "audio_storage embedded_voice\n"
           << "note_count " << project.canonical_notes.size() << '\n';
    for (const auto& note : project.canonical_notes) {
        output << "note "
               << static_cast<unsigned>(note.pitch) << ' '
               << static_cast<unsigned>(note.velocity) << ' '
               << note.start_seconds << ' '
               << note.duration_seconds << ' '
               << static_cast<unsigned>(note.channel) << ' ' << (note.muted ? 1 : 0) << '\n';
    }
    const auto tracks = project.tracks.empty()
        ? std::vector<TrackDefinition>(project.track_count) : project.tracks;
    output << "track_count " << project.track_count << '\n';
    for (const auto& track : tracks) {
        output << "track " << trackKindToken(track.kind) << ' ' << instrumentToken(track.instrument) << ' ' << (track.muted ? 1 : 0) << '\n';
    }
    output << "region_count " << project.playlist_regions.size() << '\n';
    for (const auto& region : project.playlist_regions) {
        output << "region " << region.track << ' ' << std::quoted(region.name)
               << ' ' << region.note_indices.size();
        for (const auto index : region.note_indices) { output << ' ' << index; }
        output << '\n';
    }
    output << "lyrics " << std::quoted(project.lyrics) << '\n';
    output << "voice_count " << project.voice_clips.size() << '\n';
    constexpr char hex[] = "0123456789abcdef";
    for (const auto& clip : project.voice_clips) {
        output << "voice " << std::quoted(clip.name) << ' ' << clip.start_seconds << ' '
               << clip.sample_rate << ' ' << clip.muted << ' ' << clip.samples.size() << ' ';
        for (const auto sample : clip.samples) {
            const auto bits = static_cast<std::uint16_t>(sample);
            for (int shift = 12; shift >= 0; shift -= 4) output << hex[(bits >> shift) & 15];
        }
        output << '\n';
    }
    output << "end\n";
    return output.str();
}

ProjectDecodeResult ProjectFileCodec::decode(const std::string_view bytes) {
    try {
        if (bytes.size() > 64U * 1024U * 1024U) return corrupt("Project exceeds 64 MiB.");
        std::istringstream input{std::string(bytes)};
        std::string magic;
        std::uint64_t version = 0;
        if (!(input >> magic >> version) || magic != "HUM_TO_SING_PROJECT") {
            return corrupt("This is not a Hum-to-MIDI project file.");
        }
        if (version < 1 || version > kCurrentProjectSchemaVersion) {
            return {
                ProjectDecodeStatus::unsupported_version,
                std::nullopt,
                "This project uses an unsupported schema version.",
            };
        }

        ProjectDocument project;
        // Version 1 files remain readable as a single phrase on the first lane.
        project.schema_version = kCurrentProjectSchemaVersion;
        unsigned ticks = 0;
        unsigned root = 0;
        unsigned magnet = 0;
        unsigned snap_enabled = 0;
        std::string mode;
        std::string audio_storage;
        std::uint64_t note_count = 0;
        if (!readKey(input, "tempo_bpm") || !(input >> project.tempo_bpm)
            || !readKey(input, "ticks_per_quarter") || !(input >> ticks)
            || !readKey(input, "root_pitch_class") || !(input >> root)
            || !readKey(input, "scale_mode") || !(input >> mode)
            || !readKey(input, "magnet_enabled") || !(input >> magnet)
            || !readKey(input, "rhythm_snap_enabled") || !(input >> snap_enabled)
            || !readKey(input, "rhythm_snap_seconds") || !(input >> project.rhythm_snap.interval_seconds)) {
            return corrupt("The project header is incomplete or malformed.");
        }
        if (version >= 3) {
            unsigned metronome = 0;
            if (!readKey(input, "metronome_enabled") || !(input >> metronome) || metronome > 1
                || !readKey(input, "grid_division") || !(input >> project.grid_division)) {
                return corrupt("The metronome or grid setting is invalid.");
            }
            project.metronome_enabled = metronome == 1;
        } else {
            project.grid_division = 0;
        }
        if (!readKey(input, "audio_storage") || !(input >> audio_storage)
            || !readKey(input, "note_count") || !(input >> note_count)) {
            return corrupt("The project header is incomplete or malformed.");
        }
        const auto parsed_mode = parseMode(mode);
        if (!parsed_mode || ticks > std::numeric_limits<std::uint16_t>::max()
            || root > std::numeric_limits<std::uint8_t>::max()
            || magnet > 1 || snap_enabled > 1 || audio_storage != (version >= 6 ? "embedded_voice" : "omitted")
            || note_count > kMaximumProjectNotes) {
            return corrupt("The project settings are invalid.");
        }
        project.ticks_per_quarter = static_cast<std::uint16_t>(ticks);
        project.root_pitch_class = static_cast<std::uint8_t>(root);
        project.scale_mode = *parsed_mode;
        project.magnet_enabled = magnet == 1;
        project.rhythm_snap.enabled = snap_enabled == 1;
        project.canonical_notes.reserve(static_cast<std::size_t>(note_count));

        for (std::uint64_t index = 0; index < note_count; ++index) {
            unsigned pitch = 0;
            unsigned velocity = 0;
            unsigned channel = 0;
            TimedMidiNote note;
            if (!readKey(input, "note")
                || !(input >> pitch >> velocity >> note.start_seconds
                    >> note.duration_seconds >> channel)
                || pitch > std::numeric_limits<std::uint8_t>::max()
                || velocity > std::numeric_limits<std::uint8_t>::max()
                || channel > std::numeric_limits<std::uint8_t>::max()) {
                return corrupt("A project note is incomplete or malformed.");
            }
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.velocity = static_cast<std::uint8_t>(velocity);
            note.channel = static_cast<std::uint8_t>(channel);
            if (version >= 3) {
                unsigned muted = 0;
                if (!(input >> muted) || muted > 1) { return corrupt("A note mute flag is invalid."); }
                note.muted = muted == 1;
            }
            project.canonical_notes.push_back(note);
        }

        if (version >= 2) {
            std::uint64_t track_count = 0, region_count = 0;
            if (!readKey(input, "track_count") || !(input >> track_count)
                || track_count == 0 || track_count > 16) {
                return corrupt("The playlist lane or region count is invalid.");
            }
            project.track_count = static_cast<std::size_t>(track_count);
            if (version >= 4) {
                project.tracks.reserve(project.track_count);
                for (std::size_t i = 0; i < project.track_count; ++i) {
                    std::string kind, instrument;
                    if (!readKey(input, "track") || !(input >> kind >> instrument)) {
                        return corrupt("A track definition is incomplete or malformed.");
                    }
                    const auto parsed_kind = parseTrackKind(kind);
                    const auto parsed_instrument = parseInstrument(instrument);
                    if (!parsed_kind || !parsed_instrument) {
                        return corrupt("A track kind or instrument is invalid.");
                    }
                    unsigned muted = 0;
                    if (version >= 5 && (!(input >> muted) || muted > 1)) {
                        return corrupt("A track mute flag is invalid.");
                    }
                    project.tracks.push_back({*parsed_kind, *parsed_instrument, muted == 1});
                }
            } else {
                project.tracks.assign(project.track_count, {});
            }
            if (!readKey(input, "region_count") || !(input >> region_count) || region_count > 10'000) {
                return corrupt("The playlist region count is invalid.");
            }
            std::uint64_t assigned_count = 0;
            for (std::uint64_t i = 0; i < region_count; ++i) {
                PlaylistRegion region;
                std::uint64_t track = 0, size = 0;
                if (!readKey(input, "region") || !(input >> track >> std::quoted(region.name) >> size)
                    || track >= track_count || size == 0 || size > note_count
                    || assigned_count + size > note_count) {
                    return corrupt("A playlist region is malformed.");
                }
                assigned_count += size;
                region.track = static_cast<std::size_t>(track);
                for (std::uint64_t j = 0; j < size; ++j) {
                    std::uint64_t index = 0;
                    if (!(input >> index) || index >= note_count) {
                        return corrupt("A playlist region references a missing note.");
                    }
                    region.note_indices.push_back(static_cast<std::size_t>(index));
                }
                project.playlist_regions.push_back(std::move(region));
            }
        }
        if (version >= 6) {
            std::size_t count = 0, total = 0;
            if (!readKey(input, "lyrics") || !(input >> std::quoted(project.lyrics)) || project.lyrics.size() > 1024U * 1024U
                || !readKey(input, "voice_count") || !(input >> count) || count > 16) return corrupt("Invalid lyrics or voice count.");
            for (std::size_t i = 0; i < count; ++i) {
                VoiceClip clip; std::size_t size = 0; unsigned muted = 0; std::string pcm;
                if (!readKey(input, "voice") || !(input >> std::quoted(clip.name) >> clip.start_seconds >> clip.sample_rate >> muted >> size)
                    || muted > 1 || size == 0 || size > kMaximumVoiceSamples - total) return corrupt("Invalid voice clip.");
                if (!(input >> pcm) || pcm.size() != size * 4) return corrupt("Truncated voice audio.");
                clip.muted = muted == 1; clip.samples.reserve(size); total += size;
                for (std::size_t j = 0; j < pcm.size(); j += 4) {
                    unsigned bits = 0;
                    for (std::size_t k = 0; k < 4; ++k) {
                        const auto c = pcm[j + k];
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return corrupt("Invalid voice audio encoding.");
                        bits = (bits << 4) | static_cast<unsigned>(c <= '9' ? c - '0' : c - 'a' + 10);
                    }
                    clip.samples.push_back(static_cast<std::int16_t>(bits < 32768 ? int(bits) : int(bits) - 65536));
                }
                if (!clip.valid()) return corrupt("Invalid voice timing or audio format.");
                project.voice_clips.push_back(std::move(clip));
            }
        }
        std::string end;
        if (!(input >> end) || end != "end") {
            return corrupt("The project end marker is missing.");
        }
        std::string trailing;
        if (input >> trailing) {
            return corrupt("The project contains unexpected trailing data.");
        }
        if (!project.isValid()) {
            return corrupt("The project contains values outside the supported range.");
        }
        return {ProjectDecodeStatus::ok, std::move(project), {}};
    } catch (...) {
        return corrupt("The project could not be decoded safely.");
    }
}

} // namespace hum::core
