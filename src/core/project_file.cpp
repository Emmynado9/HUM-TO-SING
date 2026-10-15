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
        && canonical_notes.size() <= kMaximumProjectNotes
        && track_count > 0 && track_count <= 16
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
           << "audio_storage omitted\n"
           << "note_count " << project.canonical_notes.size() << '\n';
    for (const auto& note : project.canonical_notes) {
        output << "note "
               << static_cast<unsigned>(note.pitch) << ' '
               << static_cast<unsigned>(note.velocity) << ' '
               << note.start_seconds << ' '
               << note.duration_seconds << ' '
               << static_cast<unsigned>(note.channel) << '\n';
    }
    output << "track_count " << project.track_count << '\n'
           << "region_count " << project.playlist_regions.size() << '\n';
    for (const auto& region : project.playlist_regions) {
        output << "region " << region.track << ' ' << std::quoted(region.name)
               << ' ' << region.note_indices.size();
        for (const auto index : region.note_indices) { output << ' ' << index; }
        output << '\n';
    }
    output << "end\n";
    return output.str();
}

ProjectDecodeResult ProjectFileCodec::decode(const std::string_view bytes) {
    try {
        std::istringstream input{std::string(bytes)};
        std::string magic;
        std::uint64_t version = 0;
        if (!(input >> magic >> version) || magic != "HUM_TO_SING_PROJECT") {
            return corrupt("This is not a Hum-to-MIDI project file.");
        }
        if (version != 1 && version != kCurrentProjectSchemaVersion) {
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
            || !readKey(input, "rhythm_snap_seconds") || !(input >> project.rhythm_snap.interval_seconds)
            || !readKey(input, "audio_storage") || !(input >> audio_storage)
            || !readKey(input, "note_count") || !(input >> note_count)) {
            return corrupt("The project header is incomplete or malformed.");
        }
        const auto parsed_mode = parseMode(mode);
        if (!parsed_mode || ticks > std::numeric_limits<std::uint16_t>::max()
            || root > std::numeric_limits<std::uint8_t>::max()
            || magnet > 1 || snap_enabled > 1 || audio_storage != "omitted"
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
            project.canonical_notes.push_back(note);
        }

        if (version >= 2) {
            std::uint64_t track_count = 0, region_count = 0;
            if (!readKey(input, "track_count") || !(input >> track_count)
                || !readKey(input, "region_count") || !(input >> region_count)
                || track_count == 0 || track_count > 16 || region_count > 10'000) {
                return corrupt("The playlist lane or region count is invalid.");
            }
            project.track_count = static_cast<std::size_t>(track_count);
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
