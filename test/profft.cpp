/*
 * profft.cpp - Regressions for the PRO spectrum processing contract.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <vis_milk2/profft.h>
#include <CppUnitTest.h>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
TEST_CLASS(ProFFTTest)
{
public:
    TEST_METHOD(NormalisesGatesAndSquaresBeforeAttack)
    {
        ProFFT::Row input{}, history{};
        input[10] = 10.0f;
        input[100] = 2.0f;
        input[200] = 1.99f;
        ProFFT::Update(input.data(), input.size(), history, 0.5f, 0.7f);
        Assert::AreEqual(0.75f, history[10], 0.000001f);
        Assert::AreEqual(0.03f, history[100], 0.000001f);
        // Ascending peak spreading leaves a negligible tail from earlier bins.
        Assert::AreEqual(0.0f, history[200], 1e-20f);
    }

    TEST_METHOD(SpreadsPeaksInTheObservedAscendingOrder)
    {
        ProFFT::Row input{}, history{};
        input[0] = 1.0f;
        ProFFT::Update(input.data(), input.size(), history, 1.0f, 1.0f);
        Assert::AreEqual(1.0f, history[0]);
        Assert::AreEqual(0.75f, history[1]);
        Assert::AreEqual(0.5625f, history[2]);
        Assert::AreEqual(0.421875f, history[3]);
        Assert::AreEqual(0.31640625f, history[4]);
    }

    TEST_METHOD(UsesPreviousRightNeighboursAndDifferentFallCoefficient)
    {
        ProFFT::Row input{}, history{};
        history[511] = 0.8f;
        ProFFT::Update(input.data(), input.size(), history, 1.0f, 0.5f);
        Assert::AreEqual(0.6f, history[510], 0.000001f);
        Assert::AreEqual(0.76f, history[511], 0.000001f);
        Assert::AreEqual(0.0f, history[0]);
    }

    TEST_METHOD(SilenceAndInvalidInputCannotPoisonTheHistory)
    {
        ProFFT::Row input{}, history{};
        ProFFT::Update(input.data(), input.size(), history, 0.5f, 0.7f);
        for (float value : history) Assert::AreEqual(0.0f, value);
        input[1] = (std::numeric_limits<float>::infinity)();
        input[2] = (std::numeric_limits<float>::quiet_NaN)();
        input[3] = -1.0f;
        history[5] = (std::numeric_limits<float>::quiet_NaN)();
        ProFFT::Update(input.data(), input.size(), history, input[2], input[1]);
        for (float value : history) Assert::AreEqual(0.0f, value);
        history.fill(1.0f);
        ProFFT::Update(nullptr, 0, history, 1, 1);
        for (float value : history) Assert::AreEqual(0.0f, value);
    }

    TEST_METHOD(ClampsMalformedPresetControlsToFiniteBounds)
    {
        Assert::AreEqual(0.0f, ProFFT::Control(-5, 0.5f));
        Assert::AreEqual(1.0f, ProFFT::Control(5, 0.5f));
        Assert::AreEqual(0.5f, ProFFT::Control((std::numeric_limits<float>::quiet_NaN)(), 0.5f));
        Assert::AreEqual(0.001f, ProFFT::Control(0.001f, 0.5f));
    }
};
}
