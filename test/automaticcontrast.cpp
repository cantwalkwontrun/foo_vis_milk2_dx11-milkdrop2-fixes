/* Language: en-GB. SPDX-License-Identifier: MPL-2.0 */
#include "pch.h"
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <vis_milk2/contrastrecovery.h>
#include <vis_milk2/contrastpreferences.h>
#include <CppUnitTest.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace MilkDrop2
{
TEST_CLASS(AutomaticContrastTest)
{
    static ContrastImageSample Scene(float phase, int kind = 0, float exponent = 1)
    {
        ContrastImageAccumulator pixels;
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 64; ++x)
            {
                float raw = (7 + 5 * std::sin(x*.23f + phase)) / 255;
                if (kind == 1) raw = (7 + .5f * std::sin(x*.23f + phase)) / 255;
                if (kind == 2) raw = x == 31 ? .8f : 0;
                if (kind == 3) raw = .3f + .2f * std::sin(x*.23f + phase);
                if (kind == 4)
                {
                    uint32_t seed = x + 64*y + static_cast<uint32_t>(phase*1000)*4096 + 1;
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    raw = (2 + seed % 13) / 255.0f;
                }
                const auto value = static_cast<uint8_t>(std::lround(std::pow(raw, exponent)*255));
                pixels.Add(value,value,value,exponent,x/8 + 8*(y/8));
            }
        return pixels.Finish();
    }
  public:
    TEST_METHOD(UnrecognisedMovingDarkDetailRecoversGentlyAndRestoresBrightOutput)
    {
        AutomaticContrastRecovery policy;
        policy.Reset(0,true);
        for(int i=0;i<20;++i){policy.Observe(Scene(i*.9f));policy.Advance(i*.5f);}
        Assert::AreEqual(.7f,policy.Exponent(),.00001f);
        for(int i=20;i<30;++i){policy.Observe(Scene(i*.9f,3));policy.Advance(i*.5f);}
        Assert::AreEqual(1.0f,policy.Exponent());
    }

    TEST_METHOD(UnrecognisedStaticLowContrastAndSparseArtStayUnchanged)
    {
        for(int kind=0;kind<3;++kind)
        {
            AutomaticContrastRecovery policy;
            policy.Reset(0,true);
            for(int i=0;i<40;++i)
            {
                policy.Observe(Scene(kind==0 ? 0 : i*.9f,kind));
                policy.Advance(i*.5f);
            }
            Assert::AreEqual(1.0f,policy.Exponent());
        }
    }

    TEST_METHOD(UnrecognisedHighlightsAndUnavailableSamplesPreventRecovery)
    {
        AutomaticContrastRecovery policy;
        policy.Reset(0,true);
        for(int i=0;i<30;++i)
        {
            auto sample=Scene(i*.9f);
            sample.peak=.95f;
            policy.Observe(sample);policy.Advance(i*.5f);
        }
        Assert::AreEqual(1.0f,policy.Exponent());
        policy.Reset(0,true);
        for(int i=0;i<30;++i)
        {
            policy.Observe(Scene(i*.9f));
            if(i%4==0) policy.Observe({});
            policy.Advance(i*.5f);
        }
        Assert::AreEqual(1.0f,policy.Exponent());
    }

    TEST_METHOD(UnrecognisedRecoveryRemainsStableWhileAnalysingCorrectedImages)
    {
        AutomaticContrastRecovery policy;
        policy.Reset(0,true);
        for(int i=0;i<100;++i)
        {
            policy.Observe(Scene(i*.9f,0,policy.Exponent()));policy.Advance(i*.5f);
            if(i>20) Assert::AreEqual(.7f,policy.Exponent(),.00001f);
        }
    }

    TEST_METHOD(UnrecognisedAnimatedGrainDoesNotQualifyAsCoherentDetail)
    {
        AutomaticContrastRecovery policy;
        policy.Reset(0,true);
        for(int i=0;i<50;++i){policy.Observe(Scene(i*.9f,4));policy.Advance(i*.5f);}
        Assert::AreEqual(1.0f,policy.Exponent());
    }

    TEST_METHOD(ContrastSettlingIsRelativeToLoadAndRejectsInvalidTime)
    {
        AutomaticContrastRecovery policy;
        policy.Reset(10);
        Assert::IsFalse(policy.Ready(9));
        Assert::IsFalse(policy.Ready(11.99f));
        Assert::IsTrue(policy.Ready(12));
        Assert::IsFalse(policy.Ready(std::numeric_limits<float>::quiet_NaN()));
        policy.Reset(20);
        Assert::IsFalse(policy.Ready(21));
        Assert::IsTrue(policy.Ready(22));
    }

    TEST_METHOD(RequiresConsecutiveDetailAndSmoothlyRestoresBrightScenes)
    {
        AutomaticContrastRecovery policy;
        policy.Reset(0);
        const ContrastImageSample dim{true,.02f,.3f,.2f,.0002f};
        policy.Observe(dim); policy.Observe(dim); policy.Advance(.25f);
        Assert::AreEqual(1.0f,policy.Exponent());
        policy.Observe({}); policy.Observe(dim); policy.Observe(dim); policy.Advance(.5f);
        Assert::AreEqual(1.0f,policy.Exponent());
        policy.Observe(dim); policy.Advance(.75f);
        Assert::IsTrue(policy.Exponent()>.35f && policy.Exponent()<1);
        policy.Advance(1);
        Assert::AreEqual(.35f,policy.Exponent(),.00001f);
        const ContrastImageSample bright{true,.2f,.8f,.6f,.01f};
        policy.Observe(bright); policy.Observe(bright); policy.Advance(1.25f);
        Assert::AreEqual(.35f,policy.Exponent(),.00001f);
        policy.Observe(bright); policy.Advance(1.5f);
        Assert::IsTrue(policy.Exponent()>.35f && policy.Exponent()<1);
        policy.Advance(1.75f);
        Assert::AreEqual(1.0f,policy.Exponent());
    }

    TEST_METHOD(UnknownInvalidUniformAndHysteresisBandDoNotEnrolOrOscillate)
    {
        AutomaticContrastRecovery policy;
        policy.Reset(0);
        for (const ContrastImageSample sample : {ContrastImageSample{}, {true,0,0,0,0},
            {true,1,1,0,0}, {true,.02f,.3f,0,0},
            {true,std::numeric_limits<float>::quiet_NaN(),.3f,.2f,.001f}})
            for (int i=0;i<4;++i) policy.Observe(sample);
        policy.Advance(.25f);
        Assert::AreEqual(1.0f,policy.Exponent());
        for (int i=0;i<3;++i) policy.Observe({true,.02f,.3f,.2f,.0002f});
        policy.Advance(.5f); policy.Advance(.75f);
        for (int i=0;i<50;++i)
        {
            policy.Observe({true,.09f,.5f,.25f,.002f}); policy.Advance(1.0f+i*.1f);
        }
        Assert::AreEqual(.35f,policy.Exponent(),.00001f);
        policy.Advance(-1); policy.Advance(std::numeric_limits<float>::quiet_NaN());
        Assert::AreEqual(.35f,policy.Exponent(),.00001f);
        policy.Reset(0);
        Assert::AreEqual(1.0f,policy.Exponent());
    }

    TEST_METHOD(InvertsOurOwnToneCurveSoSuccessfulBoostDoesNotSwitchItselfOff)
    {
        ContrastImageAccumulator original,displayed;
        for (const std::array<uint8_t,3> pixel : {std::array<uint8_t,3>{2,4,8}, {20,30,40}})
        {
            original.Add(pixel[0],pixel[1],pixel[2],1);
            const float peak=(std::max)({pixel[0],pixel[1],pixel[2]})/255.0f;
            const float gain=std::pow(peak,.35f-1);
            displayed.Add(static_cast<uint8_t>(std::lround(pixel[0]*gain)),
                static_cast<uint8_t>(std::lround(pixel[1]*gain)),
                static_cast<uint8_t>(std::lround(pixel[2]*gain)),.35f);
        }
        const auto before=original.Finish(),after=displayed.Finish();
        Assert::AreEqual(before.mean,after.mean,1.5f/255);
        Assert::AreEqual(before.peak,after.peak,1.5f/255);
        AutomaticContrastRecovery policy;
        policy.Reset(0);
        for(int i=0;i<100;++i){policy.Observe(after);policy.Advance(i*.25f);}
        Assert::AreEqual(.35f,policy.Exponent(),.00001f);
        ContrastImageAccumulator invalid;
        invalid.Add(1,2,3,std::numeric_limits<float>::quiet_NaN());
        Assert::IsFalse(invalid.Finish().available);
    }

    TEST_METHOD(ContentHashUsesExactBytesAndPreservesTheImportStreamPosition)
    {
        wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathW(MAX_PATH,folder)>0);
        Assert::IsTrue(GetTempFileNameW(folder,L"mdc",0,path)!=0);
        struct Remove{const wchar_t* path;~Remove(){DeleteFileW(path);}} cleanup{path};
        FILE* file=nullptr;
        Assert::AreEqual(0,_wfopen_s(&file,path,L"w+b"));
        const auto close=[](FILE* f){if(f)fclose(f);};
        std::unique_ptr<FILE,decltype(close)> owned(file,close);
        Assert::AreEqual(size_t(3),fwrite("abc",1,3,file));
        Assert::AreEqual(0,_fseeki64(file,1,SEEK_SET));
        const auto digest=PresetContrast::ContentDigest(file);
        Assert::IsTrue(digest=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        Assert::AreEqual(__int64(1),_ftelli64(file));
        Assert::AreEqual(int('b'),fgetc(file));
        Assert::AreEqual(0,_fseeki64(file,0,SEEK_END));
        Assert::AreEqual(size_t(1),fwrite("x",1,1,file));
        Assert::AreEqual(0,fflush(file));
        Assert::IsTrue(PresetContrast::ContentDigest(file)!=digest);
        Assert::IsTrue(PresetContrast::Enabled(nullptr,digest));
        Assert::IsTrue(PresetContrast::UseDiscovery(digest));
        Assert::IsFalse(PresetContrast::ValidDigest(std::string(64,'/')));
    }

    TEST_METHOD(PersistentChoicesOverrideKnownProfilesAndStayIsolatedByContent)
    {
        wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathW(MAX_PATH,folder)>0);
        Assert::IsTrue(GetTempFileNameW(folder,L"mdc",0,path)!=0);
        struct Remove{const wchar_t* path;~Remove(){DeleteFileW(path);}} cleanup{path};
        const char* known="0ca9b4631cb941efaef1c17e6839c6c016e102cc2080f97252d066924b491e3d";
        const std::string unknown(64,'a'),other(64,'b');
        Assert::IsTrue(PresetContrast::Enabled(path,known));
        Assert::IsTrue(PresetContrast::SaveChoice(path,known,false));
        Assert::IsFalse(PresetContrast::Enabled(path,known));
        Assert::IsTrue(PresetContrast::Enabled(path,unknown));
        Assert::IsTrue(PresetContrast::SaveChoice(path,unknown,false));
        Assert::IsFalse(PresetContrast::Enabled(path,unknown));
        Assert::IsTrue(PresetContrast::SaveChoice(path,unknown,true));
        Assert::IsTrue(PresetContrast::Enabled(path,unknown));
        Assert::IsTrue(PresetContrast::Enabled(path,other));
        Assert::IsTrue(PresetContrast::UseDiscovery(unknown));
        Assert::IsFalse(PresetContrast::UseDiscovery(known));
        Assert::IsFalse(PresetContrast::SaveChoice(L"relative.ini",unknown,true));
        Assert::IsFalse(PresetContrast::SaveChoice(path,"not-a-digest",true));
    }
};
}
