// Language: en-GB. SPDX-License-Identifier: MPL-2.0
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <foo_vis_milk2/renderdispatch.h>
#include <CppUnitTest.h>
#include <thread>
#include <algorithm>
#include <atomic>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace MilkDrop2
{
struct DispatchWindow
{
    Milk2RenderDispatch gate;
    unsigned frames=0,depth=0,maximumDepth=0,modalFrames=0;
    bool queuedWhileActive=false,insideModal=false;
};
static LRESULT CALLBACK DispatchProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
    if(message==WM_NCCREATE)
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams));
    auto* state=reinterpret_cast<DispatchWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_APP+18 && state)
    {
        state->insideModal=true;
        for(unsigned i=0;i<3;++i)
        {
            uint32_t token=0;
            if(state->gate.TryQueue(token)) PostMessageW(window,WM_APP+17,token,0);
            MSG nested{};while(PeekMessageW(&nested,window,WM_APP+17,WM_APP+17,PM_REMOVE)) DispatchMessageW(&nested);
        }
        state->insideModal=false;
        return 0;
    }
    if(message==WM_APP+17 && state)
    {
        if(!state->gate.Begin(static_cast<uint32_t>(wParam))) return 0;
        ++state->frames;++state->depth;state->maximumDepth=(std::max)(state->maximumDepth,state->depth);
        if(state->insideModal) ++state->modalFrames;
        uint32_t next=0;state->queuedWhileActive=state->queuedWhileActive||state->gate.TryQueue(next);
        if(lParam==0)
        {
            // A nested Win32 modal pump sees a duplicate old frame. It must
            // not enter the renderer again or consume a newer request.
            PostMessageW(window,WM_APP+17,wParam,1);
            MSG nested{};while(PeekMessageW(&nested,window,WM_APP+17,WM_APP+17,PM_REMOVE)) DispatchMessageW(&nested);
        }
        --state->depth;state->gate.Finish();return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
TEST_CLASS(RenderDispatchTest)
{
public:
    TEST_METHOD(DelayedQueuedFramesCannotGenerateAWatchdogBacklog)
    {
        Milk2RenderDispatch gate;uint32_t token=0,extra=0;Assert::IsTrue(gate.TryQueue(token));
        for(unsigned i=0;i<10000;++i) Assert::IsFalse(gate.TryQueue(extra));
        Assert::IsTrue(gate.Begin(token));Assert::IsFalse(gate.TryQueue(extra));gate.Finish();
        Assert::IsTrue(gate.TryQueue(extra));Assert::IsTrue(extra!=token);
    }
    TEST_METHOD(AnActiveFrameRejectsNestedDispatchAndQueueAttempts)
    {
        Milk2RenderDispatch gate;uint32_t token=0,next=0;gate.TryQueue(token);Assert::IsTrue(gate.Begin(token));
        Assert::IsFalse(gate.Begin(token));Assert::IsFalse(gate.TryQueue(next));
        gate.ResetQueued();Assert::IsFalse(gate.TryQueue(next));gate.Finish();Assert::IsTrue(gate.TryQueue(next));
    }
    TEST_METHOD(StaleMessagesCannotConsumeANewerFrameAfterResume)
    {
        Milk2RenderDispatch gate;uint32_t old=0,current=0;gate.TryQueue(old);gate.ResetQueued();gate.TryQueue(current);
        Assert::IsTrue(old!=current);Assert::IsFalse(gate.Begin(old));
        Assert::IsTrue(gate.Begin(current));gate.Finish();
    }
    TEST_METHOD(ModalMenusSuppressRenderingAndResumeWithoutABurst)
    {
        Milk2RenderDispatch gate;uint32_t token=0,next=0;gate.TryQueue(token);
        Assert::IsFalse(gate.Pause());Assert::IsTrue(gate.IsPaused());Assert::IsFalse(gate.Begin(token));
        for(unsigned i=0;i<1000;++i) Assert::IsFalse(gate.TryQueue(next));
        gate.Resume();Assert::IsTrue(gate.TryQueue(next));Assert::IsTrue(gate.Begin(next));gate.Finish();
    }
    TEST_METHOD(NestedModalScopesAndAnActiveFramePreserveTheOuterPause)
    {
        Milk2RenderDispatch gate;uint32_t token=0,next=0;gate.TryQueue(token);gate.Begin(token);
        const bool outer=gate.Pause(),inner=gate.Pause();Assert::IsFalse(outer);Assert::IsTrue(inner);
        if(!inner)gate.Resume();gate.Finish();Assert::IsFalse(gate.TryQueue(next));
        if(!outer)gate.Resume();Assert::IsTrue(gate.TryQueue(next));
    }
    TEST_METHOD(FailedPostsCanBeCancelledWithoutCancellingNewerMessages)
    {
        Milk2RenderDispatch gate;uint32_t old=0,current=0;gate.TryQueue(old);gate.CancelPost(old);
        Assert::IsTrue(gate.TryQueue(current));gate.CancelPost(old);Assert::IsTrue(gate.Begin(current));gate.Finish();
    }
    TEST_METHOD(ConcurrentProducersCannotRunTwoFramesTogether)
    {
        Milk2RenderDispatch gate;std::atomic<unsigned> active{0},errors{0},frames{0};
        auto producer=[&] {for(unsigned i=0;i<5000;++i){uint32_t token=0;if(gate.TryQueue(token)&&gate.Begin(token))
            {if(active.fetch_add(1)!=0)++errors;++frames;active.fetch_sub(1);gate.Finish();}}};
        std::thread first(producer),second(producer);first.join();second.join();
        Assert::AreEqual(0U,errors.load());Assert::IsTrue(frames.load()>0);
    }
    TEST_METHOD(RealNestedWindowDispatchDoesNotReenterTheRenderer)
    {
        constexpr auto name=L"Milk2RenderDispatchTestWindow";WNDCLASSW wc{};
        wc.hInstance=GetModuleHandle(nullptr);wc.lpfnWndProc=DispatchProcedure;wc.lpszClassName=name;
        const auto registered=RegisterClassW(&wc);DispatchWindow state;
        HWND window=registered?CreateWindowExW(0,name,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,&state):nullptr;
        if(window)
        {
            uint32_t token=0;state.gate.TryQueue(token);PostMessageW(window,WM_APP+17,token,0);
            MSG message{};while(PeekMessageW(&message,window,WM_APP+17,WM_APP+17,PM_REMOVE))DispatchMessageW(&message);
            // The menu's nested pump can keep advancing complete frames;
            // duplicate messages inside each active frame still cannot re-enter.
            PostMessageW(window,WM_APP+18,0,0);
            while(PeekMessageW(&message,window,WM_APP+18,WM_APP+18,PM_REMOVE))DispatchMessageW(&message);
            DestroyWindow(window);
        }
        if(registered)UnregisterClassW(name,wc.hInstance);
        Assert::IsTrue(window!=nullptr);Assert::AreEqual(4U,state.frames);Assert::AreEqual(3U,state.modalFrames);
        Assert::AreEqual(1U,state.maximumDepth);
        Assert::IsFalse(state.queuedWhileActive);
    }
};
}
