/*
 * shadercompatibility.cpp - Tests for legacy HLSL global initialiser binding.
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#include <vis_milk2/framework.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vis_milk2/plugin.h>
#include <vis_milk2/shadercompat.h>
#include <vis_milk2/eelcompat.h>
#include <vis_milk2/runtimesafetykey.h>
#include <CppUnitTest.h>

extern CPlugin g_plugin;
template<class Tag, typename Tag::type Member> struct BandTestAccess { friend typename Tag::type bandMember(Tag) { return Member; } };
struct AnalyseBandsTag { using type=void(CPluginShell::*)(float*,float*); friend type bandMember(AnalyseBandsTag); };
template struct BandTestAccess<AnalyseBandsTag,&CPluginShell::AnalyzeNewSound>;
struct SoundBandsTag { using type=td_soundinfo CPluginShell::*; friend type bandMember(SoundBandsTag); };
template struct BandTestAccess<SoundBandsTag,&CPluginShell::m_sound>;
struct SkipCompTag { using type=int CPluginShell::*; friend type bandMember(SkipCompTag); };
template struct BandTestAccess<SkipCompTag,&CPluginShell::m_skip_comp_shaders>;
using Microsoft::WRL::ComPtr;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
TEST_CLASS(RuntimeSafetyKeyTest)
{
  public:
    TEST_METHOD(DistinguishesSeparateRootsButNormalisesWindowsCaseAndSeparators)
    {
        const auto first = RuntimeSafetyKey::Make(L"C:\\Presets\\Pack A\\example.milk", 100, 200);
        const auto second = RuntimeSafetyKey::Make(L"C:\\Presets\\Pack B\\example.milk", 100, 200);
        Assert::IsFalse(first.empty());
        Assert::IsTrue(first != second, L"Failures of one file must not hide an unrelated same-named file.");
        Assert::IsTrue(first == RuntimeSafetyKey::Make(L"c:/presets/pack a/EXAMPLE.MILK", 100, 200));
        Assert::IsTrue(RuntimeSafetyKey::IsCurrent(first));
        Assert::IsTrue(RuntimeSafetyKey::IsCurrent(
            RuntimeSafetyKey::Make(L"\\\\server\\share\\example.milk", 100, 200)));
    }

    TEST_METHOD(ChangedFileMetadataAndCompatibilityRevisionCauseCacheMisses)
    {
        const auto original = RuntimeSafetyKey::Make(L"C:\\Presets\\example.milk", 100, 200);
        Assert::IsTrue(original != RuntimeSafetyKey::Make(L"C:\\Presets\\example.milk", 101, 200));
        Assert::IsTrue(original != RuntimeSafetyKey::Make(L"C:\\Presets\\example.milk", 100, 201));
        const unsigned next = RuntimeSafetyKey::CompatibilityRevision + 1;
        const auto updated = RuntimeSafetyKey::Make(L"C:\\Presets\\example.milk", 100, 200, next);
        Assert::IsTrue(original != updated);
        Assert::IsFalse(RuntimeSafetyKey::IsCurrent(original, next));
        Assert::IsFalse(RuntimeSafetyKey::IsCurrent(updated));
        Assert::IsTrue(RuntimeSafetyKey::IsCurrent(updated, next));
    }

    TEST_METHOD(RejectsAmbiguousLegacyAndMalformedCacheRecords)
    {
        const auto prefix = RuntimeSafetyKey::Prefix(RuntimeSafetyKey::CompatibilityRevision);
        for (const std::wstring& key : {std::wstring(L"example.milk"), std::wstring(L"C:\\Presets\\example.milk"),
            prefix + L"100/200/example.milk", prefix + L"/200/c:\\presets\\example.milk",
            prefix + L"100//c:\\presets\\example.milk", prefix + L"100/200/",
            prefix + L"100/200/c:\\presets\\example.milk\tother"})
            Assert::IsFalse(RuntimeSafetyKey::IsCurrent(key));
        Assert::IsTrue(RuntimeSafetyKey::Make(L"example.milk", 100, 200).empty());
    }
};

TEST_CLASS(ShaderBodyLocationTest)
{
  public:
    TEST_METHOD(FindsTheExactMarkerAndMatchingBodyBeforeTrailingHelpers)
    {
        const std::string source = "float shader_body_helper(float x) {return x;}"
            "shader_body {if(uv.x>.25) {ret=float3(.25,.5,.75);}else{ret=0;}}"
            "float after(float x) {return x*x;}";
        PresetShaderCompat::ShaderBodySpan body;
        Assert::IsTrue(PresetShaderCompat::LocateShaderBody(source, body));
        Assert::IsTrue(source.substr(body.marker, 11) == "shader_body");
        Assert::IsTrue(body.marker == source.find("shader_body {"));
        Assert::IsTrue(source.substr(body.closeBrace + 1) == "float after(float x) {return x*x;}");
        Assert::IsTrue(source[body.openBrace] == '{');
    }

    TEST_METHOD(IgnoresCommentAndStringMarkersAndAllowsNonStructuralDefines)
    {
        const std::string source = "// shader_body { }\1/* shader_body { } */"
            "#define SCALE 2 // { shader_body }\1"
            "#if 1\1#define PI 3.14\1#endif\1"
            "shader_body {ret=0;/* } */ ret+=1;}\1"
            "\"shader_body { }\"";
        PresetShaderCompat::ShaderBodySpan body;
        Assert::IsTrue(PresetShaderCompat::LocateShaderBody(source, body));
        Assert::IsTrue(body.marker == source.find("shader_body {ret"));
        Assert::IsTrue(source.substr(body.closeBrace + 1) == "\1\"shader_body { }\"");
    }

    TEST_METHOD(DeclinesMissingDuplicateUnbalancedOrPreprocessorControlledBodies)
    {
        for (const char* source : {"float shader_body_helper(){return 1;}",
                                  "shader_body {ret=0;", "shader_body {ret=0;}}",
                                  "shader_body {ret=0;} shader_body {ret=1;}",
                                  "#define OPEN {\nshader_body OPEN ret=0;}",
                                  "#define shader_body other\nshader_body {ret=0;}",
                                  "#if CHOICE\nshader_body {ret=0;}\n#endif",
                                  "shader_body {\n#if CHOICE\n}\n#endif\n}",
                                  "#define OPEN \\\n{\nshader_body OPEN ret=0;}"})
        {
            PresetShaderCompat::ShaderBodySpan body{123,456,789};
            Assert::IsFalse(PresetShaderCompat::LocateShaderBody(source, body));
            Assert::IsTrue(body.marker == 123 && body.openBrace == 456 && body.closeBrace == 789,
                L"An uncertain body must not return partial splice offsets.");
        }
    }
};

TEST_CLASS(LegacyShaderBindingTest)
{
  public:
    TEST_METHOD(QualifiesProvenGlobalInitialisersAndPreservesLaterLocalUses)
    {
        constexpr const char* include = "float4 _c0;\n#define aspect _c0\n";
        const std::string aspect = "shader_body { float aspect = aspect.x / aspect.y; ret=float3(aspect,0,0); }";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(aspect, include) ==
                       "shader_body { float aspect = ::_c0.x / ::_c0.y; ret=float3(aspect,0,0); }");
        const std::string ret = "float3 ret1,other;\nshader_body { ret1=float3(0.25,0.5,0.75); float3 ret1=ret1; ret=ret1; }";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(ret, include) ==
                       "float3 ret1,other;\nshader_body { ret1=float3(0.25,0.5,0.75); float3 ret1=::ret1; ret=ret1; }");
    }

    TEST_METHOD(DoesNotInventGlobalsOrReplaceNearerLocalBindings)
    {
        constexpr const char* include = "float4 _c0;\n#define aspect _c0\n";
        for (const char* source : {
                 "shader_body { float3 ret1=ret1; ret=ret1; }",
                 "shader_body { float3 ret1=float3(1,2,3); { float3 ret1=ret1; ret=ret1; } }",
                 "float3 ret1; shader_body { float3 ret1=float3(1,2,3); { float3 ret1=ret1; ret=ret1; } }",
                 "shader_body { float aspect=2; { float aspect=aspect.x/aspect.y; ret=aspect; } }",
                 "float3 ret1; shader_body { float3 ret1=object.ret1; ret=ret1; }",
                 "#define aspect other\nshader_body { float aspect=aspect.x/aspect.y; ret=aspect; }"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(source, include) == source,
                           L"An uncertain or nearer local binding must not be forced to a global.");
        const std::string aspect = "shader_body { float aspect=aspect.x/aspect.y; ret=aspect; }";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(aspect, "") == aspect);
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(aspect,
                       "// float4 _c0;\n// #define aspect _c0\n") == aspect);
    }

    TEST_METHOD(PreservesCommentsStringsAndOrdinaryShaderSource)
    {
        constexpr const char* include = "float4 _c0;\n#define aspect _c0\n";
        for (const char* source : {
                 "shader_body { float local=0.25; ret=float3(local,0.5,0.75); }",
                 "// float3 ret1;\nshader_body { /* float aspect=aspect.x/aspect.y; */ ret=float3(1,2,3); }",
                 "\"float3 ret1=ret1;\" 'float aspect=aspect.x/aspect.y;'",
                 "shader_body { float aspect=texsize.x/texsize.y; ret=aspect; }"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(source, include) == source);
    }

    TEST_METHOD(AllowsProvenNumericConstantDefinesFromRealPresetPatterns)
    {
        constexpr const char* include = "float4 _c0;\n#define aspect _c0\n";
        for (const char* prefix : {"#define PI 3.141592654\n#define TAU (2.0*PI)\n",
                                   "#define P 3.14159265359\n",
                                   "#define SNOW_FORCE 0.25\n#define INIT_SPEED 20.0\n#define AMOUNT 3.0\n"})
        {
            const std::string source = std::string(prefix) +
                "shader_body { float aspect=aspect.x/aspect.y; ret=float3(aspect,0,0); }";
            const std::string expected = std::string(prefix) +
                "shader_body { float aspect=::_c0.x/::_c0.y; ret=float3(aspect,0,0); }";
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(source, include) == expected);
        }
        // Three corpus payloads place PI inside shader_body. An accepted
        // directive must not obscure the following statement boundary.
        const std::string source = "shader_body {\n#define PI 3.14159265359\nfloat aspect=aspect.x/aspect.y; ret=aspect; }";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(source, include) ==
                       "shader_body {\n#define PI 3.14159265359\nfloat aspect=::_c0.x/::_c0.y; ret=aspect; }");
    }

    TEST_METHOD(RejectsBindingChangingOrUncertainPreprocessorDefinitions)
    {
        constexpr const char* include = "float4 _c0;\n#define aspect _c0\n";
        for (const char* prefix : {"#define DECLARE float4 _c0;\n",
                                   "#if 1\n#define PI 3.14\n#endif\n",
                                   "#define SCALE(x) (x)\n",
                                   "#define PI 3.14 \\\n+0.0\n",
                                   "#define PI 3.14\n#undef PI\n",
                                   "#define aspect other\n", "#define float 1\n", "#define x 1\n",
                                   "#define INIT_SPEED unproved_constant\n"})
        {
            const std::string source = std::string(prefix) +
                "shader_body { float aspect=aspect.x/aspect.y; ret=aspect; }";
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyShadowInitialisers(source, include) == source);
        }
    }
};

TEST_CLASS(LegacyDoubleTypeTest)
{
  public:
    TEST_METHOD(ConvertsExactLegacyDoubleTypesOnlyForShaderModelFourPixels)
    {
        const std::string source = "double4 helper(double2 v) { double a=(double)v.x; return double4(v,a,1); }"
                                   "shader_body { double3 tint=double3(0.125,0.375,0.625); ret=tint; }";
        const std::string expected = "float4 helper(float2 v) { float a=(float)v.x; return float4(v,a,1); }"
                                     "shader_body { float3 tint=float3(0.125,0.375,0.625); ret=tint; }";
        for (const char* profile : {"ps_4_0", "ps_4_1", "ps_4_0_level_9_1", "ps_4_0_level_9_3"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyDoubleTypes(source, "", profile) == expected);
        for (const char* profile : {"ps_5_0", "vs_4_0", "cs_4_0", "cs_5_0", "ps_3_0"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyDoubleTypes(source, "", profile) == source,
                           L"Modern authored precision and non-pixel stages must remain unchanged.");
    }

    TEST_METHOD(DoesNotRewriteLongerNamesCommentsStringsOrOrdinaryFloatShaders)
    {
        const std::string source = "// double3 comment\n/* double2 */\"double4\"'double'"
                                   " mydouble3;doubleValue;double3suffix;Double3;"
                                   "shader_body { float3 tint=float3(0.25,0.5,0.75); ret=tint; }";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyDoubleTypes(source, "", "ps_4_0") == source);
    }

    TEST_METHOD(DeclinesMacrosAndTypedefsThatCouldChangeTypeMeaning)
    {
        for (const char* source : {"#define double3 other\nshader_body { double3 tint=0; ret=tint; }",
                                   "#define DECLARE float4 _c0;\nshader_body { double3 tint=0; ret=tint; }",
                                   "typedef double3 colour; shader_body { colour tint=0; ret=tint; }"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyDoubleTypes(source, "", "ps_4_0") == source);
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyDoubleTypes(
                           "#define AMOUNT 3\nshader_body { double3 tint=0; ret=tint; }", "", "ps_4_0") ==
                       "#define AMOUNT 3\nshader_body { float3 tint=0; ret=tint; }");
        const std::string source = "shader_body { double3 tint=double3(0.25,0.5,0.75); ret=tint; }";
        for (const char* include : {"#define double3 other\n", "#undef double\n"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyDoubleTypes(source, include, "ps_4_0") == source);
    }
};

TEST_CLASS(LegacyPointBindingTest)
{
  public:
    TEST_METHOD(RenamesOnePointDeclaratorAndItsScopedUnqualifiedUses)
    {
        const std::string source = "shader_body { for(int i=0;i<2;++i) { float2 point=float2(q1,q2);"
                                   "point+=float2(0.125,0.125);ret+=float3(point,q3); } }";
        const std::string expected = "shader_body { for(int i=0;i<2;++i) { float2 __md_legacy_point=float2(q1,q2);"
                                     "__md_legacy_point+=float2(0.125,0.125);ret+=float3(__md_legacy_point,q3); } }";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyPointBinding(source, "", "ps_4_0") == expected);
    }

    TEST_METHOD(PreservesPointCommentsStringsMembersAndLongerNames)
    {
        const std::string source = "// point\nshader_body { { float2 point=float2(0.25,0.5);"
                                   "/* point */ret=float3(point,object.point);float midpoint=0; } ret=object.point; }"
                                   "\"point\"'point'\"escaped \\\" point\"";
        const std::string expected = "// point\nshader_body { { float2 __md_legacy_point=float2(0.25,0.5);"
                                     "/* point */ret=float3(__md_legacy_point,object.point);float midpoint=0; } ret=object.point; }"
                                     "\"point\"'point'\"escaped \\\" point\"";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyPointBinding(source, "", "ps_4_0") == expected);
    }

    TEST_METHOD(DeclinesUnboundShadowedMacroOrCollidingPointNamesAndOtherProfiles)
    {
        for (const char* source : {
                 "shader_body { ret=float3(point,0); }",
                 "float2 point; shader_body { float2 point=float2(1,2); ret=float3(point,0); }",
                 "float2 helper(float2 point) {return point;} shader_body {float2 point=float2(1,2);ret=float3(point,0);}",
                 "shader_body {float2 point=float2(1,2);{float2 point=float2(2,3);ret=float3(point,0);}}",
                 "shader_body {float2 point=float2(1,2);{float2 other=0,point=float2(2,3);ret=float3(point,0);}}",
                 "shader_body {float2 point=float2(1,2);{float2 point[2];ret=float3(point[0],0);}}",
                 "shader_body {ret=float3(point,0);float2 point=float2(1,2);}",
                 "shader_body {{float2 point=float2(1,2);ret=float3(point,0);}ret=float3(point,0);}",
                 "shader_body {float2 point=point;ret=float3(point,0);}",
                 "#define MAKE float2\nshader_body {float2 point=float2(1,2);ret=float3(point,0);}",
                 "typedef float2 colour; shader_body {float2 point=float2(1,2);ret=float3(point,0);}",
                 "float2 __md_legacy_point;shader_body {float2 point=float2(1,2);ret=float3(point,0);}"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyPointBinding(source, "", "ps_4_0") == source);
        const std::string source = "shader_body {float2 point=float2(1,2);ret=float3(point,0);}";
        for (const char* include : {"#define point other\n", "float2 __md_legacy_point;\n",
                                    "#define LABEL(name) #name\n"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyPointBinding(source, include, "ps_4_0") == source);
        const std::string pasted = "float3 colour_point(){return float3(0.25,0.5,0.75);}"
                                   "shader_body {float2 point=float2(1,2);ret=PICK(point);}";
        Assert::IsTrue(PresetShaderCompat::RewriteLegacyPointBinding(pasted,
                       "#define PICK(name) colour_##name()\n", "ps_4_0") == pasted,
                       L"Renaming a macro argument must not change the identifier produced by token pasting.");
        for (const char* profile : {"ps_5_0", "vs_4_0", "cs_4_0"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyPointBinding(source, "", profile) == source);
    }
};

TEST_CLASS(RecoveryRotationPreparationTest)
{
  public:
    TEST_METHOD(LeavesGlobalsOtherMatricesCommentsAndStructuralMacrosUnchanged)
    {
        for (const char* source : {
            "float2x2 global={q4,q5,-q5,q4};shader_body {ret=0;}",
            "shader_body {float2x2 explicitZero={0,0,0,0};ret=0;}",
            "shader_body {float2x2 other={q4,q5,q5,q4};ret=0;}",
            "shader_body {/*float2x2 rot={q4,q5,-q5,q4};*/ret=0;}",
            "#define uv float2(0,0)\nshader_body {float2x2 rot={q4,q5,-q5,q4};ret=0;}",
            "#define OPEN {\nshader_body OPEN float2x2 rot={q4,q5,-q5,q4};ret=0;}"})
            Assert::IsTrue(PresetShaderCompat::GuardRecoveryRotationInitialisers(source) == source);
    }

    TEST_METHOD(GuardsLocalQPairsIncludingUpperBanksWithoutChangingSurroundingCode)
    {
        const std::string source = "// keep this\nshader_body { float2x2 spin={q63,q64,-q64,q63};ret=1;}";
        const auto guarded = PresetShaderCompat::GuardRecoveryRotationInitialisers(source);
        Assert::IsTrue(guarded.find("__md2_recover_rotation(float2x2(q63,q64,-q64,q63),uv,uv_orig,__md2_rotation_decay)") != std::string::npos);
        Assert::IsTrue(guarded.starts_with("// keep this\nshader_body { float2x2 spin="));
        Assert::IsTrue(guarded.ends_with(";ret=1;}"));
    }
};

TEST_CLASS(LegacyShaderBindingGpuTest)
{
  private:
    std::array<float,4> SampleLoadedTexture(ID3D11Resource* texture,float u,float v)
    {
        ComPtr<ID3D11Texture2D> image;Assert::IsTrue(SUCCEEDED(texture->QueryInterface(IID_PPV_ARGS(image.GetAddressOf()))));
        const std::string code="shader_body {ret=tex2D(sampler_main,float2("+std::to_string(u)+","+std::to_string(v)+")).rgb;}";
        return RenderProductionPresetShader(code.c_str(),"ps_4_0",image.Get());
    }

    static std::vector<uint8_t> TextureDDS(unsigned bits)
    {
        std::vector<uint8_t> data(128);
        auto field=[&](size_t offset,uint32_t value){for(unsigned i=0;i<4;++i)data[offset+i]=uint8_t(value>>(i*8));};
        field(0,0x20534444);field(4,124);field(8,0x100f);field(12,2);field(16,2);field(20,2*bits/8);field(28,1);
        field(76,32);field(80,bits==32?0x41:0x40);field(88,bits);field(92,0xff0000);field(96,0xff00);field(100,0xff);field(104,bits==32?0xff000000:0);field(108,0x1000);
        for (const std::array<uint8_t,3> rgb : {std::array<uint8_t,3>{255,0,0},{0,255,0},{0,0,255},{255,255,255}})
        {data.push_back(rgb[2]);data.push_back(rgb[1]);data.push_back(rgb[0]);if(bits==32)data.push_back(255);}
        return data;
    }

    static std::vector<uint8_t> TextureTGA(bool rle)
    {
        std::vector<uint8_t> data(18);data[2]=rle?10:2;data[12]=data[14]=2;data[16]=24;
        // Bottom-origin input: blue bottom row; red top row.
        if(rle)data.insert(data.end(),{129,255,0,0,129,0,0,255});
        else data.insert(data.end(),{255,0,0,255,0,0,0,0,255,0,0,255});
        return data;
    }

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    std::unique_ptr<CState> m_state;
    std::filesystem::path m_cacheFixtureDirectory;
    std::vector<std::filesystem::path> m_cacheFixtureFiles;
    std::wstring m_originalConfiguration;

    std::array<float, 4> RenderProductionPresetShader(const char* source, const char* profile = "ps_4_0",
                                                    ID3D11Texture2D* feedback = nullptr, int shaderType = SHADER_COMP)
    {
        CConstantTable* rawTable = nullptr;
        ID3D11PixelShader* rawShader = nullptr;
        const bool compiled = g_plugin.LoadShaderFromMemory(source, "PS", profile, &rawTable,
                                  reinterpret_cast<void**>(&rawShader), shaderType, false);
        ComPtr<ID3D11PixelShader> pixel;
        pixel.Attach(rawShader);
        const auto releaseTable = [](CConstantTable* table) { if (table) table->Release(); };
        std::unique_ptr<CConstantTable, decltype(releaseTable)> table(rawTable, releaseTable);
        Assert::IsTrue(compiled, L"The test must compile through the production preset preparation path.");
        Assert::IsNotNull(table.get());
        CShaderParams parameters;
        parameters.CacheParams(table.get(), false);
        g_plugin.ApplyShaderParams(&parameters, table.get(), m_state.get(), feedback);
        table->ApplyChanges(m_context.Get());
        const size_t bufferCount = table->GetBuffersCount();
        if (bufferCount)
        {
            std::vector<ID3D11Buffer*> buffers(bufferCount);
            table->GetBuffers(buffers.data());
            m_context->PSSetConstantBuffers(0, static_cast<UINT>(bufferCount), buffers.data());
        }
        const char vertexSource[] =
            // Match the production VS output order and the wrapped PS inputs.
            "struct Vertex {float4 c:COLOR0;float4 uv:TEXCOORD0;float2 ra:TEXCOORD1;float4 p:SV_Position;};"
            "Vertex main(uint id:SV_VertexID) {float2 xy[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};"
            "Vertex v;v.p=float4(xy[id],0,1);v.c=float4(1,1,1,1);v.uv=float4(0.5,0.5,0.25,0.75);v.ra=0;return v;}";
        ComPtr<ID3DBlob> vertexCode;
        Assert::IsTrue(SUCCEEDED(D3DCompile(vertexSource, sizeof(vertexSource) - 1, nullptr, nullptr, nullptr,
                                           "main", "vs_4_0", 0, 0, vertexCode.GetAddressOf(), nullptr)));
        ComPtr<ID3D11VertexShader> vertex;
        Assert::IsTrue(SUCCEEDED(m_device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                                                              nullptr, vertex.GetAddressOf())));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target, staging;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, target.GetAddressOf())));
        ComPtr<ID3D11RenderTargetView> view;
        Assert::IsTrue(SUCCEEDED(m_device->CreateRenderTargetView(target.Get(), nullptr, view.GetAddressOf())));
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        ID3D11RenderTargetView* output = view.Get();
        m_context->OMSetRenderTargets(1, &output, nullptr);
        m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = true;
        ComPtr<ID3D11RasterizerState> raster;
        Assert::IsTrue(SUCCEEDED(m_device->CreateRasterizerState(&rasterDesc, raster.GetAddressOf())));
        m_context->RSSetState(raster.Get());
        const D3D11_VIEWPORT viewport{0, 0, 1, 1, 0, 1};
        m_context->RSSetViewports(1, &viewport);
        m_context->IASetInputLayout(nullptr);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(vertex.Get(), nullptr, 0);
        m_context->PSSetShader(pixel.Get(), nullptr, 0);
        m_context->Draw(3, 0);
        output = nullptr;
        m_context->OMSetRenderTargets(1, &output, nullptr);
        m_context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::array<float, 4> actual{};
        memcpy(actual.data(), mapped.pData, sizeof(actual));
        m_context->Unmap(staging.Get(), 0);
        return actual;
    }

  public:
    TEST_METHOD(PresetInitialisationCannotMultiplyNestedWhileAllowances)
    {
        g_plugin.m_errors.clear();
        strcpy_s(m_state->m_szPerFrameInit,"q1=0;while((q1+=1;while(1);1));");
        m_state->RecompileExpressions(RECOMPILE_PRESET_CODE,1);
        Assert::AreEqual(1.0,m_state->q_values_after_init_code[0]);
        Assert::IsTrue(g_plugin.m_errors.empty());
    }

    TEST_METHOD(PerFrameWhileLimitsCanChangeWithoutRewritingPresetTextOrInitValues)
    {
        strcpy_s(m_state->m_szPerFrameInit,"q2=42;");
        const char* source="i=0;while((i+=1;1));q1=i;";
        strcpy_s(m_state->m_szPerFrameExpr,source);
        m_state->RecompileExpressions(RECOMPILE_PRESET_CODE,1);
        auto* limit=NSEEL_VM_regvar(m_state->m_pf_eel,PresetEelCompat::FrameWhileLimit);
        Assert::AreEqual(double(PresetEelCompat::WhileIterationBudget),*limit);
        *limit=64;
        for(int run=0;run<2;++run)
        {
            ExecutePresetPerFrameExpression(m_state->m_pf_eel,m_state->m_pf_codehandle);
            Assert::AreEqual(64.0,*m_state->var_pf_q[0]);
            Assert::AreEqual(42.0,m_state->q_values_after_init_code[1]);
            Assert::AreEqual(64.0,*limit);
        }
        Assert::IsTrue(std::string(m_state->m_szPerFrameExpr)==source);
    }

    TEST_METHOD(FastPerFrameExpressionsRetainTheirOriginalWhileAllowance)
    {
        strcpy_s(m_state->m_szPerFrameExpr,"i=0;while((i+=1;i<8));q1=i;");
        m_state->RecompileExpressions(RECOMPILE_PRESET_CODE,1);
        ExecutePresetPerFrameExpression(m_state->m_pf_eel,m_state->m_pf_codehandle);
        Assert::AreEqual(8.0,*m_state->var_pf_q[0]);
        Assert::AreEqual(double(PresetEelCompat::WhileIterationBudget),*NSEEL_VM_regvar(m_state->m_pf_eel,PresetEelCompat::FrameWhileLimit));
    }

    TEST_METHOD(Legacy24BitDDSLoadsThroughTheProductionMemoryLoader)
    {
        const auto data=TextureDDS(24);ComPtr<ID3D11Resource> texture;
        Assert::IsTrue(SUCCEEDED(g_plugin.m_lpDX->m_lpDevice->CreateTextureFromMemory(data.data(),data.size(),texture.GetAddressOf(),0)));
        const auto red=SampleLoadedTexture(texture.Get(),.25f,.25f);
        Assert::AreEqual(1.0f,red[0]);Assert::AreEqual(0.0f,red[1]);Assert::AreEqual(0.0f,red[2]);
        const auto blue=SampleLoadedTexture(texture.Get(),.25f,.75f);
        Assert::AreEqual(0.0f,blue[0]);Assert::AreEqual(0.0f,blue[1]);Assert::AreEqual(1.0f,blue[2]);
    }

    TEST_METHOD(LegacyTGALoadsFromFilesAndRLEMemoryWithoutChangingOrientation)
    {
        const auto path=std::filesystem::temp_directory_path()/(L"milkdrop-native-texture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".TGA");
        struct Remove{std::filesystem::path path;~Remove(){std::error_code e;std::filesystem::remove(path,e);}} remove{path};
        const auto data=TextureTGA(false);std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),data.size());file.close();
        ComPtr<ID3D11Resource> texture;
        Assert::IsTrue(SUCCEEDED(g_plugin.m_lpDX->m_lpDevice->CreateTextureFromFile(path.c_str(),texture.GetAddressOf())));
        const auto red=SampleLoadedTexture(texture.Get(),.25f,.25f);
        Assert::AreEqual(1.0f,red[0]);Assert::AreEqual(0.0f,red[1]);Assert::AreEqual(0.0f,red[2]);
        texture.Reset();const auto packed=TextureTGA(true);
        Assert::IsTrue(SUCCEEDED(g_plugin.m_lpDX->m_lpDevice->CreateTextureFromMemory(packed.data(),packed.size(),texture.GetAddressOf(),1)));
        const auto blue=SampleLoadedTexture(texture.Get(),.25f,.75f);
        Assert::AreEqual(0.0f,blue[0]);Assert::AreEqual(0.0f,blue[1]);Assert::AreEqual(1.0f,blue[2]);
    }

    TEST_METHOD(LegacyDDSConversionPreservesTheAuthoredMipChain)
    {
        auto data=TextureDDS(24);
        auto field=[&](size_t offset,uint32_t value){for(unsigned i=0;i<4;++i)data[offset+i]=uint8_t(value>>(i*8));};
        field(8,0x2100f);field(28,2);field(108,0x401008);data.insert(data.end(),{64,128,192});
        ComPtr<ID3D11Resource> texture;
        Assert::IsTrue(SUCCEEDED(g_plugin.m_lpDX->m_lpDevice->CreateTextureFromMemory(data.data(),data.size(),texture.GetAddressOf(),0)));
        ComPtr<ID3D11Texture2D> image;Assert::IsTrue(SUCCEEDED(texture.As(&image)));
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);Assert::AreEqual(2U,desc.MipLevels);
        const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8X8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Width=desc.Height=desc.MipLevels=1;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc,nullptr,&staging)));
        m_context->CopySubresourceRegion(staging.Get(),0,0,0,0,image.Get(),1,nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
        const auto* pixel=static_cast<const uint8_t*>(mapped.pData);
        const unsigned r=pixel[bgra?2:0],g=pixel[1],b=pixel[bgra?0:2];m_context->Unmap(staging.Get(),0);
        Assert::AreEqual(192U,r);Assert::AreEqual(128U,g);Assert::AreEqual(64U,b);
    }

    TEST_METHOD(WorkingDDSAndInvalidTexturesKeepTheirExistingBehaviour)
    {
        auto data=TextureDDS(32);ComPtr<ID3D11Resource> texture;
        Assert::IsTrue(SUCCEEDED(g_plugin.m_lpDX->m_lpDevice->CreateTextureFromMemory(data.data(),data.size(),texture.GetAddressOf(),0)));
        const auto green=SampleLoadedTexture(texture.Get(),.75f,.25f);
        Assert::AreEqual(0.0f,green[0]);Assert::AreEqual(1.0f,green[1]);Assert::AreEqual(0.0f,green[2]);
        for(unsigned type:{0U,1U})
        {
            texture.Reset();data=type==0?TextureDDS(24):TextureTGA(true);data.resize(data.size()-5);
            Assert::IsTrue(FAILED(g_plugin.m_lpDX->m_lpDevice->CreateTextureFromMemory(data.data(),data.size(),texture.GetAddressOf(),type)));
            Assert::IsNull(texture.Get());
        }
    }

    TEST_METHOD_INITIALIZE(InitialiseProductionShaderPipeline)
    {
        Assert::IsTrue(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                                   nullptr, 0, D3D11_SDK_VERSION,
                                                   m_device.GetAddressOf(), nullptr, m_context.GetAddressOf())));
        DXCONTEXT_PARAMS params{};
        params.back_buffer_format = DXGI_FORMAT_B8G8R8A8_UNORM;
        params.depth_buffer_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        params.back_buffer_count = 2;
        params.min_feature_level = D3D_FEATURE_LEVEL_10_0;
        g_plugin.m_lpDX = std::make_unique<DXContext>(nullptr, &params);
        g_plugin.m_lpDX->m_client_width = 8;
        g_plugin.m_lpDX->m_client_height = 4;
        g_plugin.m_lpDX->m_lpDevice = std::make_unique<D3D11Shim>(m_device.Get(), m_context.Get());
        g_plugin.m_nTexSizeX = 8;
        g_plugin.m_nTexSizeY = 4;
        g_plugin.m_fNextPresetTime = 30;
        g_plugin.m_fPresetStartTime = g_plugin.m_fStartTime = 0;
        g_plugin.m_fftSampleRate = 44100;
        g_plugin.m_szMilkdrop2Path[0] = 0; // Disable logs; this test never configures cache paths.
        const auto includePath = std::filesystem::path(__FILE__).parent_path().parent_path() / "external/winamp/data/include.fx";
        std::ifstream includeFile(includePath, std::ios::binary);
        Assert::IsTrue(includeFile.good());
        const std::string include{std::istreambuf_iterator<char>(includeFile), std::istreambuf_iterator<char>()};
        strcpy_s(g_plugin.m_szShaderIncludeText, include.c_str());
        g_plugin.m_nShaderIncludeTextLen = include.size();
        m_state = std::make_unique<CState>();
        m_state->Initialize();
        g_plugin.m_pState = m_state.get();
        g_plugin.m_pOldState = nullptr;
    }

    TEST_METHOD_CLEANUP(ReleaseProductionShaderPipeline)
    {
        g_plugin.ResetVisualInactivityTracking(false);
        (g_plugin.*bandMember(SkipCompTag{}))=false;
        g_plugin.m_pState = nullptr;
        if (m_state)
            m_state->Finish();
        m_state.reset();
        g_plugin.m_textures.clear();
        SafeRelease(g_plugin.m_fftTexture);
        g_plugin.m_fftHistory.fill(0.0f);
        g_plugin.m_lpDX.reset();
        if (g_plugin.m_pShaderCompileErrors)
        {
            g_plugin.m_pShaderCompileErrors->Release();
            g_plugin.m_pShaderCompileErrors = nullptr;
        }
        m_context.Reset();
        m_device.Reset();
        if (!m_cacheFixtureDirectory.empty())
        {
            // Reset only the test library's cache paths. Do not load a live
            // user profile while restoring the previous configuration string.
            g_plugin.m_szConfigIniFile[0] = 0;
            g_plugin.MilkDropPreInitialize();
            wcscpy_s(g_plugin.m_szConfigIniFile, m_originalConfiguration.c_str());
            g_plugin.m_pState = nullptr;
            g_plugin.m_pOldState = nullptr;
            std::error_code error;
            for (const auto& path : m_cacheFixtureFiles)
                std::filesystem::remove(path, error);
            std::filesystem::remove(m_cacheFixtureDirectory / "shader-cache", error);
            std::filesystem::remove(m_cacheFixtureDirectory, error);
            m_cacheFixtureDirectory.clear();
            m_cacheFixtureFiles.clear();
        }
    }

    TEST_METHOD(LegacyAspectInitialiserReadsGlobalVectorAndKeepsLocalScalar)
    {
        const auto actual = RenderProductionPresetShader(
            "shader_body { float aspect=aspect.x/aspect.y; ret=float3(aspect,0.25,0.5); }");
        Assert::AreEqual(2.0f, actual[0]);
        Assert::AreEqual(0.25f, actual[1]);
        Assert::AreEqual(0.5f, actual[2]);
        Assert::AreEqual(1.0f, actual[3]);
    }

    TEST_METHOD(ZeroRotationIsRepairedOnlyDuringRecoveryAndValidCoefficientsRemainUnchanged)
    {
        const char* source = "shader_body {float2x2 rot={q4,q5,-q5,q4};"
            "ret=float3(mul(float2(.25,.5),rot),.75);}";
        g_plugin.m_bFoobarIdlePresetActive = false;
        g_plugin.m_bPresetLockedByCode = false;
        g_plugin.m_nVisualRecoveryStage = 0;
        *m_state->var_pf_q[3] = *m_state->var_pf_q[4] = 0;
        const auto original = RenderProductionPresetShader(source, "ps_4_0", nullptr, SHADER_WARP);
        Assert::AreEqual(0.0f, original[0]);
        Assert::AreEqual(0.0f, original[1]);
        g_plugin.m_nVisualRecoveryStage = 1;
        const auto repaired = RenderProductionPresetShader(source, "ps_4_0", nullptr, SHADER_WARP);
        Assert::AreEqual(.25f * .98f, repaired[0], .000001f);
        Assert::AreEqual(.5f * .98f, repaired[1], .000001f);
        const auto coordinates = RenderProductionPresetShader(
            "shader_body {float2x2 rot={q4,q5,-q5,q4};ret=float3(uv,rot[0][0]);}",
            "ps_4_0", nullptr, SHADER_WARP);
        const auto inputCoordinates = RenderProductionPresetShader(
            "shader_body {ret=float3(uv_orig,0);}", "ps_4_0", nullptr, SHADER_WARP);
        Assert::AreEqual(.25f, inputCoordinates[0]);
        Assert::AreEqual(.75f, inputCoordinates[1]);
        Assert::AreEqual(.25f * .98f, coordinates[0], .000001f);
        Assert::AreEqual(.75f * .98f, coordinates[1], .000001f);
        *m_state->var_pf_q[3] = 2;
        *m_state->var_pf_q[4] = 1;
        const auto valid = RenderProductionPresetShader(source, "ps_4_0", nullptr, SHADER_WARP);
        Assert::AreEqual(0.0f, valid[0]);
        Assert::AreEqual(1.25f, valid[1]);
        const auto validCoordinates = RenderProductionPresetShader(
            "shader_body {float2x2 rot={q4,q5,-q5,q4};ret=float3(uv,rot[0][0]);}",
            "ps_4_0", nullptr, SHADER_WARP);
        Assert::AreEqual(.5f, validCoordinates[0]);
        Assert::AreEqual(.5f, validCoordinates[1]);
        g_plugin.ResetVisualInactivityTracking(false);
    }

    TEST_METHOD(ContrastBoostPreservesShaderColourRatiosAndRecoversSubByteDetail)
    {
        const char* source = "shader_body {ret=float3(.00005,.0002,.0001);}";
        g_plugin.m_bFoobarIdlePresetActive = false;
        g_plugin.m_bPresetLockedByCode = false;
        g_plugin.m_bVisualContrastRecovery = false;
        const auto original = RenderProductionPresetShader(source);
        Assert::AreEqual(.0002f, original[1], .000001f);
        g_plugin.m_bVisualContrastRecovery = true;
        const auto boosted = RenderProductionPresetShader(source);
        Assert::IsTrue(boosted[1] > .04f, L"Boost before UNORM quantisation can retain otherwise lost detail.");
        Assert::AreEqual(.25f, boosted[0] / boosted[1], .00001f);
        Assert::AreEqual(.5f, boosted[2] / boosted[1], .00001f);
        const auto black = RenderProductionPresetShader("shader_body {ret=0;}");
        Assert::AreEqual(0.0f, black[0]);
        const auto bright = RenderProductionPresetShader("shader_body {ret=float3(2,.5,1);}");
        Assert::AreEqual(2.0f, bright[0]);
        m_state->m_bBlending = true;
        const auto blending = RenderProductionPresetShader(source);
        Assert::AreEqual(original[1], blending[1], .000001f);
        m_state->m_bBlending = false;
        g_plugin.ResetVisualInactivityTracking(false);
        Assert::IsFalse(g_plugin.m_bVisualContrastRecovery);
        const auto restored = RenderProductionPresetShader(source);
        Assert::AreEqual(original[1], restored[1], .000001f);
    }

    TEST_METHOD(AutomaticContrastPolicyUploadsItsRampAndRestoresOriginalPixels)
    {
        g_plugin.m_bFoobarIdlePresetActive = false;
        g_plugin.m_bPresetLockedByCode = false;
        g_plugin.ResetVisualInactivityTracking(false);
        const char* source = "shader_body {ret=float3(.00005,.0002,.0001);}";
        const auto original = RenderProductionPresetShader(source);
        g_plugin.m_bVisualContrastAutomatic = true;
        g_plugin.m_automaticContrast.Reset(0);
        for (int i=0;i<3;++i)
            g_plugin.m_automaticContrast.Observe({true,.02f,.3f,.2f,.0002f});
        g_plugin.m_automaticContrast.Advance(.25f);
        const auto partial = RenderProductionPresetShader(source);
        g_plugin.m_automaticContrast.Advance(.5f);
        const auto corrected = RenderProductionPresetShader(source);
        Assert::IsTrue(partial[1]>original[1] && partial[1]<corrected[1]);
        Assert::IsTrue(corrected[1]>.04f);
        for (int i=0;i<3;++i)
            g_plugin.m_automaticContrast.Observe({true,.2f,.8f,.6f,.01f});
        g_plugin.m_automaticContrast.Advance(.75f);
        g_plugin.m_automaticContrast.Advance(1);
        const auto restored = RenderProductionPresetShader(source);
        Assert::AreEqual(original[1],restored[1],.000001f);
        g_plugin.ResetVisualInactivityTracking(false);
    }

    TEST_METHOD(ContrastCanRearmAfterRestoreWithoutReenablingSourceRecovery)
    {
        const auto previousSound=(g_plugin.*bandMember(SoundBandsTag{}));
        const auto previousFile=std::wstring(g_plugin.m_szCurrentPresetFile);
        const bool previousPlayback=g_plugin.m_bPlaybackActive;
        struct Restore
        {
            td_soundinfo sound;std::wstring file;bool playback;
            ~Restore(){g_plugin.ResetVisualInactivityTracking(false);(g_plugin.*bandMember(SoundBandsTag{}))=std::move(sound);
                wcscpy_s(g_plugin.m_szCurrentPresetFile,file.c_str());g_plugin.m_bPlaybackActive=playback;}
        } restore{previousSound,previousFile,previousPlayback};
        g_plugin.m_bPlaybackActive=true;g_plugin.m_bFoobarIdlePresetActive=false;g_plugin.m_bPresetLockedByCode=false;
        g_plugin.m_nLoadingPreset=0;g_plugin.m_bAutoVisualRecoveryEnabled=true;
        wcscpy_s(g_plugin.m_szCurrentPresetFile,L"synthetic-review.milk");
        strcpy_s(m_state->m_presetContentDigest,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        for(auto& channel:(g_plugin.*bandMember(SoundBandsTag{})).fWaveform)channel.fill(1);
        g_plugin.ResetVisualInactivityTracking(false);
        Assert::IsFalse(g_plugin.CanSampleVisualOutput());
        g_plugin.ToggleAutomaticContrast(); // No configured profile; enables for this session even if persistence fails.
        Assert::IsTrue(g_plugin.m_bVisualContrastAutomatic);
        Assert::IsTrue(g_plugin.CanSampleVisualOutput());
        Assert::IsFalse(g_plugin.m_bAutoVisualRecoveryEnabled);
        Assert::IsFalse(g_plugin.CanRecoverDormantDrawing());
        g_plugin.ToggleAutomaticContrast();
        Assert::IsFalse(g_plugin.CanSampleVisualOutput());
    }

    TEST_METHOD(QueuedContrastAnalysisUsesTheActualCompositePath)
    {
        g_plugin.m_bPlaybackActive=true;g_plugin.m_bFoobarIdlePresetActive=false;g_plugin.m_bPresetLockedByCode=false;
        g_plugin.m_nLoadingPreset=0;m_state->m_bBlending=false;
        wcscpy_s(g_plugin.m_szCurrentPresetFile,L"synthetic-queue-review.milk");
        for(auto& channel:(g_plugin.*bandMember(SoundBandsTag{})).fWaveform)channel.fill(1);
        ComPtr<ID3D11Texture2D> output;
        auto* device=g_plugin.m_lpDX->m_lpDevice.get();
        device->Initialize();
        Assert::IsTrue(device->CreateTexture(8,8,1,D3D11_BIND_RENDER_TARGET,DXGI_FORMAT_B8G8R8A8_UNORM,output.GetAddressOf()));
        device->SetRenderTarget(output.Get());
        for(int mode=0;mode<5;++mode)
        {
            g_plugin.ResetVisualInactivityTracking(true);
            g_plugin.m_bVisualContrastAutomatic=true;
            g_plugin.m_automaticContrast.Reset(0);
            for(int i=0;i<3;++i)g_plugin.m_automaticContrast.Observe({true,.02f,.3f,.2f,.0002f});
            g_plugin.m_automaticContrast.Advance(.25f);g_plugin.m_automaticContrast.Advance(.5f);
            g_plugin.m_fVisualRecoveryWarmupUntil=-1000;
            g_plugin.m_fVisualInactivityNextSampleTime=-1000;
            g_plugin.m_nVisualRecoveryStage=mode<=2?mode:0;
            (g_plugin.*bandMember(SkipCompTag{}))=mode==3;
            m_state->m_nCompPSVersion=mode==4?0:4;
            g_plugin.QueueVisualOutputSample();
            Assert::IsTrue(g_plugin.m_bVisualOutputSamplePending);
            Assert::AreEqual(mode<2?.35f:1.0f,g_plugin.m_fQueuedContrastExponent,.00001f);
        }
        (g_plugin.*bandMember(SkipCompTag{}))=false;
        g_plugin.ResetVisualInactivityTracking(false);
    }

    TEST_METHOD(CpuBandsKeepPhysicalTonesInTheSameBandAtDifferentRates)
    {
        for(unsigned rate:{44100U,48000U,96000U,192000U})
            for(int band=0;band<3;++band)
            {
                const float frequency=band==0?1000.0f:band==1?5500.0f:9000.0f;
                g_plugin.m_fftSampleRate=rate;
                for(auto& channel:(g_plugin.*bandMember(SoundBandsTag{})).fWaveform)
                    for(size_t i=0;i<channel.size();++i)channel[i]=static_cast<float>(25.6*std::sin(2*3.141592653589793*frequency*i/rate));
                g_plugin.DoCustomSoundAnalysis();
                Assert::AreEqual(band,static_cast<int>(std::max_element(std::begin(g_plugin.mdsound.imm),std::end(g_plugin.mdsound.imm))-std::begin(g_plugin.mdsound.imm)));
                (g_plugin.*bandMember(AnalyseBandsTag{}))((g_plugin.*bandMember(SoundBandsTag{})).fWaveform[0].data(),(g_plugin.*bandMember(SoundBandsTag{})).fWaveform[1].data());
                for(int channel=0;channel<2;++channel)
                    Assert::AreEqual(band,static_cast<int>(std::max_element(std::begin((g_plugin.*bandMember(SoundBandsTag{})).imm[channel]),std::end((g_plugin.*bandMember(SoundBandsTag{})).imm[channel]))-std::begin((g_plugin.*bandMember(SoundBandsTag{})).imm[channel])));
            }
    }

    TEST_METHOD(CpuBandsPreserveLegacyReferenceSumsAndHandleEmptyBands)
    {
        for(auto& channel:(g_plugin.*bandMember(SoundBandsTag{})).fWaveform)
            for(size_t i=0;i<channel.size();++i)channel[i]=20*std::sin(i*.31f)+7*std::cos(i*.79f);
        std::array<float,3> expected{};
        for(unsigned rate:{44100U,0U})
        {
            g_plugin.m_fftSampleRate=rate;g_plugin.DoCustomSoundAnalysis();
            for(int band=0;band<3;++band)
            {
                float sum=0;for(int i=NUM_FFT_SAMPLES*band/6;i<NUM_FFT_SAMPLES*(band+1)/6;++i)sum+=g_plugin.mdsound.fSpecLeft[i];
                Assert::AreEqual(sum,g_plugin.mdsound.imm[band]);
                if(rate==44100)expected[band]=sum;else Assert::AreEqual(expected[band],sum);
            }
            (g_plugin.*bandMember(AnalyseBandsTag{}))((g_plugin.*bandMember(SoundBandsTag{})).fWaveform[0].data(),(g_plugin.*bandMember(SoundBandsTag{})).fWaveform[1].data());
            const float mult=std::pow(2.0f,(std::log(11025.0f/200)/std::log(2.0f))/3);
            const float divisors[]{.326781557f,.380873770f,.199888934f};
            for(int channel=0;channel<2;++channel)for(int band=0;band<3;++band)
            {
                int start=static_cast<int>(NUM_FREQUENCIES*200.0f*std::pow(mult,float(band))/11025);
                int end=static_cast<int>(NUM_FREQUENCIES*200.0f*std::pow(mult,float(band+1))/11025);
                end=(std::min)(end,NUM_FREQUENCIES);
                float sum=0;for(int i=start;i<end;++i)sum+=(g_plugin.*bandMember(SoundBandsTag{})).fSpectrum[channel][i];
                const float old=(sum/float(end-start))/divisors[band];
                Assert::AreEqual(old,(g_plugin.*bandMember(SoundBandsTag{})).imm[channel][band],
                    (std::max)(1e-8f,std::abs(old)*1e-6f)); // The production build uses fast floating-point division.
            }
        }
        g_plugin.m_fftSampleRate=8000;g_plugin.DoCustomSoundAnalysis();
        Assert::AreEqual(0.0f,g_plugin.mdsound.imm[2]);
        (g_plugin.*bandMember(AnalyseBandsTag{}))((g_plugin.*bandMember(SoundBandsTag{})).fWaveform[0].data(),(g_plugin.*bandMember(SoundBandsTag{})).fWaveform[1].data());
        for(const auto& channel:(g_plugin.*bandMember(SoundBandsTag{})).imm)for(float value:channel)Assert::IsTrue(std::isfinite(value));
        Assert::AreEqual(0.0f,(g_plugin.*bandMember(SoundBandsTag{})).imm[0][2]);
    }

    TEST_METHOD(MixedStateCannotInheritAutomaticContrastFromAnImportedPreset)
    {
        g_plugin.m_bVisualContrastAutomatic = true;
        g_plugin.m_bVisualContrastRecovery = true;
        m_state->m_presetContentDigest[0] = 0;
        g_plugin.UpdateVisualInactivityTracking();
        Assert::IsFalse(g_plugin.m_bVisualContrastAutomatic);
        Assert::IsFalse(g_plugin.m_bVisualContrastRecovery);
        Assert::AreEqual(1.0f,g_plugin.VisualContrastExponent());
    }

    TEST_METHOD(ShaderBodyWrappingPreservesTrailingHelperFunctions)
    {
        const auto actual = RenderProductionPresetShader(
            "float3 helper(float x); shader_body {ret=helper(.25);}"
            "float3 helper(float x){return float3(x,.5,.75);}");
        Assert::AreEqual(.25f, actual[0]);
        Assert::AreEqual(.5f, actual[1]);
        Assert::AreEqual(.75f, actual[2]);
        Assert::AreEqual(1.0f, actual[3]);
    }

    TEST_METHOD(LegacyAnnotationsDoNotForceThePresetOntoFallbackShaders)
    {
        for (const char* source : {
                 "shader_body {ret=float3(.25,.5,.75);}\1written by martin\1END\1",
                 "shader_body {ret=float3(.25,.5,.75);}\1martin\1END\1",
                 ".#define sat saturate\nshader_body {ret=sat(float3(.25,.5,.75));}\n",
                 "#define SET {ret=float3(.25,.5,.75);}\nshader_body {SET}\nwritten by martin\nEND\n",
                 "shader_body {ret=float3(.25,.5,.75);\n . \n}\n",
                 "shader_body {ret=float3(.25,.5,.75);\n.// A legacy export credit\n}\n",
                 "shader_body {ret=float3(.25,.5,.75);}\nwritten by martin\nFROM: martin - fresco flash preset\nThanks Martin! :-)\n",
                 "shader_body {if(uv.x>=0){ret=float3(.25,.5,.75);}\1.\1}\1\1written by martin\1END\1"})
        {
            const auto actual = RenderProductionPresetShader(source);
            Assert::AreEqual(.25f, actual[0]);
            Assert::AreEqual(.5f, actual[1]);
            Assert::AreEqual(.75f, actual[2]);
            Assert::AreEqual(1.0f, actual[3]);
        }
    }

    TEST_METHOD(LegacyIntrinsicInitialisersUseFunctionsBeforeTheirNewLocals)
    {
        const auto maximum = RenderProductionPresetShader(
            "shader_body {float3 max=max(float3(.1,.5,.3),max(float3(.25,.1,.75),0));ret=max;}");
        Assert::AreEqual(.25f, maximum[0]);
        Assert::AreEqual(.5f, maximum[1]);
        Assert::AreEqual(.75f, maximum[2]);
        const auto dotProduct = RenderProductionPresetShader(
            "shader_body {float dot=dot(float2(.25,.5),float2(1,1));ret=dot;}");
        for (int channel=0; channel<3; ++channel) Assert::AreEqual(.75f, dotProduct[channel]);
        const auto scopes = RenderProductionPresetShader(
            "shader_body {float max=max(.25,.1);{float max=.9;ret=max;}"
            "ret.x=max;ret.y=dot(float2(.25,.25),1);ret.z=max;}");
        Assert::AreEqual(.25f, scopes[0]);
        Assert::AreEqual(.5f, scopes[1]);
        Assert::AreEqual(.25f, scopes[2]);
    }

    TEST_METHOD(NonNegativeLiteralSquareRootsRemainConstantInitialisers)
    {
        for (const char* profile : {"ps_4_0", "ps_5_0"})
        {
            const auto actual = RenderProductionPresetShader(
                "float r3=sqrt(3);float halfRoot=sqrt( .25f );"
                "shader_body {ret=float3(r3,halfRoot,sqrt(-1));}", profile);
            Assert::AreEqual(std::sqrt(3.0f), actual[0], .000001f);
            Assert::AreEqual(.5f, actual[1]);
            Assert::AreEqual(-1.0f, actual[2]); // Legacy signed-root behaviour remains intact.
        }
    }

    TEST_METHOD(ResourceAliasMacrosDoNotPreventProvenLegacyBindings)
    {
        const auto actual = RenderProductionPresetShader(
            "#define sat saturate\n#define source sampler_main\n#define size texsize_main\n"
            "shader_body {float3 max=max(float3(.1,.5,.3),float3(.25,.1,.75));ret=sat(max);}");
        Assert::AreEqual(.25f, actual[0]);Assert::AreEqual(.5f, actual[1]);Assert::AreEqual(.75f, actual[2]);
        const auto precision = RenderProductionPresetShader(
            "#define sat saturate\nstatic const double pi=3.14159265359;"
            "shader_body {ret=sat(float3(pi/4,.5,.75));}");
        Assert::AreEqual(.78539816f, precision[0], .000001f);
        Assert::AreEqual(.5f, precision[1]);Assert::AreEqual(.75f, precision[2]);
    }

    TEST_METHOD(VectorAspectInitialiserReadsGlobalAspect)
    {
        const auto actual = RenderProductionPresetShader(
            "shader_body {float2 aspect=float2(aspect.x,1.0);ret=float3(aspect,.75);}");
        Assert::AreEqual(1.0f, actual[0]);Assert::AreEqual(1.0f, actual[1]);Assert::AreEqual(.75f, actual[2]);
    }

    TEST_METHOD(WritableLegacyBooleansAndCompoundMasksKeepTheirValues)
    {
        for (const char* profile : {"ps_4_0", "ps_5_0"})
        {
            const auto actual = RenderProductionPresetShader(
                "#define sat saturate\nbool istube;"
                "shader_body {istube=uv.x>0;bool hex=(uv.x>0)*(uv.y>0);"
                "hex*=(uv.x<2)*(uv.y<2);bool off=true;off*=0;ret=float3(istube,hex,off);}", profile);
            Assert::AreEqual(1.0f, actual[0]);Assert::AreEqual(1.0f, actual[1]);Assert::AreEqual(0.0f, actual[2]);
        }
        for (const std::string source : {"bool enabled;shader_body {ret=enabled;}",
            "uniform bool enabled;shader_body {ret=enabled;}",
            "bool enabled;shader_body {bool enabled;enabled=true;ret=enabled;}",
            "#define MASK(x) x\nbool enabled;shader_body {enabled=true;ret=enabled;}",
            "shader_body {float hex=.5;hex*=.5;ret=hex;}",
            "// bool enabled;\nshader_body {/*enabled=true;*/ret=.5;}"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyBooleanBindings(source, "") == source);
    }

    TEST_METHOD(LegacyLocalLineNameRendersOnBothNativeShaderModels)
    {
        for (const char* profile : {"ps_4_0", "ps_5_0"})
        {
            const auto actual = RenderProductionPresetShader(
                "shader_body {float line=.25;float3 mask=float3(0,.5,.75);"
                "if(line>.1)mask.x=line;ret=float4(mask,1);}", profile);
            Assert::AreEqual(.25f, actual[0]);
            Assert::AreEqual(.5f, actual[1]);
            Assert::AreEqual(.75f, actual[2]);
        }
        for (const char* source : {
                 "float line=.25;shader_body {ret=line;}",
                 "shader_body {float line=line;ret=line;}",
                 "#define line .25\nshader_body {float line=.5;ret=line;}",
                 "shader_body {float line=.25;{float line=.5;ret=line;}}"})
            Assert::IsTrue(PresetShaderCompat::RewriteLegacyLineBinding(source,"","ps_4_0")==source);
    }

    TEST_METHOD(MixedVectorPowerArgumentsMatchTheNativeIntrinsic)
    {
        for (const char* arguments : {
                 "float2(.25,.5),float3(1,2,3)", "float2(.25,.5),float4(1,2,3,4)",
                 "float3(.25,.5,.75),float2(1,2)", "float3(.25,.5,.75),float4(1,2,3,4)",
                 "float4(.25,.5,.75,.9),float2(1,2)", "float4(.25,.5,.75,.9),float3(1,2,3)"})
        {
            const bool two = std::string_view(arguments).find("float2") != std::string_view::npos;
            const std::string declaration = two ? "float2 value=" : "float3 value=";
            const std::string output = two ? ");ret=float3(value,0);}" : ");ret=value;}";
            const auto original = RenderProductionPresetShader(
                (std::string("#define original_power pow\nshader_body {") + declaration + "original_power(" + arguments + output).c_str());
            const auto corrected = RenderProductionPresetShader(
                (std::string("shader_body {") + declaration + "pow(" + arguments + output).c_str());
            for (int channel=0; channel<4; ++channel) Assert::AreEqual(original[channel], corrected[channel], .000001f);
        }
    }

    TEST_METHOD(LegacyAnnotationRepairPreservesCodeAndDeclinesAmbiguousText)
    {
        for (const char* source : {
                 "shader_body {ret=float3(.25,.5,.75);}\nfloat after(){return .5;}",
                 "shader_body {ret=0;}\nwritten by martin;\nEND\n",
                 "shader_body {ret=0;}\nwritten by martin\nEND\nfloat after;",
                 "shader_body {ret=0;}\nwritten by martin\nEND_MORE\n",
                 "shader_body {ret=0;}\n// written by martin\n// END\n",
                 "shader_body {ret=0;\nret=ret\n.\nx;}\n",
                 "shader_body {ret=0; .\n}\n",
                 "shader_body {ret=0;\n.\n}\n}\n"})
            Assert::IsTrue(PresetShaderCompat::SanitiseLegacyShaderAnnotations(source) == source);
    }

    TEST_METHOD(EmptyResultSelectorsRecoverWithoutChangingValidAccessesOrComments)
    {
        const std::string damaged = "shader_body {ret=saturate(GetPixel(uv).);}";
        Assert::IsTrue(PresetShaderCompat::SanitiseLegacyShaderAnnotations(damaged) ==
            "shader_body {ret=saturate(GetPixel(uv) );}");
        for (const char* source : {
                 "shader_body {ret=saturate(GetPixel(uv).rgb);}",
                 "shader_body {ret=float3(.25,.5,.75);}",
                 "shader_body {ret=GetPixel(uv);/* GetPixel(uv).) */}",
                 "shader_body {ret=GetPixel(uv);}// GetPixel(uv).)\n",
                 "#define EXPORTED_TAIL GetPixel(uv).)\nshader_body {ret=0;}",
                 "shader_body {ret=0;}float3 helper(){return saturate(GetPixel(uv).);}"})
            Assert::IsTrue(PresetShaderCompat::SanitiseLegacyShaderAnnotations(source) == source);
    }

    TEST_METHOD(RecoveredEmptyResultSelectorsProduceNativeColourOnBothShaderModels)
    {
        for (const char* profile : {"ps_4_0", "ps_5_0"})
        {
            const auto actual = RenderProductionPresetShader(
                "float3 colour(){return float3(.25,.5,.75);}shader_body {ret=saturate(colour().);}", profile);
            Assert::AreEqual(.25f, actual[0]);
            Assert::AreEqual(.5f, actual[1]);
            Assert::AreEqual(.75f, actual[2]);
        }
    }

    TEST_METHOD(ShaderBodyWrappingIgnoresLongerMarkerIdentifiers)
    {
        const auto actual = RenderProductionPresetShader(
            "float3 shader_body_helper(){return float3(.25,.5,.75);}"
            "shader_body {ret=shader_body_helper();}");
        Assert::AreEqual(.25f, actual[0]);
        Assert::AreEqual(.5f, actual[1]);
        Assert::AreEqual(.75f, actual[2]);
    }

    TEST_METHOD(ShaderBodyWrappingHonoursConditionalBodiesAndStructuralMacros)
    {
        for (const char* source : {
                 "#if 1\nshader_body {ret=float3(.25,.5,.75);}\n"
                 "#else\nshader_body {ret=float3(1,0,0);}\n#endif\n",
                 "#define OPEN {\n#define CLOSE }\n"
                 "shader_body OPEN ret=float3(.25,.5,.75); CLOSE\n",
                 "shader_body {\n#if 0\n}\n#endif\nret=float3(.25,.5,.75);}\n"})
        {
            const auto actual = RenderProductionPresetShader(source);
            Assert::AreEqual(.25f, actual[0]);
            Assert::AreEqual(.5f, actual[1]);
            Assert::AreEqual(.75f, actual[2]);
            Assert::AreEqual(1.0f, actual[3]);
        }
    }

    TEST_METHOD(CorruptBytecodeCacheRecompilesSourceAndReplacesTheBadEntry)
    {
        m_originalConfiguration = g_plugin.m_szConfigIniFile;
        ReleaseProductionShaderPipeline();
        const auto temporary = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 32; ++attempt)
        {
            const auto path = temporary / ("milkdrop-shader-cache-" + std::to_string(GetCurrentProcessId()) +
                "-" + std::to_string(GetTickCount64()) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path))
            {
                m_cacheFixtureDirectory = path;
                break;
            }
        }
        Assert::IsFalse(m_cacheFixtureDirectory.empty());
        wcscpy_s(g_plugin.m_szConfigIniFile, (m_cacheFixtureDirectory / "probe.ini").c_str());
        g_plugin.MilkDropPreInitialize();
        InitialiseProductionShaderPipeline();

        constexpr const char* source = "shader_body {ret=float3(.1875,.3125,.6875);}";
        const auto expected = RenderProductionPresetShader(source);
        for (const auto& entry : std::filesystem::directory_iterator(m_cacheFixtureDirectory / "shader-cache"))
            if (entry.is_regular_file())
                m_cacheFixtureFiles.push_back(entry.path());
        Assert::AreEqual(static_cast<size_t>(1), m_cacheFixtureFiles.size());
        const auto cached = m_cacheFixtureFiles[0];
        {
            std::ofstream corrupt(cached, std::ios::binary | std::ios::trunc);
            corrupt << "not valid DXBC";
            corrupt.close();
            Assert::IsTrue(corrupt.good());
        }
        const auto actual = RenderProductionPresetShader(source);
        for (size_t channel = 0; channel < actual.size(); ++channel)
            Assert::AreEqual(expected[channel], actual[channel]);
        std::ifstream input(cached, std::ios::binary);
        const std::string bytecode{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        ComPtr<ID3D11ShaderReflection> reflection;
        Assert::IsTrue(SUCCEEDED(D3DReflect(bytecode.data(), bytecode.size(),
            __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(reflection.GetAddressOf()))));
        for (const auto& entry : std::filesystem::directory_iterator(m_cacheFixtureDirectory / "shader-cache"))
            Assert::IsTrue(entry.path() == cached, L"A successful atomic replacement must not leave temporary files.");
    }

    TEST_METHOD(ZeroBasePositivePowersRemainZeroForLegacyAndModernShaders)
    {
        for (const char* profile : {"ps_4_0", "ps_5_0"})
        {
            const auto scalar = RenderProductionPresetShader(
                "shader_body {float zero=q1;ret=float3(pow(zero,.01),pow(zero,.5),pow(zero,2));}", profile);
            for (int i = 0; i < 3; ++i)
                Assert::AreEqual(0.0f, scalar[i]);
            const auto vector = RenderProductionPresetShader(
                "shader_body {ret=pow(float3(q1,.25,.5),float3(.01,.5,2));}", profile);
            Assert::AreEqual(0.0f, vector[0]);
            Assert::AreEqual(.5f, vector[1], .00001f);
            Assert::AreEqual(.25f, vector[2], .00001f);
            const auto broadcast = RenderProductionPresetShader(
                "shader_body {float zero=q1;ret=pow(zero,float3(.01,.5,2));}", profile);
            for (int i = 0; i < 3; ++i)
                Assert::AreEqual(0.0f, broadcast[i]);
        }
    }

    TEST_METHOD(ProFFTReadsProcessedSpectrumThroughProductionTextureBinding)
    {
        g_plugin.m_lpDX->m_lpDevice->Initialize();
        Assert::IsTrue(g_plugin.CreateFFTTexture());
        g_plugin.mdsound.fSpecLeft.assign(ProFFT::Bins, 0.0f);
        g_plugin.mdsound.fSpecLeft[0] = 1.0f;
        g_plugin.mdsound.fSpecLeft[511] = 0.5f;
        m_state->m_fftAttack = m_state->m_fftDecay = 1.0f;
        g_plugin.UpdateFFTTexture();
        const auto actual = RenderProductionPresetShader(
            "shader_body { ret=float3(get_fft(-1),get_fft(2),get_fft_hz(22050)); }");
        Assert::AreEqual(1.0f, actual[0], 0.000001f);
        Assert::AreEqual(0.25f, actual[1], 0.000001f);
        Assert::AreEqual(0.25f, actual[2], 0.000001f);
    }

    TEST_METHOD(ProFFTInitialisesBothRowsAndRecreatesWithoutStaleBindings)
    {
        g_plugin.m_lpDX->m_lpDevice->Initialize();
        Assert::IsTrue(g_plugin.CreateFFTTexture());
        const auto silent = RenderProductionPresetShader(
            "shader_body { ret=float3(get_fft(0.1),get_fft(0.5),get_fft(0.9)); }");
        for (int i = 0; i < 3; ++i) Assert::AreEqual(0.0f, silent[i]);
        g_plugin.mdsound.fSpecLeft.assign(ProFFT::Bins, 1.0f);
        g_plugin.UpdateFFTTexture();
        Assert::IsTrue(g_plugin.CreateFFTTexture());
        const auto recreated = RenderProductionPresetShader(
            "shader_body { ret=float3(tex2D(sampler_fft,float2(0.5,0)).r,"
            "tex2D(sampler_fft,float2(0.5,1)).r,get_fft(0.5)); }");
        for (int i = 0; i < 3; ++i) Assert::AreEqual(0.0f, recreated[i]);
    }

    TEST_METHOD(ProFFTHertzLookupTracksActualPcmSampleRateThroughTheProductionFFT)
    {
        g_plugin.m_lpDX->m_lpDevice->Initialize();
        for (unsigned rate : {44100U, 48000U, 96000U, 192000U})
        {
            Assert::IsTrue(g_plugin.CreateFFTTexture());
            g_plugin.m_fftSampleRate = rate;
            m_state->m_fftAttack = 1.0f;
            m_state->m_fftDecay = 0.0f;
            // An exact FFT bin distinguishes true Hz mapping from an assumed rate.
            const float frequency = rate * 32.0f / 1024.0f;
            std::vector<float> waveform(NUM_AUDIO_BUFFER_SAMPLES);
            for (size_t i = 0; i < waveform.size(); ++i)
                waveform[i] = std::sin(6.283185307179586f * frequency * i / rate);
            g_plugin.mdfft.TimeToFrequencyDomain(waveform, g_plugin.mdsound.fSpecLeft);
            g_plugin.UpdateFFTTexture();
            const std::string source = "shader_body { ret=float3(get_fft_hz(" +
                std::to_string(frequency) + "),get_fft((32.5)/512.0),0); }";
            const auto actual = RenderProductionPresetShader(source.c_str());
            Assert::IsTrue(actual[0] > 0.8f);
            Assert::AreEqual(g_plugin.m_fftHistory[32], actual[0], 0.00001f);
            Assert::AreEqual(actual[0], actual[1], 0.00001f);
        }
    }

    TEST_METHOD(UnrecognisedTexturePrefixPreservesTheActualResourceName)
    {
        const float colour[4] = {0.125f, 0.375f, 0.625f, 1.0f};
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data{colour, sizeof(colour), 0};
        ComPtr<ID3D11Texture2D> texture;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, &data, texture.GetAddressOf())));
        TexInfo info{};
        wcscpy_s(info.texname, L"my_logo");
        info.texptr = texture.Get();
        info.w = info.h = 1;
        g_plugin.m_textures.push_back(info);
        const auto actual = RenderProductionPresetShader(
            "sampler2D sampler_my_logo; shader_body {ret=tex2D(sampler_my_logo,uv).xyz;} ");
        for (int i = 0; i < 3; ++i) Assert::AreEqual(colour[i], actual[i]);
        g_plugin.m_textures.clear();
    }

    TEST_METHOD(ReflectedHighTextureSlotsRenderAndDoNotLeakIntoTheNextShader)
    {
        g_plugin.m_lpDX->m_lpDevice->Initialize();
        const float colour[4] = {.125f, .375f, .625f, 1.0f};
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data{colour, sizeof(colour), 0};
        ComPtr<ID3D11Texture2D> texture;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, &data, texture.GetAddressOf())));

        for (unsigned slot : {20U, 127U})
        {
            // A minimal include permits explicitly reflected texture and sampler
            // registers while retaining the production wrapper and binding code.
            const std::string include = "Texture2D PrevFrameImage : register(t" + std::to_string(slot) +
                "); SamplerState sampler_main : register(s0);\n";
            strcpy_s(g_plugin.m_szShaderIncludeText, include.c_str());
            g_plugin.m_nShaderIncludeTextLen = include.size();
            const auto sampled = RenderProductionPresetShader(
                "shader_body {ret=PrevFrameImage.Sample(sampler_main,uv).xyz;}", "ps_4_0", texture.Get());
            for (size_t channel = 0; channel < sampled.size(); ++channel)
                Assert::AreEqual(colour[channel], sampled[channel]);
            ComPtr<ID3D11ShaderResourceView> bound;
            m_context->PSGetShaderResources(slot, 1, bound.GetAddressOf());
            Assert::IsNotNull(bound.Get(), L"The reflected high resource slot must receive the texture.");

            const auto next = RenderProductionPresetShader("shader_body {ret=float3(.25,.5,.75);}");
            Assert::AreEqual(.25f, next[0]);
            Assert::AreEqual(.5f, next[1]);
            Assert::AreEqual(.75f, next[2]);
            ID3D11ShaderResourceView* views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
            m_context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
            bool allEmpty = true;
            for (auto* view : views)
            {
                allEmpty = allEmpty && view == nullptr;
                SafeRelease(view);
            }
            Assert::IsTrue(allEmpty, L"The next shader must clear all prior resource slots.");

            RenderProductionPresetShader(
                "shader_body {ret=PrevFrameImage.Sample(sampler_main,uv).xyz;}", "ps_4_0", texture.Get());
            g_plugin.RestoreShaderParams();
            m_context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
            allEmpty = true;
            for (auto* view : views)
            {
                allEmpty = allEmpty && view == nullptr;
                SafeRelease(view);
            }
            Assert::IsTrue(allEmpty, L"Restoring fixed-function state must clear every resource slot.");
        }
    }

    TEST_METHOD(LongReflectedResourceNamesCannotAbortTheHostOrBlockOtherBindings)
    {
        *m_state->var_pf_q[0] = 0.375;
        const std::string name(300, 'a');
        const std::string source = "sampler2D sampler_" + name + ";float4 texsize_" + name +
            ";shader_body {ret=tex2D(sampler_" + name + ",uv).xyz+texsize_" + name + ".xyz+q1;}";
        const auto actual = RenderProductionPresetShader(source.c_str());
        for (int i = 0; i < 3; ++i) Assert::AreEqual(0.375f, actual[i]);
    }

    TEST_METHOD(LegacyRetInitialiserKeepsComputedGlobalResultRatherThanZero)
    {
        *m_state->var_pf_q[0] = 0.125;
        *m_state->var_pf_q[1] = 0.375;
        *m_state->var_pf_q[2] = 0.625;
        const auto actual = RenderProductionPresetShader(
            "float3 ret1,other; shader_body { ret1=float3(q1,q2,q3); float3 ret1=ret1; ret=ret1; }");
        Assert::AreEqual(0.125f, actual[0]);
        Assert::AreEqual(0.375f, actual[1]);
        Assert::AreEqual(0.625f, actual[2]);
        Assert::AreEqual(1.0f, actual[3]);
    }

    TEST_METHOD(OrdinaryLocalShaderOutputRemainsUnchanged)
    {
        const auto actual = RenderProductionPresetShader(
            "shader_body { float local=0.25; ret=float3(local,0.5,0.75); }");
        Assert::AreEqual(0.25f, actual[0]);
        Assert::AreEqual(0.5f, actual[1]);
        Assert::AreEqual(0.75f, actual[2]);
        Assert::AreEqual(1.0f, actual[3]);
    }

    TEST_METHOD(LegacyDoubleThreeProducesNonZeroRuntimeColourThroughSm4)
    {
        *m_state->var_pf_q[0] = 0.125;
        *m_state->var_pf_q[1] = 0.375;
        *m_state->var_pf_q[2] = 0.625;
        const auto actual = RenderProductionPresetShader(
            "shader_body { double3 tint=double3(q1,q2,q3); tint+=double3(0.125,0.125,0.125); ret=tint; }");
        Assert::AreEqual(0.25f, actual[0]);
        Assert::AreEqual(0.5f, actual[1]);
        Assert::AreEqual(0.75f, actual[2]);
        Assert::AreEqual(1.0f, actual[3]);
    }

    TEST_METHOD(LegacyPointLocalProducesNonZeroRuntimeColourThroughSm4)
    {
        *m_state->var_pf_q[0] = 0.125;
        *m_state->var_pf_q[1] = 0.375;
        *m_state->var_pf_q[2] = 0.75;
        const auto actual = RenderProductionPresetShader(
            "shader_body {float2 point=float2(q1,q2);point+=float2(0.125,0.125);ret=float3(point,q3);}");
        Assert::AreEqual(0.25f, actual[0]);
        Assert::AreEqual(0.5f, actual[1]);
        Assert::AreEqual(0.75f, actual[2]);
        Assert::AreEqual(1.0f, actual[3]);
    }
};
} // namespace MilkDrop2
