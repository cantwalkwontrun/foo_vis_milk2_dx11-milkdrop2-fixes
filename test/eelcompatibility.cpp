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
    TEST_METHOD(ApproximateMathsRetainsSuppliedDigitsAndNeutralisesMissingAdditiveCoefficients)
    {
        const auto known=[](const char* name){return std::string_view(name)=="sin" || std::string_view(name)=="cos";};
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("rot=1-.*3;",known)=="rot=1-.3;");
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("rot=1+.*sin(time);",known)=="rot=1+0*sin(time);");
        for (const char* source:{"q1=1+.3;", "q1=1+. *3;", "q1=1+.*3.5;", "q1=1+.*thing(2);", "q1=1; //+.*sin(x)"})
            Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths(source,known)==source);
    }
    TEST_METHOD(ApproximateEmptyValuesHoldOnlyATerminalBareAssignment)
    {
        const auto known=[](const char*){return false;};
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("spin =",known)=="spin = spin;");
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("q1=3;xrot = // unfinished\1",known)=="q1=3;xrot = xrot; // unfinished\1");
        for (const char* source:{"spin+=", "spin=2+", "q1=(3;spin=", "q1=spin=", "spin(1)=", "q1=3; //spin="})
            Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths(source,known)==source);
    }
    TEST_METHOD(ApproximateProductsRequireAnEarlierAssignmentAndPreserveIntrinsicCalls)
    {
        const auto known=[](const char* name){return std::string_view(name)=="sin";};
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("mx=.5;mx=mx(mx*.98);",known)=="mx=.5;mx=mx*(mx*.98);");
        for (const char* source:{"q1=unknown(2);", "mx=mx(2);", "sin=2;q1=sin(2);", "mx=2; // mx(2)"})
            Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths(source,known)==source);
    }
    TEST_METHOD(ApproximateClosingRepairsKeepBalancedGroupsAndDeclineAmbiguousOpenings)
    {
        const auto known=[](const char*){return false;};
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("rot=1)-rad);",known)=="rot=1-rad;");
        for (const char* source:{"q1=sin((2));", "q1=(2;", "q1=[2);", "q1=2];", "q1=\"a)\";", "function f(x)(x);q1=1);"})
            Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths(source,known)==source);
    }
    TEST_METHOD(ApproximateComparisonUsesAnExplicitZeroBoundOnlyForTheObservedBrokenStatement)
    {
        const auto known=[](const char*){return true;};
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("my = if(above(x*.3)),.2, my);",known)=="my=if(above(x*.3,0),.2,my);");
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("x=.5;my=if(above(x*.3)),.2,my);y=my;",known)=="x=.5;my=if(above(x*.3,0),.2,my);y=my;");
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths("my=if(above(x*.3),.2,my);",known)=="my=if(above(x*.3),.2,my);");
    }
    TEST_METHOD(ApproximateMathsBoundsInputAndEditCounts)
    {
        const auto known=[](const char*){return false;};
        const std::string large=std::string(65537,' ')+"q1=1);";
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths(large,known)==large);
        std::string many;for (int i=0;i<65;++i) many+="q1=1);";
        Assert::IsTrue(PresetEelCompat::ApproximateDamagedMaths(many,known)==many);
    }
    TEST_METHOD(PartialStatementsPreserveNestedBodiesAndAuthoredOrder)
    {
        const std::string source="q1=1;\1q2=if(q1,(q3=2;q3+1),0);bad=3+.*2;zoom=1;";
        const auto parts=PresetEelCompat::PartialRecoveryStatements(source);
        Assert::AreEqual(size_t(4),parts.size());
        Assert::IsTrue(parts[1]=="q2=if(q1,(q3=2;q3+1),0);");
        Assert::IsTrue(parts[2]=="bad=3+.*2;");Assert::IsTrue(parts[3]=="zoom=1;");
    }
    TEST_METHOD(PartialStatementsKeepExtraClosersInsideTheirOwnStatement)
    {
        const auto parts=PresetEelCompat::PartialRecoveryStatements("q1=2;rot=1)-rad);q2=3;");
        Assert::AreEqual(size_t(3),parts.size());Assert::IsTrue(parts[1]=="rot=1)-rad);");
    }
    TEST_METHOD(PartialStatementsRejectAmbiguousUnclosedGroupsAndDeclarations)
    {
        for (const char* text:{"q1=2;bad=if(x,3;q2=4;", "q1=2;bad=([)];q2=4;",
                              "q1=2;function f(x)(x);bad=3+;", "q1=\"text\";bad=3+;",
                              "q1=2;/* unfinished", "q1=1;"})
            Assert::IsTrue(PresetEelCompat::PartialRecoveryStatements(text).empty());
    }
    TEST_METHOD(PartialStatementsBoundRetriesAndIgnoreEmptyCommentedRows)
    {
        const auto parts=PresetEelCompat::PartialRecoveryStatements(";; // bad=2+;\1q1=1;/*x;y;*/;q2=2;");
        Assert::AreEqual(size_t(2),parts.size());
        std::string source;for (int i=0;i<32;++i) source+="q1=1;";
        Assert::AreEqual(size_t(32),PresetEelCompat::PartialRecoveryStatements(source).size());
        Assert::IsTrue(PresetEelCompat::PartialRecoveryStatements(source+"q1=2;").empty());
        Assert::IsTrue(PresetEelCompat::PartialRecoveryStatements(std::string(65537,' ')+"q1=1;q2=2;").empty());
    }
    TEST_METHOD(RandomWrapperRepairsRequireTheCorroboratedWholeStatement)
    {
        for (const auto& entry:PresetEelCompat::Catalogue::RandomWrapper)
        {
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(entry.broken)==entry.complete);
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(entry.complete)==entry.complete);
            auto different=std::string(entry.broken);different.insert(0,"other_");
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(different)==different);
        }
        const char* unknown="q1=int(rand(4))(10);";
        Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(unknown)==unknown);
    }
    TEST_METHOD(KnownTruncationsRequireExactLinesAndAnAssignmentBoundary)
    {
        for (const auto& item:PresetEelCompat::Catalogue::Truncated)
        {
            const std::string source=std::string(item.broken)+"\1q64=1;";
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(source)==std::string(item.complete)+"\1q64=1;");
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(item.complete)==item.complete);
            const auto continued=std::string(item.broken)+"\1 3;";
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(continued)==continued);
            auto changed=source;changed.insert(0,"different_");
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(changed)==changed);
        }
    }
    TEST_METHOD(SplitDecimalsAreJoinedOnlyInsideCorroboratedWholeStatements)
    {
        for (const auto statement:PresetEelCompat::Catalogue::SplitDecimalZoom)
        {
            std::string source(statement);const auto position=source.find("0.52");Assert::IsTrue(position!=std::string::npos);
            source.insert(position+3," \1");
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(source)==statement);
            auto changed=source;changed.replace(changed.find("zoom"),4,"other");
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(changed)==changed);
        }
        const std::string arbitrary="q1=0.5\1 2*sin(time);";
        Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(arbitrary)==arbitrary);
    }
    TEST_METHOD(CompleteAndUnrecognisedExpressionsStayOutsideTheCatalogue)
    {
        for (const char* source:{"q1=if(above(bass,mid),", "q1=2; // comments", "q1=\"state=if(abo\";", "function f(x)(x);"})
            Assert::IsTrue(PresetEelCompat::RecoverKnownLegacyStatements(source)==source);
    }
    TEST_METHOD(PartialRecoveryKeepsOnlyACompleteBalancedPrefix)
    {
        Assert::IsTrue(PresetEelCompat::RecoverIncompleteTrailingAssignment("q1=3;spin =\1")=="q1=3;");
        Assert::IsTrue(PresetEelCompat::RecoverIncompleteTrailingAssignment("spin =")=="0;");
        for (const char* source:{"q1=(3;spin=", "q1=3;spin=2+", "q1=3;spin=2;", "q1=spin=", "q1=if(x,spin=", "q1=3;spin+=", "q1=3;spin=="})
            Assert::IsTrue(PresetEelCompat::RecoverIncompleteTrailingAssignment(source)==source);
    }
    TEST_METHOD(PartialRecoveryDoesNotRemoveQuotedOrDeclaredCode)
    {
        for (const char* source:{"q1=\"text\";spin=", "function f(x)(x);spin=", "spin(1)=", "q1=3;spin"})
            Assert::IsTrue(PresetEelCompat::RecoverIncompleteTrailingAssignment(source)==source);
    }
    TEST_METHOD(NumericCoefficientsKeepCallsAndUnknownFunctionsDistinct)
    {
        Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax("q1=-2(x+.5);q2=2L(.25);")=="q1=-2*(x+.5);q2=2*(.25);");
        for (const char* source:{"q1=sin(2);", "q1=int(rand(4))(10);", "q1=thing(2);", "q1=2(x;", "q1=2\1(x);", "q1=2 /* note */ (x);"})
            Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax(source)==source);
    }
    TEST_METHOD(OnlySameLineRepeatedSeparatorsJoinBinaryContinuations)
    {
        Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax("q1=2;; *3;")=="q1=2 *3;");
        Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax("loop(2,q1=2;; *3;);")=="loop(2,q1=2 *3;);");
        for (const char* source:{"q1=2; *3;", "q1=2;;\1 *3;", "q1=2;; +3;", "q1=2;; /* choice */ *3;"})
            Assert::IsTrue(PresetEelCompat::RecoverArithmeticPunctuation(source)==source);
    }
    TEST_METHOD(IsolatedUnmatchedClosingGroupsDoNotInventOpeningGroups)
    {
        Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax("q1=2; );q2=3;")=="q1=2; q2=3;");
        for (const char* source:{"q1=if(1,2,3);", "q1=2);", "q1=(2;", "q1=[2);"})
            Assert::IsTrue(PresetEelCompat::RecoverArithmeticPunctuation(source)==source);
    }
    TEST_METHOD(OnlyTrailingCommentOnlySerialisedCodeKeysAreIgnored)
    {
        const std::string source="q1=2;\1per_frame_1=// copied key\1";
        const auto result=PresetEelCompat::RecoverLegacyExpressionSyntax(source);
        Assert::IsTrue(result.substr(0,6)=="q1=2;\1");
        Assert::IsTrue(result.find("per_frame_1")==std::string::npos);
        for (const char* original:{"q1=2;\1spin=// unfinished", "q1=2;\1per_frame_1=3;", "q1=2;\1per_frame_1=", "per_frame_1=// note\1 3;"})
            Assert::IsTrue(PresetEelCompat::RecoverImportAndCommentArtefacts(original)==original);
    }
    TEST_METHOD(OrphanedCommentContinuationsRequireCompleteTopLevelStatements)
    {
        const std::string source="q1=2;\1// q2=sin(time)\1*sin(time);\1q3=4;";
        const auto result=PresetEelCompat::RecoverLegacyExpressionSyntax(source);
        Assert::IsTrue(result.find("\1*sin(time);")==std::string::npos);
        Assert::IsTrue(result.find("// q2=sin(time)")!=std::string::npos && result.find("q3=4;")!=std::string::npos);
        for (const char* original:{"q1=2\1// q2=3\1*3;", "q1=2;\1// prose\1*3;", "q1=2;\1// q2=3;\1*3;",
            "q1=2;\1// q2=3\1*3;q3=4;", "loop(2,\1// q2=3\1*3;);"})
            Assert::IsTrue(PresetEelCompat::RecoverImportAndCommentArtefacts(original)==original);
    }
    TEST_METHOD(DottedVariablesRetainTheirIdentityWithoutBecomingArithmetic)
    {
        auto none=[](const char*) { return false; };
        const auto result=PresetEelCompat::RewriteLegacyVariables("node.value=3;q1=node.value;q2=time0.5;",none);
        Assert::IsTrue(result=="__md2_legacy_var_6e6f64652e76616c7565=3;q1=__md2_legacy_var_6e6f64652e76616c7565;q2=__md2_legacy_var_74696d65302e35;");
        for (const char* original:{"q1=.5;", "q1=node.call(2);", "q1=node.;", "q1=node..value;"})
            Assert::IsTrue(PresetEelCompat::RewriteLegacyVariables(original,none)==original);
    }
    TEST_METHOD(RecoveryKeepsAmbiguousMathButAcceptsEmptyStatementsAndNumericTypos)
    {
        Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax(";\1;q1=.-2.5; q2=.+.25; q3=.99L;;") ==
            "\1q1=-2.5; q2=+.25; q3=.99;");
        for (const char* source : {"q1=time0.5;", "q1=(.5\1 2*sin(time));", "q1=if(x,", "q1=1+;", "q1=\".-2\";"})
            Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax(source) == source);
    }
    TEST_METHOD(NumericRecoveryPreservesCommentsDecimalsAndIdentifiers)
    {
        const auto source = "q1=.5; // .-9\1q2=1e-3f; q3=9.0L; q4=1abc; q5=variableL;";
        Assert::IsTrue(PresetEelCompat::RecoverLegacyExpressionSyntax(source) ==
            "q1=.5; // .-9\1q2=1e-3; q3=9.0; q4=1abc; q5=variableL;");
    }
    TEST_METHOD(LegacyVariablesRemainDistinctFromIntrinsicCallsAndComments)
    {
        auto predicate = [](const char* name) { return PresetEelCompat::Syntax::Equal(name,"sin"); };
        Assert::IsTrue(PresetEelCompat::RewriteLegacyVariables("sin=2;q1=SIN+sin(0); // sin\1q2=1;",predicate) ==
            "__md2_legacy_var_73696e=2;q1=__md2_legacy_var_73696e+sin(0); // sin\1q2=1;");
        Assert::IsTrue(PresetEelCompat::RewriteLegacyVariables("\xc3\xa9=3;q1=\xc3\xa9;",predicate) ==
            "__md2_legacy_var_c3a9=3;q1=__md2_legacy_var_c3a9;");
        for (const char* source : {"q1=sin /* call */ (0);", "q1=\"sin\";", "function sin(x)(x);q1=sin(0);"})
            Assert::IsTrue(PresetEelCompat::RewriteLegacyVariables(source,predicate) == source);
    }
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
