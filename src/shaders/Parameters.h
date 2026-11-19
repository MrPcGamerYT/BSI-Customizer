#pragma once
#include <array>
#include <cmath>
#include <string>

// Original shader-only controls. Integer steps keep QML/persistence stable;
// normalized shader weights are computed only when a saved state is applied.
namespace BsiShaders {
struct Control { const char *key; const char *label; int minimum, maximum; };
inline constexpr std::array<Control, 10> controls {{
    {"saturation", "Saturation", -100, 100},
    {"contrast", "Contrast", -100, 100},
    {"warmth", "Warmth", -100, 100},
    {"shadows", "Shadow lift", 0, 100},
    {"hdr", "Tonal pop", 0, 100},
    {"ambient", "Ambient light", 0, 100},
    {"sharpen", "Sharpen", 0, 100},
    {"fxaa", "Edge smoothing", 0, 100},
    {"bloom", "Bloom", 0, 100},
    {"vignette", "Vignette", 0, 100}
}};
struct Parameters {
    bool enabled = false;
    std::array<int, controls.size()> values {};
    bool neutral() const {
        if (!enabled) return true;
        for (int value : values) if (value) return false;
        return true;
    }
    bool valid() const {
        for (std::size_t i = 0; i < values.size(); ++i)
            if (values[i] < controls[i].minimum || values[i] > controls[i].maximum) return false;
        return true;
    }
    bool operator==(const Parameters &other) const { return enabled == other.enabled && values == other.values; }
    bool operator!=(const Parameters &other) const { return !(*this == other); }
};
// Reference math for flat-image GPU tests (no sharpening/edge/bloom/vignette).
inline std::array<float, 3> grade(std::array<float, 3> color, const Parameters &p) {
    if (p.neutral()) return color;
    const float luma = color[0] * .2126f + color[1] * .7152f + color[2] * .0722f;
    for (int i = 0; i < 3; ++i) {
        float c = luma + (color[i] - luma) * (1.f + p.values[0] / 100.f);
        c = (c - .5f) * (1.f + p.values[1] / 100.f) + .5f;
        c += p.values[2] / 100.f * (i == 0 ? .08f : i == 2 ? -.08f : 0.f);
        c += p.values[3] / 100.f * .18f * std::pow(1.f - std::fmax(0.f, std::fmin(1.f, c)), 2.f);
        c += p.values[4] / 100.f * .55f * (c - .5f) * (1.f - std::fabs(2.f * c - 1.f));
        c += p.values[5] / 100.f * .08f;
        color[i] = std::fmax(0.f, std::fmin(1.f, c));
    }
    return color;
}
} // namespace BsiShaders
