/*
 * textmgr.cpp - Tests for DirectWrite text layout caching.
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#include "pch.h"

#include <memory>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <vis_milk2/textmgr.h>
#include <CppUnitTest.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
namespace
{
class MeasuredTextElement : public TextElement
{
  public:
    IDWriteTextLayout* GetLayout() const { return m_textLayout.Get(); }
};
}

TEST_CLASS(TextLayoutTest)
{
  private:
    ComPtr<IDWriteFactory> m_factory;

    void Prepare(MeasuredTextElement& element, TextStyle& style)
    {
        element.SetTextStyle(&style);
        element.SetText(std::wstring(L"MilkDrop"));
        element.SetContainer(D2D1::RectF(0.0f, 0.0f, 200.0f, 100.0f));
        element.GetBounds(m_factory.Get());
        Assert::IsNotNull(element.GetLayout());
    }

  public:
    TEST_METHOD_INITIALIZE(CreateFactory)
    {
        Assert::IsTrue(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                     reinterpret_cast<IUnknown**>(m_factory.GetAddressOf()))));
    }

    TEST_METHOD(ResizingContainerUpdatesCachedLayout)
    {
        TextStyle style(L"Segoe UI", 12.0f);
        MeasuredTextElement element;
        Prepare(element, style);
        Assert::AreEqual(200.0f, element.GetLayout()->GetMaxWidth());

        element.SetContainer(D2D1::RectF(0.0f, 0.0f, 80.0f, 40.0f));
        element.GetBounds(m_factory.Get());
        Assert::IsNotNull(element.GetLayout());
        Assert::AreEqual(80.0f, element.GetLayout()->GetMaxWidth());
        Assert::AreEqual(40.0f, element.GetLayout()->GetMaxHeight());
    }

    TEST_METHOD(SharedStyleChangeUpdatesEveryCachedLayout)
    {
        TextStyle style(L"Segoe UI", 12.0f);
        MeasuredTextElement first;
        MeasuredTextElement second;
        Prepare(first, style);
        Prepare(second, style);

        style.SetFontSize(24.0f);
        first.GetBounds(m_factory.Get());
        second.GetBounds(m_factory.Get());
        float firstSize = 0.0f;
        float secondSize = 0.0f;
        Assert::IsTrue(SUCCEEDED(first.GetLayout()->GetFontSize(0, &firstSize)));
        Assert::IsTrue(SUCCEEDED(second.GetLayout()->GetFontSize(0, &secondSize)));
        Assert::AreEqual(24.0f, firstSize);
        Assert::AreEqual(24.0f, secondSize);
    }

    TEST_METHOD(ChangingStyleUpdatesCachedLayout)
    {
        TextStyle original(L"Segoe UI", 12.0f);
        TextStyle replacement(L"Segoe UI", 30.0f);
        MeasuredTextElement first;
        MeasuredTextElement second;
        Prepare(first, original);
        Prepare(second, replacement);

        first.SetTextStyle(&replacement);
        first.GetBounds(m_factory.Get());
        float size = 0.0f;
        Assert::IsTrue(SUCCEEDED(first.GetLayout()->GetFontSize(0, &size)));
        Assert::AreEqual(30.0f, size);
    }
};
} // namespace MilkDrop2
