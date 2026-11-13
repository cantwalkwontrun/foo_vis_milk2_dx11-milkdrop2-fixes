// Frame deadlines must survive wake signals. Language: en-GB.
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <windows.h>
#include <atomic>

namespace Milk2FramePacer
{
enum class WaitResult { Ready, Recheck, Stopped };

// A wake requests another look at settings, pause state and the deadline;
// it never grants permission to post a frame early.
inline WaitResult WaitForDeadline(HANDLE stop, HANDLE wake, LONGLONG frequency,
    LONGLONG interval, std::atomic<LONGLONG>& deadline) noexcept
{
    const HANDLE events[] = {stop, wake};
    for (;;)
    {
        const auto stopped = WaitForSingleObject(stop, 0);
        if (stopped == WAIT_OBJECT_0 || stopped == WAIT_FAILED)
            return WaitResult::Stopped;
        if (frequency <= 0 || interval <= 0)
            return WaitResult::Ready;

        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) return WaitResult::Ready;
        auto next = deadline.load(std::memory_order_relaxed);
        if (next <= 0 || next < now.QuadPart - interval)
        {
            next = now.QuadPart + interval;
            deadline.store(next, std::memory_order_relaxed);
        }
        const auto remaining = next - now.QuadPart;
        if (remaining <= 0) return WaitResult::Ready;

        const auto milliseconds = static_cast<DWORD>((remaining * 1000LL) / frequency);
        if (milliseconds > 2)
        {
            const auto result = WaitForMultipleObjects(2, events, FALSE, milliseconds - 1);
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) return WaitResult::Stopped;
            if (result == WAIT_OBJECT_0 + 1) return WaitResult::Recheck;
        }
        else
            SwitchToThread();
    }
}
}
