#include "vocal/IsolatedMicRecorder.hpp"
#include "support/FakeAudioCapture.hpp"
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    int failures = 0;
    const auto expect = [&](bool value, const char* message) { if (!value) { ++failures; std::cerr << message << '\n'; } };
    hum::test::FakeAudioCapture source;
    source.device_format = {8'000, 2};
    hum::app::vocal::IsolatedMicRecorder recorder(source, .1);
    source.permission = hum::core::PermissionStatus::denied;
    expect(recorder.start("fake-1") == hum::core::ServiceStatus::permission_denied, "permission is required");
    source.permission = hum::core::PermissionStatus::granted;
    expect(recorder.start("missing") == hum::core::ServiceStatus::unavailable, "missing input rejected");
    expect(recorder.start("fake-1") == hum::core::ServiceStatus::ok, "starts its own session");
    expect(recorder.start("fake-1") == hum::core::ServiceStatus::busy, "cannot overlap recording");
    source.emit(std::vector<float>(2'000, .5F)); recorder.poll();
    expect(!recorder.recording() && recorder.limitReached(), "bounded capture stops at duration limit");
    expect(recorder.audio() && recorder.audio()->samples.size() == 800, "mono samples have correct frame count");
    expect(std::abs(recorder.elapsed() - .1) < 1e-9, "sample-based recording time");
    expect(recorder.audio()->samples[0] == .5F, "stereo is mixed correctly");
    recorder.discard(); expect(!recorder.audio(), "discard releases samples");
    expect(recorder.start("fake-1") == hum::core::ServiceStatus::ok, "new take after discard");
    source.emit({1.0F, 1.0F}); expect(recorder.clipped(), "clipping detected");
    source.fail(hum::core::ServiceStatus::device_disconnected); recorder.poll();
    expect(!recorder.recording() && !recorder.audio(), "disconnection invalidates incomplete take");
    expect(recorder.error() == hum::core::ServiceStatus::device_disconnected, "disconnect error retained");
    expect(recorder.start("fake-1") == hum::core::ServiceStatus::ok, "can retry after disconnection");
    source.emit({std::numeric_limits<float>::quiet_NaN(), 0}); recorder.poll();
    expect(!recorder.audio() && !recorder.recording(), "non-finite samples rejected safely");
    recorder.discard();
    expect(recorder.start("fake-1") == hum::core::ServiceStatus::ok, "empty take starts");
    recorder.stop(); expect(!recorder.audio(), "empty take produces no audio result");
    std::cout << "Vocal capture failures: " << failures << '\n'; return failures ? 1 : 0;
}
