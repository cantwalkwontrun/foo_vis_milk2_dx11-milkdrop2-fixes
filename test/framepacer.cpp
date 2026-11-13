// Real-event frame pacing regressions. Language: en-GB.
// SPDX-License-Identifier: MPL-2.0
#define WIN32_LEAN_AND_MEAN
#include <foo_vis_milk2/framepacer.h>
#include <CppUnitTest.h>
#include <thread>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
namespace
{
struct PacerEvents
{
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    PacerEvents() { Assert::IsTrue(stop != nullptr && wake != nullptr); }
    ~PacerEvents() { CloseHandle(wake); CloseHandle(stop); }
};
LONGLONG Clock() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
LONGLONG Frequency() { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return value.QuadPart; }
}

TEST_CLASS(FramePacerTest)
{
public:
    TEST_METHOD(AWakeSignalCannotGrantAnEarlyFrame)
    {
        PacerEvents events;
        const auto frequency = Frequency();
        std::atomic<LONGLONG> deadline{Clock() + 5 * frequency};
        SetEvent(events.wake);
        const auto result = Milk2FramePacer::WaitForDeadline(events.stop, events.wake, frequency, frequency, deadline);
        Assert::IsTrue(result == Milk2FramePacer::WaitResult::Recheck);
        Assert::IsTrue(Clock() < deadline.load());
    }
    TEST_METHOD(AWakeStormStillWaitsUntilTheRealDeadline)
    {
        PacerEvents events;
        const auto frequency = Frequency(), expected = Clock() + frequency / 5;
        std::atomic<LONGLONG> deadline{expected};
        std::thread producer([&] { for (unsigned i = 0; i < 10; ++i) { SetEvent(events.wake); Sleep(5); } });
        Milk2FramePacer::WaitResult result;
        do { result = Milk2FramePacer::WaitForDeadline(events.stop, events.wake, frequency, frequency, deadline); }
        while (result == Milk2FramePacer::WaitResult::Recheck);
        const auto completed = Clock();
        producer.join();
        Assert::IsTrue(result == Milk2FramePacer::WaitResult::Ready);
        Assert::IsTrue(completed >= expected);
    }
    TEST_METHOD(StoppingInterruptsAnOutstandingDeadline)
    {
        PacerEvents events;
        const auto frequency = Frequency();
        std::atomic<LONGLONG> deadline{Clock() + 5 * frequency};
        std::thread stopper([&] { Sleep(10); SetEvent(events.stop); });
        const auto result = Milk2FramePacer::WaitForDeadline(events.stop, events.wake, frequency, frequency, deadline);
        stopper.join();
        Assert::IsTrue(result == Milk2FramePacer::WaitResult::Stopped);
    }
    TEST_METHOD(AChangedDeadlineIsReadAfterTheWake)
    {
        PacerEvents events;
        const auto frequency = Frequency();
        std::atomic<LONGLONG> deadline{Clock() + 5 * frequency};
        SetEvent(events.wake);
        const auto first = Milk2FramePacer::WaitForDeadline(events.stop, events.wake, frequency, frequency, deadline);
        deadline = Clock() - 1;
        const auto second = Milk2FramePacer::WaitForDeadline(events.stop, events.wake, frequency, frequency, deadline);
        Assert::IsTrue(first == Milk2FramePacer::WaitResult::Recheck);
        Assert::IsTrue(second == Milk2FramePacer::WaitResult::Ready);
    }
    TEST_METHOD(InvalidStopHandlesFailClosed)
    {
        PacerEvents events;
        std::atomic<LONGLONG> deadline{Clock() + Frequency()};
        Assert::IsTrue(Milk2FramePacer::WaitForDeadline(nullptr, events.wake, Frequency(), Frequency(), deadline)
            == Milk2FramePacer::WaitResult::Stopped);
    }
    TEST_METHOD(AnObsoleteDeadlineIsRebasedWithoutAnImmediateFrame)
    {
        PacerEvents events;
        const auto frequency = Frequency(), before = Clock();
        std::atomic<LONGLONG> deadline{before - 10 * frequency};
        SetEvent(events.wake);
        const auto result = Milk2FramePacer::WaitForDeadline(events.stop, events.wake, frequency, frequency, deadline);
        Assert::IsTrue(result == Milk2FramePacer::WaitResult::Recheck);
        Assert::IsTrue(deadline.load() >= before + frequency);
    }
};
}
