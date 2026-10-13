#pragma once

#include "hum/core/audio.hpp"
#include "hum/core/midi.hpp"

#include <filesystem>

namespace hum::core {

class IPlaybackService {
public:
    virtual ~IPlaybackService() = default;
    virtual ServiceStatus play(const MidiSequence& sequence) = 0;
    virtual void stop() noexcept = 0;
};

class IMidiExporter {
public:
    virtual ~IMidiExporter() = default;
    virtual ServiceStatus exportFile(
        const MidiSequence& sequence,
        const std::filesystem::path& destination) = 0;
};

} // namespace hum::core
