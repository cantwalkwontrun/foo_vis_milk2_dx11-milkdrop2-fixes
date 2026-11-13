// Compatibility syntax and audio-band regressions. Language: en-GB.
// SPDX-License-Identifier: MPL-2.0
#include <vis_milk2/eelcompat.h>
#include <vis_milk2/eelsound.h>
#include <CppUnitTest.h>
#include <limits>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace MilkDrop2
{
TEST_CLASS(EelCompatibilityTest)
{
public:
    TEST_METHOD(RecoversOnlyClearTopLevelAssignmentBoundaries)
    {
        const auto result = PresetEelCompat::RecoverMissingStatementSeparators("zoom=(.7+x-.7)|(.7+y+.5)\1rot=.25;\1zoom=1;");
        Assert::IsTrue(result == "zoom=(.7+x-.7)|(.7+y+.5)\1;rot=.25;\1zoom=1;");
    }
    TEST_METHOD(SeparatorRecoveryRespectsCommentAndBlankLines)
    {
        const auto result = PresetEelCompat::RecoverMissingStatementSeparators("q1=1 // comment\1// blank\1\1q2=2\r\nq3=3;");
        Assert::IsTrue(result == "q1=1 // comment\1// blank\1\1;q2=2\r\n;q3=3;");
    }
    TEST_METHOD(AmbiguousAndContinuousExpressionsStayUnchanged)
    {
        for (const char* source : {"q1=1;\1q2=2;", "q1=1+\1q2=2;", "q1=(1\1q2=2;",
            "q1=(1+\1q2=2);", "q1=1?\1q2=2:3;", "function f(x)(x);\1q1=1\1q2=2;",
            "q1=1 /* unfinished\1q2=2;", "q1=\"text\"\1q2=2;"})
            Assert::IsTrue(PresetEelCompat::RecoverMissingStatementSeparators(source) == source);
    }
    TEST_METHOD(OnlyActualSoundCallsAreLowered)
    {
        Assert::IsTrue(PresetEelCompat::RewriteSoundCalls("sound=7;q1=sound+SOUND /* call */ (15,22);") ==
            "sound=7;q1=sound+__md2_sound_query /* call */ (15,22);");
        for (const char* source : {"// sound(1,2)\1q1=1;", "q1=\"sound(1,2)\";", "obj.sound=3;resound(1,2);",
            "function sound(a,b)(a+b);q1=sound(1,2);"})
            Assert::IsTrue(PresetEelCompat::RewriteSoundCalls(source) == source);
    }
    TEST_METHOD(AudioBandsUseAnIntegralRatherThanAnAverage)
    {
        PresetSound::Spectrum spectrum; std::array<float,512> values; values.fill(2);
        spectrum.Update(values.data(), values.size(), 44100);
        Assert::AreEqual(14.0, spectrum.Sum(15,22), 1e-9);
        Assert::AreEqual(2.5, spectrum.Sum(1.25,2.5), 1e-9);
        Assert::AreEqual(200.0, spectrum.Sum(100,200), 1e-9);
    }
    TEST_METHOD(AudioBandsRejectBadInputsAndClearUnavailableData)
    {
        PresetSound::Spectrum spectrum; std::array<float,512> values; values.fill(2);
        spectrum.Update(values.data(), values.size(), 44100);
        Assert::AreEqual(0.0, spectrum.Sum(22,15));
        Assert::AreEqual(0.0, spectrum.Sum(15,15));
        Assert::AreEqual(0.0, spectrum.Sum(std::numeric_limits<double>::quiet_NaN(),22));
        Assert::AreEqual(0.0, spectrum.Sum(1,std::numeric_limits<double>::infinity()));
        Assert::AreEqual(2.0, spectrum.Sum(-1000,1), 1e-9);
        Assert::AreEqual(4410.0, spectrum.Sum(-1e300,1e300), 1e-8);
        values.fill(std::numeric_limits<float>::infinity());
        spectrum.Update(values.data(), values.size(), 44100);
        Assert::AreEqual(0.0, spectrum.Sum(0,2205));
        spectrum.Update(nullptr,0,44100);
        Assert::AreEqual(0.0, spectrum.Sum(0,2205));
    }
    TEST_METHOD(AudioBandCoordinatesTrackFrequencyAcrossSampleRates)
    {
        for (double rate : {44100.0,48000.0,96000.0,192000.0})
        {
            std::array<float,512> values{};
            values[static_cast<size_t>(3000.0 * 1024 / rate)] = 10;
            PresetSound::Spectrum spectrum; spectrum.Update(values.data(),values.size(),rate);
            Assert::AreEqual(10.0 * 44100 / (20 * 512),spectrum.Sum(280,320),1e-8); // 2,800..3,200 Hz.
            Assert::AreEqual(0.0,spectrum.Sum(0,10));
        }
    }
};
}
