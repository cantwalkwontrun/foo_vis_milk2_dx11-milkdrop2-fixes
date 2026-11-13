// Bounded sound() compatibility extension. Language: en-GB.
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace PresetSound
{
// The observed 0..20 / 20..100 / 100..2200 bass/mid/treble split suggests
// 10-Hz coordinates. This is an explicit compatibility interpretation, not a
// verified contract from the undocumented original sound() implementation.
class Spectrum
{
    static constexpr size_t Bins = 512;
    std::array<double, Bins + 1> m_prefix{};
    double m_unitsPerBin = 0;

    double Integral(double coordinate) const noexcept
    {
        const double position = (std::clamp)(coordinate / m_unitsPerBin, 0.0, double(Bins));
        const auto index = static_cast<size_t>(position);
        const double value = index == Bins ? m_prefix[Bins] :
            m_prefix[index] + (m_prefix[index + 1] - m_prefix[index]) * (position - index);
        return value * m_unitsPerBin;
    }
public:
    void Update(const float* values, size_t count, double sampleRate) noexcept
    {
        m_prefix.fill(0);
        m_unitsPerBin = 0;
        if (!values || count != Bins || !std::isfinite(sampleRate) || sampleRate < 1 || sampleRate > 1000000)
            return;
        m_unitsPerBin = sampleRate / (20.0 * Bins);
        // Preserve a 44.1-kHz reference bin-energy scale as physical bin width changes.
        const double referenceScale = 44100.0 / sampleRate;
        for (size_t i = 0; i < Bins; ++i)
            m_prefix[i + 1] = m_prefix[i] + (std::isfinite(values[i]) ? (std::max)(0.0, double(values[i])) * referenceScale : 0.0);
    }

    double Sum(double first, double last) const noexcept
    {
        if (!m_unitsPerBin || !std::isfinite(first) || !std::isfinite(last) || last <= first)
            return 0;
        // Clamp before converting to an array index, including huge arguments.
        const double maximum = m_unitsPerBin * Bins;
        first = (std::clamp)(first, 0.0, maximum);
        last = (std::clamp)(last, 0.0, maximum);
        return (std::max)(0.0, Integral(last) - Integral(first));
    }
};
}
