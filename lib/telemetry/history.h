#pragma once

#include <stddef.h>
#include <stdint.h>

namespace telemetry {

struct Sample {
    uint32_t uptime_s = 0;
    float temperature_c = 0.0f;
    float humidity_pct = 0.0f;
    uint16_t light_raw = 0;
};

struct Stats {
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;
    uint16_t count = 0;
};

float light_percent(uint16_t raw, uint16_t full_scale = 4095);
bool reading_is_plausible(float temperature_c, float humidity_pct);

template <uint16_t Capacity>
class History {
public:
    void push(const Sample &sample) {
        buffer_[head_] = sample;
        head_ = static_cast<uint16_t>((head_ + 1) % Capacity);
        if (count_ < Capacity) {
            count_++;
        }
    }

    uint16_t size() const { return count_; }
    uint16_t capacity() const { return Capacity; }
    bool full() const { return count_ == Capacity; }

    const Sample &at(uint16_t index) const {
        uint16_t start = static_cast<uint16_t>((head_ + Capacity - count_) % Capacity);
        return buffer_[(start + index) % Capacity];
    }

    const Sample &oldest() const { return at(0); }
    const Sample &newest() const { return at(count_ > 0 ? count_ - 1 : 0); }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

    Stats temperature_stats() const { return stats_of(&Sample::temperature_c); }
    Stats humidity_stats() const { return stats_of(&Sample::humidity_pct); }

private:
    Stats stats_of(float Sample::*field) const {
        Stats stats;
        if (count_ == 0) {
            return stats;
        }

        float total = 0.0f;
        stats.min = at(0).*field;
        stats.max = at(0).*field;

        for (uint16_t i = 0; i < count_; i++) {
            float value = at(i).*field;
            if (value < stats.min) {
                stats.min = value;
            }
            if (value > stats.max) {
                stats.max = value;
            }
            total += value;
        }

        stats.mean = total / static_cast<float>(count_);
        stats.count = count_;
        return stats;
    }

    Sample buffer_[Capacity];
    uint16_t head_ = 0;
    uint16_t count_ = 0;
};

}
