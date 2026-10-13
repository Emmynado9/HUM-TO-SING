#include "hum/core/wave_fixture.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace hum::core {

namespace {

constexpr std::uint64_t kMaximumFixtureBytes = 512ULL * 1'024ULL * 1'024ULL;

[[nodiscard]] std::uint16_t readU16(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool hasTag(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset,
    const char (&tag)[5]) noexcept {
    return offset + 4 <= bytes.size()
        && std::memcmp(bytes.data() + offset, tag, 4) == 0;
}

} // namespace

WaveFixtureLoadResult loadWaveFixture(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return {std::nullopt, "Could not open the WAV fixture."};
    }
    const auto length = input.tellg();
    if (length < 12 || static_cast<std::uint64_t>(length) > kMaximumFixtureBytes) {
        return {std::nullopt, "The WAV fixture size is invalid or exceeds 512 MiB."};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || !hasTag(bytes, 0, "RIFF") || !hasTag(bytes, 8, "WAVE")) {
        return {std::nullopt, "The fixture is not a valid RIFF/WAVE file."};
    }

    std::uint16_t format_tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits_per_sample = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;

    std::size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const auto chunk_size = static_cast<std::size_t>(readU32(bytes, offset + 4));
        const auto chunk_data = offset + 8;
        if (chunk_size > bytes.size() - chunk_data) {
            return {std::nullopt, "The WAV fixture contains a truncated chunk."};
        }
        if (hasTag(bytes, offset, "fmt ") && chunk_size >= 16) {
            format_tag = readU16(bytes, chunk_data);
            channels = readU16(bytes, chunk_data + 2);
            sample_rate = readU32(bytes, chunk_data + 4);
            block_align = readU16(bytes, chunk_data + 12);
            bits_per_sample = readU16(bytes, chunk_data + 14);
        } else if (hasTag(bytes, offset, "data") && data_size == 0) {
            data_offset = chunk_data;
            data_size = chunk_size;
        }
        const auto padded_size = chunk_size + (chunk_size % 2U);
        if (padded_size > bytes.size() - chunk_data) {
            break;
        }
        offset = chunk_data + padded_size;
    }

    const AudioFormat format{sample_rate, channels};
    const auto bytes_per_sample = static_cast<std::uint16_t>(bits_per_sample / 8U);
    if (!format.isValid() || data_size == 0 || bits_per_sample % 8U != 0
        || !((format_tag == 1 && bits_per_sample == 16)
            || (format_tag == 3 && bits_per_sample == 32))
        || block_align != channels * bytes_per_sample || data_size % block_align != 0) {
        return {std::nullopt,
            "Only well-formed PCM 16-bit or IEEE-float 32-bit WAV fixtures are supported."};
    }
    const auto sample_count = data_size / bytes_per_sample;
    std::vector<float> samples(sample_count);
    for (std::size_t index = 0; index < sample_count; ++index) {
        const auto sample_offset = data_offset + index * bytes_per_sample;
        if (format_tag == 1) {
            const auto value = std::bit_cast<std::int16_t>(readU16(bytes, sample_offset));
            samples[index] = static_cast<float>(value) / 32'768.0F;
        } else {
            const auto value = std::bit_cast<float>(readU32(bytes, sample_offset));
            if (!std::isfinite(value)) {
                return {std::nullopt, "The float WAV fixture contains a non-finite sample."};
            }
            samples[index] = std::clamp(value, -1.0F, 1.0F);
        }
    }
    return {AudioRecording{format, std::move(samples)}, {}};
}

} // namespace hum::core
