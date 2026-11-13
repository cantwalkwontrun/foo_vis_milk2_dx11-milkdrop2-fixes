/* Language: en-GB. SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <helpers/DarkMode.h>
#include <algorithm>
#include <string>
#include <vector>

namespace MilkDropSelection
{
struct Colours { COLORREF text, background; };

inline bool GetItemText(HWND control, UINT type, int index, std::wstring& text)
{
    text.clear();
    if (!control || index < 0 || (type != ODT_COMBOBOX && type != ODT_LISTBOX)) return false;
    const bool combo = type == ODT_COMBOBOX;
    const LRESULT length = SendMessage(control, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, index, 0);
    if (length < 0 || length >= 32768) return false;
    text.resize(static_cast<size_t>(length) + 1);
    const LRESULT copied = SendMessage(control, combo ? CB_GETLBTEXT : LB_GETTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    if (copied < 0 || copied > length) { text.clear(); return false; }
    text.resize(static_cast<size_t>(copied));
    return true;
}

inline Colours GetColours(bool dark, bool selected, bool focused, bool disabled, bool highContrast)
{
    // High contrast and light mode retain the user's paired system colours.
    if (!dark || highContrast)
    {
        if (selected) return {::GetSysColor(COLOR_HIGHLIGHTTEXT), ::GetSysColor(COLOR_HIGHLIGHT)};
        return {::GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT), ::GetSysColor(COLOR_WINDOW)};
    }
    if (selected)
    {
        // Explicit pairs avoid combining native theme highlights with unrelated
        // dark-mode foregrounds. Keep disabled selections readable as well.
        return focused ? Colours{RGB(255,255,255), RGB(48,79,112)}
                       : Colours{RGB(240,240,240), RGB(64,64,64)};
    }
    return {DarkMode::GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT), DarkMode::GetSysColor(COLOR_WINDOW)};
}

class Hooks
{
  public:
    explicit Hooks(const DarkMode::CHooks& theme) : m_theme(theme) {}
    ~Hooks() { clear(); }
    Hooks(const Hooks&) = delete;
    Hooks& operator=(const Hooks&) = delete;

    void AddDialog(HWND dialog)
    {
        if (!dialog || std::find(m_dialogs.begin(), m_dialogs.end(), dialog) != m_dialogs.end()) return;
        if (!SetWindowSubclass(dialog, DialogProc, SubclassId(), reinterpret_cast<DWORD_PTR>(this))) return;
        m_dialogs.push_back(dialog);
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            if (IsSelector(child, ODT_COMBOBOX) || IsSelector(child, ODT_LISTBOX))
                SetWindowSubclass(child, ControlProc, ControlSubclassId(), 0);
            SetItemHeight(dialog, child);
        }
    }

    void clear()
    {
        for (HWND dialog : m_dialogs)
        {
            DWORD_PTR data = 0;
            if (GetWindowSubclass(dialog, DialogProc, SubclassId(), &data) && data == reinterpret_cast<DWORD_PTR>(this))
            {
                for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
                    RemoveWindowSubclass(child, ControlProc, ControlSubclassId());
                RemoveWindowSubclass(dialog, DialogProc, SubclassId());
            }
        }
        m_dialogs.clear();
    }

    static bool IsSelector(HWND window, UINT type)
    {
        wchar_t name[32]{};
        if (!GetClassNameW(window, name, static_cast<int>(std::size(name)))) return false;
        const DWORD style = static_cast<DWORD>(GetWindowLongPtr(window, GWL_STYLE));
        if (type == ODT_LISTBOX)
            return _wcsicmp(name, L"ListBox") == 0 && (style & LBS_OWNERDRAWFIXED) && (style & LBS_HASSTRINGS);
        if (type == ODT_COMBOBOX)
            return _wcsicmp(name, L"ComboBox") == 0 && (style & CBS_OWNERDRAWFIXED) && (style & CBS_HASSTRINGS);
        return false;
    }

    static bool Draw(const DRAWITEMSTRUCT& item, bool dark)
    {
        if (!item.hDC || !IsSelector(item.hwndItem, item.CtlType)) return false;
        HIGHCONTRASTW accessibility{sizeof(accessibility)};
        const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(accessibility), &accessibility, 0) &&
            (accessibility.dwFlags & HCF_HIGHCONTRASTON);
        const bool focused = !(item.itemState & ODS_INACTIVE) &&
            ((item.itemState & ODS_FOCUS) != 0 || GetFocus() == item.hwndItem);
        const auto colours = GetColours(dark, (item.itemState & ODS_SELECTED) != 0,
            focused, (item.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0, highContrast);
        const int saved = SaveDC(item.hDC);
        if (!saved) return false;
        SetDCBrushColor(item.hDC, colours.background);
        FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetTextColor(item.hDC, colours.text);
        SetBkColor(item.hDC, colours.background);
        SetBkMode(item.hDC, TRANSPARENT);
        HFONT font = reinterpret_cast<HFONT>(SendMessage(item.hwndItem, WM_GETFONT, 0, 0));
        SelectObject(item.hDC, font ? font : GetStockObject(DEFAULT_GUI_FONT));
        if (item.itemID != static_cast<UINT>(-1))
        {
            std::wstring text;
            if (GetItemText(item.hwndItem, item.CtlType, static_cast<int>(item.itemID), text))
            {
                RECT rectangle = item.rcItem;
                TEXTMETRICW metrics{};
                GetTextMetricsW(item.hDC, &metrics);
                const int padding = (std::max)(2L, metrics.tmHeight / 6);
                rectangle.left += padding; rectangle.right -= padding;
                DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &rectangle,
                    DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT))
            DrawFocusRect(item.hDC, &item.rcItem);
        RestoreDC(item.hDC, saved);
        return true;
    }

  private:
    static UINT_PTR SubclassId() { return reinterpret_cast<UINT_PTR>(&DialogProc); }
    static UINT_PTR ControlSubclassId() { return reinterpret_cast<UINT_PTR>(&ControlProc); }

    static void SetItemHeight(HWND dialog, HWND child)
    {
        const bool combo = IsSelector(child, ODT_COMBOBOX);
        if (!combo && !IsSelector(child, ODT_LISTBOX)) return;
        HDC dc = GetDC(dialog);
        if (!dc) return;
        HFONT font = reinterpret_cast<HFONT>(SendMessage(child, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW metrics{};
        if (GetTextMetricsW(dc, &metrics))
        {
            const int height = metrics.tmHeight + (std::max)(2L, metrics.tmHeight / 6);
            SendMessage(child, combo ? CB_SETITEMHEIGHT : LB_SETITEMHEIGHT, 0,
                height);
            if (combo && SendMessage(child, CB_GETITEMHEIGHT, WPARAM(-1), 0) < metrics.tmHeight)
                SendMessage(child, CB_SETITEMHEIGHT, WPARAM(-1), height);
        }
        SelectObject(dc, oldFont);
        ReleaseDC(dialog, dc);
    }

    static LRESULT CALLBACK ControlProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(control, ControlProc, ControlSubclassId());
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        // 0x02E3 is WM_DPICHANGED_AFTERPARENT, also handled when targeting older SDK API levels.
        if (message == WM_SETFONT || message == 0x02E3)
            SetItemHeight(GetParent(control), control);
        return result;
    }

    static LRESULT CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto* self = reinterpret_cast<Hooks*>(data);
        if (message == WM_DRAWITEM && lParam && Draw(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), self->m_theme.IsDark()))
            return TRUE;
        if (message == WM_NCDESTROY) RemoveWindowSubclass(dialog, DialogProc, SubclassId());
        return DefSubclassProc(dialog, message, wParam, lParam);
    }

    const DarkMode::CHooks& m_theme;
    std::vector<HWND> m_dialogs;
};
}

// One policy for preferences, font selectors, blacklist and auxiliary dialogs.
class MilkDropDarkModeHooks : public fb2k::CDarkModeHooks
{
  public:
    MilkDropDarkModeHooks() : m_selection(*this) {}
    ~MilkDropDarkModeHooks() { clear(); }
    void AddDialogWithControls(HWND dialog)
    {
        fb2k::CDarkModeHooks::AddDialogWithControls(dialog);
        m_selection.AddDialog(dialog);
    }
    void clear()
    {
        m_selection.clear();
        fb2k::CDarkModeHooks::clear();
    }
  private:
    MilkDropSelection::Hooks m_selection;
};
