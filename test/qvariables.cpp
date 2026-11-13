/*
 * qvariables.cpp - Tests for preset q-variable evaluation and shader uploads.
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
#include <CppUnitTest.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

// The library normally receives this instance from the component. Keep the test's
// state separate from the component DLL loaded by the interface smoke test.
CPlugin g_plugin;

using Microsoft::WRL::ComPtr;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
TEST_CLASS(QVariableTest)
{
  private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    std::unique_ptr<CState> m_state;
    std::filesystem::path m_fixtureDirectory;

    static std::string Assignments(double offset, bool add)
    {
        std::string code;
        for (int q = 1; q <= 64; ++q)
        {
            const std::string name = "q" + std::to_string(q);
            code += name + (add ? "+=" : "=") + std::to_string(add ? offset : q + offset) + ";";
        }
        return code;
    }

    void CompileFixture()
    {
        const std::string init = Assignments(0.25, false);
        const std::string frame = Assignments(100.0, true);
        strcpy_s(m_state->m_szPerFrameInit, init.c_str());
        strcpy_s(m_state->m_szPerFrameExpr, frame.c_str());
        strcpy_s(m_state->m_szPerPixelExpr, "zoom=q1+q32+q33+q36+q37+q60+q61+q64;");
        strcpy_s(m_state->m_wave[0].m_szInit, "t1=q1+q32+q33+q36+q37+q60+q61+q64;");
        // A zero sample count stops drawing after the production wave-to-point
        // transfer; the test can then execute and inspect the compiled EEL code.
        strcpy_s(m_state->m_wave[0].m_szPerFrame, "q64+=200;samples=0;");
        strcpy_s(m_state->m_wave[0].m_szPerPoint, "x=q1+q32+q33+q36+q37+q60+q61+q64;");
        strcpy_s(m_state->m_shape[0].m_szInit, "t1=q33+q64;");
        strcpy_s(m_state->m_shape[0].m_szPerFrame, "x=q1+q32+q33+q36+q37+q60+q61+q64;");
        m_state->m_wave[0].enabled = true;
        m_state->RecompileExpressions();
        Assert::IsNotNull(m_state->m_pf_codehandle);
        Assert::IsNotNull(m_state->m_pp_codehandle);
        Assert::IsNotNull(m_state->m_wave[0].m_pf_codehandle);
        Assert::IsNotNull(m_state->m_wave[0].m_pp_codehandle);
        Assert::IsNotNull(m_state->m_shape[0].m_pf_codehandle);
    }

    static void CheckValues(double* const* values, double offset)
    {
        for (int q = 1; q <= 64; ++q)
        {
            Assert::IsNotNull(values[q - 1]);
            Assert::AreEqual(q + offset, *values[q - 1], 0.0);
        }
    }

    static std::string ReadShaderInclude()
    {
        const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "external/winamp/data/include.fx";
        std::ifstream input(path, std::ios::binary);
        Assert::IsTrue(input.good(), L"The test must use the component's tracked shader include.");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void WritePresetFixture(const char* name, const char* text)
    {
        if (m_fixtureDirectory.empty())
        {
            const auto temporary = std::filesystem::temp_directory_path();
            for (int attempt = 0; attempt < 32; ++attempt)
            {
                const auto path = temporary / ("milkdrop-qvariables-" + std::to_string(GetCurrentProcessId()) +
                                               "-" + std::to_string(GetTickCount64()) + "-" + std::to_string(attempt));
                if (std::filesystem::create_directory(path))
                {
                    m_fixtureDirectory = path;
                    break;
                }
            }
            Assert::IsFalse(m_fixtureDirectory.empty(), L"Could not create an owned fixture directory.");
        }
        std::ofstream output(m_fixtureDirectory / name, std::ios::binary);
        output << text;
        output.close();
        Assert::IsTrue(output.good());
    }

    void CheckLastSlotEvaluation()
    {
        constexpr int slot = 15;
        Assert::IsTrue(m_state->m_wave[slot].enabled != 0);
        Assert::IsTrue(m_state->m_shape[slot].enabled != 0);
        Assert::IsNotNull(m_state->m_wave[slot].m_pf_codehandle);
        Assert::IsNotNull(m_state->m_wave[slot].m_pp_codehandle);
        Assert::IsNotNull(m_state->m_shape[slot].m_pf_codehandle);
        Assert::AreEqual(0.75, m_state->m_wave[slot].t_values_after_init_code[0], 0.0);
        Assert::AreEqual(0.75, m_state->m_shape[slot].t_values_after_init_code[0], 0.0);
        g_plugin.RunPerFrameEquations(0);
        Assert::AreEqual(0.5, *m_state->var_pf_q[32], 0.0);
        Assert::AreEqual(1.0, *m_state->var_pf_q[63], 0.0);
        g_plugin.LoadCustomShapePerFrameEvallibVars(m_state.get(), slot, 0);
        NSEEL_code_execute(m_state->m_shape[slot].m_pf_codehandle);
        Assert::AreEqual(1.5, *m_state->m_shape[slot].var_pf_x, 0.0);
        Assert::AreEqual(1.0, *m_state->m_shape[slot].var_pf_q[63], 0.0);
        g_plugin.DrawCustomWaves();
        Assert::AreEqual(0.5, *m_state->m_wave[slot].var_pp_q[32], 0.0);
        Assert::AreEqual(3.0, *m_state->m_wave[slot].var_pp_q[63], 0.0);
        NSEEL_code_execute(m_state->m_wave[slot].m_pp_codehandle);
        Assert::AreEqual(3.5, *m_state->m_wave[slot].var_pp_x, 0.0);
        Assert::AreEqual(1.0, *m_state->var_pf_q[63], 0.0);
    }

  public:
    TEST_METHOD(SpacedIniSeparatorsImportWithoutLeakingEqualsIntoCode)
    {
        WritePresetFixture("spaced-keys.milk", "// language: en-GB\n[preset00]\nfDecay \t = 0.875\n"
            "per_frame_init_1 \t= q1=3;\nper_frame_1\t= q2=q1+2;\n"
            "shape_0_init1 = t1=4;\nshape_0_per_frame1 =x=t1;\n"
            "wave_0_init1 =t1=5;\nwave_0_per_point1 =x=t1;\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "spaced-keys.milk").c_str(),0,nullptr));
        Assert::AreEqual(0.875f,m_state->m_fDecay.eval(-1),1e-6f);
        Assert::AreEqual(3.0,*m_state->var_pf_q[0],0.0);
        Assert::IsNotNull(m_state->m_pf_codehandle);
        Assert::IsNotNull(m_state->m_shape[0].m_pf_codehandle);
        Assert::IsNotNull(m_state->m_wave[0].m_pp_codehandle);
        Assert::IsTrue(std::string(m_state->m_shape[0].m_szPerFrame).find("=x=")==std::string::npos);
        WritePresetFixture("legacy-space.milk", "[preset00]\nfDecay 0.625\nper_frame_init_1 q1=7;\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "legacy-space.milk").c_str(),0,nullptr));
        Assert::AreEqual(0.625f,m_state->m_fDecay.eval(-1),1e-6f);
        Assert::AreEqual(7.0,*m_state->var_pf_q[0],0.0);
    }
    TEST_METHOD(LegacyNumericRecoveryCompilesInPresetWaveAndShapeCode)
    {
        strcpy_s(m_state->m_szPerFrameInit,";q1=.-2.5; q2=.99L;");
        strcpy_s(m_state->m_szPerFrameExpr,";q3=.+.25;");
        strcpy_s(m_state->m_wave[0].m_szPerPoint,";x=.-.25;");
        strcpy_s(m_state->m_shape[0].m_szPerFrame,";x=.-.5;");
        m_state->RecompileExpressions();
        Assert::AreEqual(-2.5,*m_state->var_pf_q[0],0.0);
        Assert::AreEqual(.99,*m_state->var_pf_q[1],1e-12);
        Assert::IsNotNull(m_state->m_pf_codehandle);
        Assert::IsNotNull(m_state->m_wave[0].m_pp_codehandle);
        Assert::IsNotNull(m_state->m_shape[0].m_pf_codehandle);
        Assert::IsTrue(std::string(m_state->m_szPerFrameInit)==";q1=.-2.5; q2=.99L;");
    }
    TEST_METHOD(IntrinsicNamedAndExtendedVariablesPersistAcrossExpressionGroups)
    {
        strcpy_s(m_state->m_szPerFrameInit,"sin=4;\xc3\xa9=5;q1=sin+\xc3\xa9;");
        strcpy_s(m_state->m_szPerFrameExpr,"q2=sin+\xc3\xa9+sin(0);");
        m_state->RecompileExpressions();
        Assert::AreEqual(9.0,*m_state->var_pf_q[0],0.0);
        Assert::IsNotNull(m_state->m_pf_codehandle);
        NSEEL_code_execute(m_state->m_pf_codehandle);
        Assert::AreEqual(9.0,*m_state->var_pf_q[1],0.0);
    }
    TEST_METHOD(MissingSeparatorsRecoverEveryExpressionGroupWithoutChangingText)
    {
        const char* init = "q1=1\1q2=2;";
        strcpy_s(m_state->m_szPerFrameInit,init);
        strcpy_s(m_state->m_szPerFrameExpr,"q3=3\1q4=4;");
        strcpy_s(m_state->m_szPerPixelExpr,"zoom=1\1rot=.25;");
        strcpy_s(m_state->m_wave[0].m_szInit,"t1=5\1t2=6;");
        strcpy_s(m_state->m_wave[0].m_szPerFrame,"t1=10\1t2=11;");
        strcpy_s(m_state->m_wave[0].m_szPerPoint,"x=.3\1y=.4;");
        strcpy_s(m_state->m_shape[0].m_szInit,"t1=7\1t2=8;");
        strcpy_s(m_state->m_shape[0].m_szPerFrame,"x=.2\1y=.6;");
        m_state->RecompileExpressions();
        Assert::AreEqual(1.0,m_state->q_values_after_init_code[0]);
        Assert::AreEqual(2.0,m_state->q_values_after_init_code[1]);
        Assert::AreEqual(5.0,m_state->m_wave[0].t_values_after_init_code[0]);
        Assert::AreEqual(6.0,m_state->m_wave[0].t_values_after_init_code[1]);
        Assert::AreEqual(7.0,m_state->m_shape[0].t_values_after_init_code[0]);
        Assert::AreEqual(8.0,m_state->m_shape[0].t_values_after_init_code[1]);
        Assert::IsTrue(std::string(m_state->m_szPerFrameInit)==init);
        for(auto program : {m_state->m_pf_codehandle,m_state->m_pp_codehandle,m_state->m_wave[0].m_pf_codehandle,
                           m_state->m_wave[0].m_pp_codehandle,m_state->m_shape[0].m_pf_codehandle})
        { Assert::IsNotNull(program); NSEEL_code_execute(program); }
        Assert::AreEqual(3.0,*m_state->var_pf_q[2]);
        Assert::AreEqual(.25,*NSEEL_VM_regvar(m_state->m_pv_eel,"rot"));
        Assert::AreEqual(.3,*m_state->m_wave[0].var_pp_x);
        Assert::AreEqual(.6,*m_state->m_shape[0].var_pf_y);
    }

    TEST_METHOD(SoundQueriesStayLiveAndDoNotReserveThePublicVariableName)
    {
        std::array<float,512> values;values.fill(2);
        g_plugin.m_expressionSpectrum.Update(values.data(),values.size(),44100);
        const char* source="sound=7;q1=sound+sound(15,22);";
        strcpy_s(m_state->m_szPerFrameExpr,source);
        m_state->RecompileExpressions(RECOMPILE_PRESET_CODE,1);
        Assert::IsNotNull(m_state->m_pf_codehandle);
        NSEEL_code_execute(m_state->m_pf_codehandle);
        Assert::AreEqual(21.0,*m_state->var_pf_q[0],1e-9);
        values.fill(4);g_plugin.m_expressionSpectrum.Update(values.data(),values.size(),44100);
        NSEEL_code_execute(m_state->m_pf_codehandle);
        Assert::AreEqual(35.0,*m_state->var_pf_q[0],1e-9);
        Assert::IsTrue(std::string(m_state->m_szPerFrameExpr)==source);
    }

    TEST_METHOD(SoundIsAvailableToPresetWaveAndShapeInitialisationAndDrawing)
    {
        std::array<float,512> values;values.fill(2);
        g_plugin.m_expressionSpectrum.Update(values.data(),values.size(),44100);
        strcpy_s(m_state->m_szPerFrameInit,"q1=sound(15,22);");
        strcpy_s(m_state->m_szPerFrameExpr,"q2=sound(15,22);");
        strcpy_s(m_state->m_szPerPixelExpr,"rot=sound(15,22);");
        strcpy_s(m_state->m_wave[0].m_szInit,"t1=sound(15,22);");
        strcpy_s(m_state->m_wave[0].m_szPerFrame,"t2=sound(15,22);");
        strcpy_s(m_state->m_wave[0].m_szPerPoint,"x=sound(15,22);");
        strcpy_s(m_state->m_shape[0].m_szInit,"t1=sound(15,22);");
        strcpy_s(m_state->m_shape[0].m_szPerFrame,"y=sound(15,22);");
        m_state->RecompileExpressions();
        Assert::AreEqual(14.0,m_state->q_values_after_init_code[0],1e-9);
        Assert::AreEqual(14.0,m_state->m_wave[0].t_values_after_init_code[0],1e-9);
        Assert::AreEqual(14.0,m_state->m_shape[0].t_values_after_init_code[0],1e-9);
        for(auto program : {m_state->m_pf_codehandle,m_state->m_pp_codehandle,m_state->m_wave[0].m_pf_codehandle,
                           m_state->m_wave[0].m_pp_codehandle,m_state->m_shape[0].m_pf_codehandle})
        { Assert::IsNotNull(program); NSEEL_code_execute(program); }
        Assert::AreEqual(14.0,*m_state->var_pf_q[1],1e-9);
        Assert::AreEqual(14.0,*NSEEL_VM_regvar(m_state->m_pv_eel,"rot"),1e-9);
        Assert::AreEqual(14.0,*m_state->m_wave[0].var_pp_x,1e-9);
        Assert::AreEqual(14.0,*m_state->m_shape[0].var_pf_y,1e-9);
    }

    TEST_METHOD(HostCallbacksAreContextBoundAndEvaluateArgumentsOnceInOrder)
    {
#ifndef NS_EEL2
        const auto callback=+[](void* data,double first,double last){return *static_cast<double*>(data)+10*first+last;};
        double firstBase=100,secondBase=200;
        std::unique_ptr<void,decltype(&NSEEL_VM_free)> first(NSEEL_VM_alloc(),NSEEL_VM_free),second(NSEEL_VM_alloc(),NSEEL_VM_free);
        Assert::IsTrue(NSEEL_VM_register_function2(first.get(),"_test_host",callback,&firstBase)!=0);
        Assert::IsTrue(NSEEL_VM_register_function2(second.get(),"_test_host",callback,&secondBase)!=0);
        Assert::IsFalse(NSEEL_VM_register_function2(first.get(),"_test_host",callback,&secondBase)!=0);
        char code[]="i=0;output=_test_host(i+=1,i+=1);";
        std::unique_ptr<void,decltype(&NSEEL_code_free)> a(NSEEL_code_compile(first.get(),code,0),NSEEL_code_free);
        std::unique_ptr<void,decltype(&NSEEL_code_free)> b(NSEEL_code_compile(second.get(),code,0),NSEEL_code_free);
        Assert::IsNotNull(a.get());Assert::IsNotNull(b.get());
        NSEEL_code_execute(a.get());NSEEL_code_execute(b.get());
        Assert::AreEqual(112.0,*NSEEL_VM_regvar(first.get(),"output"));
        Assert::AreEqual(212.0,*NSEEL_VM_regvar(second.get(),"output"));
        char literals[]="output=_test_host(1,2);";
        std::unique_ptr<void,decltype(&NSEEL_code_free)> literal(NSEEL_code_compile(first.get(),literals,0),NSEEL_code_free);
        Assert::IsNotNull(literal.get());firstBase=300;NSEEL_code_execute(literal.get());
        Assert::AreEqual(312.0,*NSEEL_VM_regvar(first.get(),"output"));
#endif
    }

    TEST_METHOD_INITIALIZE(InitialiseStateAndSoftwareDevice)
    {
        static_assert(NUM_Q_VAR == 64);
        Assert::IsTrue(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                                   nullptr, 0, D3D11_SDK_VERSION,
                                                   m_device.GetAddressOf(), nullptr, m_context.GetAddressOf())));
        DXCONTEXT_PARAMS params{};
        params.back_buffer_format = DXGI_FORMAT_B8G8R8A8_UNORM;
        params.depth_buffer_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        params.back_buffer_count = 2;
        params.min_feature_level = D3D_FEATURE_LEVEL_10_0;
        // Constructing DXContext does not start a window or a hardware device.
        g_plugin.m_lpDX = std::make_unique<DXContext>(nullptr, &params);
        g_plugin.m_lpDX->m_client_width = g_plugin.m_lpDX->m_client_height = 16;
        g_plugin.m_lpDX->m_lpDevice = std::make_unique<D3D11Shim>(m_device.Get(), m_context.Get());
        g_plugin.m_nTexSizeX = g_plugin.m_nTexSizeY = 16;
        g_plugin.m_fInvAspectX = g_plugin.m_fInvAspectY = 1.0f;
        g_plugin.m_fNextPresetTime = 30.0f;
        g_plugin.m_fPresetStartTime = g_plugin.m_fStartTime = 0.0f;
        m_state = std::make_unique<CState>();
        m_state->Initialize();
        g_plugin.m_pState = m_state.get();
        g_plugin.m_pOldState = nullptr;
        g_plugin.m_nVisualRecoveryStage = 0;
    }

    TEST_METHOD_CLEANUP(ReleaseStateAndSoftwareDevice)
    {
        g_plugin.m_pState = nullptr;
        if (m_state)
            m_state->Finish();
        m_state.reset();
        g_plugin.m_lpDX.reset();
        m_context.Reset();
        m_device.Reset();
        if (!m_fixtureDirectory.empty())
        {
            // Delete only the named synthetic files created by this fixture.
            // An unexpected file prevents directory removal rather than being deleted.
            std::error_code error;
            for (const auto name : {"last-slot.milk", "roundtrip.milk", "empty-slots.milk", "unreadable.milk"})
                std::filesystem::remove(m_fixtureDirectory / name, error);
            std::filesystem::remove(m_fixtureDirectory, error);
            m_fixtureDirectory.clear();
        }
    }

    TEST_METHOD(PresetInitAndFrameReachAllEvaluationContexts)
    {
        CompileFixture();
        for (int q = 1; q <= 64; ++q)
            Assert::AreEqual(q + 0.25, m_state->q_values_after_init_code[q - 1], 0.0);
        Assert::AreEqual(326.0, m_state->m_wave[0].t_values_after_init_code[0], 0.0);
        Assert::AreEqual(97.5, m_state->m_shape[0].t_values_after_init_code[0], 0.0);

        g_plugin.RunPerFrameEquations(0);
        CheckValues(m_state->var_pf_q, 100.25);
        CheckValues(m_state->var_pv_q, 100.25);
        NSEEL_code_execute(m_state->m_pp_codehandle);
        Assert::AreEqual(1126.0, *m_state->var_pv_zoom, 0.0);

        g_plugin.LoadCustomShapePerFrameEvallibVars(m_state.get(), 0, 0);
        CheckValues(m_state->m_shape[0].var_pf_q, 100.25);
        NSEEL_code_execute(m_state->m_shape[0].m_pf_codehandle);
        Assert::AreEqual(1126.0, *m_state->m_shape[0].var_pf_x, 0.0);

        g_plugin.DrawCustomWaves();
        for (int q = 1; q < 64; ++q)
            Assert::AreEqual(q + 100.25, *m_state->m_wave[0].var_pp_q[q - 1], 0.0);
        Assert::AreEqual(364.25, *m_state->m_wave[0].var_pp_q[63], 0.0);
        NSEEL_code_execute(m_state->m_wave[0].m_pp_codehandle);
        Assert::AreEqual(1326.0, *m_state->m_wave[0].var_pp_x, 0.0);
        // Wave-local modifications must not write back to preset q values.
        Assert::AreEqual(164.25, *m_state->var_pf_q[63], 0.0);
    }

    TEST_METHOD(ReinitialisingPresetClearsExtendedVariablesAndLocalContexts)
    {
        CompileFixture();
        g_plugin.RunPerFrameEquations(0);
        // Recompilation with new init code models a preset reload without a new
        // VM allocation, the case most likely to retain an earlier upper bank.
        strcpy_s(m_state->m_szPerFrameInit, "q1=7;q32=8;");
        m_state->m_szPerFrameExpr[0] = 0;
        m_state->RecompileExpressions();
        g_plugin.RunPerFrameEquations(0);
        g_plugin.LoadCustomWavePerFrameEvallibVars(m_state.get(), 0);
        g_plugin.LoadCustomShapePerFrameEvallibVars(m_state.get(), 0, 0);
        for (int q = 1; q <= 64; ++q)
        {
            const double expected = q == 1 ? 7.0 : (q == 32 ? 8.0 : 0.0);
            Assert::AreEqual(expected, m_state->q_values_after_init_code[q - 1], 0.0);
            Assert::AreEqual(expected, *m_state->var_pf_q[q - 1], 0.0);
            Assert::AreEqual(expected, *m_state->var_pv_q[q - 1], 0.0);
            Assert::AreEqual(expected, *m_state->m_wave[0].var_pf_q[q - 1], 0.0);
            Assert::AreEqual(expected, *m_state->m_shape[0].var_pf_q[q - 1], 0.0);
        }
    }

    TEST_METHOD(FailedImportsPreserveSettingsAndExecutableCodeForFullAndPartialLoads)
    {
        CompileFixture();
        const std::string identity(64, 'a');
        strcpy_s(m_state->m_presetContentDigest, identity.c_str());
        m_state->m_fRating = 4.25f;
        m_state->m_fWaveScale = 3.25f;
        m_state->m_fPresetStartTime = 17.0f;
        wcscpy_s(m_state->m_szDesc, L"Preserved preset");
        *m_state->var_pf_q[0] = 7.0;
        const std::string frameCode = m_state->m_szPerFrameExpr;
        const auto codeHandles = [&] {
            return std::array<NSEEL_CODEHANDLE, 5>{m_state->m_pf_codehandle, m_state->m_pp_codehandle,
                m_state->m_wave[0].m_pf_codehandle, m_state->m_wave[0].m_pp_codehandle,
                m_state->m_shape[0].m_pf_codehandle};
        };
        const auto originalHandles = codeHandles();

        WritePresetFixture("unreadable.milk", "[preset00]\nfRating=0\n");
        const auto unreadable = m_fixtureDirectory / "unreadable.milk";
        const auto missing = m_fixtureDirectory / "missing.milk";
        Assert::IsFalse(std::filesystem::exists(missing));
        const auto closeFile = [](void* file) { if (file && file != INVALID_HANDLE_VALUE) CloseHandle(file); };
        std::unique_ptr<void, decltype(closeFile)> locked(
            CreateFile(unreadable.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr), closeFile);
        Assert::IsTrue(locked.get() != INVALID_HANDLE_VALUE,
            L"A sharing lock must make the synthetic existing preset unreadable.");

        const auto finishState = [](CState* state) { if (state) { state->Finish(); delete state; } };
        std::unique_ptr<CState, decltype(finishState)> oldState(new CState, finishState);
        oldState->Initialize();
        oldState->m_fRating = .25f;
        for (const auto& path : {missing, unreadable})
        {
            for (DWORD flags : {DWORD(STATE_ALL), DWORD(STATE_WAVE), DWORD(STATE_MOTION)})
            {
                // Cover ordinary full loads, in-place mash-ups and the separate
                // destination/blend-source copy used by shader-locked loads.
                for (CState* source : {m_state.get(), oldState.get()})
                {
                    Assert::IsFalse(m_state->Import(path.c_str(), 99.0f, source, flags));
                    Assert::IsTrue(identity == m_state->m_presetContentDigest);
                    Assert::AreEqual(4.25f, m_state->m_fRating);
                    Assert::AreEqual(3.25f, m_state->m_fWaveScale.eval(-1));
                    Assert::AreEqual(17.0f, m_state->m_fPresetStartTime);
                    Assert::IsTrue(std::wstring(m_state->m_szDesc) == L"Preserved preset");
                    Assert::IsTrue(m_state->m_wave[0].enabled != 0);
                    Assert::IsTrue(frameCode == m_state->m_szPerFrameExpr);
                    Assert::IsTrue(codeHandles() == originalHandles,
                        L"A failed import must not free, replace or detach compiled expressions.");
                    Assert::AreEqual(7.0, *m_state->var_pf_q[0], 0.0);
                }
            }
        }
        // Pointer identity alone would miss invalidated executable code.
        NSEEL_code_execute(m_state->m_pf_codehandle);
        Assert::AreEqual(107.0, *m_state->var_pf_q[0], 0.0);
    }

    TEST_METHOD(ImportedIdentityFollowsBytesAndMixedImportsCannotClaimOriginalIdentity)
    {
        WritePresetFixture("identity-a.milk", "[preset00]\nfRating=3\n");
        WritePresetFixture("identity-b.milk", "[preset00]\nfRating=3\n");
        WritePresetFixture("identity-c.milk", "[preset00]\nfRating=4\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory/"identity-a.milk").c_str(),0,nullptr));
        const std::string identity=m_state->m_presetContentDigest;
        Assert::AreEqual(size_t(64),identity.size());
        Assert::IsTrue(m_state->Import((m_fixtureDirectory/"identity-b.milk").c_str(),0,nullptr));
        Assert::IsTrue(identity==m_state->m_presetContentDigest);
        Assert::IsTrue(m_state->Import((m_fixtureDirectory/"identity-c.milk").c_str(),0,nullptr));
        Assert::IsTrue(identity!=m_state->m_presetContentDigest);
        Assert::IsTrue(m_state->Import((m_fixtureDirectory/"identity-a.milk").c_str(),0,m_state.get(),STATE_WAVE));
        Assert::AreEqual(size_t(0),strlen(m_state->m_presetContentDigest));
    }

    TEST_METHOD(LastCustomSlotSurvivesImportExportAndReloadWithUpperQVariables)
    {
        static_assert(MAX_CUSTOM_WAVES == 16 && MAX_CUSTOM_SHAPES == 16);
        WritePresetFixture("last-slot.milk",
                           "// language: en-GB\n[preset00]\nfWaveAlpha=0\n"
                           "per_frame_init_1=q33=0.25;q64=0.5;\n"
                           "per_frame_1=q33+=0.25;q64+=0.5;\n"
                           "wavecode_15_enabled=1\nwavecode_15_samples=0\n"
                           "wave_15_init1=t1=q33+q64;\n"
                           "wave_15_per_frame1=q64+=2;samples=0;\n"
                           "wave_15_per_point1=x=q33+q64;\n"
                           "shapecode_15_enabled=1\nshapecode_15_sides=3\nshapecode_15_num_inst=1\n"
                           "shape_15_init1=t1=q33+q64;\n"
                           "shape_15_per_frame1=x=q33+q64;\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "last-slot.milk").c_str(), 0.0f, nullptr));
        for (int slot = 0; slot < 15; ++slot)
        {
            Assert::IsFalse(m_state->m_wave[slot].enabled != 0);
            Assert::IsFalse(m_state->m_shape[slot].enabled != 0);
        }
        CheckLastSlotEvaluation();
        Assert::IsTrue(m_state->Export((m_fixtureDirectory / "roundtrip.milk").c_str()));
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "roundtrip.milk").c_str(), 0.0f, nullptr));
        CheckLastSlotEvaluation();

        // Loading a preset without custom slot keys must disable the previous
        // slot, release its code and clear q64 across all expression contexts.
        WritePresetFixture("empty-slots.milk", "// language: en-GB\n[preset00]\nfWaveAlpha=0\nper_frame_init_1=q1=7;\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "empty-slots.milk").c_str(), 0.0f, nullptr));
        for (int slot = 0; slot < 16; ++slot)
        {
            Assert::IsFalse(m_state->m_wave[slot].enabled != 0);
            Assert::IsFalse(m_state->m_shape[slot].enabled != 0);
            Assert::IsNull(m_state->m_wave[slot].m_pf_codehandle);
            Assert::IsNull(m_state->m_wave[slot].m_pp_codehandle);
            Assert::IsNull(m_state->m_shape[slot].m_pf_codehandle);
            Assert::AreEqual(0.0, *m_state->m_wave[slot].var_pf_q[63], 0.0);
            Assert::AreEqual(0.0, *m_state->m_wave[slot].var_pp_q[63], 0.0);
            Assert::AreEqual(0.0, *m_state->m_shape[slot].var_pf_q[63], 0.0);
        }
        Assert::AreEqual(0.0, m_state->q_values_after_init_code[63], 0.0);
        Assert::AreEqual(7.0, *m_state->var_pf_q[0], 0.0);
    }

    TEST_METHOD(ProFFTControlsImportResetAndExportWithoutTouchingOriginalFiles)
    {
        WritePresetFixture("fft-controls.milk", "[preset00]\nFFTAttack=0.2\nFFTDecay=0.4\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "fft-controls.milk").c_str(), 0, nullptr));
        Assert::AreEqual(0.2f, m_state->m_fftAttack);
        Assert::AreEqual(0.4f, m_state->m_fftDecay);
        Assert::IsTrue(m_state->Export((m_fixtureDirectory / "fft-export.milk").c_str()));
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "fft-export.milk").c_str(), 0, nullptr));
        Assert::AreEqual(0.2f, m_state->m_fftAttack);
        Assert::AreEqual(0.4f, m_state->m_fftDecay);
        WritePresetFixture("fft-defaults.milk", "[preset00]\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "fft-defaults.milk").c_str(), 0, nullptr));
        Assert::AreEqual(0.5f, m_state->m_fftAttack);
        Assert::AreEqual(0.7f, m_state->m_fftDecay);
        WritePresetFixture("fft-invalid.milk", "[preset00]\nFFTAttack=-20\nFFTDecay=40\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "fft-invalid.milk").c_str(), 0, nullptr));
        Assert::AreEqual(0.0f, m_state->m_fftAttack);
        Assert::AreEqual(1.0f, m_state->m_fftDecay);
    }

    TEST_METHOD(SparsePresetKeepsWaveColourAndPositionDefaultsIndependentOfRotation)
    {
        const std::array<float, 5> defaults{m_state->m_fWaveR.eval(-1), m_state->m_fWaveG.eval(-1),
            m_state->m_fWaveB.eval(-1), m_state->m_fWaveX.eval(-1), m_state->m_fWaveY.eval(-1)};
        WritePresetFixture("sparse-wave.milk", "[preset00]\nrot=0.25\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "sparse-wave.milk").c_str(), 0, nullptr));
        const std::array<float, 5> actual{m_state->m_fWaveR.eval(-1), m_state->m_fWaveG.eval(-1),
            m_state->m_fWaveB.eval(-1), m_state->m_fWaveX.eval(-1), m_state->m_fWaveY.eval(-1)};
        Assert::IsTrue(actual == defaults);
        WritePresetFixture("explicit-wave.milk", "[preset00]\nwave_r=0.2\nwave_g=0.3\nwave_b=0.4\nwave_x=0.6\nwave_y=0.7\n");
        Assert::IsTrue(m_state->Import((m_fixtureDirectory / "explicit-wave.milk").c_str(), 0, nullptr));
        Assert::AreEqual(0.2f, m_state->m_fWaveR.eval(-1));
        Assert::AreEqual(0.7f, m_state->m_fWaveY.eval(-1));
    }

    TEST_METHOD(UploadsEveryQBankThroughProductionShaderParameters)
    {
        CompileFixture();
        g_plugin.RunPerFrameEquations(0);
        std::string source = ReadShaderInclude();
        source += "\nRWTexture2D<float4> result : register(u0);\n"
                  "[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
                  "float values[64]={";
        for (int q = 1; q <= 64; ++q)
            source += (q == 1 ? "" : ",") + std::string("q") + std::to_string(q);
        source += "};uint i=id.x*4;result[id.xy]=float4(values[i],values[i+1],values[i+2],values[i+3]);}";
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
        for (const auto handle : parameters.q_const_handles)
            Assert::IsNotNull(handle);
        g_plugin.ApplyShaderParams(&parameters, &table, m_state.get());
        Assert::IsTrue(table.ApplyChanges(m_context.Get()));
        std::vector<ID3D11Buffer*> buffers(table.GetBuffersCount());
        table.GetBuffers(buffers.data());
        m_context->CSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
        ComPtr<ID3D11ComputeShader> shader;
        Assert::IsTrue(SUCCEEDED(m_device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(),
                                                               nullptr, shader.GetAddressOf())));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 16;
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
        m_context->CSSetShader(shader.Get(), nullptr, 0);
        m_context->Dispatch(16, 1, 1);
        uav = nullptr;
        m_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        m_context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::array<float, 64> actual{};
        memcpy(actual.data(), mapped.pData, sizeof(actual));
        m_context->Unmap(staging.Get(), 0);
        for (int q = 1; q <= 64; ++q)
            Assert::AreEqual(q + 100.25f, actual[q - 1]);
    }
};
} // namespace MilkDrop2
