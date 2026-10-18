#include "hum/vocal/neural_pitch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <numbers>
#include <stdexcept>

#ifdef HUM_VOCAL_WITH_ONNX
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace hum::vocal {
std::string defaultRuntimePath() {
#ifdef HUM_VOCAL_RUNTIME_PATH
    return HUM_VOCAL_RUNTIME_PATH;
#else
    return {};
#endif
}
namespace {
std::vector<float> resample(const AudioBuffer& audio, CancelCheck cancelled) {
    if (audio.sample_rate == 16'000) { return audio.samples; }
    const auto count = static_cast<std::size_t>(std::llround(audio.samples.size() * 16'000.0 / audio.sample_rate));
    std::vector<float> result(count);
    const auto ratio = static_cast<double>(audio.sample_rate) / 16'000;
    const auto cutoff = std::min(1.0, 1 / ratio) * .94;
    const auto radius = static_cast<int>(std::ceil(24 / cutoff));
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 1024 == 0 && cancelled && cancelled()) { return {}; }
        const auto position = static_cast<double>(i) * ratio;
        const auto center = static_cast<std::int64_t>(std::floor(position));
        double sum = 0, weights = 0;
        for (int tap = -radius; tap <= radius; ++tap) {
            const auto index = center + tap;
            if (index < 0 || index >= static_cast<std::int64_t>(audio.samples.size())) { continue; }
            const auto distance = position - index;
            const auto angle = std::numbers::pi * distance * cutoff;
            const auto sinc = std::abs(angle) < 1e-10 ? 1 : std::sin(angle) / angle;
            const auto weight = sinc * .5 * (1 + std::cos(std::numbers::pi * distance / (radius + 1)));
            sum += audio.samples[static_cast<std::size_t>(index)] * weight; weights += weight;
        }
        result[i] = static_cast<float>(weights ? sum / weights : 0);
    }
    return result;
}
#ifdef HUM_VOCAL_WITH_ONNX
void initializeRuntime(const std::string& path) {
    static std::once_flag initialized;
    std::call_once(initialized, [&] {
        if (path.empty()) { throw std::runtime_error("The local neural runtime is missing. Rebuild with the prepared vocal dependencies."); }
#ifdef _WIN32
        const auto library = LoadLibraryA(path.c_str());
        const auto get_api = library ? reinterpret_cast<const OrtApiBase* (ORT_API_CALL*)()>(GetProcAddress(library, "OrtGetApiBase")) : nullptr;
#else
        const auto library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        const auto get_api = library ? reinterpret_cast<const OrtApiBase* (ORT_API_CALL*)()>(dlsym(library, "OrtGetApiBase")) : nullptr;
#endif
        if (!get_api) { throw std::runtime_error("The local neural runtime could not load. On Intel Mac, Sing to MIDI requires macOS 13.3 or newer."); }
        const auto* api = get_api()->GetApi(ORT_API_VERSION);
        if (!api) { throw std::runtime_error("The local neural runtime version does not match this build."); }
        Ort::InitApi(api); // library remains loaded while Ort objects can exist
    });
}
#endif
}
ConversionResult infer(const AudioBuffer& audio, std::span<const std::uint8_t> model,
    const std::string& runtime_path, const Config& config, CancelCheck cancelled, Progress progress) {
    ConversionResult result; result.config = config;
    try {
        if (!config.valid() || audio.sample_rate < 8'000 || audio.sample_rate > 192'000
            || audio.samples.empty() || audio.duration() > 60
            || std::any_of(audio.samples.begin(), audio.samples.end(), [](float x) { return !std::isfinite(x); })) {
            throw std::invalid_argument("The take has no valid audio to convert.");
        }
        if (cancelled && cancelled()) { result.status = Status::cancelled; return result; }
        auto samples = resample(audio, cancelled);
        if (cancelled && cancelled()) { result.status = Status::cancelled; return result; }
        if (model.empty()) { throw std::runtime_error("The bundled vocal model is missing. Reinstall the complete app."); }
        if (progress) { progress(.1); }
#ifdef HUM_VOCAL_WITH_ONNX
        initializeRuntime(runtime_path);
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "HumToSing-vocal");
        Ort::SessionOptions options; options.SetIntraOpNumThreads(2); options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
        Ort::Session session(env, model.data(), model.size(), options);
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        constexpr std::size_t hop = 256, chunk_frames = 125, left_context = 11, right_context = 10;
        // Fixed context preserves the model's receptive field across cancellable
        // two-second batches, including the first and last recording samples.
        const auto count = std::max<std::size_t>(1, samples.size() / hop);
        std::vector<PitchFrame> frames; frames.reserve(count);
        for (std::size_t start = 0; start < count; start += chunk_frames) {
            if (cancelled && cancelled()) { result.status = Status::cancelled; return result; }
            const auto end = std::min(count, start + chunk_frames);
            const auto left = start > left_context ? start - left_context : 0;
            const auto right_sample = end < count ? std::min(samples.size(), (end + right_context) * hop) : samples.size();
            const auto length = right_sample - left * hop;
            std::array<std::int64_t, 2> shape{1, static_cast<std::int64_t>(length)};
            float low = 55, high = 1'000;
            std::vector<Ort::Value> inputs;
            inputs.push_back(Ort::Value::CreateTensor<float>(memory, samples.data() + left * hop, length, shape.data(), shape.size()));
            inputs.push_back(Ort::Value::CreateTensor<float>(memory, &low, 1, nullptr, 0));
            inputs.push_back(Ort::Value::CreateTensor<float>(memory, &high, 1, nullptr, 0));
            const char* names[]{"audio", "fmin", "fmax"}; const char* outputs[]{"pitch", "confidence"};
            auto output = session.Run(Ort::RunOptions{nullptr}, names, inputs.data(), inputs.size(), outputs, 2);
            if (output[0].GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE
                || output[1].GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
                || output[0].GetTensorTypeAndShapeInfo().GetElementCount() < end - left
                || output[1].GetTensorTypeAndShapeInfo().GetElementCount() < end - left) {
                throw std::runtime_error("The vocal model returned an incompatible pitch contour.");
            }
            const auto* pitches = output[0].GetTensorData<double>();
            const auto* confidence = output[1].GetTensorData<float>();
            for (auto i = start; i < end; ++i) {
                double power = 0, peak = 0;
                const auto first = i ? (i - 1) * hop : 0;
                const auto last = std::min(samples.size(), (i + 1) * hop);
                for (auto sample = first; sample < last; ++sample) {
                    power += static_cast<double>(samples[sample]) * samples[sample];
                    if (sample >= i * hop) { peak = std::max(peak, static_cast<double>(std::abs(samples[sample]))); }
                }
                const auto hz = pitches[i - left];
                const auto valid = std::isfinite(hz) && hz >= low && hz <= high;
                const auto certainty = std::isfinite(confidence[i - left]) ? std::clamp<double>(confidence[i - left], 0, 1) : 0;
                frames.push_back({i * .016, valid ? 69 + 12 * std::log2(hz / 440) : 0,
                    peak >= .001 ? certainty : 0, std::sqrt(power / std::max<std::size_t>(1, last - first)), valid});
            }
            if (progress) { progress(.1 + .8 * static_cast<double>(end) / count); }
        }
        double last_onset = -.1;
        for (std::size_t i = 4; i < frames.size(); ++i) {
            const auto valley = frames[i - 2].rms;
            const auto prior_peak = std::max(frames[i - 4].rms, frames[i - 3].rms);
            if (frames[i].time - last_onset >= .08 && frames[i].rms > std::max(.003, valley * 2.2)
                && prior_peak > valley * 2.5) {
                frames[i].onset = true; last_onset = frames[i].time;
            }
        }
        result = transcribe(frames, audio.duration(), config, cancelled);
        if (std::any_of(audio.samples.begin(), audio.samples.end(), [](float x) { return std::abs(x) >= .98F; })) {
            result.warnings.push_back("The take clipped; a quieter recording may transcribe more accurately.");
        }
        if (progress) { progress(1); }
#else
        (void)runtime_path;
        throw std::runtime_error("This build does not include the local neural runtime. The existing Hum to MIDI engine remains available.");
#endif
    } catch (const std::exception& error) {
        result.status = Status::failed; result.error = error.what();
    }
    return result;
}
}
