#include "hum/core/recording.hpp"
#include "support/FakeAudioCapture.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void expect(const bool condition, const std::string_view message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expectNear(const double actual, const double expected, const double tolerance,
    const std::string_view message) {
    expect(std::abs(actual - expected) <= tolerance, message);
}

void testStateTransitionsAndRepeatedControls() {
    hum::test::FakeAudioCapture source;
    hum::core::RecordingController recorder(source);

    expect(recorder.state() == hum::core::RecordingState::idle, "recorder begins idle");
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "first Record starts capture");
    expect(recorder.state() == hum::core::RecordingState::recording, "state becomes recording");
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::busy,
        "duplicate Record is rejected");
    expect(source.start_count == 1, "duplicate Record does not open a second source");
    expect(recorder.stop(), "first Stop completes capture");
    expect(recorder.state() == hum::core::RecordingState::ready, "state becomes ready");
    expect(!recorder.stop(), "duplicate Stop is harmless");
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "Record works again after Stop");
    expect(recorder.stop(), "second recording stops normally");
    expect(source.start_count == 2 && source.stop_count == 2,
        "repeated sessions open and close exactly once each");
}

void testDurationAndMetadata() {
    hum::test::FakeAudioCapture source;
    source.device_format = {48'000, 1};
    hum::core::RecordingController recorder(source);
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "48 kHz source starts");
    source.emit(std::vector<float>(4'800, 0.25F));
    expect(recorder.stop(), "metadata recording stops");

    const auto recording = recorder.recording();
    expect(recording != nullptr, "completed recording is retained");
    expect(recording && recording->format().sample_rate_hz == 48'000,
        "actual sample rate is retained");
    expect(recording && recording->format().channel_count == 1,
        "actual channel count is retained");
    expect(recording && recording->sampleCount() == 4'800, "sample count is exact");
    expect(recording && recording->frameCount() == 4'800, "frame count is exact");
    expectNear(recording ? recording->durationSeconds() : 0.0, 0.1, 1.0e-9,
        "duration is derived from frames and sample rate");
}

void testStereoToMono() {
    hum::core::AudioRecording recording({44'100, 2}, {1.0F, -1.0F, 0.5F, 0.5F});
    const auto mono = recording.monoSamples();
    expect(mono.size() == 2, "stereo recording produces one mono sample per frame");
    expectNear(mono[0], 0.0, 1.0e-7, "opposite stereo samples average to silence");
    expectNear(mono[1], 0.5, 1.0e-7, "matching stereo samples preserve amplitude");
}

void testLivePublishedFrameCopy() {
    hum::test::FakeAudioCapture source;
    source.device_format = {48'000, 2};
    hum::core::RecordingController recorder(source);
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "stereo source starts for live analysis copy");
    source.emit({1.0F, -1.0F, 0.5F, 0.25F, -0.5F, -0.25F});
    const auto middle = recorder.copyMonoFrames(1, 2);
    expect(middle && middle->size() == 2,
        "published live frames can be copied outside the callback");
    expectNear(middle ? (*middle)[0] : 0.0, 0.375, 1.0e-7,
        "live analysis copy averages the requested stereo frame");
    expectNear(middle ? (*middle)[1] : 0.0, -0.375, 1.0e-7,
        "live analysis copy preserves frame order");
    expect(!recorder.copyMonoFrames(2, 2),
        "live analysis copy never reads beyond published frames");
    expect(recorder.stop(), "stereo source stops after live analysis copy");
    expect(recorder.copyMonoFrames(0, 3).has_value(),
        "the same frame-copy path remains available after Stop");
}

void testSupportedSampleRates() {
    for (const std::uint32_t rate : {44'100U, 48'000U, 96'000U}) {
        hum::test::FakeAudioCapture source;
        source.device_format = {rate, 1};
        hum::core::RecordingController recorder(source);
        expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
            "source-reported sample rate starts");
        source.emit(std::vector<float>(rate / 100U, 0.1F));
        expect(recorder.stop(), "sample-rate recording stops");
        expectNear(recorder.recording()->durationSeconds(), 0.01, 1.0e-9,
            "duration is correct at source-reported sample rate");
    }
}

void testBoundedBufferAndClipping() {
    hum::test::FakeAudioCapture source;
    source.device_format = {1'000, 1};
    hum::core::RecordingController recorder(source, 0.01, 0.98F);
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "limited recorder starts");
    source.emit(std::vector<float>(20, 1.0F));
    const auto event = recorder.service();
    expect(event == hum::core::RecordingStopReason::duration_limit,
        "buffer limit is serviced outside the audio callback");
    expect(recorder.recording()->sampleCount() == 10, "buffer never exceeds its configured bound");
    const auto metrics = recorder.pollMetrics();
    expect(metrics.clipped, "clipping is latched for user feedback");
    expect(metrics.limit_reached, "limit remains visible after automatic stop");
}

void testSilenceAndVeryShortCapture() {
    hum::test::FakeAudioCapture source;
    source.device_format = {48'000, 1};
    hum::core::RecordingController recorder(source);
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "silent recording starts");
    source.emit(std::vector<float>(240, 0.0F));
    expect(recorder.stop(), "very short silent recording stops safely");
    expect(recorder.recording()->isSilent(), "silence is identified without inventing notes");
    expectNear(recorder.recording()->durationSeconds(), 0.005, 1.0e-9,
        "very short recording metadata remains accurate");
}

void testDeviceFailureRecovery() {
    hum::test::FakeAudioCapture source;
    hum::core::RecordingController recorder(source);
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "recording starts before disconnect");
    source.emit(std::vector<float>(480, 0.2F));
    source.fail(hum::core::ServiceStatus::device_disconnected);
    const auto event = recorder.service();
    expect(event == hum::core::RecordingStopReason::device_failure,
        "device failure is handled by the control thread");
    expect(recorder.state() == hum::core::RecordingState::error, "failure enters error state");
    expect(recorder.lastError() == hum::core::ServiceStatus::device_disconnected,
        "disconnect reason is retained");
    expect(recorder.recording() && recorder.recording()->sampleCount() == 480,
        "safe partial audio is retained after disconnect");
    expect(recorder.acknowledgeError(), "error can be acknowledged");
    expect(recorder.state() == hum::core::RecordingState::ready,
        "recorder recovers to ready when partial audio exists");
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
        "a new session can start after recovery");
    expect(recorder.stop(), "recovered session stops");
}

void testPermissionAndMissingDevice() {
    hum::test::FakeAudioCapture source;
    source.permission = hum::core::PermissionStatus::denied;
    hum::core::RecordingController recorder(source);
    expect(recorder.start("fake-1") == hum::core::StartRecordingResult::permission_denied,
        "denied permission blocks capture");
    expect(source.start_count == 0, "denied permission never opens hardware");
    expect(recorder.acknowledgeError(), "permission error can be acknowledged");
    source.permission = hum::core::PermissionStatus::granted;
    expect(recorder.start("missing") == hum::core::StartRecordingResult::device_unavailable,
        "missing device produces an actionable start result");
}

void testPlaybackConversionPreservesTiming() {
    std::vector<float> samples(44'100);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = static_cast<float>(index) / static_cast<float>(samples.size());
    }
    hum::core::AudioRecording source({44'100, 1}, std::move(samples));
    const auto converted = source.convertedForPlayback(48'000, 2);
    expect(converted.has_value(), "explicit playback conversion succeeds");
    expect(converted && converted->format().sample_rate_hz == 48'000,
        "converted playback declares its target sample rate");
    expect(converted && converted->format().channel_count == 2,
        "mono playback is duplicated to the target channels");
    expect(converted && converted->frameCount() == 48'000,
        "one second remains one second after resampling");
    expectNear(converted ? converted->durationSeconds() : 0.0, 1.0, 1.0e-9,
        "resampling preserves duration");

    const hum::core::AudioRecording tiny({96'000, 1}, {0.25F});
    const auto tiny_converted = tiny.convertedForPlayback(8'000, 1);
    expect(tiny_converted && tiny_converted->frameCount() == 1,
        "a non-empty very short recording remains non-empty after playback conversion");
    expect(tiny_converted && tiny_converted->interleavedSamples().front() == 0.25F,
        "very short playback conversion preserves its source sample");
}

void testShutdownCleanup() {
    hum::test::FakeAudioCapture source;
    {
        hum::core::RecordingController recorder(source);
        expect(recorder.start("fake-1") == hum::core::StartRecordingResult::started,
            "capture starts before shutdown");
        source.emit(std::vector<float>(1'000, 0.2F));
        recorder.shutdown();
        expect(recorder.state() == hum::core::RecordingState::closed, "shutdown closes recorder");
        expect(!source.isCapturing(), "shutdown closes the audio source");
        expect(recorder.recording() == nullptr, "shutdown releases audio memory");
    }
    expect(source.stop_count == 1, "app closure stops hardware exactly once");
}

} // namespace

int main() {
    testStateTransitionsAndRepeatedControls();
    testDurationAndMetadata();
    testStereoToMono();
    testLivePublishedFrameCopy();
    testSupportedSampleRates();
    testBoundedBufferAndClipping();
    testSilenceAndVeryShortCapture();
    testDeviceFailureRecovery();
    testPermissionAndMissingDevice();
    testPlaybackConversionPreservesTiming();
    testShutdownCleanup();

    if (failures == 0) {
        std::cout << "All recording tests passed (" << checks << " assertions).\n";
        return 0;
    }
    std::cerr << failures << " recording assertion(s) failed.\n";
    return 1;
}
