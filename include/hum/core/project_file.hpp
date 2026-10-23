#pragma once

#include "hum/core/midi.hpp"
#include "hum/core/voice_clip.hpp"
#include "hum/core/note_editor.hpp"
#include "hum/core/scale.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hum::core {

inline constexpr std::uint32_t kCurrentProjectSchemaVersion = 6;

struct ProjectDocument {
    std::uint32_t schema_version{kCurrentProjectSchemaVersion};
    double tempo_bpm{kDefaultTempoBpm};
    std::uint16_t ticks_per_quarter{kDefaultTicksPerQuarter};
    std::uint8_t root_pitch_class{0};
    ScaleMode scale_mode{ScaleMode::major};
    bool magnet_enabled{false};
    bool metronome_enabled{false};
    unsigned grid_division{16};
    RhythmSnap rhythm_snap{};
    std::string lyrics;
    std::vector<VoiceClip> voice_clips;
    std::vector<TimedMidiNote> canonical_notes;
    std::size_t track_count{1};
    std::vector<TrackDefinition> tracks;
    std::vector<PlaylistRegion> playlist_regions;

    [[nodiscard]] bool isValid() const noexcept;
};

enum class ProjectDecodeStatus {
    ok,
    corrupt,
    unsupported_version,
};

struct ProjectDecodeResult {
    ProjectDecodeStatus status{ProjectDecodeStatus::corrupt};
    std::optional<ProjectDocument> project;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == ProjectDecodeStatus::ok && project.has_value();
    }
};

class ProjectFileCodec final {
public:
    [[nodiscard]] static std::optional<std::string> encode(
        const ProjectDocument& project);
    [[nodiscard]] static ProjectDecodeResult decode(std::string_view bytes);
};

} // namespace hum::core
