/*
 * audioinput.cpp - Fixed-window PCM and FFT regressions.
 * SPDX-License-Identifier: MPL-2.0
 */
#include <foo_vis_milk2/audioinput.h>
#include <vis_milk2/fft.h>
#include <CppUnitTest.h>
#include <limits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
namespace
{
constexpr size_t samples = 576;
using Waves = std::array<std::array<float, samples>, 2>;
struct Chunk
{
    unsigned rate = 48000, channels = 2;
    size_t frames = 0;
    std::vector<double> data;
    unsigned get_srate() const { return rate; }
    unsigned get_channel_count() const { return channels; }
    size_t get_sample_count() const { return frames; }
    size_t get_data_size() const { return data.size(); }
    const double* get_data() const { return data.data(); }
};
struct Stream
{
    unsigned rate = 48000, channels = 2;
    int calls = 0;
    double lastDuration = 0.0;
    static double value(long long frame, unsigned channel)
    {
        return 0.25 + 0.125 * std::sin(frame * 0.04) + channel * 0.125;
    }
    bool operator()(Chunk& chunk, double start, double duration)
    {
        ++calls;
        lastDuration = duration;
        chunk.rate = rate;
        chunk.channels = channels;
        chunk.frames = static_cast<size_t>(std::llround(duration * rate));
        chunk.data.resize(chunk.frames * channels);
        const auto first = std::llround((start + duration) * rate) - chunk.frames;
        for (size_t i = 0; i < chunk.frames; ++i)
            for (unsigned c = 0; c < channels; ++c)
                chunk.data[i * channels + c] = value(first + i, c);
        return true;
    }
};
void AssertSilent(const Waves& waves)
{
    for (const auto& channel : waves)
        for (float value : channel)
            Assert::AreEqual(0.0f, value);
}
}

TEST_CLASS(AudioInputTest)
{
  public:
    TEST_METHOD(FullNewestWindowAcrossSampleRatesAndFrameRates)
    {
        for (unsigned rate : {8000U, 44100U, 48000U, 96000U, 192000U})
            for (unsigned fps : {30U, 60U, 120U, 144U, 240U, 1000U})
            {
                Stream stream;
                stream.rate = rate;
                Chunk chunk;
                Waves waves;
                unsigned cachedRate = 44100;
                const double time = 2.0 + 1.0 / fps;
                Assert::IsTrue(ReadAudioWindow(time, cachedRate, chunk, stream, waves));
                Assert::AreEqual(rate, cachedRate);
                Assert::IsTrue(std::abs(stream.lastDuration - 577.0 / rate) < 1e-10);
                const auto first = std::llround(time * rate) - samples;
                for (size_t i = 0; i < samples; ++i)
                    for (unsigned c = 0; c < 2; ++c)
                        Assert::AreEqual(static_cast<float>(Stream::value(first + i, c) * 128.0), waves[c][i]);
            }
    }

    TEST_METHOD(RepeatedTimestampsDoNotDropTheAudio)
    {
        Stream stream;
        Chunk chunk;
        Waves first, next;
        unsigned rate = 48000;
        Assert::IsTrue(ReadAudioWindow(3.0, rate, chunk, stream, first));
        Assert::IsTrue(ReadAudioWindow(3.0, rate, chunk, stream, next));
        Assert::IsTrue(first == next);
        Assert::IsTrue(first[0][200] != 0.0f);
    }

    TEST_METHOD(SeekAndRateChangesReplaceTheOldWindow)
    {
        Stream stream;
        Chunk chunk;
        Waves waves;
        unsigned rate = 44100;
        Assert::IsTrue(ReadAudioWindow(120.0, rate, chunk, stream, waves));
        stream.rate = 8000;
        Assert::IsTrue(ReadAudioWindow(0.5, rate, chunk, stream, waves));
        Assert::AreEqual(8000U, rate);
        Assert::AreEqual(static_cast<float>(Stream::value(4000 - 576, 0) * 128.0), waves[0][0]);
        stream.rate = 96000;
        Assert::IsTrue(ReadAudioWindow(1.0, rate, chunk, stream, waves));
        Assert::AreEqual(96000U, rate);
        Assert::AreEqual(static_cast<float>(Stream::value(96000 - 1, 1) * 128.0), waves[1][575]);
    }

    TEST_METHOD(StartupPadsUnavailableHistoryBeforeTheNewestAudio)
    {
        Stream stream;
        Chunk chunk;
        Waves waves;
        unsigned rate = 48000;
        Assert::IsTrue(ReadAudioWindow(0.003, rate, chunk, stream, waves));
        for (size_t i = 0; i < 432; ++i)
            Assert::AreEqual(0.0f, waves[0][i]);
        Assert::AreEqual(32.0f, waves[0][432]);
        Assert::AreEqual(static_cast<float>(Stream::value(143, 0) * 128.0), waves[0][575]);
    }

    TEST_METHOD(MonoIsDuplicatedAndMultichannelKeepsTheFrontPair)
    {
        Stream stream;
        Chunk chunk;
        Waves waves;
        unsigned rate = 48000;
        stream.channels = 1;
        Assert::IsTrue(ReadAudioWindow(1.0, rate, chunk, stream, waves));
        Assert::IsTrue(waves[0] == waves[1]);
        stream.channels = 6;
        Assert::IsTrue(ReadAudioWindow(1.0, rate, chunk, stream, waves));
        Assert::AreEqual(static_cast<float>(Stream::value(48000 - 1, 0) * 128.0), waves[0][575]);
        Assert::AreEqual(static_cast<float>(Stream::value(48000 - 1, 1) * 128.0), waves[1][575]);
    }

    TEST_METHOD(UnavailableDataClearsPreviousAudio)
    {
        Stream stream;
        Chunk chunk;
        Waves waves;
        unsigned rate = 48000;
        Assert::IsTrue(ReadAudioWindow(1.0, rate, chunk, stream, waves));
        auto missing = [](Chunk&, double, double) { return false; };
        Assert::IsFalse(ReadAudioWindow(1.01, rate, chunk, missing, waves));
        AssertSilent(waves);
    }

    TEST_METHOD(InvalidTimestampsDoNotRequestAudio)
    {
        for (double time : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()})
        {
            Stream stream;
            Chunk chunk;
            Waves waves;
            waves[0].fill(10.0f);
            unsigned rate = 48000;
            Assert::IsFalse(ReadAudioWindow(time, rate, chunk, stream, waves));
            Assert::AreEqual(0, stream.calls);
            AssertSilent(waves);
        }
    }

    TEST_METHOD(MalformedPacketsCannotReadBeyondTheirStorage)
    {
        for (int kind = 0; kind < 4; ++kind)
        {
            Chunk chunk;
            Waves waves;
            unsigned rate = 48000;
            auto malformed = [kind](Chunk& c, double, double)
            {
                c.rate = kind == 0 ? 0 : 48000;
                c.channels = kind == 1 ? 0 : 2;
                c.frames = kind == 2 ? 100 : 0;
                c.data.assign(4, 1.0);
                return true;
            };
            Assert::IsFalse(ReadAudioWindow(1.0, rate, chunk, malformed, waves));
            AssertSilent(waves);
        }
    }

    TEST_METHOD(NonFiniteAndOverflowingSamplesDoNotPoisonTheFFT)
    {
        Chunk chunk;
        Waves waves;
        unsigned rate = 48000;
        auto fetch = [](Chunk& c, double, double)
        {
            c.frames = samples;
            c.data.assign(samples * 2, 0.5);
            c.data[0] = std::numeric_limits<double>::quiet_NaN();
            c.data[1] = std::numeric_limits<double>::infinity();
            c.data[2] = std::numeric_limits<double>::max();
            return true;
        };
        Assert::IsTrue(ReadAudioWindow(1.0, rate, chunk, fetch, waves));
        Assert::AreEqual(0.0f, waves[0][0]);
        Assert::AreEqual(0.0f, waves[1][0]);
        Assert::AreEqual(0.0f, waves[0][1]);
        Assert::AreEqual(64.0f, waves[1][1]);
    }

    TEST_METHOD(FFTIsIndependentOfRenderFrequencyAtTheSamePlaybackPosition)
    {
        FFT fft(samples, 512, true, 1.0f);
        std::vector<float> reference;
        for (unsigned fps : {30U, 60U, 120U, 144U, 240U, 1000U})
        {
            Stream stream;
            Chunk chunk;
            Waves waves;
            unsigned rate = 44100;
            // Visit the preceding render time, then compare the same current
            // playback position for each frame rate (including sub-ms gaps).
            Assert::IsTrue(ReadAudioWindow(2.0 - 1.0 / fps, rate, chunk, stream, waves));
            Assert::IsTrue(ReadAudioWindow(2.0, rate, chunk, stream, waves));
            std::vector<float> input(waves[0].begin(), waves[0].end()), spectrum;
            fft.TimeToFrequencyDomain(input, spectrum);
            if (reference.empty()) reference = spectrum;
            Assert::IsTrue(spectrum == reference);
        }
    }
};
}
