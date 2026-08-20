#include "history.h"

#include <math.h>

namespace telemetry {

float light_percent(uint16_t raw, uint16_t full_scale) {
    if (full_scale == 0) {
        return 0.0f;
    }
    if (raw > full_scale) {
        raw = full_scale;
    }
    return (static_cast<float>(raw) / static_cast<float>(full_scale)) * 100.0f;
}

bool reading_is_plausible(float temperature_c, float humidity_pct) {
    if (isnan(temperature_c) || isnan(humidity_pct)) {
        return false;
    }
    if (temperature_c < -40.0f || temperature_c > 80.0f) {
        return false;
    }
    return humidity_pct >= 0.0f && humidity_pct <= 100.0f;
}

}
