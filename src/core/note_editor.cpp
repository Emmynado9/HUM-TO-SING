#include "hum/core/note_editor.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <numeric>
#include <limits>
#include <utility>

namespace hum::core {

namespace {

[[nodiscard]] bool notesEqual(
    const TimedMidiNote& left,
    const TimedMidiNote& right) noexcept {
    return left.pitch == right.pitch
        && left.velocity == right.velocity
        && left.start_seconds == right.start_seconds
        && left.duration_seconds == right.duration_seconds
        && left.channel == right.channel;
}

[[nodiscard]] bool allValid(const std::vector<TimedMidiNote>& notes) noexcept {
    return std::all_of(notes.cbegin(), notes.cend(), [](const TimedMidiNote& note) {
        return note.isValid();
    });
}

} // namespace

bool RhythmSnap::isValid() const noexcept {
    return std::isfinite(interval_seconds) && interval_seconds > 0.0;
}

double RhythmSnap::apply(const double seconds) const noexcept {
    if (!std::isfinite(seconds)) {
        return 0.0;
    }
    if (!enabled || !isValid()) {
        return std::max(0.0, seconds);
    }
    return std::max(0.0, std::round(seconds / interval_seconds) * interval_seconds);
}

bool PianoRollTransform::isValid() const noexcept {
    return std::isfinite(pixels_per_second) && pixels_per_second > 0.0
        && std::isfinite(pixels_per_semitone) && pixels_per_semitone > 0.0;
}

double PianoRollTransform::timeToX(const double seconds) const noexcept {
    return isValid() && std::isfinite(seconds) ? seconds * pixels_per_second : 0.0;
}

double PianoRollTransform::xToTime(const double x) const noexcept {
    return isValid() && std::isfinite(x) ? x / pixels_per_second : 0.0;
}

double PianoRollTransform::pitchToY(const std::uint8_t pitch) const noexcept {
    if (!isValid() || pitch > 127) {
        return 0.0;
    }
    return static_cast<double>(127U - pitch) * pixels_per_semitone;
}

std::optional<std::uint8_t> PianoRollTransform::yToPitch(const double y) const noexcept {
    if (!isValid() || !std::isfinite(y) || y < 0.0 || y >= contentHeight()) {
        return std::nullopt;
    }
    const auto row = static_cast<int>(std::floor(y / pixels_per_semitone));
    if (row < 0 || row > 127) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(127 - row);
}

double PianoRollTransform::contentHeight() const noexcept {
    return isValid() ? 128.0 * pixels_per_semitone : 0.0;
}

bool validPlaylistRegions(const std::vector<PlaylistRegion>& regions,
    const std::size_t note_count, const std::size_t track_count) noexcept {
    if (track_count == 0 || track_count > 16 || regions.size() > 10'000) {
        return false;
    }
    try {
        std::vector<bool> assigned(note_count, false);
        for (const auto& region : regions) {
            if (region.track >= track_count || region.note_indices.empty()
                || region.name.empty() || region.name.size() > 80
                || std::any_of(region.name.begin(), region.name.end(),
                    [](unsigned char c) { return c < 32; })) {
                return false;
            }
            for (const auto index : region.note_indices) {
                if (index >= note_count || assigned[index]) {
                    return false;
                }
                assigned[index] = true;
            }
        }
        return std::all_of(assigned.begin(), assigned.end(), [](bool value) { return value; });
    } catch (...) {
        return false;
    }
}

bool NoteEditor::replaceAll(std::vector<TimedMidiNote> notes,
    std::vector<PlaylistRegion> regions, const std::size_t track_count) {
    if (!allValid(notes)) {
        return false;
    }
    if (regions.empty() && !notes.empty()) {
        PlaylistRegion region;
        region.name = "Melody 01";
        region.note_indices.resize(notes.size());
        std::iota(region.note_indices.begin(), region.note_indices.end(), 0);
        regions.push_back(std::move(region));
    }
    if (!validPlaylistRegions(regions, notes.size(), track_count)) {
        return false;
    }
    state_.notes = std::move(notes);
    state_.regions = std::move(regions);
    state_.track_count = track_count;
    state_.selected_index.reset();
    clearHistory();
    refreshProjection();
    return true;
}

const std::vector<PlaylistRegion>& NoteEditor::regions() const noexcept { return state_.regions; }
std::size_t NoteEditor::trackCount() const noexcept { return state_.track_count; }

NoteEditResult NoteEditor::addTrack() {
    if (state_.track_count >= 16) { return NoteEditResult::invalid_index; }
    auto next = state_;
    ++next.track_count;
    return commit(std::move(next), "Add track");
}

NoteEditResult NoteEditor::appendRegion(std::vector<TimedMidiNote> notes,
    const double offset_seconds, const std::size_t track) {
    if (notes.empty() || !std::isfinite(offset_seconds) || offset_seconds < 0
        || track >= state_.track_count || state_.notes.size() + notes.size() > 100'000) {
        return NoteEditResult::invalid_note;
    }
    for (auto& note : notes) { note.start_seconds += offset_seconds; }
    if (!allValid(notes)) { return NoteEditResult::invalid_note; }
    auto next = state_;
    PlaylistRegion region;
    region.track = track;
    region.name = "Melody " + std::to_string(next.regions.size() + 1);
    region.note_indices.resize(notes.size());
    std::iota(region.note_indices.begin(), region.note_indices.end(), next.notes.size());
    next.notes.insert(next.notes.end(), notes.begin(), notes.end());
    next.regions.push_back(std::move(region));
    next.selected_index.reset();
    return commit(std::move(next), "Add phrase region");
}

NoteEditResult NoteEditor::moveRegion(const std::size_t index,
    const double delta_seconds, const std::size_t track) {
    if (index >= state_.regions.size() || track >= state_.track_count) {
        return NoteEditResult::invalid_index;
    }
    if (!std::isfinite(delta_seconds)) { return NoteEditResult::invalid_note; }
    auto next = state_;
    for (const auto note_index : next.regions[index].note_indices) {
        next.notes[note_index].start_seconds += delta_seconds;
    }
    next.regions[index].track = track;
    return commit(std::move(next), "Move region");
}

NoteEditResult NoteEditor::duplicateRegion(const std::size_t index) {
    if (index >= state_.regions.size()) { return NoteEditResult::invalid_index; }
    const auto& source = state_.regions[index];
    if (state_.notes.size() + source.note_indices.size() > 100'000) {
        return NoteEditResult::invalid_note;
    }
    double start = std::numeric_limits<double>::max(), end = 0;
    for (const auto note_index : source.note_indices) {
        const auto& note = state_.notes[note_index];
        start = std::min(start, note.start_seconds);
        end = std::max(end, note.start_seconds + note.duration_seconds);
    }
    auto next = state_;
    auto copy = source;
    copy.name = source.name.substr(0, 75) + " copy";
    copy.note_indices.clear();
    for (const auto note_index : source.note_indices) {
        auto note = state_.notes[note_index];
        note.start_seconds += end - start;
        copy.note_indices.push_back(next.notes.size());
        next.notes.push_back(note);
    }
    next.regions.push_back(std::move(copy));
    next.selected_index.reset();
    return commit(std::move(next), "Duplicate region");
}

NoteEditResult NoteEditor::deleteRegion(const std::size_t index) {
    if (index >= state_.regions.size()) { return NoteEditResult::invalid_index; }
    auto next = state_;
    std::vector<bool> removed(state_.notes.size(), false);
    for (const auto note_index : state_.regions[index].note_indices) { removed[note_index] = true; }
    std::vector<std::size_t> remapped(state_.notes.size());
    next.notes.clear();
    for (std::size_t i = 0; i < state_.notes.size(); ++i) {
        remapped[i] = next.notes.size();
        if (!removed[i]) { next.notes.push_back(state_.notes[i]); }
    }
    next.regions.erase(next.regions.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto& region : next.regions) {
        for (auto& note_index : region.note_indices) { note_index = remapped[note_index]; }
    }
    if (next.selected_index) {
        if (removed[*next.selected_index]) { next.selected_index.reset(); }
        else { next.selected_index = remapped[*next.selected_index]; }
    }
    return commit(std::move(next), "Delete region");
}

const std::vector<TimedMidiNote>& NoteEditor::notes() const noexcept {
    return state_.notes;
}

const std::vector<TimedMidiNote>& NoteEditor::projectedNotes() const noexcept {
    return projected_notes_;
}

std::optional<std::size_t> NoteEditor::selectedIndex() const noexcept {
    return state_.selected_index;
}

const TimedMidiNote* NoteEditor::selectedNote() const noexcept {
    if (!state_.selected_index || *state_.selected_index >= state_.notes.size()) {
        return nullptr;
    }
    return &state_.notes[*state_.selected_index];
}

bool NoteEditor::select(const std::optional<std::size_t> index) noexcept {
    if (index && *index >= state_.notes.size()) {
        return false;
    }
    state_.selected_index = index;
    return true;
}

NoteEditResult NoteEditor::add(TimedMidiNote note) {
    if (!note.isValid()) {
        return NoteEditResult::invalid_note;
    }
    auto next = state_;
    next.notes.push_back(note);
    auto region = next.regions.end();
    if (state_.selected_index) {
        region = std::find_if(next.regions.begin(), next.regions.end(), [this](const auto& r) {
            return std::find(r.note_indices.begin(), r.note_indices.end(), *state_.selected_index)
                != r.note_indices.end();
        });
    }
    if (region == next.regions.end()) {
        if (next.regions.empty()) { next.regions.push_back({{}, 0, "Melody 01"}); }
        region = std::prev(next.regions.end());
    }
    region->note_indices.push_back(next.notes.size() - 1);
    next.selected_index = next.notes.size() - 1;
    return commit(std::move(next), "Add note");
}

NoteEditResult NoteEditor::updateSelected(TimedMidiNote note, std::string label) {
    if (!state_.selected_index) {
        return NoteEditResult::no_selection;
    }
    if (*state_.selected_index >= state_.notes.size()) {
        return NoteEditResult::invalid_index;
    }
    if (!note.isValid()) {
        return NoteEditResult::invalid_note;
    }
    auto next = state_;
    next.notes[*next.selected_index] = note;
    return commit(std::move(next), std::move(label));
}

NoteEditResult NoteEditor::moveSelected(
    const double delta_seconds,
    const int delta_pitch) {
    const auto* selected = selectedNote();
    if (!selected) {
        return NoteEditResult::no_selection;
    }
    const auto pitch = static_cast<int>(selected->pitch) + delta_pitch;
    TimedMidiNote moved = *selected;
    moved.start_seconds += delta_seconds;
    if (pitch < 0 || pitch > 127) {
        return NoteEditResult::invalid_note;
    }
    moved.pitch = static_cast<std::uint8_t>(pitch);
    return updateSelected(moved, "Move note");
}

NoteEditResult NoteEditor::resizeSelected(const double duration_seconds) {
    const auto* selected = selectedNote();
    if (!selected) {
        return NoteEditResult::no_selection;
    }
    TimedMidiNote resized = *selected;
    resized.duration_seconds = duration_seconds;
    return updateSelected(resized, "Resize note");
}

NoteEditResult NoteEditor::deleteSelected() {
    if (!state_.selected_index) {
        return NoteEditResult::no_selection;
    }
    if (*state_.selected_index >= state_.notes.size()) {
        return NoteEditResult::invalid_index;
    }
    auto next = state_;
    const auto deleted = *next.selected_index;
    next.notes.erase(next.notes.begin() + static_cast<std::ptrdiff_t>(*next.selected_index));
    for (auto& region : next.regions) {
        std::erase(region.note_indices, deleted);
        for (auto& index : region.note_indices) { if (index > deleted) { --index; } }
    }
    std::erase_if(next.regions, [](const auto& region) { return region.note_indices.empty(); });
    if (next.notes.empty()) {
        next.selected_index.reset();
    } else if (*next.selected_index >= next.notes.size()) {
        next.selected_index = next.notes.size() - 1;
    }
    return commit(std::move(next), "Delete note");
}

bool NoteEditor::setScale(ScaleDefinition scale) {
    if (!scale.isValid()) {
        return false;
    }
    scale_ = std::move(scale);
    refreshProjection();
    return true;
}

void NoteEditor::setMagnetEnabled(const bool enabled) {
    magnet_enabled_ = enabled;
    refreshProjection();
}

const ScaleDefinition& NoteEditor::scale() const noexcept {
    return scale_;
}

bool NoteEditor::magnetEnabled() const noexcept {
    return magnet_enabled_;
}

bool NoteEditor::pitchIsInScale(const std::uint8_t pitch) const noexcept {
    return scale_.contains(pitch);
}

std::uint8_t NoteEditor::projectedPitch(const std::uint8_t canonical_pitch) const noexcept {
    return magnet_enabled_ ? scale_.nearestPitch(canonical_pitch) : canonical_pitch;
}

bool NoteEditor::canUndo() const noexcept {
    return history_cursor_ > 0;
}

bool NoteEditor::canRedo() const noexcept {
    return history_cursor_ < history_.size();
}

std::string NoteEditor::undoLabel() const {
    return canUndo() ? history_[history_cursor_ - 1].label : std::string{};
}

std::string NoteEditor::redoLabel() const {
    return canRedo() ? history_[history_cursor_].label : std::string{};
}

bool NoteEditor::undo() {
    if (!canUndo()) {
        return false;
    }
    --history_cursor_;
    state_ = history_[history_cursor_].before;
    refreshProjection();
    return true;
}

bool NoteEditor::redo() {
    if (!canRedo()) {
        return false;
    }
    state_ = history_[history_cursor_].after;
    ++history_cursor_;
    refreshProjection();
    return true;
}

void NoteEditor::clearHistory() noexcept {
    history_.clear();
    history_cursor_ = 0;
}

NoteOverlapPolicy NoteEditor::overlapPolicy() const noexcept {
    return NoteOverlapPolicy::preserve_independent_events;
}

std::size_t NoteEditor::overlapCount() const noexcept {
    struct Interval {
        double start;
        double end;
    };
    std::vector<Interval> intervals;
    intervals.reserve(state_.notes.size());
    for (const auto& note : state_.notes) {
        intervals.push_back({note.start_seconds, note.start_seconds + note.duration_seconds});
    }
    std::sort(intervals.begin(), intervals.end(), [](const Interval& left, const Interval& right) {
        return left.start < right.start;
    });

    std::priority_queue<double, std::vector<double>, std::greater<>> active_ends;
    std::size_t count = 0;
    for (const auto& interval : intervals) {
        while (!active_ends.empty() && active_ends.top() <= interval.start) {
            active_ends.pop();
        }
        count += active_ends.size();
        active_ends.push(interval.end);
    }
    return count;
}

NoteEditResult NoteEditor::commit(State next, std::string label) {
    if (!allValid(next.notes)
        || !validPlaylistRegions(next.regions, next.notes.size(), next.track_count)) {
        return NoteEditResult::invalid_note;
    }
    if (next.selected_index && *next.selected_index >= next.notes.size()) {
        return NoteEditResult::invalid_index;
    }
    if (state_.selected_index == next.selected_index
        && state_.regions == next.regions && state_.track_count == next.track_count
        && state_.notes.size() == next.notes.size()
        && std::equal(
            state_.notes.cbegin(), state_.notes.cend(), next.notes.cbegin(), notesEqual)) {
        return NoteEditResult::no_change;
    }
    history_.erase(
        history_.begin() + static_cast<std::ptrdiff_t>(history_cursor_),
        history_.end());
    HistoryEntry entry{state_, std::move(next), std::move(label)};
    state_ = entry.after;
    history_.push_back(std::move(entry));
    ++history_cursor_;
    refreshProjection();
    return NoteEditResult::applied;
}

void NoteEditor::refreshProjection() {
    if (magnet_enabled_) {
        projected_notes_ = scale_processor_.process(state_.notes, scale_);
    } else {
        projected_notes_ = state_.notes;
    }
}

bool notesOverlap(
    const TimedMidiNote& left,
    const TimedMidiNote& right) noexcept {
    if (!left.isValid() || !right.isValid()) {
        return false;
    }
    const auto left_end = left.start_seconds + left.duration_seconds;
    const auto right_end = right.start_seconds + right.duration_seconds;
    return std::max(left.start_seconds, right.start_seconds)
        < std::min(left_end, right_end);
}

const char* noteEditResultText(const NoteEditResult result) noexcept {
    switch (result) {
    case NoteEditResult::applied:
        return "Edit applied";
    case NoteEditResult::no_change:
        return "Nothing changed";
    case NoteEditResult::no_selection:
        return "Select a note first";
    case NoteEditResult::invalid_index:
        return "The selected note is no longer available";
    case NoteEditResult::invalid_note:
        return "That edit would create an invalid note";
    }
    return "The note could not be edited";
}

} // namespace hum::core
