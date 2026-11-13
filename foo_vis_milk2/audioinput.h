/*
 * audioinput.h - Fetch a fixed, recent PCM window independently of render FPS.
 * SPDX-License-Identifier: MPL-2.0
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

template <size_t Samples, typename Chunk, typename Fetch>
bool ReadAudioWindow(double time, unsigned& sampleRate, Chunk& chunk, Fetch&& fetch,
                     std::array<std::array<float, Samples>, 2>& output)
{
    static_assert(Samples > 0, "An audio window must contain samples");
    for (auto& channel : output)
        channel.fill(0.0f);
    if (!std::isfinite(time) || time <= 0.0)
        return false;

    const unsigned requestedRate = sampleRate ? sampleRate : 44100;
    auto read = [&](unsigned rate)
    {
        // One extra frame avoids a short window from timestamp rounding. The
        // newest Samples frames are selected below. At startup, only request
        // the history that exists rather than asking for a negative timestamp.
        const double start = (std::max)(0.0, time - (static_cast<double>(Samples) + 1.0) / rate);
        const double duration = time - start;
        return duration > 0.0 && fetch(chunk, start, duration);
    };
    if (!read(requestedRate) || chunk.get_srate() == 0)
        return false;

    sampleRate = chunk.get_srate();
    if (sampleRate != requestedRate)
    {
        // The first chunk after a format/track change tells us its actual rate.
        // Re-request once so low-rate sources also supply a complete window.
        if (!read(sampleRate) || chunk.get_srate() == 0)
            return false;
        sampleRate = chunk.get_srate();
    }

    const size_t channels = chunk.get_channel_count();
    const size_t frames = chunk.get_sample_count();
    const auto* data = chunk.get_data();
    if (!data || channels == 0 || frames == 0 || frames > chunk.get_data_size() / channels)
        return false;
    const size_t count = (std::min)(frames, Samples);
    const size_t first = frames - count;
    const size_t destination = Samples - count;
    auto scale = [](double value)
    {
        value *= 128.0;
        return std::isfinite(value) && std::abs(value) <= (std::numeric_limits<float>::max)()
            ? static_cast<float>(value) : 0.0f;
    };
    for (size_t i = 0; i < count; ++i)
    {
        const size_t source = (first + i) * channels;
        output[0][destination + i] = scale(data[source]);
        output[1][destination + i] = channels >= 2 ? scale(data[source + 1]) : output[0][destination + i];
    }
    return true;
}
