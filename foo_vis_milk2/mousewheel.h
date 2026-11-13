/*
 * mousewheel.h - Accumulate fractional wheel detents without amplifying input.
 * SPDX-License-Identifier: MPL-2.0
 */
#pragma once

class WheelDeltaAccumulator
{
  public:
    int Add(short delta) noexcept
    {
        // Win32 sends 120 units per detent, potentially split across packets.
        // Keep the signed remainder so reversing direction cancels partial input.
        const int total = m_remainder + static_cast<int>(delta);
        const int steps = total / 120;
        m_remainder = total % 120;
        return steps;
    }

    void Reset() noexcept { m_remainder = 0; }

  private:
    int m_remainder = 0;
};
