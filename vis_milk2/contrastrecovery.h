/*
 * contrastrecovery.h - Conservative, measured automatic contrast recovery.
 * Language: en-GB
 * SPDX-License-Identifier: MPL-2.0
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct ContrastImageSample
{
    bool available = false;
    float mean = 0;
    float peak = 0;
    float range = 0;
    float variance = 0;
    bool distributionAvailable = false;
    float upperBrightness = 0;
    float detailFraction = 0;
    std::array<float, 64> tiles{};
};

class ContrastImageAccumulator
{
  public:
    void Add(uint8_t r, uint8_t g, uint8_t b, float appliedExponent, unsigned tile = 0) noexcept
    {
        if (!std::isfinite(appliedExponent) || appliedExponent < .25f || appliedExponent > 1)
            return;
        double rgb[]{r / 255.0, g / 255.0, b / 255.0};
        const double peak = (std::max)({rgb[0], rgb[1], rgb[2]});
        // Analyse the original brightness, not our corrected output. Otherwise
        // successful correction would repeatedly switch itself off and on.
        const double scale = appliedExponent < 1 && peak > 0 && peak < 1
            ? std::pow(peak, 1.0 / appliedExponent - 1.0) : 1.0;
        for (unsigned c = 0; c < 3; ++c)
        {
            rgb[c] *= scale;
            m_min[c] = (std::min)(m_min[c], rgb[c]);
            m_max[c] = (std::max)(m_max[c], rgb[c]);
        }
        const double mean = (rgb[0] + rgb[1] + rgb[2]) / 3;
        m_sum += mean;
        m_square += mean * mean;
        const double originalPeak = peak * scale;
        const unsigned bin = static_cast<unsigned>(std::lround(originalPeak * 255));
        ++m_histogram[(std::min)(255U, bin)];
        m_visible += originalPeak > 2.0 / 255;
        tile = (std::min)(63U, tile);
        m_tiles[tile] += mean;
        ++m_tileCounts[tile];
        ++m_count;
    }

    ContrastImageSample Finish() const noexcept
    {
        if (!m_count) return {};
        const double mean = m_sum / m_count;
        ContrastImageSample result{true, static_cast<float>(mean), static_cast<float>((std::max)({m_max[0], m_max[1], m_max[2]})),
            static_cast<float>((std::max)({m_max[0]-m_min[0], m_max[1]-m_min[1], m_max[2]-m_min[2]})),
            static_cast<float>((std::max)(0.0, m_square / m_count - mean * mean))};
        unsigned cumulative = 0;
        for (unsigned i = 0; i < m_histogram.size(); ++i)
        {
            cumulative += m_histogram[i];
            if (cumulative * 10 >= m_count * 9) { result.upperBrightness = i / 255.0f; break; }
        }
        result.detailFraction = static_cast<float>(m_visible) / m_count;
        unsigned populated = 0;
        for (unsigned i = 0; i < result.tiles.size(); ++i)
            if (m_tileCounts[i]) { result.tiles[i] = static_cast<float>(m_tiles[i] / m_tileCounts[i]); ++populated; }
        result.distributionAvailable = populated == result.tiles.size();
        return result;
    }

  private:
    double m_min[3]{1,1,1}, m_max[3]{};
    double m_sum = 0, m_square = 0;
    unsigned m_count = 0;
    unsigned m_visible = 0;
    std::array<unsigned, 256> m_histogram{};
    std::array<double, 64> m_tiles{};
    std::array<unsigned, 64> m_tileCounts{};
};

class AutomaticContrastRecovery
{
  public:
    void Reset(float now, bool discover = false) noexcept
    {
        m_target = false;
        m_weight = 0;
        m_lastTime = std::isfinite(now) ? now : 0;
        m_readyAt = m_lastTime + 2.0f;
        m_discover = discover;
        m_previousAvailable = false;
        ClearEvidence();
    }
    void ClearEvidence() noexcept { m_darkSamples = m_brightSamples = m_motionHistory = 0; m_previousAvailable = false; }
    bool Ready(float now) const noexcept { return std::isfinite(now) && now >= m_readyAt; }
    void Observe(const ContrastImageSample& sample) noexcept
    {
        if (!sample.available || !std::isfinite(sample.mean) || !std::isfinite(sample.peak) ||
            !std::isfinite(sample.range) || !std::isfinite(sample.variance) ||
            sample.mean < 0 || sample.mean > 1 || sample.peak < sample.mean || sample.peak > 1 ||
            sample.range < 0 || sample.range > 1 || sample.variance < 0)
        {
            ClearEvidence();
            return;
        }
        if (m_discover)
        {
            ObserveUnrecognised(sample);
            return;
        }
        // Confirmed contents use the stronger policy.
        // Uniform black/white belongs to source recovery instead.
        const bool dimDetail = sample.mean > 0 && sample.mean < .075f && sample.peak > 4.0f/255 &&
            sample.peak < .75f && sample.range > 2.0f/255 && sample.variance > 1e-8f;
        const bool bright = sample.mean >= .11f || sample.peak >= .9f;
        if (dimDetail)
        {
            m_brightSamples = 0;
            if (++m_darkSamples >= 3) { m_darkSamples = 3; m_target = true; }
        }
        else if (bright)
        {
            m_darkSamples = 0;
            if (++m_brightSamples >= 3) { m_brightSamples = 3; m_target = false; }
        }
        else ClearEvidence();
    }
    void Advance(float now) noexcept
    {
        if (!std::isfinite(now)) return;
        const float elapsed = (std::clamp)(now - m_lastTime, 0.0f, .25f);
        m_lastTime = now;
        const float step = elapsed * 2.0f;
        m_weight = m_target ? (std::min)(1.0f, m_weight + step) : (std::max)(0.0f, m_weight - step);
    }
    float Exponent() const noexcept { return 1.0f - (m_discover ? .30f : .65f) * m_weight; }

  private:
    void ObserveUnrecognised(const ContrastImageSample& sample) noexcept
    {
        // This detects a compressed moving image, not artistic intent. Sparse
        // lines, static images, uniform fills and visible highlights stay neutral.
        // Unknown contents get a modest lift only after eight consecutive matches.
        if (!sample.distributionAvailable || !std::isfinite(sample.upperBrightness) ||
            !std::isfinite(sample.detailFraction) || sample.upperBrightness < 0 || sample.upperBrightness > 1 ||
            sample.detailFraction < 0 || sample.detailFraction > 1)
        {
            ClearEvidence();
            return;
        }
        float change = 0, tileSum = 0, tileSquare = 0;
        for (unsigned i = 0; i < sample.tiles.size(); ++i)
        {
            if (!std::isfinite(sample.tiles[i]) || sample.tiles[i] < 0 || sample.tiles[i] > 1)
            {
                ClearEvidence();
                return;
            }
            change += std::abs(sample.tiles[i] - m_previousTiles[i]);
            tileSum += sample.tiles[i];
            tileSquare += sample.tiles[i] * sample.tiles[i];
        }
        const bool moving = m_previousAvailable &&
            change / sample.tiles.size() > .01f * (std::max)(sample.mean, .005f);
        m_motionHistory = ((m_motionHistory << 1) | (moving ? 1U : 0U)) & 255U;
        unsigned movingSamples = 0;
        for (unsigned bits = m_motionHistory; bits; bits >>= 1) movingSamples += bits & 1U;
        m_previousTiles = sample.tiles;
        m_previousAvailable = true;
        const float tileMean = tileSum / sample.tiles.size();
        const float tileVariance = tileSquare / sample.tiles.size() - tileMean * tileMean;
        const bool detailed = sample.detailFraction >= .20f && sample.variance > 1e-6f &&
            sample.variance > .08f * sample.mean * sample.mean && tileVariance > .05f * sample.variance;
        const bool dark = sample.mean > 1.0f/255 && sample.mean < .06f &&
            sample.peak > 4.0f/255 && sample.peak < .6f &&
            sample.upperBrightness > 2.0f/255 && sample.upperBrightness < .18f;
        const bool restore = sample.mean >= .085f || sample.peak >= .75f || sample.upperBrightness >= .25f ||
            !detailed;
        if (dark && detailed)
        {
            m_darkSamples = (std::min)(8U, m_darkSamples + 1);
            m_brightSamples = moving ? 0 : (std::min)(3U, m_brightSamples + 1);
            if (m_brightSamples >= 3) m_target = false;
            else if (m_darkSamples >= 8 && movingSamples >= 4) m_target = true;
        }
        else if (restore)
        {
            m_darkSamples = 0;
            if (++m_brightSamples >= 3) { m_brightSamples = 3; m_target = false; }
        }
        else m_darkSamples = m_brightSamples = 0;
    }
    bool m_target = false;
    bool m_discover = false, m_previousAvailable = false;
    std::array<float, 64> m_previousTiles{};
    float m_weight = 0, m_lastTime = 0, m_readyAt = 0;
    unsigned m_darkSamples = 0, m_brightSamples = 0;
    unsigned m_motionHistory = 0;
};
