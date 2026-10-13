#pragma once

#include "hum/core/services.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace hum::core {

class StandardMidiFileExporter final : public IMidiExporter {
public:
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> serialize(
        const MidiSequence& sequence) noexcept;

    ServiceStatus exportFile(
        const MidiSequence& sequence,
        const std::filesystem::path& destination) override;
};

} // namespace hum::core
