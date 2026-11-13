/*
 * constanttable.cpp - Tests for Direct3D 11 shader constant uploads.
 *
 * SPDX-License-Identifier: MPL-2.0
 */

#include "pch.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vis_milk2/constanttable.h>
#include <vis_milk2/d3d11shim.h>
#include <vis_milk2/shapesafety.h>
#include <vis_milk2/support.h>
#include <CppUnitTest.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MilkDrop2
{
TEST_CLASS(ConstantTableTest)
{
  private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    std::array<float, 8> ReadConstants(CConstantTable& table)
    {
        Assert::AreEqual(static_cast<size_t>(1), table.GetBuffersCount());
        ID3D11Buffer* buffer = nullptr;
        table.GetBuffers(&buffer);
        Assert::IsNotNull(buffer);

        D3D11_BUFFER_DESC desc{};
        buffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ComPtr<ID3D11Buffer> staging;
        Assert::IsTrue(SUCCEEDED(m_device->CreateBuffer(&desc, nullptr, staging.GetAddressOf())));
        m_context->CopyResource(staging.Get(), buffer);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::array<float, 8> values{};
        memcpy(values.data(), mapped.pData, sizeof(values));
        m_context->Unmap(staging.Get(), 0);
        return values;
    }

  public:
    TEST_METHOD_INITIALIZE(CreateSoftwareDevice)
    {
        Assert::IsTrue(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                                   nullptr, 0, D3D11_SDK_VERSION,
                                                   m_device.GetAddressOf(), nullptr, m_context.GetAddressOf())));
    }

    TEST_METHOD(BindsSparseConstantBuffersInBothShaderStagesAndClearsUnusedSlots)
    {
        // A register at the API's upper bound also checks that null gaps are
        // not mistaken for reflection-index order or shortened to b0..bN.
        const char vertexSource[] =
            "cbuffer VertexConstants : register(b3) { float4 vertexTint; };"
            "struct V {float4 p:SV_Position;float4 c:COLOR0;};"
            "V main(uint id:SV_VertexID) {"
            "float2 xy[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};"
            "V v;v.p=float4(xy[id],0,1);v.c=vertexTint;return v;}";
        const char pixelSource[] =
            "cbuffer PixelConstants : register(b13) { float4 pixelTint; };"
            "struct V {float4 p:SV_Position;float4 c:COLOR0;};"
            "float4 main(V v):SV_Target {return v.c+pixelTint;}";
        auto compile = [](const char* source, const char* profile)
        {
            ComPtr<ID3DBlob> code;
            Assert::IsTrue(SUCCEEDED(D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                "main", profile, 0, 0, code.GetAddressOf(), nullptr)));
            return code;
        };
        auto reflect = [](ID3DBlob* code)
        {
            ComPtr<ID3D11ShaderReflection> reflection;
            Assert::IsTrue(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(reflection.GetAddressOf()))));
            return std::make_unique<CConstantTable>(reflection.Detach());
        };
        auto vertexCode = compile(vertexSource, "vs_4_0");
        auto pixelCode = compile(pixelSource, "ps_4_0");
        auto vertexTable = reflect(vertexCode.Get());
        auto pixelTable = reflect(pixelCode.Get());
        D3D11Shim shim(m_device.Get(), m_context.Get());
        ComPtr<ID3D11VertexShader> vertex;
        ComPtr<ID3D11PixelShader> pixel;
        Assert::IsTrue(S_OK == shim.CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
            vertex.GetAddressOf(), vertexTable.get()));
        Assert::IsTrue(S_OK == shim.CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
            pixel.GetAddressOf(), pixelTable.get()));
        Assert::AreEqual(static_cast<size_t>(1), vertexTable->GetBuffersCount());
        Assert::AreEqual(static_cast<size_t>(1), pixelTable->GetBuffersCount());
        const auto vertexBindings = vertexTable->GetBufferBindings();
        const auto pixelBindings = pixelTable->GetBufferBindings();
        Assert::IsNotNull(vertexBindings[3]);
        Assert::IsNotNull(pixelBindings[13]);
        for (size_t slot = 0; slot < vertexBindings.size(); ++slot)
        {
            if (slot != 3) Assert::IsNull(vertexBindings[slot]);
            if (slot != 13) Assert::IsNull(pixelBindings[slot]);
        }
        XMFLOAT4 vertexTint(0.125f, 0.25f, 0.375f, 0.5f);
        XMFLOAT4 pixelTint(0.5f, 0.375f, 0.25f, 0.5f);
        Assert::IsTrue(vertexTable->SetVector("vertexTint", &vertexTint));
        Assert::IsTrue(pixelTable->SetVector("pixelTint", &pixelTint));
        // Seed b0 with stale state to prove production binding clears gaps.
        ID3D11Buffer* stale = vertexBindings[3];
        m_context->VSSetConstantBuffers(0, 1, &stale);
        m_context->PSSetConstantBuffers(0, 1, &stale);
        shim.SetVertexShader(vertex.Get(), vertexTable.get());
        shim.SetPixelShader(pixel.Get(), pixelTable.get());
        ComPtr<ID3D11Buffer> bound;
        m_context->VSGetConstantBuffers(0, 1, bound.GetAddressOf());
        Assert::IsNull(bound.Get());
        m_context->PSGetConstantBuffers(0, 1, bound.GetAddressOf());
        Assert::IsNull(bound.Get());
        m_context->VSGetConstantBuffers(3, 1, bound.GetAddressOf());
        Assert::IsTrue(bound.Get() == vertexBindings[3]);
        bound.Reset();
        m_context->PSGetConstantBuffers(13, 1, bound.GetAddressOf());
        Assert::IsTrue(bound.Get() == pixelBindings[13]);
        bound.Reset();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> output, staging;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, output.GetAddressOf())));
        ComPtr<ID3D11RenderTargetView> view;
        Assert::IsTrue(SUCCEEDED(m_device->CreateRenderTargetView(output.Get(), nullptr, view.GetAddressOf())));
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        ID3D11RenderTargetView* target = view.Get();
        m_context->OMSetRenderTargets(1, &target, nullptr);
        m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = true;
        ComPtr<ID3D11RasterizerState> raster;
        Assert::IsTrue(SUCCEEDED(m_device->CreateRasterizerState(&rasterDesc, raster.GetAddressOf())));
        m_context->RSSetState(raster.Get());
        const D3D11_VIEWPORT viewport{0, 0, 1, 1, 0, 1};
        m_context->RSSetViewports(1, &viewport);
        m_context->IASetInputLayout(nullptr);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->Draw(3, 0);
        target = nullptr;
        m_context->OMSetRenderTargets(1, &target, nullptr);
        m_context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::array<float, 4> actual{};
        memcpy(actual.data(), mapped.pData, sizeof(actual));
        m_context->Unmap(staging.Get(), 0);
        Assert::AreEqual(0.625f, actual[0]);
        Assert::AreEqual(0.625f, actual[1]);
        Assert::AreEqual(0.625f, actual[2]);
        Assert::AreEqual(1.0f, actual[3]);

        auto emptyCode = compile("float4 main():SV_Target{return 1;}", "ps_4_0");
        auto emptyTable = reflect(emptyCode.Get());
        ComPtr<ID3D11PixelShader> emptyShader;
        Assert::IsTrue(S_OK == shim.CreatePixelShader(emptyCode->GetBufferPointer(), emptyCode->GetBufferSize(),
            emptyShader.GetAddressOf(), emptyTable.get()));
        Assert::AreEqual(static_cast<size_t>(0), emptyTable->GetBuffersCount());
        shim.SetPixelShader(emptyShader.Get(), emptyTable.get());
        m_context->PSGetConstantBuffers(13, 1, bound.GetAddressOf());
        Assert::IsNull(bound.Get(), L"Switching to a buffer-free shader must clear the old binding.");
    }

    TEST_METHOD(FeedbackAndFinalReadbacksRemainIndependentAcrossDeferredSubmission)
    {
        ComPtr<ID3D11DeviceContext> deferred;
        Assert::IsTrue(SUCCEEDED(m_device->CreateDeferredContext(0, deferred.GetAddressOf())));
        D3D11Shim shim(m_device.Get(), deferred.Get());
        shim.Initialize();
        ComPtr<ID3D11Texture2D> feedback, output;
        Assert::IsTrue(shim.CreateTexture(8, 4, 1, D3D11_BIND_RENDER_TARGET,
            DXGI_FORMAT_B8G8R8A8_UNORM, feedback.GetAddressOf()));
        Assert::IsTrue(shim.CreateTexture(4, 8, 1, D3D11_BIND_RENDER_TARGET,
            DXGI_FORMAT_B8G8R8A8_UNORM, output.GetAddressOf()));
        const float black[4]{0,0,0,1}, white[4]{1,1,1,1};
        shim.ClearRenderTarget(feedback.Get(), black);
        shim.ClearRenderTarget(output.Get(), white);
        shim.SetRenderTarget(output.Get());
        Assert::IsTrue(shim.QueueVisualFeedbackSample(feedback.Get()));
        Assert::IsTrue(shim.QueueVisualOutputSample());
        float low,high,range,dominant=0;
        ContrastImageSample contrast;
        contrast.available = true;
        Assert::IsFalse(shim.TryReadVisualFeedbackSample(low,high,range));
        Assert::IsFalse(shim.TryReadVisualOutputSample(low,high,range,nullptr,&contrast,1));
        Assert::IsFalse(contrast.available);
        ComPtr<ID3D11CommandList> commands;
        Assert::IsTrue(SUCCEEDED(deferred->FinishCommandList(FALSE, commands.GetAddressOf())));
        m_context->ExecuteCommandList(commands.Get(), FALSE);
        m_context->Flush();
        bool ready=false;
        for(int attempt=0;attempt<100 && !ready;++attempt)
        {
            ready=shim.TryReadVisualOutputSample(low,high,range,&dominant,&contrast,1);
            if(!ready) Sleep(1);
        }
        Assert::IsTrue(ready);
        Assert::AreEqual(1.0f,low);Assert::AreEqual(1.0f,high);Assert::AreEqual(0.0f,range);
        Assert::AreEqual(1.0f,dominant);
        Assert::IsTrue(contrast.available);
        Assert::AreEqual(1.0f,contrast.mean);
        Assert::AreEqual(1.0f,contrast.peak);
        Assert::AreEqual(0.0f,contrast.variance,1e-12f);
        Assert::IsTrue(shim.TryReadVisualFeedbackSample(low,high,range));
        Assert::AreEqual(0.0f,low);Assert::AreEqual(0.0f,high);Assert::AreEqual(0.0f,range);
        shim.ResetVisualOutputSample();
        Assert::IsFalse(shim.TryReadVisualFeedbackSample(low,high,range));
        Assert::IsFalse(shim.TryReadVisualOutputSample(low,high,range));
    }

    TEST_METHOD(ExtremaEarlyExitPreservesLateHighlightsContrastAndDiagnostics)
    {
        D3D11Shim shim(m_device.Get(),m_context.Get());shim.Initialize();
        ComPtr<ID3D11Texture2D> output;
        Assert::IsTrue(shim.CreateTexture(8,8,1,D3D11_BIND_RENDER_TARGET,DXGI_FORMAT_B8G8R8A8_UNORM,output.GetAddressOf()));
        for(int scenario=0;scenario<5;++scenario)
        {
            std::array<unsigned char,8*8*4> pixels{};
            for(size_t i=0;i<64;++i)
            {
                pixels[i*4+3]=255;
                for(int c=0;c<3;++c)pixels[i*4+c]=scenario<=1?64:scenario==3?90:0;
                if(scenario==4){pixels[i*4]=0;pixels[i*4+1]=128;pixels[i*4+2]=255;}
            }
            if(scenario<=1)for(int c=0;c<3;++c){pixels[c]=0;pixels[4+c]=255;}
            if(scenario==2)for(int c=0;c<3;++c)pixels[63*4+c]=255;
            if(scenario==4)for(int c=0;c<3;++c)pixels[63*4+c]=128;
            m_context->UpdateSubresource(output.Get(),0,nullptr,pixels.data(),8*4,0);
            shim.SetRenderTarget(output.Get());
            Assert::IsTrue(shim.QueueVisualOutputSample());m_context->Flush();
            float low=0,high=0,range=0,dominant=0;ContrastImageSample contrast;
            bool ready=false;
            for(int attempt=0;attempt<100&&!ready;++attempt)
            {
                ready=shim.TryReadVisualOutputSample(low,high,range,scenario==1?&dominant:nullptr,&contrast,1);
                if(!ready)Sleep(1);
            }
            Assert::IsTrue(ready);Assert::IsTrue(contrast.available);
            const float expectedLow=scenario==3?90.0f/255:0;
            const float expectedHigh=scenario==3?90.0f/255:1;
            const float expectedRange=scenario==3?0:scenario==4?128.0f/255:1;
            Assert::AreEqual(expectedLow,low);Assert::AreEqual(expectedHigh,high);Assert::AreEqual(expectedRange,range);
            double mean=0;for(size_t i=0;i<64;++i)for(int c=0;c<3;++c)mean+=pixels[i*4+c]/(64.0*3*255);
            Assert::AreEqual(static_cast<float>(mean),contrast.mean,.000001f);
            if(scenario==1)Assert::AreEqual(62.0f/64,dominant);
        }
    }

    TEST_METHOD(RestoresFixedFunctionTransformsAfterACustomVertexShader)
    {
        D3D11Shim shim(m_device.Get(), m_context.Get());
        shim.Initialize();
        shim.SetShader(2);
        ComPtr<ID3D11Buffer> transforms;
        m_context->VSGetConstantBuffers(0, 1, transforms.GetAddressOf());
        Assert::IsNotNull(transforms.Get());

        const char source[] = "cbuffer C:register(b3){float4 offset;}"
            "float4 main(float3 p:POSITION):SV_Position{return float4(p,1)+offset;}";
        ComPtr<ID3DBlob> code;
        Assert::IsTrue(SUCCEEDED(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr,
            "main", "vs_4_0", 0, 0, code.GetAddressOf(), nullptr)));
        ComPtr<ID3D11ShaderReflection> reflection;
        Assert::IsTrue(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
            __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(reflection.GetAddressOf()))));
        CConstantTable table(reflection.Detach());
        ComPtr<ID3D11VertexShader> vertex;
        Assert::IsTrue(S_OK == shim.CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(),
            vertex.GetAddressOf(), &table));
        shim.SetVertexShader(vertex.Get(), &table);
        shim.SetVertexShader(nullptr, nullptr);
        shim.SetVertexColor(true); // The custom-wave path, without SetShader().

        ComPtr<ID3D11Texture2D> target, staging;
        Assert::IsTrue(shim.CreateTexture(1, 1, 1, D3D11_BIND_RENDER_TARGET,
            DXGI_FORMAT_R32G32B32A32_FLOAT, target.GetAddressOf()));
        shim.SetRenderTarget(target.Get());
        const float black[4]{};
        shim.ClearRenderTarget(target.Get(), black);
        shim.SetRasterizerState(D3D11_CULL_NONE, D3D11_FILL_SOLID);
        shim.SetDepth(false);
        shim.SetBlendState(false);
        WFVERTEX triangle[3]{};
        triangle[0].x=-1;triangle[0].y=-1;
        triangle[1].x=-1;triangle[1].y=3;
        triangle[2].x=3;triangle[2].y=-1;
        for (auto& v : triangle) { v.r=.25f;v.g=.5f;v.b=.75f;v.a=1; }
        shim.DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 1, triangle, sizeof(WFVERTEX));
        D3D11_TEXTURE2D_DESC desc{};
        target->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        m_context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::array<float,4> pixel{};
        memcpy(pixel.data(), mapped.pData, sizeof(pixel));
        m_context->Unmap(staging.Get(), 0);
        Assert::AreEqual(.25f, pixel[0], L"Custom waves must survive the preceding shader's constant-buffer binding.");
        Assert::AreEqual(.5f, pixel[1]);
        Assert::AreEqual(.75f, pixel[2]);
        ComPtr<ID3D11Buffer> restored;
        m_context->VSGetConstantBuffers(0, 1, restored.GetAddressOf());
        Assert::IsTrue(restored.Get()==transforms.Get());
    }

    TEST_METHOD(ClearsAllPixelShaderResourcesAndRestoresUntexturedDrawing)
    {
        D3D11Shim shim(m_device.Get(), m_context.Get());
        shim.Initialize();
        shim.SetShader(0);
        shim.SetRasterizerState(D3D11_CULL_NONE, D3D11_FILL_SOLID);
        shim.SetDepth(false);
        shim.SetBlendState(false);
        ComPtr<ID3D11Texture2D> source, target, staging;
        Assert::IsTrue(shim.CreateTexture(1, 1, 1, D3D11_BIND_SHADER_RESOURCE,
            DXGI_FORMAT_R8G8B8A8_UNORM, source.GetAddressOf()));
        const uint32_t black = 0xff000000;
        m_context->UpdateSubresource(source.Get(), 0, nullptr, &black, sizeof(black), 0);
        Assert::IsTrue(shim.CreateTexture(1, 1, 1, D3D11_BIND_RENDER_TARGET,
            DXGI_FORMAT_R32G32B32A32_FLOAT, target.GetAddressOf()));
        shim.SetRenderTarget(target.Get());
        shim.SetTexture(0, source.Get());
        shim.SetTexture(20, source.Get());
        shim.SetTexture(127, source.Get());
        shim.ClearShaderResources();
        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views{};
        m_context->PSGetShaderResources(0, static_cast<UINT>(views.size()), views.data());
        bool allCleared = true;
        for (auto* view : views)
        {
            allCleared = allCleared && !view;
            if (view) view->Release();
        }
        Assert::IsTrue(allCleared);
        WFVERTEX triangle[3]{};
        triangle[0].x = -1; triangle[0].y = -1;
        triangle[1].x = -1; triangle[1].y = 3;
        triangle[2].x = 3; triangle[2].y = -1;
        for (auto& vertex : triangle)
        {
            vertex.r = .25f; vertex.g = .5f; vertex.b = .75f; vertex.a = 1;
        }
        shim.DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 1, triangle, sizeof(WFVERTEX));
        D3D11_TEXTURE2D_DESC desc{};
        target->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        m_context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::array<float, 4> pixel{};
        memcpy(pixel.data(), mapped.pData, sizeof(pixel));
        m_context->Unmap(staging.Get(), 0);
        Assert::AreEqual(.25f, pixel[0], L"Clearing slot zero must restore colour-only drawing.");
        Assert::AreEqual(.5f, pixel[1]);
        Assert::AreEqual(.75f, pixel[2]);
        Assert::AreEqual(1.0f, pixel[3]);

        const char customSource[] = "float4 main():SV_Target{return float4(1,0,0,1);}";
        ComPtr<ID3DBlob> code;
        Assert::IsTrue(SUCCEEDED(D3DCompile(customSource, sizeof(customSource) - 1, nullptr, nullptr, nullptr,
            "main", "ps_4_0", 0, 0, code.GetAddressOf(), nullptr)));
        ComPtr<ID3D11PixelShader> custom, bound;
        Assert::IsTrue(SUCCEEDED(m_device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
            nullptr, custom.GetAddressOf())));
        shim.SetPixelShader(custom.Get(), nullptr);
        shim.SetTexture(0, source.Get());
        shim.ClearShaderResources();
        m_context->PSGetShader(bound.GetAddressOf(), nullptr, nullptr);
        Assert::IsTrue(bound.Get() == custom.Get(), L"Resource unbinding must preserve an active custom pixel shader.");
    }

    TEST_METHOD(PreservesBlendPixelsAcrossCacheHitsEvictionAndExternalStateChanges)
    {
        D3D11Shim shim(m_device.Get(), m_context.Get());
        shim.Initialize();
        shim.SetShader(2);
        shim.SetVertexColor(true);
        shim.SetRasterizerState(D3D11_CULL_NONE, D3D11_FILL_SOLID);
        shim.SetDepth(false);
        ComPtr<ID3D11Texture2D> target, staging;
        Assert::IsTrue(shim.CreateTexture(1, 1, 1, D3D11_BIND_RENDER_TARGET,
            DXGI_FORMAT_R32G32B32A32_FLOAT, target.GetAddressOf()));
        shim.SetRenderTarget(target.Get());
        D3D11_TEXTURE2D_DESC desc{};
        target->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        WFVERTEX triangle[3]{};
        triangle[0].x = -1; triangle[0].y = -1;
        triangle[1].x = -1; triangle[1].y = 3;
        triangle[2].x = 3; triangle[2].y = -1;
        for (auto& vertex : triangle)
        {
            vertex.r = .8f; vertex.g = .6f; vertex.b = .4f; vertex.a = .25f;
        }
        struct BlendCase
        {
            bool enabled;
            D3D11_BLEND source, destination;
            std::array<float, 4> expected;
        };
        const std::array<BlendCase, 4> cases = {{
            {true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, {.35f, .45f, .55f, .6625f}},
            {false, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, {.8f, .6f, .4f, .25f}},
            {true, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, {.4f, .55f, .7f, .8625f}},
            {true, D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_COLOR, {.04f, .16f, .36f, .6f}}
        }};
        auto checkPixels = [&]()
        {
            const float background[] = {.2f, .4f, .6f, .8f};
            for (const auto& blend : cases)
            {
                shim.ClearRenderTarget(target.Get(), background);
                // Context users outside the shim may replace a cached state.
                m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
                shim.SetBlendState(blend.enabled, blend.source, blend.destination);
                shim.DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, 1, triangle, sizeof(WFVERTEX));
                m_context->CopyResource(staging.Get(), target.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
                std::array<float, 4> actual{};
                memcpy(actual.data(), mapped.pData, sizeof(actual));
                m_context->Unmap(staging.Get(), 0);
                for (size_t component = 0; component < actual.size(); ++component)
                    Assert::AreEqual(blend.expected[component], actual[component], .00001f);
            }
        };
        checkPixels(); // Cache misses.
        checkPixels(); // Cache hits must still restore the context state.
        const D3D11_BLEND destinations[] = {D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA,
            D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA};
        for (const bool enabled : {false, true})
            for (const D3D11_BLEND source : {D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA})
                for (const D3D11_BLEND destination : destinations)
                    shim.SetBlendState(enabled, source, destination);
        checkPixels(); // More than 16 distinct keys exercise bounded replacement.
    }

    TEST_METHOD(PreservesUnchangedConstantsWhenDiscardingBuffer)
    {
        const char source[] =
            "cbuffer Constants : register(b0) { float4 first; float4 second; };"
            "float4 main() : SV_Target { return first + second; }";
        ComPtr<ID3DBlob> code;
        Assert::IsTrue(SUCCEEDED(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr,
                                           "main", "ps_4_0", 0, 0, code.GetAddressOf(), nullptr)));

        ComPtr<ID3D11ShaderReflection> reflection;
        Assert::IsTrue(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                                           __uuidof(ID3D11ShaderReflection),
                                           reinterpret_cast<void**>(reflection.GetAddressOf()))));
        CConstantTable table(reflection.Detach());
        Assert::IsTrue(table.GrabShaderData(m_device.Get()));

        DirectX::XMFLOAT4 first(1.0f, 2.0f, 3.0f, 4.0f);
        DirectX::XMFLOAT4 second(5.0f, 6.0f, 7.0f, 8.0f);
        Assert::IsTrue(table.SetVector("first", &first));
        Assert::IsTrue(table.SetVector("second", &second));
        Assert::IsTrue(table.ApplyChanges(m_context.Get()));
        const auto initial = ReadConstants(table);
        for (size_t i = 0; i < initial.size(); ++i)
            Assert::AreEqual(static_cast<float>(i + 1), initial[i]);

        // A discarded allocation may contain any data. Replace its previous contents
        // so this test cannot pass merely because WARP happens to preserve the bytes.
        ID3D11Buffer* buffer = nullptr;
        table.GetBuffers(&buffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Assert::IsTrue(SUCCEEDED(m_context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
        memset(mapped.pData, 0, sizeof(initial));
        m_context->Unmap(buffer, 0);

        second = DirectX::XMFLOAT4(9.0f, 10.0f, 11.0f, 12.0f);
        Assert::IsTrue(table.SetVector("second", &second));
        Assert::IsTrue(table.ApplyChanges(m_context.Get()));
        const auto updated = ReadConstants(table);
        for (size_t i = 0; i < 4; ++i)
        {
            Assert::AreEqual(initial[i], updated[i], L"Updating one variable must preserve the other variable.");
            Assert::AreEqual(static_cast<float>(i + 9), updated[i + 4]);
        }
        Assert::IsFalse(table.ApplyChanges(m_context.Get()));
    }

    TEST_METHOD(PreservesShaderInitialisersBeforeAndAfterUpdates)
    {
        const char source[] =
            "float4 first = float4(1,2,3,4); float4 second;"
            "float4 main() : SV_Target { return first + second; }";
        ComPtr<ID3DBlob> code;
        Assert::IsTrue(SUCCEEDED(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr,
                                           "main", "ps_4_0", 0, 0, code.GetAddressOf(), nullptr)));
        ComPtr<ID3D11ShaderReflection> reflection;
        Assert::IsTrue(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                                           __uuidof(ID3D11ShaderReflection),
                                           reinterpret_cast<void**>(reflection.GetAddressOf()))));
        CConstantTable table(reflection.Detach());
        Assert::IsTrue(table.GrabShaderData(m_device.Get()));

        const auto initial = ReadConstants(table);
        for (size_t i = 0; i < 4; ++i)
        {
            Assert::AreEqual(static_cast<float>(i + 1), initial[i]);
            Assert::AreEqual(0.0f, initial[i + 4]);
        }

        DirectX::XMFLOAT4 second(5, 6, 7, 8);
        Assert::IsTrue(table.SetVector("second", &second));
        Assert::IsTrue(table.ApplyChanges(m_context.Get()));
        const auto updated = ReadConstants(table);
        for (size_t i = 0; i < updated.size(); ++i)
            Assert::AreEqual(static_cast<float>(i + 1), updated[i]);
    }

    TEST_METHOD(UploadsMilkDropRotationMatricesInShaderColumnOrder)
    {
        // Execute the matrix in a shader, so this checks HLSL packing rather
        // than duplicating the uploader's byte-layout assumptions.
        const char source[] =
            "cbuffer Constants : register(b0) { float4x3 rot_s1; };"
            "RWTexture2D<float4> result : register(u0);"
            "[numthreads(1,1,1)] void main() {"
            "result[uint2(0,0)] = float4(mul(float4(2,3,5,1), rot_s1), 1); }";
        ComPtr<ID3DBlob> code;
        Assert::IsTrue(SUCCEEDED(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr,
                                           "main", "cs_5_0", 0, 0, code.GetAddressOf(), nullptr)));
        ComPtr<ID3D11ShaderReflection> reflection;
        Assert::IsTrue(SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                                           __uuidof(ID3D11ShaderReflection),
                                           reinterpret_cast<void**>(reflection.GetAddressOf()))));
        CConstantTable table(reflection.Detach());
        Assert::IsTrue(table.GrabShaderData(m_device.Get()));

        ComPtr<ID3D11ComputeShader> shader;
        Assert::IsTrue(SUCCEEDED(m_device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(),
                                                               nullptr, shader.GetAddressOf())));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> result;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, result.GetAddressOf())));
        ComPtr<ID3D11UnorderedAccessView> view;
        Assert::IsTrue(SUCCEEDED(m_device->CreateUnorderedAccessView(result.Get(), nullptr, view.GetAddressOf())));
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Assert::IsTrue(SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));

        DirectX::XMMATRIX matrices[] = {
            DirectX::XMMatrixIdentity(),
            DirectX::XMMatrixSet(1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, 1)
        };
        const std::array<std::array<float, 4>, 2> expected = {{{2, 3, 5, 1}, {59, 70, 81, 1}}};
        for (size_t i = 0; i < expected.size(); ++i)
        {
            Assert::IsTrue(table.SetMatrix("rot_s1", &matrices[i]));
            Assert::IsTrue(table.ApplyChanges(m_context.Get()));
            Assert::AreEqual(static_cast<size_t>(1), table.GetBuffersCount());
            ID3D11Buffer* buffer = nullptr;
            table.GetBuffers(&buffer);
            m_context->CSSetConstantBuffers(0, 1, &buffer);
            m_context->CSSetShader(shader.Get(), nullptr, 0);
            ID3D11UnorderedAccessView* output = view.Get();
            m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            m_context->Dispatch(1, 1, 1);
            output = nullptr;
            m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
            m_context->CopyResource(staging.Get(), result.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            Assert::IsTrue(SUCCEEDED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
            std::array<float, 4> actual{};
            memcpy(actual.data(), mapped.pData, sizeof(actual));
            m_context->Unmap(staging.Get(), 0);
            for (size_t component = 0; component < actual.size(); ++component)
                Assert::AreEqual(expected[i][component], actual[component]);
        }
    }
};

TEST_CLASS(ShapeSafetyTest)
{
  public:
    TEST_METHOD(BoundsMalformedInstanceAndSideCountsBeforeConversion)
    {
        Assert::AreEqual(0, ShapeSafety::InstanceCount((std::numeric_limits<int>::min)()));
        Assert::AreEqual(0, ShapeSafety::InstanceCount(-1));
        Assert::AreEqual(0, ShapeSafety::InstanceCount(0));
        Assert::AreEqual(311, ShapeSafety::InstanceCount(311));
        Assert::AreEqual(1024, ShapeSafety::InstanceCount((std::numeric_limits<int>::max)()));
        int sides = 0;
        Assert::IsTrue(ShapeSafety::Sides((std::numeric_limits<double>::max)(), sides));
        Assert::AreEqual(100, sides);
        Assert::IsTrue(ShapeSafety::Sides(-(std::numeric_limits<double>::max)(), sides));
        Assert::AreEqual(3, sides);
        Assert::IsTrue(ShapeSafety::Sides(4.9, sides));
        Assert::AreEqual(4, sides);
        Assert::IsFalse(ShapeSafety::Sides(std::numeric_limits<double>::infinity(), sides));
        Assert::IsFalse(ShapeSafety::Sides(std::numeric_limits<double>::quiet_NaN(), sides));
    }

    TEST_METHOD(RejectsUnsafeGeometryAndColoursWithoutClampingNormalValues)
    {
        const double huge = (std::numeric_limits<double>::max)();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        Assert::IsTrue(ShapeSafety::FloatValue(-1000.5));
        Assert::IsFalse(ShapeSafety::FloatValue(huge));
        Assert::IsFalse(ShapeSafety::FloatValue(nan));
        Assert::IsFalse(ShapeSafety::FloatValue(infinity));
        Assert::IsTrue(ShapeSafety::Colour(1.5)); // Preserve legacy wrapped colour values.
        Assert::IsTrue(ShapeSafety::Colour(-.5));
        Assert::IsFalse(ShapeSafety::Colour(huge));
        Assert::IsFalse(ShapeSafety::Colour(nan));
        Assert::IsFalse(ShapeSafety::Colour(infinity));
        Assert::IsFalse(ShapeSafety::Flag(.5)); // Fractional flags retain integer truncation.
        Assert::IsTrue(ShapeSafety::Flag(-1.5));
        Assert::IsTrue(ShapeSafety::Flag(huge));
        Assert::IsFalse(ShapeSafety::Flag(nan));
        Assert::IsFalse(ShapeSafety::Flag(infinity));
    }
};
} // namespace MilkDrop2
