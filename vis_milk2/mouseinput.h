// SPDX-License-Identifier: MPL-2.0
#ifndef MILKDROP_PRESET_MOUSE_INPUT_H
#define MILKDROP_PRESET_MOUSE_INPUT_H

#include <array>

// MilkDrop 3PRO exposes bottom-origin EEL coordinates and top-origin shader coordinates.
class PresetMouseState
{
  public:
    void Reset() noexcept
    {
        m_values = {-1.0f, -1.0f, 0.0f, 0.0f};
        m_leftWasDown = false;
    }

    void Update(int clientX, int clientY, int width, int height, bool leftDown, bool positionValid = true) noexcept
    {
        const bool inside = positionValid && width > 0 && height > 0 &&
                            clientX >= 0 && clientY >= 0 && clientX < width && clientY < height;
        m_values[0] = inside ? static_cast<float>(clientX) / static_cast<float>(width) : -1.0f;
        m_values[1] = inside ? 1.0f - static_cast<float>(clientY) / static_cast<float>(height) : -1.0f;
        // Button state is independent of whether the cursor is inside the render window.
        m_values[2] = leftDown ? 1.0f : 0.0f;
        m_values[3] = !leftDown && m_leftWasDown ? 1.0f : 0.0f;
        m_leftWasDown = leftDown;
    }

    const std::array<float, 4>& EelValues() const noexcept { return m_values; }

    std::array<float, 4> ShaderValues() const noexcept
    {
        return {m_values[0], m_values[1] == -1.0f ? -1.0f : 1.0f - m_values[1], m_values[2], m_values[3]};
    }

  private:
    std::array<float, 4> m_values{-1.0f, -1.0f, 0.0f, 0.0f};
    bool m_leftWasDown = false;
};

#endif
