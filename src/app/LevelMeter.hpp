#pragma once

#include <QWidget>

namespace hum::app {

class LevelMeter final : public QWidget {
public:
    explicit LevelMeter(QWidget* parent = nullptr);

    void setLevel(float linear_peak, bool clipped);
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float level_{0.0F};
    bool clipped_{false};
};

} // namespace hum::app
