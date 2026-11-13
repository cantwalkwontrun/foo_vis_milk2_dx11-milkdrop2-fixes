/*
 * shapesafety.h - Bound untrusted custom-shape drawing values.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace ShapeSafety
{
// Match the existing editor maximum; zero/negative counts keep drawing disabled.
constexpr int MaxInstances = 1024;

inline int InstanceCount(int count) noexcept
{
    return (std::clamp)(count, 0, MaxInstances);
}

inline bool Sides(double requested, int& sides) noexcept
{
    if (!std::isfinite(requested))
        return false;
    sides = static_cast<int>((std::clamp)(requested, 3.0, 100.0));
    return true;
}

inline bool FloatValue(double value) noexcept
{
    constexpr double limit = (std::numeric_limits<float>::max)();
    return std::isfinite(value) && value >= -limit && value <= limit;
}

inline bool Colour(double value) noexcept
{
    const double scaled = value * 255.0;
    return std::isfinite(scaled) && scaled >= static_cast<double>((std::numeric_limits<int>::min)()) &&
        scaled <= static_cast<double>((std::numeric_limits<int>::max)());
}

inline bool Flag(double value) noexcept
{
    // Retain the existing truncation of fractional flags without converting
    // arbitrarily large EEL doubles to an int.
    return std::isfinite(value) && std::trunc(value) != 0.0;
}
}
