/*
 * resizelifecycle.cpp - No-window regressions for shell resource ownership.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <vis_milk2/framework.h>
#include <vis_milk2/pluginshell.h>
#include <CppUnitTest.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
namespace
{
class ResizeProbe final : public CPluginShell
{
public:
    unsigned allocations = 0;
    unsigned cleanups = 0;
    unsigned retainedState = 123;

    ResizeProbe()
    {
        m_fix_slow_text = 0;
        for (auto& font : m_fontinfo)
        {
            font = {};
            wcscpy_s(font.szFace, L"Segoe UI");
            font.nSize = 16;
        }
        DXCONTEXT_PARAMS params{};
        params.back_buffer_format = DXGI_FORMAT_B8G8R8A8_UNORM;
        params.depth_buffer_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        params.back_buffer_count = 2;
        params.min_feature_level = D3D_FEATURE_LEVEL_10_0;
        // This constructor creates neither a window nor a Direct3D device.
        m_lpDX = std::make_unique<DXContext>(nullptr, &params);
        m_lpDX->m_client_width = 64;
        m_lpDX->m_client_height = 32;
        m_lpDX->m_ready = true;
    }

    void OverrideDefaults() override {}
    void MilkDropPreInitialize() override {}
    void MilkDropReadConfig() override {}
    void MilkDropWriteConfig() override {}
    int AllocateMilkDropNonDX11() override { return true; }
    void CleanUpMilkDropNonDX11() override {}
    int AllocateMilkDropDX11() override { ++allocations; retainedState = 0; return true; }
    void CleanUpMilkDropDX11(int) override { ++cleanups; retainedState = 0; }
    void MilkDropRenderFrame(int) override {}
    void MilkDropRenderUI(int*, int*, int*, int*, int, int) override {}
    void OnAltK() override {}
    void DumpDebugMessage(const wchar_t*) override {}
    void PopupMessage(int, int, bool) override {}
    void ConsoleMessage(const wchar_t*, int, int) override {}
};
}

TEST_CLASS(ResizeLifecycleTest)
{
public:
    TEST_METHOD(UnchangedAndInvalidSizesPreserveOwnedResources)
    {
        ResizeProbe shell;
        for (int i = 0; i < 20; ++i)
            shell.OnWindowSizeChanged(64, 32);
        shell.OnWindowSizeChanged(0, 32);
        shell.OnWindowSizeChanged(64, 0);
        shell.OnWindowSizeChanged(-1, 32);
        Assert::AreEqual(0U, shell.allocations);
        Assert::AreEqual(0U, shell.cleanups);
        Assert::AreEqual(123U, shell.retainedState);
        Assert::AreEqual(64, shell.m_lpDX->m_client_width);
        Assert::AreEqual(32, shell.m_lpDX->m_client_height);
        Assert::IsTrue(shell.m_lpDX->m_ready != 0);
    }

    TEST_METHOD(FailedResizeAndInvalidSwapDoNotReleaseOwnedResources)
    {
        ResizeProbe shell;
        shell.OnWindowSwap(nullptr, 128, 64);
        // No HWND was created: the real DXContext must reject this resize.
        shell.OnWindowSizeChanged(128, 64);
        Assert::AreEqual(0U, shell.allocations);
        Assert::AreEqual(0U, shell.cleanups);
        Assert::AreEqual(123U, shell.retainedState);
        Assert::IsFalse(shell.m_lpDX->m_ready != 0);
        Assert::AreEqual(64, shell.m_lpDX->m_client_width,
            L"A rejected resize must not commit its requested dimensions.");
        Assert::AreEqual(32, shell.m_lpDX->m_client_height);
    }

    TEST_METHOD(UnavailableContextIgnoresResizeAndSwap)
    {
        ResizeProbe shell;
        shell.m_lpDX.reset();
        shell.OnWindowSizeChanged(128, 64);
        shell.OnWindowSwap(nullptr, 128, 64);
        Assert::AreEqual(0U, shell.allocations);
        Assert::AreEqual(0U, shell.cleanups);
        Assert::AreEqual(123U, shell.retainedState);
    }
};
}
