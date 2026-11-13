/*
 * profft.h - Process the PRO shader spectrum without changing CPU band analysis.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ProFFT
{
constexpr size_t Bins = 512;
using Row = std::array<float, Bins>;

inline float Control(float value, float fallback)
{
    return std::isfinite(value) ? (std::clamp)(value, 0.0f, 1.0f) : fallback;
}

inline void Update(const float* spectrum, size_t count, Row& history, float attack, float decay)
{
    if (!spectrum || count != Bins)
    {
        history.fill(0.0f);
        return;
    }
    attack = Control(attack, 0.5f);
    decay = Control(decay, 0.7f);
    float peak = 0.001f;
    for (size_t i = 0; i < Bins; ++i)
        if (std::isfinite(spectrum[i]))
            peak = (std::max)(peak, spectrum[i]);
    // PRO updates in ascending bin order. Left neighbours therefore contain
    // this frame's values, while right neighbours still contain the last frame.
    for (size_t i = 0; i < Bins; ++i)
    {
        float value = std::isfinite(spectrum[i]) ? spectrum[i] / peak : 0.0f;
        value = value >= 0.2f ? value * value : 0.0f;
        for (int offset = -3; offset <= 3; ++offset)
        {
            const auto neighbour = static_cast<ptrdiff_t>(i) + offset;
            if (offset != 0 && neighbour >= 0 && neighbour < static_cast<ptrdiff_t>(Bins))
            {
                const float previous = history[neighbour];
                if (std::isfinite(previous))
                    value = (std::max)(value, previous * (1.0f - 0.25f * std::abs(offset)));
            }
        }
        const float previous = std::isfinite(history[i]) ? (std::clamp)(history[i], 0.0f, 1.0f) : 0.0f;
        history[i] = value > previous ? previous + (value - previous) * (0.5f + 0.5f * attack)
                                      : previous * (0.9f + 0.1f * decay);
    }
}
}
