#include "hum/vocal/transcription.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace hum::vocal {
namespace {
double median(std::vector<double> values) {
    if (values.empty()) { return 0; }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end()); return *middle;
}
bool validNote(const NoteEvent& note) {
    return note.pitch >= 0 && note.pitch <= 127 && note.velocity >= 1 && note.velocity <= 127
        && std::isfinite(note.start) && note.start >= 0 && std::isfinite(note.duration)
        && note.duration > 0 && note.start + note.duration <= 600;
}
}
bool Config::valid() const noexcept {
    return std::isfinite(sensitivity) && sensitivity >= 0 && sensitivity <= 1
        && std::isfinite(smoothing_ms) && smoothing_ms >= 20 && smoothing_ms <= 150
        && std::isfinite(gap_ms) && gap_ms >= 0 && gap_ms <= 120
        && std::isfinite(minimum_note_ms) && minimum_note_ms >= 50 && minimum_note_ms <= 150
        && std::isfinite(hysteresis_ms) && hysteresis_ms >= 30 && hysteresis_ms <= 120;
}
Config Config::forPreset(Preset preset) noexcept {
    switch (preset) {
    case Preset::clean: return {.5, 55, 65, 70, 60};
    case Preset::tight: return {.5, 85, 95, 85, 85};
    default: return {};
    }
}

ConversionResult transcribe(std::span<const PitchFrame> frames, double audio_duration,
    const Config& config, CancelCheck cancelled) {
    ConversionResult result; result.config = config;
    const auto fail = [&](const char* error) { result.status = Status::failed; result.error = error; return result; };
    if (!config.valid() || !std::isfinite(audio_duration) || audio_duration <= 0 || audio_duration > 60
        || frames.size() > 60'001) { return fail("Invalid vocal analysis configuration or duration."); }
    if (frames.empty()) { return result; }
    double previous = -1;
    for (const auto& frame : frames) {
        if (!std::isfinite(frame.time) || frame.time < 0 || frame.time <= previous || frame.time > audio_duration
            || !std::isfinite(frame.confidence) || frame.confidence < 0 || frame.confidence > 1
            || !std::isfinite(frame.rms) || frame.rms < 0
            || (frame.voiced && (!std::isfinite(frame.midi_pitch) || frame.midi_pitch < 0 || frame.midi_pitch > 127))) {
            return fail("Invalid or out-of-order vocal pitch frames.");
        }
        previous = frame.time;
    }
    result.frames.assign(frames.begin(), frames.end()); // evidence remains untouched
    std::vector<double> pitches(frames.size(), std::numeric_limits<double>::quiet_NaN());
    const auto threshold = .75 - .5 * config.sensitivity;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        // A held, agreeing pitch gets a lower release threshold. Confidence
        // hovering just below the onset threshold must not chop a sustain.
        const auto continuation = i > 0 && std::isfinite(pitches[i - 1])
            && std::abs(frames[i].midi_pitch - pitches[i - 1]) < .7
            && frames[i].confidence >= threshold * .75;
        if (frames[i].voiced && (frames[i].confidence >= threshold || continuation) && frames[i].rms >= .001) {
            pitches[i] = frames[i].midi_pitch;
        }
    }
    // Repair only short, audible uncertainty with agreeing anchors and no new
    // articulation. Digital silence and credible re-onsets are never filled.
    for (std::size_t i = 1; i + 1 < frames.size(); ++i) {
        if (std::isfinite(pitches[i]) || !std::isfinite(pitches[i - 1])) { continue; }
        auto end = i;
        while (end < frames.size() && !std::isfinite(pitches[end])) { ++end; }
        if (end == frames.size()) { break; }
        bool audible = !frames[end].onset;
        const auto gate = std::max(.001, std::min(frames[i - 1].rms, frames[end].rms) * .15);
        for (auto j = i; j < end; ++j) { audible = audible && frames[j].rms >= gate && !frames[j].onset; }
        if (audible && frames[end].time - frames[i].time <= config.gap_ms / 1000 + 1e-9
            && std::abs(pitches[end] - pitches[i - 1]) < .55) {
            for (auto j = i; j < end; ++j) { pitches[j] = (pitches[i - 1] + pitches[end]) / 2; }
        }
        i = end - 1;
    }
    auto smooth = pitches;
    const auto radius = config.smoothing_ms / 2000;
    std::size_t left = 0, right = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (cancelled && cancelled()) { result.status = Status::cancelled; return result; }
        while (frames[i].time - frames[left].time > radius) { ++left; }
        while (right + 1 < frames.size() && frames[right + 1].time - frames[i].time <= radius) { ++right; }
        if (!std::isfinite(pitches[i])) { continue; }
        std::vector<double> neighborhood;
        for (auto j = left; j <= right; ++j) {
            if (std::isfinite(pitches[j])) { neighborhood.push_back(pitches[j]); }
        }
        smooth[i] = median(std::move(neighborhood));
    }
    const auto step = frames.size() > 1 ? (frames.back().time - frames.front().time) / (frames.size() - 1) : .016;
    bool active = false;
    std::size_t first = 0, last = 0, pending = frames.size();
    double anchor = 0;
    std::vector<double> stable;
    const auto finish = [&](double end) {
        const auto start = std::max(0.0, frames[first].time - step / 2);
        end = std::clamp(end, start, audio_duration);
        if (end - start + 1e-9 < config.minimum_note_ms / 1000 || stable.empty()) { return; }
        const auto pitch = std::clamp(static_cast<int>(std::lround(median(stable))), 0, 127);
        result.notes.push_back({pitch, start, end - start, 96});
        result.tokens.push_back({TokenType::note_start, start, static_cast<double>(pitch)});
        for (auto j = first; j <= last; ++j) {
            if (frames[j].time >= end) { break; }
            const auto cents = std::isfinite(pitches[j]) ? 100 * (pitches[j] - pitch) : 0;
            result.tokens.push_back({std::abs(cents) >= 30 ? TokenType::pitch_slide : TokenType::note_hold,
                std::max(start, frames[j].time), static_cast<double>(pitch), cents});
        }
        result.tokens.push_back({TokenType::note_end, end, static_cast<double>(pitch)});
    };
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (cancelled && cancelled()) { result.notes.clear(); result.tokens.clear(); result.status = Status::cancelled; return result; }
        if (!std::isfinite(smooth[i])) {
            if (active) { finish(frames[last].time + step / 2); }
            active = false; pending = frames.size(); stable.clear(); continue;
        }
        if (!active) { active = true; first = last = i; anchor = std::round(smooth[i]); stable = {smooth[i]}; continue; }
        const auto repeated = frames[i].onset && frames[i].time - frames[first].time >= config.minimum_note_ms / 1000;
        if (repeated) {
            finish(frames[i].time - step / 2); first = last = i; anchor = std::round(smooth[i]);
            stable = {smooth[i]}; pending = frames.size(); continue;
        }
        if (std::abs(smooth[i] - anchor) >= .7) {
            if (pending == frames.size() || std::abs(smooth[i] - smooth[pending]) > .65) { pending = i; }
            if (frames[i].time - frames[pending].time + step >= config.hysteresis_ms / 1000) {
                finish(frames[pending].time - step / 2);
                first = pending; stable.clear();
                for (auto j = first; j <= i; ++j) { stable.push_back(smooth[j]); }
                anchor = std::round(median(stable)); pending = frames.size();
            }
        } else {
            pending = frames.size(); stable.push_back(smooth[i]);
            // Slow intonation drift should not make the anchor chase vibrato.
        }
        last = i;
    }
    if (active) { finish(std::min(audio_duration, frames[last].time + step / 2)); }
    // REST is an explicit event with a duration, ordered with all note tokens.
    double end = 0;
    for (const auto& note : result.notes) {
        if (note.start > end + .001) { result.tokens.push_back({TokenType::rest, end, 0, 0, note.start - end}); }
        end = note.start + note.duration;
    }
    if (end < audio_duration) { result.tokens.push_back({TokenType::rest, end, 0, 0, audio_duration - end}); }
    std::stable_sort(result.tokens.begin(), result.tokens.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    result.notes = reconstruct(result.tokens);
    result.status = result.notes.empty() ? Status::empty : Status::success;
    return result;
}

std::vector<NoteEvent> reconstruct(std::span<const MusicalToken> tokens) {
    std::vector<NoteEvent> notes;
    bool active = false; double start = 0, previous = -1; int pitch = 60;
    for (const auto& token : tokens) {
        if (!std::isfinite(token.time) || token.time < 0 || token.time < previous) {
            throw std::invalid_argument("Invalid musical event timing.");
        }
        previous = token.time;
        if (token.type == TokenType::note_start) {
            if (active || !std::isfinite(token.pitch) || token.pitch < 0 || token.pitch > 127) {
                throw std::invalid_argument("Invalid monophonic note start.");
            }
            active = true; start = token.time; pitch = static_cast<int>(std::lround(token.pitch));
        } else if (token.type == TokenType::note_end) {
            if (!active || token.time <= start) { throw std::invalid_argument("Invalid monophonic note end."); }
            notes.push_back({pitch, start, token.time - start, 96}); active = false;
        } else if (token.type == TokenType::rest && active) {
            throw std::invalid_argument("Rest inside an active note.");
        }
        // HOLD and SLIDE preserve the note. The host has no bend automation,
        // so slide evidence stays in tokens while output uses one fixed pitch.
    }
    if (active) { throw std::invalid_argument("Unfinished monophonic note."); }
    return notes;
}

std::vector<NoteEvent> correct(std::span<const NoteEvent> original, const Correction& correction) {
    if (!std::isfinite(correction.bpm) || correction.bpm < 20 || correction.bpm > 400
        || !std::isfinite(correction.origin) || correction.origin < 0 || correction.origin > 600
        || (correction.quantize_division != 0 && correction.quantize_division != 4
            && correction.quantize_division != 8 && correction.quantize_division != 16)
        || (correction.scale_snap && correction.scale_pitch_classes.empty())
        || std::any_of(correction.scale_pitch_classes.begin(), correction.scale_pitch_classes.end(), [](int p) { return p < 0 || p > 11; })) {
        throw std::invalid_argument("Invalid vocal preview correction.");
    }
    std::vector<NoteEvent> notes(original.begin(), original.end());
    double previous_start = -1;
    for (auto& note : notes) {
        if (!validNote(note) || note.start < previous_start) { throw std::invalid_argument("Invalid vocal preview note."); }
        previous_start = note.start;
        if (correction.scale_snap) {
            int distance = 128, nearest = note.pitch;
            for (int pitch = 0; pitch <= 127; ++pitch) {
                if (std::find(correction.scale_pitch_classes.begin(), correction.scale_pitch_classes.end(), pitch % 12) == correction.scale_pitch_classes.end()) { continue; }
                if (std::abs(pitch - note.pitch) < distance) { distance = std::abs(pitch - note.pitch); nearest = pitch; }
            }
            note.pitch = nearest;
        }
        if (correction.quantize_division != 0) {
            const auto interval = 240.0 / (correction.bpm * correction.quantize_division);
            const auto start = std::max(correction.origin, std::round((correction.origin + note.start) / interval) * interval);
            const auto end = std::round((correction.origin + note.start + note.duration) / interval) * interval;
            note.start = start - correction.origin; note.duration = std::max(.001, end - start);
        }
    }
    if (correction.quantize_division) {
        for (std::size_t i = 1; i < notes.size(); ++i) { notes[i].start = std::max(notes[i].start, notes[i - 1].start + .001); }
        for (std::size_t i = 0; i + 1 < notes.size(); ++i) { notes[i].duration = std::min(notes[i].duration, notes[i + 1].start - notes[i].start); }
    }
    return notes;
}
}
