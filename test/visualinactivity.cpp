/*
 * visualinactivity.cpp - Tests for preset output classification.
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#include "pch.h"

#include <initializer_list>
#include <limits>
#include <vis_milk2/visualinactivity.h>
#include <CppUnitTest.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
TEST_CLASS(VisualInactivityTest)
{
  public:
    TEST_METHOD(SparseAndDimVisibleEffectsNeverTriggerAutomaticRecovery)
    {
        // A thin animated line can leave more than 99% of the image black.
        // A procedural composite can legitimately colour black feedback.
        // Neither occupancy nor an unused disabled wave proves a failure.
        for (const auto sample : {VisualOutputSample{true, 0, .8f, .8f, .995f},
                                  VisualOutputSample{true, .09f, .11f, .02f, 1.0f}})
        {
            VisualRecoveryState state;
            for (int count = 0; count < 12; ++count)
                Assert::IsFalse(HasPersistentlyUniformExtremeOutput(sample, state));
            Assert::AreEqual(0, state.consecutiveSamples);
        }
    }

    TEST_METHOD(ShortAudioGapsPreserveRecoveryButSustainedSilenceDoesNot)
    {
        float lastSignal = -1000.0f;
        Assert::IsFalse(HasRecentVisualRecoveryAudio(false, 10.0f, lastSignal));
        Assert::IsTrue(HasRecentVisualRecoveryAudio(true, 10.0f, lastSignal));
        Assert::IsTrue(HasRecentVisualRecoveryAudio(false, 10.1f, lastSignal));
        Assert::IsTrue(HasRecentVisualRecoveryAudio(false, 10.5f, lastSignal));
        Assert::IsFalse(HasRecentVisualRecoveryAudio(false, 10.6f, lastSignal));
        Assert::IsFalse(HasRecentVisualRecoveryAudio(false, 9.0f, lastSignal));
        Assert::IsFalse(HasRecentVisualRecoveryAudio(true, std::numeric_limits<float>::quiet_NaN(), lastSignal));
    }

    TEST_METHOD(MissingDrawingRequiresEligibleFeedbackDependencyAndConsecutiveBlackSamples)
    {
        VisualRecoveryState state;
        const VisualOutputSample black{true, 0, 0, 0};
        for (int i = 0; i < 6; ++i)
            Assert::IsFalse(HasPersistentlyMissingDrawingSource(black, false, state));
        Assert::IsFalse(HasPersistentlyMissingDrawingSource(black, true, state));
        Assert::IsFalse(HasPersistentlyMissingDrawingSource({}, true, state));
        Assert::IsFalse(HasPersistentlyMissingDrawingSource(black, true, state));
        Assert::IsFalse(HasPersistentlyMissingDrawingSource(black, true, state));
        Assert::IsTrue(HasPersistentlyMissingDrawingSource(black, true, state));
        Assert::IsFalse(HasPersistentlyMissingDrawingSource({true, .1f, .2f, .1f}, true, state));
        Assert::IsFalse(HasPersistentlyMissingDrawingSource({true, 1, 1, 0}, true, state));
    }

    TEST_METHOD(UnavailableOutputNeverHidesPreset)
    {
        int darkSamples = 0;
        for (int frame = 0; frame < 1800; ++frame)
            Assert::IsFalse(HasPersistentlyDarkOutput(false, 0.0f, darkSamples));
        Assert::AreEqual(0, darkSamples);
    }

    TEST_METHOD(VisibleStaticOutputNeverHidesPreset)
    {
        int darkSamples = 0;
        for (int frame = 0; frame < 1800; ++frame)
            Assert::IsFalse(HasPersistentlyDarkOutput(true, 0.5f, darkSamples));
        Assert::AreEqual(0, darkSamples);
    }

    TEST_METHOD(RequiresConsecutiveMeasuredBlackFrames)
    {
        int darkSamples = 0;
        Assert::IsFalse(HasPersistentlyDarkOutput(true, 0.0f, darkSamples));
        Assert::IsFalse(HasPersistentlyDarkOutput(true, 0.0f, darkSamples));
        Assert::IsFalse(HasPersistentlyDarkOutput(true, 0.5f, darkSamples));
        Assert::IsFalse(HasPersistentlyDarkOutput(true, 0.0f, darkSamples));
        Assert::IsFalse(HasPersistentlyDarkOutput(true, 0.0f, darkSamples));
        Assert::IsTrue(HasPersistentlyDarkOutput(true, 0.0f, darkSamples));
    }

    TEST_METHOD(UnavailableOrInvalidSampleBreaksDarkSequence)
    {
        int darkSamples = 2;
        Assert::IsFalse(HasPersistentlyDarkOutput(false, 0.0f, darkSamples));
        Assert::AreEqual(0, darkSamples);
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(), -1.0f})
        {
            darkSamples = 2;
            Assert::IsFalse(HasPersistentlyDarkOutput(true, invalid, darkSamples));
            Assert::AreEqual(0, darkSamples);
        }
    }

    TEST_METHOD(UniformBlackAndWhiteRequireThreeMatchingSamples)
    {
        for (const VisualOutputSample sample : {VisualOutputSample{true, 0.0f, 0.0f, 0.0f},
                                               VisualOutputSample{true, 1.0f, 1.0f, 0.0f}})
        {
            VisualRecoveryState state;
            Assert::IsFalse(HasPersistentlyUniformExtremeOutput(sample, state));
            Assert::IsFalse(HasPersistentlyUniformExtremeOutput(sample, state));
            Assert::IsTrue(HasPersistentlyUniformExtremeOutput(sample, state));
            Assert::IsTrue(HasPersistentlyUniformExtremeOutput(sample, state));
            Assert::AreEqual(3, state.consecutiveSamples);
        }
    }

    TEST_METHOD(FlashesAndAlternatingExtremesDoNotAccumulate)
    {
        const VisualOutputSample black{true, 0.0f, 0.0f, 0.0f};
        const VisualOutputSample white{true, 1.0f, 1.0f, 0.0f};
        VisualRecoveryState state;
        Assert::IsFalse(HasPersistentlyUniformExtremeOutput(black, state));
        Assert::IsFalse(HasPersistentlyUniformExtremeOutput(black, state));
        Assert::IsFalse(HasPersistentlyUniformExtremeOutput(white, state));
        Assert::AreEqual(1, state.consecutiveSamples);
        for (int sample = 0; sample < 20; ++sample)
            Assert::IsFalse(HasPersistentlyUniformExtremeOutput(sample % 2 ? white : black, state));
        Assert::IsFalse(HasPersistentlyUniformExtremeOutput({true, 0.25f, 0.75f, 0.5f}, state));
        Assert::AreEqual(0, state.consecutiveSamples);
    }

    TEST_METHOD(UniformMidColoursAndStaticDetailRemainUntouched)
    {
        const VisualOutputSample samples[] = {
            {true, 0.5f, 0.5f, 0.0f}, // Uniform grey.
            {true, 0.0f, 1.0f, 0.0f}, // Uniform saturated colour, e.g. red.
            {true, 0.0f, 1.0f, 1.0f}, // Static black/white pattern.
            {true, 0.0f, 4.0f / 255.0f, 4.0f / 255.0f}, // Dim detail.
            {true, 245.0f / 255.0f, 1.0f, 10.0f / 255.0f} // Bright detail.
        };
        for (const auto& sample : samples)
        {
            VisualRecoveryState state;
            for (int repeat = 0; repeat < 10; ++repeat)
                Assert::IsFalse(HasPersistentlyUniformExtremeOutput(sample, state));
            Assert::AreEqual(0, state.consecutiveSamples);
        }
    }

    TEST_METHOD(ExtremeThresholdsIncludeOneLevelOfChannelVariation)
    {
        Assert::IsTrue(ClassifyUniformExtremeOutput({true, 3.0f / 255.0f, 4.0f / 255.0f, 1.0f / 255.0f}) == VisualOutputExtreme::Dark);
        Assert::IsTrue(ClassifyUniformExtremeOutput({true, 245.0f / 255.0f, 246.0f / 255.0f, 1.0f / 255.0f}) == VisualOutputExtreme::Bright);
        Assert::IsTrue(ClassifyUniformExtremeOutput({true, 5.0f / 255.0f, 5.0f / 255.0f, 0.0f}) == VisualOutputExtreme::None);
        Assert::IsTrue(ClassifyUniformExtremeOutput({true, 244.0f / 255.0f, 244.0f / 255.0f, 0.0f}) == VisualOutputExtreme::None);
    }

    TEST_METHOD(UnavailableAndInvalidMeasurementsBreakRecoverySequence)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const VisualOutputSample samples[] = {
            {}, {false, 1.0f, 1.0f, 0.0f},
            {true, nan, 0.0f, 0.0f}, {true, 0.0f, nan, 0.0f}, {true, 0.0f, 0.0f, nan},
            {true, inf, 1.0f, 0.0f}, {true, 0.0f, inf, 0.0f}, {true, 0.0f, 0.0f, inf},
            {true, -0.1f, 0.0f, 0.0f}, {true, 1.0f, 1.1f, 0.0f},
            {true, 0.1f, 0.0f, 0.0f}, {true, 0.0f, 0.0f, -0.1f}
        };
        for (const auto& sample : samples)
        {
            VisualRecoveryState state{VisualOutputExtreme::Dark, 2};
            Assert::IsFalse(HasPersistentlyUniformExtremeOutput(sample, state));
            Assert::AreEqual(0, state.consecutiveSamples);
            Assert::IsTrue(state.lastExtreme == VisualOutputExtreme::None);
            Assert::IsFalse(HasPersistentlyUniformExtremeOutput({true, 0.0f, 0.0f, 0.0f}, state));
            Assert::AreEqual(1, state.consecutiveSamples);
        }
    }
};
}
