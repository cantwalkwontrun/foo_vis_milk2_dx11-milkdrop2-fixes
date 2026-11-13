/*
 * visualinactivity.h - Require measured output before classifying a preset.
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#pragma once

#include <cmath>

inline bool HasRecentVisualRecoveryAudio(bool signal, float now, float& lastSignal) noexcept
{
    if (!std::isfinite(now))
        return false;
    if (signal)
        lastSignal = now;
    return now >= lastSignal && now - lastSignal <= 0.5f;
}

struct VisualOutputSample
{
    bool available = false;
    // Normalised RGB extrema across the final image. Channel range is the
    // largest spatial range of one channel, not maxBrightness-minBrightness.
    float minBrightness = 0.0f;
    float maxBrightness = 0.0f;
    float maxChannelRange = 0.0f;
    float dominantFraction = 0.0f; // Diagnostic only; occupancy cannot prove failure.
};

enum class VisualOutputExtreme
{
    None,
    Dark,
    Bright
};

struct VisualRecoveryState
{
    VisualOutputExtreme lastExtreme = VisualOutputExtreme::None;
    int consecutiveSamples = 0;
};

inline VisualOutputExtreme ClassifyUniformExtremeOutput(const VisualOutputSample& sample) noexcept
{
    if (!sample.available || !std::isfinite(sample.minBrightness) ||
        !std::isfinite(sample.maxBrightness) || !std::isfinite(sample.maxChannelRange) ||
        sample.minBrightness < 0.0f || sample.maxBrightness > 1.0f ||
        sample.minBrightness > sample.maxBrightness || sample.maxChannelRange < 0.0f ||
        sample.maxChannelRange > 1.0f / 255.0f)
        return VisualOutputExtreme::None;

    if (sample.maxBrightness <= 4.0f / 255.0f)
        return VisualOutputExtreme::Dark;
    if (sample.minBrightness >= 245.0f / 255.0f)
        return VisualOutputExtreme::Bright;
    return VisualOutputExtreme::None;
}

inline bool AccumulateVisualExtreme(VisualOutputExtreme extreme, VisualRecoveryState& state) noexcept
{
    constexpr int kRequiredSamples = 3;
    if (extreme == VisualOutputExtreme::None)
    {
        state = {};
        return false;
    }

    if (state.lastExtreme != extreme)
    {
        state.lastExtreme = extreme;
        state.consecutiveSamples = 1;
    }
    else if (state.consecutiveSamples < kRequiredSamples)
    {
        ++state.consecutiveSamples;
    }
    return state.consecutiveSamples >= kRequiredSamples;
}

inline bool HasPersistentlyUniformExtremeOutput(const VisualOutputSample& sample,
                                               VisualRecoveryState& state) noexcept
{
    return AccumulateVisualExtreme(ClassifyUniformExtremeOutput(sample), state);
}

inline bool HasPersistentlyMissingDrawingSource(const VisualOutputSample& feedback,
                                               bool eligible, VisualRecoveryState& state) noexcept
{
    // Eligibility requires dormant authored drawing and feedback-dependent
    // shaders. A dark procedural composite alone cannot justify seeding it.
    const auto extreme = eligible && ClassifyUniformExtremeOutput(feedback) == VisualOutputExtreme::Dark
        ? VisualOutputExtreme::Dark : VisualOutputExtreme::None;
    return AccumulateVisualExtreme(extreme, state);
}

inline bool HasPersistentlyDarkOutput(bool sampled, float brightness, int& darkSamples) noexcept
{
    // Missing readback is unknown output, not a black frame. Legacy control
    // values cannot describe custom waves, shapes or time-driven shaders.
    constexpr float kBlackFrameBrightnessThreshold = 0.018f;
    constexpr int kBlackFrameSampleThreshold = 3;
    if (!sampled || !std::isfinite(brightness) || brightness < 0.0f ||
        brightness > kBlackFrameBrightnessThreshold)
    {
        darkSamples = 0;
        return false;
    }

    if (darkSamples < kBlackFrameSampleThreshold)
        ++darkSamples;
    return darkSamples >= kBlackFrameSampleThreshold;
}
