#include "MacAudioInput.hpp"
#include <CoreAudio/CoreAudio.h>
#include <cmath>
#include <limits>
#include <utility>

namespace hum::app {
namespace {

std::string deviceString(AudioDeviceID device, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr || !value) {
        return {};
    }
    std::string result(static_cast<std::size_t>(CFStringGetMaximumSizeForEncoding(
        CFStringGetLength(value), kCFStringEncodingUTF8)) + 1, '\0');
    if (!CFStringGetCString(value, result.data(), static_cast<CFIndex>(result.size()), kCFStringEncodingUTF8)) {
        result.clear();
    } else {
        result.resize(std::char_traits<char>::length(result.c_str()));
    }
    CFRelease(value);
    return result;
}

std::vector<AudioDeviceID> deviceIds() {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) {
        return {};
    }
    std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
    if (size == 0 || AudioObjectGetPropertyData(kAudioObjectSystemObject, &address,
            0, nullptr, &size, devices.data()) != noErr) {
        return {};
    }
    devices.resize(size / sizeof(AudioDeviceID));
    return devices;
}

std::optional<core::AudioFormat> deviceFormat(AudioDeviceID device) {
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr
        || size < sizeof(AudioBufferList)) {
        return std::nullopt;
    }
    std::vector<std::uint64_t> storage((size + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto* buffers = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, buffers) != noErr) {
        return std::nullopt;
    }
    std::uint32_t channels = 0;
    for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index) {
        channels += buffers->mBuffers[index].mNumberChannels;
    }
    if (channels == 0 || channels > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }
    Float64 rate = 0;
    size = sizeof(rate);
    address = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &rate) != noErr
        || !std::isfinite(rate) || rate < 8000 || rate > 384000) {
        return std::nullopt;
    }
    return core::AudioFormat{static_cast<std::uint32_t>(std::lround(rate)),
        static_cast<std::uint16_t>(channels)};
}

}

MacAudioInput::~MacAudioInput() { stop(); }

std::vector<core::AudioInputDevice> MacAudioInput::devices() const {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioDeviceID default_device = kAudioObjectUnknown;
    UInt32 size = sizeof(default_device);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &default_device);
    std::vector<core::AudioInputDevice> result;
    for (const auto device : deviceIds()) {
        if (!deviceFormat(device)) { continue; }
        const auto id = deviceString(device, kAudioDevicePropertyDeviceUID);
        if (id.empty()) { continue; }
        result.push_back({id, deviceString(device, kAudioObjectPropertyName), device == default_device});
    }
    return result;
}

std::optional<core::AudioFormat> MacAudioInput::preferredFormat(const std::string& id) const {
    for (const auto device : deviceIds()) {
        if (deviceString(device, kAudioDevicePropertyDeviceUID) == id) { return deviceFormat(device); }
    }
    return std::nullopt;
}

core::ServiceStatus MacAudioInput::start(const std::string& id, core::AudioFormat format,
    core::AudioBlockCallback callback, core::CaptureFailureCallback failure) {
    stop();
    if (!preferredFormat(id)) { return core::ServiceStatus::unavailable; }
    if (format.sample_rate_hz < 8000 || format.sample_rate_hz > 384000
        || format.channel_count == 0 || format.channel_count > 32) {
        return core::ServiceStatus::invalid_input;
    }
    format_ = format;
    callback_ = std::move(callback);
    failure_ = std::move(failure);
    AudioStreamBasicDescription description{};
    description.mSampleRate = format.sample_rate_hz;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    description.mBytesPerFrame = sizeof(float) * format.channel_count;
    description.mBytesPerPacket = description.mBytesPerFrame;
    description.mFramesPerPacket = 1;
    description.mChannelsPerFrame = format.channel_count;
    description.mBitsPerChannel = 32;
    if (AudioQueueNewInput(&description, receive, this, nullptr, nullptr, 0, &queue_) != noErr) {
        stop();
        return core::ServiceStatus::io_error;
    }
    auto uid = CFStringCreateWithCString(nullptr, id.c_str(), kCFStringEncodingUTF8);
    const auto selected = AudioQueueSetProperty(queue_, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
    CFRelease(uid);
    if (selected != noErr) { stop(); return core::ServiceStatus::unavailable; }
    for (int index = 0; index < 3; ++index) {
        AudioQueueBufferRef buffer = nullptr;
        if (AudioQueueAllocateBuffer(queue_, 1024 * description.mBytesPerFrame, &buffer) != noErr
            || AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr) != noErr) {
            stop();
            return core::ServiceStatus::io_error;
        }
    }
    running_.store(true);
    if (AudioQueueStart(queue_, nullptr) != noErr) { stop(); return core::ServiceStatus::io_error; }
    return core::ServiceStatus::ok;
}

void MacAudioInput::stop() noexcept {
    running_.store(false);
    if (queue_) {
        AudioQueueStop(queue_, true);
        AudioQueueDispose(queue_, true);
        queue_ = nullptr;
    }
    callback_ = {};
    failure_ = {};
}

bool MacAudioInput::isCapturing() const noexcept { return running_.load(); }

void MacAudioInput::receive(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer,
    const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*) {
    auto& input = *static_cast<MacAudioInput*>(context);
    if (!input.running_.load()) { return; }
    if (input.callback_) {
        input.callback_({{static_cast<const float*>(buffer->mAudioData), buffer->mAudioDataByteSize / sizeof(float)},
            input.format_.sample_rate_hz, input.format_.channel_count});
    }
    if (input.running_.load() && AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr) != noErr && input.failure_) {
        input.failure_(core::ServiceStatus::io_error);
    }
}

}
