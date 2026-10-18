#pragma once
#include <QByteArray>
#include <QIODevice>
#include <algorithm>
#include <cstring>

namespace hum::app {
// Repeats exact frame-aligned bytes in the sink's pull callback, independent of GUI polling.
class MelodyBuffer final : public QIODevice {
public:
    MelodyBuffer(const QByteArray& bytes, bool loop, QObject* parent = nullptr,
        qint64 first = 0, qint64 last = -1)
        : QIODevice(parent), bytes_(bytes), loop_(loop),
          first_(std::clamp<qint64>(first, 0, bytes.size())),
          last_(last < 0 ? bytes.size() : std::clamp<qint64>(last, first_, bytes.size())), cursor_(first_) {}
    bool isSequential() const override { return true; }
    bool atEnd() const override { return first_ == last_ || (!loop_ && cursor_ >= last_); }
    qint64 bytesAvailable() const override {
        return (loop_ ? last_ - first_ : last_ - cursor_) + QIODevice::bytesAvailable();
    }
    void setOffset(qint64 offset) {
        cursor_ = loop_ && last_ > first_
            ? first_ + ((offset - first_) % (last_ - first_) + (last_ - first_)) % (last_ - first_)
            : std::clamp(offset, first_, last_);
    }
protected:
    qint64 readData(char* destination, qint64 maximum) override {
        qint64 total = 0;
        while (total < maximum && last_ > first_) {
            if (cursor_ >= last_) { if (!loop_) { break; } cursor_ = first_; }
            const auto count = std::min(maximum - total, last_ - cursor_);
            std::memcpy(destination + total, bytes_.constData() + cursor_, static_cast<std::size_t>(count));
            total += count; cursor_ += count;
        }
        return total;
    }
    qint64 writeData(const char*, qint64) override { return -1; }
private:
    const QByteArray& bytes_;
    bool loop_;
    qint64 first_, last_, cursor_;
};
}
