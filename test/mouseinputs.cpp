/*
 * mouseinputs.cpp - Tests for MilkDrop 3 PRO mouse input conventions.
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#include <vis_milk2/framework.h>
#include <foo_vis_milk2/mousewheel.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vis_milk2/eelcompat.h>
#include <vis_milk2/mouseinput.h>
#include <vis_milk2/plugin.h>
#include <CppUnitTest.h>

extern CPlugin g_plugin;
using Microsoft::WRL::ComPtr;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
TEST_CLASS(VolumeWheelInputTest)
{
  public:
    TEST_METHOD(FractionalPacketsProduceTheSameStepsAsOneDetent)
    {
        for (const short direction : {short(1), short(-1)})
        {
            WheelDeltaAccumulator fragmented, whole;
            for (int packet = 0; packet < 7; ++packet)
                Assert::AreEqual(0, fragmented.Add(static_cast<short>(15 * direction)));
            Assert::AreEqual(whole.Add(static_cast<short>(120 * direction)),
                             fragmented.Add(static_cast<short>(15 * direction)));
            Assert::AreEqual(0, fragmented.Add(0));
        }
    }

    TEST_METHOD(DirectionReversalsCancelUnconsumedMovement)
    {
        WheelDeltaAccumulator wheel;
        Assert::AreEqual(0, wheel.Add(90));
        Assert::AreEqual(0, wheel.Add(-30));
        Assert::AreEqual(1, wheel.Add(60));
        Assert::AreEqual(0, wheel.Add(-90));
        Assert::AreEqual(0, wheel.Add(90));
        Assert::AreEqual(-1, wheel.Add(-120));
        Assert::AreEqual(1, wheel.Add(120));
    }

    TEST_METHOD(MultipleDetentsPreserveTheirRemainderInEitherDirection)
    {
        WheelDeltaAccumulator wheel;
        Assert::AreEqual(2, wheel.Add(300));
        Assert::AreEqual(1, wheel.Add(60));
        Assert::AreEqual(-2, wheel.Add(-300));
        Assert::AreEqual(-1, wheel.Add(-60));
    }

    TEST_METHOD(IndependentViewsAndResetDoNotSharePartialInput)
    {
        WheelDeltaAccumulator first, second;
        Assert::AreEqual(0, first.Add(90));
        Assert::AreEqual(0, second.Add(30));
        first.Reset();
        Assert::AreEqual(0, first.Add(30));
        Assert::AreEqual(1, second.Add(90));
        Assert::AreEqual(1, first.Add(90));
    }
};

TEST_CLASS(MouseEelCompatibilityTest)
{
  public:
    TEST_METHOD(RewritesOnlyStandaloneDottedMouseInputsCaseInsensitively)
    {
        const std::string source = "q1=mouse.x;q2=MoUsE.Y;q3=MOUSE.z;q4=mouse.W;";
        std::string expected;
        for (int component = 0; component < 4; ++component)
            expected += "q" + std::to_string(component + 1) + "=" + PresetEelCompat::MouseNames[component] + ";";
        Assert::IsTrue(PresetEelCompat::RewriteMouseIdentifiers(source) == expected);
        for (const char* unchanged : {"mymouse.x", "_mouse.x", "mouse.xy", "mouse.x_suffix", "mouse.x1",
                                      "mouse.x.y", "mouse.z_", "mouse_x", "mouse . x", "mouse..x"})
            Assert::IsTrue(PresetEelCompat::RewriteMouseIdentifiers(unchanged) == unchanged,
                           L"Unrelated identifiers or dotted chains must not be partially rewritten.");
    }

    TEST_METHOD(PreservesCommentsQuotedStringsAndEscapedQuotes)
    {
        const std::string source = "// mouse.x\nq1=mouse.x;\\\\ mouse.y\nq2=mouse.y;"
                                   "/* mouse.z */\"mouse.w\"'mouse.x'\"escaped \\\" mouse.y\"q3=mouse.z;";
        const std::string expected = "// mouse.x\nq1=" + std::string(PresetEelCompat::MouseNames[0]) +
            ";\\\\ mouse.y\nq2=" + PresetEelCompat::MouseNames[1] +
            ";/* mouse.z */\"mouse.w\"'mouse.x'\"escaped \\\" mouse.y\"q3=" + PresetEelCompat::MouseNames[2] + ";";
        Assert::IsTrue(PresetEelCompat::RewriteMouseIdentifiers(source) == expected,
                       L"Only live identifier tokens may change; quoted/comment bytes must remain intact.");
    }

    TEST_METHOD(ExpandedIdentifiersDoNotTruncateLongPresetCode)
    {
        std::string source, expected;
        for (int i = 0; i < 2000; ++i)
        {
            source += "q1=mouse.x;";
            expected += "q1=" + std::string(PresetEelCompat::MouseNames[0]) + ";";
        }
        source += "q2=mouse_x;";
        expected += "q2=mouse_x;";
        Assert::IsTrue(source.size() < MAX_BIGSTRING_LEN);
        Assert::IsTrue(PresetEelCompat::RewriteMouseIdentifiers(source) == expected,
                       L"Expansion must retain the complete preset and the unrelated final variable.");
    }
};

TEST_CLASS(WhileExecutionBudgetTest)
{
  private:
    static double Evaluate(const char* source, bool bounded, unsigned budget=64)
    {
        struct Execution
        {
            NSEEL_VMCTX vm=NSEEL_VM_alloc();NSEEL_CODEHANDLE code=nullptr;
            ~Execution(){if(code)NSEEL_code_free(code);if(vm)NSEEL_VM_free(vm);}
        } execution;
        Assert::IsNotNull(execution.vm);
        std::string text=bounded?PresetEelCompat::BoundWhileIterations(source,budget):std::string(source);
        execution.code=NSEEL_code_compile(execution.vm,text.data(),0);
        if(!execution.code)Logger::WriteMessage(text.c_str());
        Assert::IsNotNull(execution.code);
        double result=0,first=0;
        for(int run=0;run<2;++run)
        {
            NSEEL_code_execute(execution.code);
            result=*NSEEL_VM_regvar(execution.vm,"result");
            if(run==0)first=result;
            else Assert::AreEqual(first,result);
        }
        return result;
    }
  public:
    TEST_METHOD(SharedBudgetStopsNestedNonTerminatingWhilesAndResetsForEachExecution)
    {
        Assert::AreEqual(63.0,Evaluate("o=0;i=0;while((o+=1;while((i+=1;1));1));result=i;",true));
    }
    TEST_METHOD(FiniteNestedAndCountedLoopsKeepTheirResults)
    {
        for(const char* source:{
            "i=0;s=0;while((i+=1;s+=i;i<4));loop(12,s+=2);result=s;",
            "i=0;while((i+=1;i<50));j=0;while((j+=1;j<50));result=i+j;",
            "o=0;s=0;WHILE((o+=1;i=0;while((i+=1;s+=i;i<3));o<4));result=s;"})
            Assert::AreEqual(Evaluate(source,false),Evaluate(source,true));
    }
    TEST_METHOD(UnrelatedTokensCommentsStringsAndUnbalancedCodeStayUntouched)
    {
        for(const char* source:{"result=12;loop(3,result+=2);","foo.while(x);","// while(1)\nresult=1;",
            "/*while(1)*/\"while(1)\"'while(1)'","while((x+=1;x<2);","while(/* unterminated",
            "function f(x) (while((x-=1;x>0));x;);result=f(4);"})
            Assert::IsTrue(PresetEelCompat::BoundWhileIterations(source)==source);
    }
};

TEST_CLASS(PresetMouseStateTest)
{
  public:
    TEST_METHOD(CoordinatesUseEelBottomOriginAndShaderTopOrigin)
    {
        PresetMouseState mouse;
        mouse.Update(50, 25, 200, 100, false);
        Assert::AreEqual(0.25f, mouse.EelValues()[0]);
        Assert::AreEqual(0.75f, mouse.EelValues()[1]);
        Assert::AreEqual(0.25f, mouse.ShaderValues()[0]);
        Assert::AreEqual(0.25f, mouse.ShaderValues()[1]);
        mouse.Update(0, 0, 200, 100, false);
        Assert::AreEqual(0.0f, mouse.EelValues()[0]);
        Assert::AreEqual(1.0f, mouse.EelValues()[1]);
        Assert::AreEqual(0.0f, mouse.ShaderValues()[1]);
        mouse.Update(199, 99, 200, 100, false);
        Assert::AreEqual(0.995f, mouse.EelValues()[0], 0.000001f);
        Assert::AreEqual(0.01f, mouse.EelValues()[1], 0.000001f);
        Assert::AreEqual(0.99f, mouse.ShaderValues()[1], 0.000001f);
    }

    TEST_METHOD(OutsideZeroAreaAndUnavailablePositionsPreserveMinusOne)
    {
        PresetMouseState mouse;
        const std::array<std::array<int, 4>, 7> cases = {{{-1, 25, 200, 100}, {50, -1, 200, 100},
            {200, 25, 200, 100}, {50, 100, 200, 100}, {0, 0, 0, 100}, {0, 0, 200, 0}, {0, 0, -1, 100}}};
        for (const auto& sample : cases)
        {
            mouse.Update(sample[0], sample[1], sample[2], sample[3], false);
            Assert::AreEqual(-1.0f, mouse.EelValues()[0]);
            Assert::AreEqual(-1.0f, mouse.EelValues()[1]);
            Assert::AreEqual(-1.0f, mouse.ShaderValues()[0]);
            Assert::AreEqual(-1.0f, mouse.ShaderValues()[1]);
        }
        mouse.Update(50, 25, 200, 100, true, false);
        Assert::AreEqual(-1.0f, mouse.EelValues()[0]);
        Assert::AreEqual(-1.0f, mouse.EelValues()[1]);
        Assert::AreEqual(1.0f, mouse.EelValues()[2]);
        mouse.Update(50, 25, 200, 100, false, false);
        Assert::AreEqual(1.0f, mouse.EelValues()[3]);
    }

    TEST_METHOD(ReleasePulseLastsOneUpdateAndTracksButtonOutsideClient)
    {
        PresetMouseState mouse;
        mouse.Update(50, 25, 200, 100, false);
        Assert::AreEqual(0.0f, mouse.EelValues()[3]);
        mouse.Update(50, 25, 200, 100, true);
        Assert::AreEqual(1.0f, mouse.EelValues()[2]);
        Assert::AreEqual(0.0f, mouse.EelValues()[3]);
        mouse.Update(-1, -1, 200, 100, true);
        Assert::AreEqual(1.0f, mouse.EelValues()[2]);
        Assert::AreEqual(0.0f, mouse.EelValues()[3]);
        mouse.Update(-1, -1, 200, 100, false);
        Assert::AreEqual(0.0f, mouse.EelValues()[2]);
        Assert::AreEqual(1.0f, mouse.EelValues()[3]);
        Assert::AreEqual(1.0f, mouse.ShaderValues()[3]);
        mouse.Update(50, 25, 200, 100, false);
        Assert::AreEqual(0.0f, mouse.EelValues()[3]);
        mouse.Update(50, 25, 200, 100, true);
        mouse.Reset();
        mouse.Update(50, 25, 200, 100, false);
        Assert::AreEqual(0.0f, mouse.EelValues()[2]);
        Assert::AreEqual(0.0f, mouse.EelValues()[3], L"Reset must not manufacture a release from an earlier session.");
    }
};

TEST_CLASS(MousePipelineTest)
{
  private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    std::unique_ptr<CState> m_state;

    static std::string ReadShaderInclude()
    {
        const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() / "external/winamp/data/include.fx";
        std::ifstream input(path, std::ios::binary);
        Assert::IsTrue(input.good());
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::array<float, 12> ExecuteMouseShader(CConstantTable& table, ID3D11ComputeShader* shader)
    {
        std::vector<ID3D11Buffer*> buffers(table.GetBuffersCount());
        table.GetBuffers(buffers.data());
        m_context->CSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 3;
        desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> output, staging;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, output.GetAddressOf())));
        ComPtr<ID3D11UnorderedAccessView> view;
        Assert::IsTrue(SUCCEEDED(m_device->CreateUnorderedAccessView(output.Get(), nullptr, view.GetAddressOf())));
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        ID3D11UnorderedAccessView* uav = view.Get();
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        m_context->CSSetShader(shader, nullptr, 0);
        m_context->Dispatch(1, 1, 1);
        uav = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        m_context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::array<float, 12> actual{};
        memcpy(actual.data(), mapped.pData, sizeof(actual));
        m_context->Unmap(staging.Get(), 0);
        return actual;
    }

  public:
    TEST_METHOD_INITIALIZE(InitialiseMousePipeline)
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
        g_plugin.m_lpDX->m_client_width = g_plugin.m_lpDX->m_client_height = 16;
        g_plugin.m_lpDX->m_lpDevice = std::make_unique<D3D11Shim>(m_device.Get(), m_context.Get());
        g_plugin.m_nTexSizeX = g_plugin.m_nTexSizeY = 16;
        g_plugin.m_fNextPresetTime = 30.0f;
        g_plugin.m_fPresetStartTime = g_plugin.m_fStartTime = 0.0f;
        g_plugin.m_presetMouse.Reset();
        m_state = std::make_unique<CState>();
        m_state->Initialize();
        g_plugin.m_pState = m_state.get();
        g_plugin.m_pOldState = nullptr;
    }

    TEST_METHOD_CLEANUP(ReleaseMousePipeline)
    {
        g_plugin.m_pState = nullptr;
        if (m_state)
            m_state->Finish();
        m_state.reset();
        g_plugin.m_presetMouse.Reset();
        g_plugin.m_lpDX.reset();
        m_context.Reset();
        m_device.Reset();
    }

    TEST_METHOD(RegisteredMouseVariablesReachPerFrameAndPerVertexCode)
    {
        strcpy_s(m_state->m_szPerFrameExpr, "q1=mouse.x;q2=mouse.y;q3=mouse.z;q4=mouse.w;");
        strcpy_s(m_state->m_szPerPixelExpr, "zoom=mouse.x+10*mouse.y+100*mouse.z+1000*mouse.w;");
        m_state->RecompileExpressions();
        Assert::IsNotNull(m_state->m_pf_codehandle);
        Assert::IsNotNull(m_state->m_pp_codehandle);
        g_plugin.m_presetMouse.Update(50, 25, 200, 100, true);
        g_plugin.RunPerFrameEquations(0);
        Assert::AreEqual(0.25, *m_state->var_pf_q[0], 0.0);
        Assert::AreEqual(0.75, *m_state->var_pf_q[1], 0.0);
        Assert::AreEqual(1.0, *m_state->var_pf_q[2], 0.0);
        Assert::AreEqual(0.0, *m_state->var_pf_q[3], 0.0);
        NSEEL_code_execute(m_state->m_pp_codehandle);
        Assert::AreEqual(107.75, *m_state->var_pv_zoom, 0.0);
        g_plugin.m_presetMouse.Update(-1, -1, 200, 100, false);
        g_plugin.RunPerFrameEquations(0);
        Assert::AreEqual(-1.0, *m_state->var_pf_q[0], 0.0);
        Assert::AreEqual(-1.0, *m_state->var_pf_q[1], 0.0);
        Assert::AreEqual(0.0, *m_state->var_pf_q[2], 0.0);
        Assert::AreEqual(1.0, *m_state->var_pf_q[3], 0.0);
        NSEEL_code_execute(m_state->m_pp_codehandle);
        Assert::AreEqual(989.0, *m_state->var_pv_zoom, 0.0);
        g_plugin.m_presetMouse.Update(-1, -1, 200, 100, false);
        g_plugin.RunPerFrameEquations(0);
        Assert::AreEqual(0.0, *m_state->var_pf_q[3], 0.0);
    }

    TEST_METHOD(ProductionMouseUploadUsesProShaderAliasesAndIgnoresEelWrites)
    {
        const std::string source = ReadShaderInclude() +
            "\nRWTexture2D<float4> result : register(u0);\n"
            "[numthreads(1,1,1)] void main() {"
            "result[uint2(0,0)]=mouse;"
            "result[uint2(1,0)]=float4(mouse_x,mouse_y,mouse_z,mouse_w);"
            "result[uint2(2,0)]=float4(mouse_pos,mouse_z,mouse_w);}";
        ComPtr<ID3DBlob> code, errors;
        const HRESULT hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
                                      "main", "cs_5_0", D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY,
                                      0, code.GetAddressOf(), errors.GetAddressOf());
        if (errors)
            Logger::WriteMessage(static_cast<const char*>(errors->GetBufferPointer()));
        Assert::IsTrue(SUCCEEDED(hr));
        ComPtr<ID3D11ShaderReflection> reflection;
        Assert::IsTrue(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                                           __uuidof(ID3D11ShaderReflection),
                                           reinterpret_cast<void**>(reflection.GetAddressOf()))));
        CConstantTable table(reflection.Detach());
        Assert::IsTrue(table.GrabShaderData(m_device.Get()));
        CShaderParams parameters;
        parameters.CacheParams(&table, false);
        Assert::IsNotNull(parameters.const_handles[14]);
        ComPtr<ID3D11ComputeShader> shader;
        Assert::IsTrue(SUCCEEDED(m_device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(),
                                                               nullptr, shader.GetAddressOf())));
        const std::array<std::array<float, 4>, 2> expected = {{{0.25f, 0.25f, 1, 0}, {-1, -1, 0, 1}}};
        for (size_t sample = 0; sample < expected.size(); ++sample)
        {
            g_plugin.m_presetMouse.Update(sample == 0 ? 50 : -1, sample == 0 ? 25 : -1,
                                          200, 100, sample == 0);
            // Mutating the writable EEL variable must not corrupt raw GPU input.
            *m_state->var_pf_mouse[0] = 99.0;
            *m_state->var_pf_mouse[1] = 99.0;
            g_plugin.ApplyShaderParams(&parameters, &table, m_state.get());
            Assert::IsTrue(table.ApplyChanges(m_context.Get()));
            const auto actual = ExecuteMouseShader(table, shader.Get());
            for (size_t component = 0; component < actual.size(); ++component)
                Assert::AreEqual(expected[sample][component % 4], actual[component]);
        }
    }

    TEST_METHOD(DottedMouseInputsDoNotAliasOrdinaryPresetVariableNames)
    {
        // These are legal user variables, not alternate spellings of the PRO
        // dotted inputs. In particular, shader-style mouse_x must stay separate.
        strcpy_s(m_state->m_szPerFrameExpr,
                 "mouse_x=17;mouse_y=19;mymouse_x=23;mouse_x_suffix=29;"
                 "q1=mouse_x+mouse_y+mymouse_x+mouse_x_suffix;"
                 "q2=MoUsE.X;q3=MOUSE.Y;q4=mouse.z;");
        strcpy_s(m_state->m_szPerPixelExpr, "zoom=mouse.x+10*mouse.y;");
        m_state->RecompileExpressions();
        Assert::IsNotNull(m_state->m_pf_codehandle);
        Assert::IsNotNull(m_state->m_pp_codehandle);
        g_plugin.m_presetMouse.Update(50, 25, 200, 100, true);
        g_plugin.RunPerFrameEquations(0);
        Assert::AreEqual(88.0, *m_state->var_pf_q[0], 0.0);
        Assert::AreEqual(0.25, *m_state->var_pf_q[1], 0.0);
        Assert::AreEqual(0.75, *m_state->var_pf_q[2], 0.0);
        Assert::AreEqual(1.0, *m_state->var_pf_q[3], 0.0);
        NSEEL_code_execute(m_state->m_pp_codehandle);
        Assert::AreEqual(7.75, *m_state->var_pv_zoom, 0.0);
    }
};
} // namespace MilkDrop2
