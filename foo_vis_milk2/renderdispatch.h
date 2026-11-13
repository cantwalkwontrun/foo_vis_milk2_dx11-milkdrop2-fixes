// One outstanding visual frame, including execution and modal UI pauses.
// Language: en-GB. SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <cstdint>

class Milk2RenderDispatch
{
    static constexpr uint64_t Pending=1, Active=2, Paused=4, Flags=7;
    std::atomic<uint64_t> m_state{0};
    static uint32_t Generation(uint64_t state) noexcept {return uint32_t(state>>3);}
public:
    bool TryQueue(uint32_t& token) noexcept
    {
        auto state=m_state.load(std::memory_order_acquire);
        while((state&Flags)==0)
        {
            uint32_t next=Generation(state)+1;
            if(!next) next=1;
            const uint64_t queued=(uint64_t(next)<<3)|Pending;
            if(m_state.compare_exchange_weak(state,queued,std::memory_order_acq_rel)) {token=next;return true;}
        }
        return false;
    }
    bool Begin(uint32_t token) noexcept
    {
        auto state=m_state.load(std::memory_order_acquire);
        while((state&Pending) && !(state&Active) && Generation(state)==token)
        {
            const bool paused=(state&Paused)!=0;
            const auto next=(state&~Pending)|(paused?0:Active);
            if(m_state.compare_exchange_weak(state,next,std::memory_order_acq_rel)) return !paused;
        }
        return false;
    }
    void Finish() noexcept {m_state.fetch_and(~Active,std::memory_order_acq_rel);}
    void ResetQueued() noexcept {m_state.fetch_and(~Pending,std::memory_order_acq_rel);}
    void CancelPost(uint32_t token) noexcept
    {
        auto state=m_state.load(std::memory_order_acquire);
        while((state&Pending) && Generation(state)==token)
            if(m_state.compare_exchange_weak(state,state&~Pending,std::memory_order_acq_rel)) return;
    }
    // Return the previous pause state so nested modal scopes restore it correctly.
    bool Pause() noexcept {return (m_state.fetch_or(Paused,std::memory_order_acq_rel)&Paused)!=0;}
    void Resume() noexcept {m_state.fetch_and(~Paused,std::memory_order_acq_rel);}
    bool IsPaused() const noexcept {return (m_state.load(std::memory_order_acquire)&Paused)!=0;}
};
