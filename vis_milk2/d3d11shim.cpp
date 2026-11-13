/*
 * d3d11shim.cpp - Direct3D 9 to Direct3D 11 conversion functions.
 *                 References:
 *                 - https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-d3d9-to-d3d10-considerations
 *                 - https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d11-programming-guide-migrating
 *
 * Copyright (c) 2021-2024 Jimmy Cassis
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pch.h"
#include "d3d11shim.h"

#include <DirectXHelpers.h>
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <DirectXTex.h>
#include <fstream>
#include <filesystem>
#include "support.h"
#include "utility.h"

using namespace DirectX;

namespace
{
HRESULT LoadLegacyTexture(ID3D11Device* device, const uint8_t* bytes, size_t size, bool dds, ID3D11Resource** texture)
{
    if (!bytes || size == 0 || size > 512ULL * 1024 * 1024)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    TexMetadata metadata{};
    HRESULT hr = dds ? GetMetadataFromDDSMemory(bytes, size, DDS_FLAGS_NONE, metadata)
                     : GetMetadataFromTGAMemory(bytes, size, TGA_FLAGS_NONE, metadata);
    if (FAILED(hr))
        return hr;
    // Bound conversion before allocating decoded pixels. Only the missing 2D
    // legacy paths use this fallback; native DDS arrays/volumes remain native.
    if (metadata.dimension != TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 || metadata.depth != 1 ||
        metadata.IsCubemap() || !IsSupportedTexture(device, metadata))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    size_t rowPitch = 0, slicePitch = 0;
    hr = ComputePitch(metadata.format, metadata.width, metadata.height, rowPitch, slicePitch);
    if (FAILED(hr))
        return hr;
    if (slicePitch > 128ULL * 1024 * 1024)
        return E_OUTOFMEMORY; // The complete mip chain fits within 256 MiB.
    ScratchImage image;
    hr = dds ? LoadFromDDSMemory(bytes, size, DDS_FLAGS_NONE, nullptr, image)
             : LoadFromTGAMemory(bytes, size, TGA_FLAGS_NONE, nullptr, image);
    if (FAILED(hr))
        return hr;
    return DirectX::CreateTexture(device, image.GetImages(), image.GetImageCount(), image.GetMetadata(), texture);
}

HRESULT LoadLegacyTextureFile(ID3D11Device* device, const wchar_t* filename, bool dds, ID3D11Resource** texture)
{
    try
    {
        std::ifstream input(std::filesystem::path(filename), std::ios::binary | std::ios::ate);
        if (!input)
            return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
        const auto size = input.tellg();
        if (size <= 0 || static_cast<uint64_t>(size) > 512ULL * 1024 * 1024)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
        return LoadLegacyTexture(device, bytes.data(), bytes.size(), dds, texture);
    }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

#include <defaultvs.inc>
#include <diffuseps.inc>
#include <texture0ps.inc>
#include <colorps.inc>
#include <texture1ps.inc>

void CopyVertexCommon(MDVERTEX& dst, float x, float y, float z, float r, float g, float b, float a)
{
    dst.x = x;
    dst.y = y;
    dst.z = z;
    dst.r = r;
    dst.g = g;
    dst.b = b;
    dst.a = a;
    dst.tu = 0.0f;
    dst.tv = 0.0f;
    dst.tu0 = 0.0f;
    dst.tv0 = 0.0f;
    dst.rad = 0.0f;
    dst.ang = 0.0f;
}

template <typename TSpriteVertex>
void CopySpriteLikeVertex(MDVERTEX& dst, const TSpriteVertex& src)
{
    CopyVertexCommon(dst, src.x, src.y, src.z, src.r, src.g, src.b, src.a);
    dst.tu = src.tu;
    dst.tv = src.tv;
}

void CopySimpleVertex(MDVERTEX& dst, const SIMPLEVERTEX& src)
{
    const DWORD diffuse = src.Diffuse;
    CopyVertexCommon(dst, src.x, src.y, src.z,
                     static_cast<float>((diffuse >> 16) & 0xff) / 255.0f,
                     static_cast<float>((diffuse >> 8) & 0xff) / 255.0f,
                     static_cast<float>(diffuse & 0xff) / 255.0f,
                     static_cast<float>((diffuse >> 24) & 0xff) / 255.0f);
}
}

D3D11Shim::D3D11Shim(ID3D11Device* pDevice, ID3D11DeviceContext* pContext) :
    m_pDevice(pDevice),
    m_pContext(pContext),
    m_pImmContext(nullptr),
    m_pVShader(nullptr),
    m_pInputLayout(nullptr),
    m_pVBuffer(nullptr),
    m_pIBuffer(nullptr),
    m_pIFanBuffer(nullptr),
    m_pCBuffer(nullptr),
    m_bCBufferIsDirty(false),
    m_uCurrShader(0),
    m_bTexture0Bound(false),
    m_bVertexColorOnly(false),
    m_bCustomPixelShaderActive(false),
    m_transforms({})
{
    for (size_t i = 0; i < MAX_NUM_SHADERS; i++)
        m_pPShader[i] = nullptr;
}

D3D11Shim::~D3D11Shim()
{
    ResetVisualOutputSample();
    SafeRelease(m_pVShader);
    for (size_t i = 0; i < MAX_NUM_SHADERS; i++)
        SafeRelease(m_pPShader[i]);
    SafeRelease(m_pInputLayout);
    SafeRelease(m_pVBuffer);
    SafeRelease(m_pIBuffer);
    SafeRelease(m_pIFanBuffer);
    SafeRelease(m_pCBuffer);
    for (auto& entry : m_blendStates)
        SafeRelease(entry.state);
    SafeRelease(m_pImmContext);
    m_states.reset();
}

void D3D11Shim::Initialize()
{
    if (!m_pDevice)
        return;

    m_states = std::make_unique<CommonStates>(m_pDevice);

    // Note: These must match layouts in "support.h"!
    D3D11_INPUT_ELEMENT_DESC MilkDropLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    D3D11_INPUT_ELEMENT_DESC wfLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    D3D11_INPUT_ELEMENT_DESC spriteLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    m_pDevice->CreateInputLayout(MilkDropLayout, ARRAYSIZE(MilkDropLayout), defaultvsCode, sizeof(defaultvsCode), &m_pInputLayout);

    m_pDevice->CreateVertexShader(defaultvsCode, sizeof(defaultvsCode), NULL, &m_pVShader);

    m_pDevice->CreatePixelShader(diffusepsCode, sizeof(diffusepsCode), NULL, &m_pPShader[0]);
    m_pDevice->CreatePixelShader(texture0psCode, sizeof(texture0psCode), NULL, &m_pPShader[1]);
    m_pDevice->CreatePixelShader(colorpsCode, sizeof(colorpsCode), NULL, &m_pPShader[2]);
    m_pDevice->CreatePixelShader(texture1psCode, sizeof(texture1psCode), NULL, &m_pPShader[3]);

    CD3D11_BUFFER_DESC bDesc(sizeof(MDVERTEX) * MAX_VERTICES_COUNT, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    m_pDevice->CreateBuffer(&bDesc, NULL, &m_pVBuffer);

    bDesc.ByteWidth = sizeof(uint16_t) * MAX_INDICES_COUNT;
    bDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    m_pDevice->CreateBuffer(&bDesc, NULL, &m_pIBuffer);

    std::vector<uint16_t> indices;
    for (size_t i = 1; i < MAX_VERTICES_COUNT - 1; ++i)
    {
        indices.push_back(0);
        indices.push_back(static_cast<uint16_t>(i));
        indices.push_back(static_cast<uint16_t>(i + 1));
    }
    bDesc.ByteWidth = static_cast<UINT>(sizeof(uint16_t) * indices.size());
    bDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bDesc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA bData = {indices.data()};

    if (FAILED(m_pDevice->CreateBuffer(&bDesc, &bData, &m_pIFanBuffer)))
        return;

    XMStoreFloat4x4(&m_transforms.world, XMMatrixIdentity());
    XMStoreFloat4x4(&m_transforms.view, XMMatrixIdentity());
    XMStoreFloat4x4(&m_transforms.proj, XMMatrixIdentity());

    bDesc.ByteWidth = sizeof(cbTransforms);
    bDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bDesc.Usage = D3D11_USAGE_DYNAMIC;
    bDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bData.pSysMem = &m_transforms;

    m_pDevice->CreateBuffer(&bDesc, &bData, &m_pCBuffer);

    m_pDevice->GetImmediateContext(&m_pImmContext);
}

bool D3D11Shim::CreateTexture(unsigned int uWidth, unsigned int uHeight, unsigned int mipLevels, UINT bindFlags, DXGI_FORMAT format,
                              ID3D11Texture2D** ppTexture, unsigned int miscFlags, D3D11_USAGE usage)
{
    if (!m_pDevice || !ppTexture || uWidth == 0 || uHeight == 0 || mipLevels == 0)
        return false;

    *ppTexture = nullptr;

    CD3D11_TEXTURE2D_DESC texDesc(format, uWidth, uHeight, 1, mipLevels, bindFlags, usage);
    texDesc.MiscFlags = miscFlags;
    if (usage == D3D11_USAGE_DYNAMIC)
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (usage == D3D11_USAGE_STAGING)
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE /*| D3D11_CPU_ACCESS_READ*/;

    return (S_OK == m_pDevice->CreateTexture2D(&texDesc, NULL, ppTexture));
}

bool D3D11Shim::CreateVolumeTexture(unsigned int uWidth, unsigned int uHeight, unsigned int uDepth, unsigned int mipLevels, UINT bindFlags,
                                    DXGI_FORMAT format, ID3D11Texture3D** ppTexture, unsigned int miscFlags, D3D11_USAGE usage)
{
    if (!m_pDevice || !ppTexture || uWidth == 0 || uHeight == 0 || uDepth == 0 || mipLevels == 0)
        return false;

    *ppTexture = nullptr;

    CD3D11_TEXTURE3D_DESC texDesc(format, uWidth, uHeight, uDepth, mipLevels, bindFlags, usage);
    texDesc.MiscFlags = miscFlags;
    if (usage == D3D11_USAGE_DYNAMIC)
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (usage == D3D11_USAGE_STAGING)
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;

    return (S_OK == m_pDevice->CreateTexture3D(&texDesc, NULL, ppTexture));
}

void D3D11Shim::DrawPrimitive(unsigned int primType, unsigned int iPrimCount, const void* pVData, unsigned int vertexStride)
{
    if (!m_pContext || !m_pVBuffer || !m_pInputLayout || !pVData || vertexStride == 0)
        return;

    int numVerts = NumVertsFromType(primType, iPrimCount);
    if (numVerts <= 0)
        return;

    const unsigned int drawVerts = std::min(static_cast<unsigned int>(numVerts), MAX_VERTICES_COUNT);

    if (m_bCBufferIsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE res{};
        if (m_pCBuffer && S_OK == m_pContext->Map(m_pCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res))
        {
            memcpy(res.pData, &m_transforms, sizeof(cbTransforms));
            m_pContext->Unmap(m_pCBuffer, 0);
        }
        m_bCBufferIsDirty = false;
    }

    if (!UpdateVBuffer(drawVerts, pVData, vertexStride))
        return;

    const unsigned int gpuVertexStride = sizeof(MDVERTEX);
    unsigned int offsets = 0;
    m_pContext->IASetVertexBuffers(0, 1, &m_pVBuffer, &gpuVertexStride, &offsets);
    m_pContext->IASetInputLayout(m_pInputLayout);
    if (primType == D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN)
    {
        if (!m_pIFanBuffer)
            return;

        const unsigned int maxFanPrims = (drawVerts >= 2U) ? (drawVerts - 2U) : 0U;
        const unsigned int fanPrims = std::min(iPrimCount, maxFanPrims);
        if (fanPrims == 0)
            return;

        m_pContext->IASetIndexBuffer(m_pIFanBuffer, DXGI_FORMAT_R16_UINT, 0);
        m_pContext->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_pContext->DrawIndexed(fanPrims * 3, 0, 0);
    }
    else
    {
        m_pContext->IASetPrimitiveTopology(static_cast<D3D_PRIMITIVE_TOPOLOGY>(primType));
        m_pContext->Draw(drawVerts, 0);
    }
}

void D3D11Shim::DrawIndexedPrimitive(unsigned int primType, unsigned int uStartVertex, unsigned int iNumVertices, unsigned int iPrimCount,
                                     const void* pIData, const void* pVData, unsigned int vertexStride)
{
    if (!m_pContext || !m_pVBuffer || !m_pIBuffer || !m_pInputLayout || !pIData || !pVData || vertexStride == 0)
        return;

    int numIndices = NumVertsFromType(primType, iPrimCount);
    if (numIndices <= 0 || iNumVertices == 0)
        return;

    const unsigned int drawVertices = iNumVertices;
    const unsigned int drawIndices = static_cast<unsigned int>(numIndices);
    if (drawVertices > MAX_VERTICES_COUNT || drawIndices > MAX_INDICES_COUNT)
        return;

    const auto* indices = static_cast<const uint16_t*>(pIData);
    for (unsigned int i = 0; i < drawIndices; ++i)
    {
        if (static_cast<unsigned int>(indices[i]) + uStartVertex >= drawVertices)
            return;
    }

    if (m_bCBufferIsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE res;
        if (m_pCBuffer && SUCCEEDED(m_pContext->Map(m_pCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res)))
        {
            memcpy(res.pData, &m_transforms, sizeof(cbTransforms));
            m_pContext->Unmap(m_pCBuffer, 0);
        }
        m_bCBufferIsDirty = false;
    }

    if (!UpdateVBuffer(drawVertices, pVData, vertexStride) || !UpdateIBuffer(drawIndices, pIData))
        return;

    const unsigned int gpuVertexStride = sizeof(MDVERTEX);
    unsigned int offsets = 0;
    m_pContext->IASetVertexBuffers(0, 1, &m_pVBuffer, &gpuVertexStride, &offsets);
    m_pContext->IASetIndexBuffer(m_pIBuffer, DXGI_FORMAT_R16_UINT, 0);
    m_pContext->IASetInputLayout(m_pInputLayout);
    m_pContext->IASetPrimitiveTopology((D3D_PRIMITIVE_TOPOLOGY)primType);
    m_pContext->DrawIndexed(drawIndices, 0, uStartVertex);
}

void D3D11Shim::GetRenderTarget(ID3D11Texture2D** ppTexture)
{
    if (!m_pContext || !ppTexture)
        return;

    *ppTexture = nullptr;

    ID3D11RenderTargetView* pRTView = NULL;
    ID3D11Resource* pResource = NULL;

    m_pContext->OMGetRenderTargets(1, &pRTView, NULL);
    if (pRTView)
    {
        pRTView->GetResource(&pResource);
        pResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(ppTexture));
        pResource->Release();
        pRTView->Release();
    }
}

void D3D11Shim::GetDepthView(ID3D11DepthStencilView** ppView)
{
    if (!m_pContext || !ppView)
        return;

    m_pContext->OMGetRenderTargets(0, nullptr, ppView);
}

void D3D11Shim::GetViewport(D3D11_VIEWPORT* vp)
{
    if (!m_pContext || !vp)
        return;

    unsigned int numVP = 1;
    m_pContext->RSGetViewports(&numVP, vp);
}

void D3D11Shim::SetBlendState(bool bEnable, D3D11_BLEND srcBlend, D3D11_BLEND destBlend)
{
    if (!m_pDevice || !m_pContext)
        return;

    for (const auto& entry : m_blendStates)
    {
        if (entry.state && entry.enabled == bEnable && entry.source == srcBlend && entry.destination == destBlend)
        {
            // Direct2D and other users of the context may have changed OM state.
            m_pContext->OMSetBlendState(entry.state, 0, 0xFFFFFFFF);
            return;
        }
    }

    D3D11_BLEND_DESC desc{};
    desc.RenderTarget[0].BlendEnable = bEnable; //(srcBlend != D3D11_BLEND_ONE) || (destBlend != D3D11_BLEND_ZERO);
    desc.RenderTarget[0].SrcBlend = srcBlend;
    desc.RenderTarget[0].DestBlend = destBlend;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;

    D3D11_BLEND srcAlphaBlend = srcBlend;
    D3D11_BLEND destAlphaBlend = destBlend;

    if (srcBlend == D3D11_BLEND_SRC_COLOR || srcBlend == D3D11_BLEND_INV_SRC_COLOR)
        srcAlphaBlend = static_cast<D3D11_BLEND>(srcAlphaBlend + 2);
    else if (srcBlend == D3D11_BLEND_DEST_COLOR || srcBlend == D3D11_BLEND_INV_DEST_COLOR)
        srcAlphaBlend = static_cast<D3D11_BLEND>(srcAlphaBlend - 2);

    if (destBlend == D3D11_BLEND_SRC_COLOR || destBlend == D3D11_BLEND_INV_SRC_COLOR)
        destAlphaBlend = static_cast<D3D11_BLEND>(destAlphaBlend + 2);
    else if (destBlend == D3D11_BLEND_DEST_COLOR || destBlend == D3D11_BLEND_INV_DEST_COLOR)
        destAlphaBlend = static_cast<D3D11_BLEND>(destAlphaBlend - 2);

    desc.RenderTarget[0].SrcBlendAlpha = srcAlphaBlend;
    desc.RenderTarget[0].DestBlendAlpha = destAlphaBlend;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    ID3D11BlendState* state = nullptr;
    if (SUCCEEDED(m_pDevice->CreateBlendState(&desc, &state)))
    {
        auto& entry = m_blendStates[m_nextBlendState];
        SafeRelease(entry.state);
        entry = { bEnable, srcBlend, destBlend, state };
        m_nextBlendState = (m_nextBlendState + 1) % m_blendStates.size();
        m_pContext->OMSetBlendState(state, 0, 0xFFFFFFFF);
    }
}

void D3D11Shim::SetDepth(bool bEnabled)
{
    if (!m_pContext || !m_states)
        return;

    ID3D11DepthStencilState* pState = bEnabled ? m_states->DepthDefault() : m_states->DepthNone();
    if (pState)
        m_pContext->OMSetDepthStencilState(pState, 0);
}

void D3D11Shim::SetRasterizerState(D3D11_CULL_MODE cullMode, D3D11_FILL_MODE fillMode)
{
    if (!m_pContext || !m_states)
        return;

    ID3D11RasterizerState* pState = nullptr;

    if (fillMode == D3D11_FILL_SOLID)
    {
        if (cullMode == D3D11_CULL_NONE)
            pState = m_states->CullNone();
        if (cullMode == D3D11_CULL_FRONT)
            pState = m_states->CullClockwise();
        if (cullMode == D3D11_CULL_BACK)
            pState = m_states->CullCounterClockwise();
    }
    else if (fillMode == D3D11_FILL_WIREFRAME)
    {
        if (cullMode == D3D11_CULL_NONE)
            pState = m_states->Wireframe();
    }

    if (pState)
        m_pContext->RSSetState(pState);
}

void D3D11Shim::SetRenderTarget(ID3D11Texture2D* pTexture, ID3D11DepthStencilView** ppView)
{
    if (!m_pDevice || !m_pContext)
        return;

    ID3D11RenderTargetView* pRTView = nullptr;
    ID3D11DepthStencilView* pDSView = nullptr;

    if (!ppView)
        // Store current z-buffer.
        m_pContext->OMGetRenderTargets(0, nullptr, &pDSView);
    else
        pDSView = *ppView;

    if (pTexture)
    {

        CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc(pTexture, D3D11_RTV_DIMENSION_TEXTURE2D);
        m_pDevice->CreateRenderTargetView(pTexture, &rtvDesc, &pRTView);
        if (!pRTView)
        {
            if (!ppView)
                SafeRelease(pDSView);
            return;
        }
        CD3D11_VIEWPORT vp(pTexture, pRTView);
        m_pContext->RSSetViewports(1, &vp);
    }

    m_pContext->OMSetRenderTargets(1, &pRTView, pDSView);

    SafeRelease(pRTView);
    if (!ppView)
        SafeRelease(pDSView);
}

void D3D11Shim::SetSamplerState(UINT uSlot, D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode)
{
    if (!m_pContext || !m_states)
        return;

    ID3D11SamplerState* pState = nullptr;
    if (addressMode == D3D11_TEXTURE_ADDRESS_WRAP)
    {
        if (filter == D3D11_FILTER_MIN_MAG_MIP_POINT)
            pState = m_states->PointWrap();
        if (filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR)
            pState = m_states->LinearWrap();
        if (filter == D3D11_FILTER_ANISOTROPIC)
            pState = m_states->AnisotropicWrap();
    }
    if (addressMode == D3D11_TEXTURE_ADDRESS_CLAMP)
    {
        if (filter == D3D11_FILTER_MIN_MAG_MIP_POINT)
            pState = m_states->PointClamp();
        if (filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR)
            pState = m_states->LinearClamp();
        if (filter == D3D11_FILTER_ANISOTROPIC)
            pState = m_states->AnisotropicClamp();
    }

    if (pState)
        m_pContext->PSSetSamplers(uSlot, 1, &pState);
}

void D3D11Shim::SetShader(unsigned int iIndex)
{
    if (!m_pContext)
        return;

    m_pContext->VSSetShader(m_pVShader, NULL, 0);
    m_pContext->VSSetConstantBuffers(0, 1, &m_pCBuffer);

    if (iIndex >= MAX_NUM_SHADERS)
        return;

    m_uCurrShader = iIndex;
    m_bCustomPixelShaderActive = false;
    m_bVertexColorOnly = false;
    ApplyFixedFunctionPixelShader();
}

void D3D11Shim::SetTexture(unsigned int iSlot, ID3D11Resource* pResource)
{
    if (!m_pContext)
        return;

    ID3D11ShaderResourceView* views[1] = {NULL};
    if (m_pDevice && pResource)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        D3D11_RESOURCE_DIMENSION dim{};
        pResource->GetType(&dim);

        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc1(reinterpret_cast<ID3D11Texture2D*>(pResource), D3D11_SRV_DIMENSION_TEXTURE2D);
            srvDesc = srvDesc1;
        }
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
        {
            CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc1(reinterpret_cast<ID3D11Texture3D*>(pResource));
            srvDesc = srvDesc1;
        }
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D || dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
            m_pDevice->CreateShaderResourceView(pResource, &srvDesc, &views[0]);
    }

    m_pContext->PSSetShaderResources(iSlot, 1, views);

    if (iSlot == 0)
    {
        m_bTexture0Bound = (pResource != nullptr);
        ApplyFixedFunctionPixelShader();
    }

    if (views[0])
        views[0]->Release();
}

void D3D11Shim::ClearShaderResources()
{
    if (!m_pContext)
        return;

    ID3D11ShaderResourceView* views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    m_pContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
    m_bTexture0Bound = false;
    ApplyFixedFunctionPixelShader();
}

void D3D11Shim::SetTransform(unsigned int transType, DirectX::XMMATRIX* pMatrix)
{
    if (!pMatrix)
        return;

    if (transType == 2) // view
        XMStoreFloat4x4(&m_transforms.view, XMMatrixTranspose(*pMatrix));
    else if (transType == 3) // projection
        XMStoreFloat4x4(&m_transforms.proj, XMMatrixTranspose(*pMatrix));
    else if (transType == 256) // world
        XMStoreFloat4x4(&m_transforms.world, XMMatrixTranspose(*pMatrix));

    m_bCBufferIsDirty = true;
}

void D3D11Shim::SetVertexColor(bool bUseColor)
{
    m_bCustomPixelShaderActive = false;
    m_bVertexColorOnly = bUseColor;
    ApplyFixedFunctionPixelShader();
}

HRESULT D3D11Shim::CreateVertexShader(const void* pByteCode, SIZE_T codeLength, ID3D11VertexShader** ppShader,
                                      CConstantTable* p_constant_table)
{
    if (!m_pDevice || !pByteCode || codeLength == 0 || !ppShader || !p_constant_table)
        return E_INVALIDARG;

    if (!p_constant_table->GrabShaderData(m_pDevice))
        return S_FALSE;
    return m_pDevice->CreateVertexShader(pByteCode, codeLength, NULL, ppShader);
}

HRESULT D3D11Shim::CreatePixelShader(const void* pByteCode, SIZE_T codeLength, ID3D11PixelShader** ppShader,
                                     CConstantTable* p_constant_table)
{
    if (!m_pDevice || !pByteCode || codeLength == 0 || !ppShader || !p_constant_table)
        return E_INVALIDARG;

    if (!p_constant_table->GrabShaderData(m_pDevice))
        return S_FALSE;
    return m_pDevice->CreatePixelShader(pByteCode, codeLength, NULL, ppShader);
}

void D3D11Shim::SetVertexShader(ID3D11VertexShader* pVShader, CConstantTable* pTable)
{
    if (!m_pContext)
        return;

    if (pTable)
    {
        pTable->ApplyChanges(m_pContext);
        const auto buffers = pTable->GetBufferBindings();
        m_pContext->VSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
    }
    else if (!pVShader)
    {
        // Returning to the built-in vertex shader also returns to its matrix
        // buffer. Custom waves do not call SetShader(), and a preceding warp
        // or blur shader may have replaced or cleared b0.
        m_pContext->VSSetConstantBuffers(0, 1, &m_pCBuffer);
    }
    m_pContext->VSSetShader((pVShader ? pVShader : m_pVShader), NULL, 0);
}

void D3D11Shim::SetPixelShader(ID3D11PixelShader* pPShader, CConstantTable* pTable)
{
    if (!m_pContext)
        return;

    if (pTable)
    {
        pTable->ApplyChanges(m_pContext);
        const auto buffers = pTable->GetBufferBindings();
        m_pContext->PSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
    }
    if (pPShader)
    {
        m_bCustomPixelShaderActive = true;
        m_pContext->PSSetShader(pPShader, NULL, 0);
    }
    else
    {
        m_bCustomPixelShaderActive = false;
        m_bVertexColorOnly = false;
        ApplyFixedFunctionPixelShader();
    }
}

void D3D11Shim::ApplyFixedFunctionPixelShader()
{
    if (!m_pContext || m_bCustomPixelShaderActive)
        return;

    ID3D11PixelShader* shader = (m_uCurrShader < MAX_NUM_SHADERS) ? m_pPShader[m_uCurrShader] : nullptr;

    if (m_bVertexColorOnly || !m_bTexture0Bound)
        shader = m_pPShader[2];

    m_pContext->PSSetShader(shader, NULL, 0);
}

void D3D11Shim::ClearRenderTarget(ID3D11Texture2D* pRTTexture, const float color[4])
{
    if (!m_pDevice || !m_pContext || !pRTTexture || !color)
        return;

    ID3D11RenderTargetView* pRTView = nullptr;
    CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc(pRTTexture, D3D11_RTV_DIMENSION_TEXTURE2D);
    m_pDevice->CreateRenderTargetView(pRTTexture, &rtvDesc, &pRTView);
    if (!pRTView)
        return;
    m_pContext->ClearRenderTargetView(pRTView, color);
    if (pRTView)
        pRTView->Release();
}

void D3D11Shim::CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource)
{
    if (!m_pImmContext || !pDstResource || !pSrcResource)
        return;

    m_pImmContext->CopyResource(pDstResource, pSrcResource);
}

HRESULT D3D11Shim::CreateTextureFromFile(LPCWSTR szFileName, ID3D11Resource** texture)
{
    if (!m_pDevice || !szFileName || !texture)
        return E_INVALIDARG;

    *texture = nullptr;

    //char* u8FileName = _WideToUTF8(szFileName);
    std::wstring strFileName(szFileName);
    //delete[] u8FileName;
    const std::wstring extension = GetExtension(strFileName);
    const bool dds = _wcsicmp(extension.c_str(), L"dds") == 0;
    const HRESULT hr = dds ? CreateDDSTextureFromFile(m_pDevice, szFileName, texture, nullptr)
                           : CreateWICTextureFromFile(m_pDevice, szFileName, texture, nullptr);
    if (SUCCEEDED(hr) || (!dds && _wcsicmp(extension.c_str(), L"tga") != 0))
        return hr;
    return LoadLegacyTextureFile(m_pDevice, szFileName, dds, texture);
}

HRESULT D3D11Shim::CreateTextureFromMemory(const uint8_t* data, size_t dataSize, ID3D11Resource** texture, UINT type)
{
    if (!m_pDevice || !data || dataSize == 0 || !texture)
        return E_INVALIDARG;

    *texture = nullptr;

    const HRESULT hr = type == 0 ? CreateDDSTextureFromMemory(m_pDevice, data, dataSize, texture, nullptr)
                                 : CreateWICTextureFromMemory(m_pDevice, data, dataSize, texture, nullptr);
    return SUCCEEDED(hr) ? hr : LoadLegacyTexture(m_pDevice, data, dataSize, type == 0, texture);
}

bool D3D11Shim::LockRect(ID3D11Resource* pResource, UINT uSubRes, D3D11_MAP mapType, D3D11_MAPPED_SUBRESOURCE* res)
{
    if (!m_pImmContext || !pResource || !res)
        return false;

    HRESULT hr = m_pImmContext->Map(pResource, uSubRes, mapType, 0, res);
    return (S_OK == hr);
}

void D3D11Shim::UnlockRect(ID3D11Resource* pResource, UINT uSubRes)
{
    if (!m_pImmContext || !pResource)
        return;

    m_pImmContext->Unmap(pResource, uSubRes);
}

void D3D11Shim::ResetVisualOutputSample()
{
    ResetVisualReadback(m_visualOutput);
    ResetVisualReadback(m_visualFeedback);
}

bool D3D11Shim::QueueVisualOutputSample()
{
    return QueueVisualReadback(m_visualOutput, nullptr);
}

bool D3D11Shim::QueueVisualFeedbackSample(ID3D11Texture2D* texture)
{
    return texture && QueueVisualReadback(m_visualFeedback, texture);
}

bool D3D11Shim::TryReadVisualOutputSample(float& minimum, float& maximum, float& range, float* dominantFraction,
                                       ContrastImageSample* contrast, float appliedExponent)
{
    return TryReadVisualReadback(m_visualOutput, minimum, maximum, range, dominantFraction, contrast, appliedExponent);
}

bool D3D11Shim::TryReadVisualFeedbackSample(float& minimum, float& maximum, float& range)
{
    return TryReadVisualReadback(m_visualFeedback, minimum, maximum, range);
}

void D3D11Shim::ResetVisualReadback(VisualReadback& sample)
{
    sample.queued = false;
    SafeRelease(sample.query);
    SafeRelease(sample.texture);
}

bool D3D11Shim::QueueVisualReadback(VisualReadback& sample, ID3D11Texture2D* texture)
{
    if (!m_pDevice || !m_pContext || !m_pImmContext || sample.queued)
        return false;

    ID3D11Texture2D* source = nullptr;
    if (texture) { source = texture; source->AddRef(); }
    else GetRenderTarget(&source);
    if (!source)
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0 || desc.Width > 16384 || desc.Height > 16384 ||
        desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1 ||
        (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM))
    {
        SafeRelease(source);
        ResetVisualReadback(sample);
        return false;
    }

    if (sample.texture)
    {
        D3D11_TEXTURE2D_DESC previous{};
        sample.texture->GetDesc(&previous);
        if (previous.Width != desc.Width || previous.Height != desc.Height || previous.Format != desc.Format)
            ResetVisualReadback(sample);
    }
    if (!sample.texture)
    {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(m_pDevice->CreateTexture2D(&desc, nullptr, &sample.texture)))
        {
            SafeRelease(source);
            return false;
        }
    }

    // A new event is essential: an old completed event could otherwise allow a
    // staging read before this deferred context's copy has even been submitted.
    const D3D11_QUERY_DESC queryDesc{D3D11_QUERY_EVENT, 0};
    SafeRelease(sample.query);
    if (FAILED(m_pDevice->CreateQuery(&queryDesc, &sample.query)))
    {
        SafeRelease(source);
        return false;
    }
    m_pContext->CopyResource(sample.texture, source);
    m_pContext->End(sample.query);
    sample.queued = true;
    SafeRelease(source);
    return true;
}

bool D3D11Shim::TryReadVisualReadback(VisualReadback& sample, float& minBrightness, float& maxBrightness,
                                    float& maxChannelRange, float* dominantFraction,
                                    ContrastImageSample* contrast, float appliedExponent)
{
    if (contrast) *contrast = {};
    minBrightness = maxBrightness = maxChannelRange = 0.0f;
    if (dominantFraction)
        *dominantFraction = 0.0f;
    if (!m_pImmContext || !sample.queued || !sample.query || !sample.texture)
        return false;

    BOOL complete = FALSE;
    const HRESULT ready = m_pImmContext->GetData(sample.query, &complete, sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    // A query recorded on a deferred context is not readable until that
    // command list is submitted; DXGI reports INVALID_CALL during this gap.
    if (ready == S_FALSE || ready == DXGI_ERROR_INVALID_CALL || (ready == S_OK && !complete))
        return false;
    if (FAILED(ready))
    {
        ResetVisualReadback(sample);
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = m_pImmContext->Map(sample.texture, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (result == DXGI_ERROR_WAS_STILL_DRAWING)
        return false;
    if (FAILED(result))
    {
        ResetVisualReadback(sample);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    sample.texture->GetDesc(&desc);
    unsigned int minimum[3] = {255, 255, 255};
    unsigned int maximum[3] = {};
    // Explicit diagnostics can also request coarse colour occupancy.
    std::array<UINT, 4096> histogram;
    if (dominantFraction)
        histogram.fill(0);
    UINT dominantPixels = 0;
    for (UINT y = 0; y < desc.Height; ++y)
    {
        const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x)
        {
            if (dominantFraction)
            {
                const auto* pixel = row + static_cast<size_t>(x) * 4;
                const UINT bin = (pixel[0] >> 4) | ((pixel[1] >> 4) << 4) | ((pixel[2] >> 4) << 8);
                dominantPixels = std::max(dominantPixels, ++histogram[bin]);
            }
            // These aggregates are invariant under the RGB/BGR channel order.
            // Ignore alpha: the classifier measures the displayed colour.
            for (unsigned int channel = 0; channel < 3; ++channel)
            {
                const unsigned int value = row[static_cast<size_t>(x) * 4 + channel];
                minimum[channel] = std::min(minimum[channel], value);
                maximum[channel] = std::max(maximum[channel], value);
            }
        }
        // Once one channel spans the entire byte range, all three returned
        // extrema are final. Histograms still need every pixel; contrast has
        // its own independent bounded grid scan below.
        if (!dominantFraction && (maximum[0] - minimum[0] == 255 ||
            maximum[1] - minimum[1] == 255 || maximum[2] - minimum[2] == 255))
            break;
    }
    if (contrast)
    {
        // Bounded work on the existing readback: at most 64x64 pixels, with no
        // extra texture copy or render pass. RGB/BGR order does not affect it.
        ContrastImageAccumulator accumulator;
        const UINT stepX = (std::max)(1U, (desc.Width + 63) / 64);
        const UINT stepY = (std::max)(1U, (desc.Height + 63) / 64);
        for (UINT y = stepY / 2; y < desc.Height; y += stepY)
        {
            const auto* row = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            for (UINT x = stepX / 2; x < desc.Width; x += stepX)
            {
                const auto* pixel = row + static_cast<size_t>(x) * 4;
                accumulator.Add(pixel[0], pixel[1], pixel[2], appliedExponent,
                    (std::min)(7U, x * 8 / desc.Width) + 8 * (std::min)(7U, y * 8 / desc.Height));
            }
        }
        *contrast = accumulator.Finish();
    }
    m_pImmContext->Unmap(sample.texture, 0);
    sample.queued = false;
    SafeRelease(sample.query);
    minBrightness = std::min(minimum[0], std::min(minimum[1], minimum[2])) / 255.0f;
    maxBrightness = std::max(maximum[0], std::max(maximum[1], maximum[2])) / 255.0f;
    maxChannelRange = std::max(maximum[0] - minimum[0],
        std::max(maximum[1] - minimum[1], maximum[2] - minimum[2])) / 255.0f;
    // Preserve even small highlights missed by the coarse grid. Compare in the
    // original domain so our own correction cannot turn itself off.
    if (contrast && contrast->available && std::isfinite(appliedExponent) && appliedExponent >= .25f && appliedExponent <= 1)
        contrast->peak = std::max(contrast->peak, std::pow(maxBrightness, 1.0f / appliedExponent));
    if (dominantFraction)
        *dominantFraction = static_cast<float>(dominantPixels) /
            (static_cast<float>(desc.Width) * desc.Height);
    return true;
}

int D3D11Shim::NumVertsFromType(unsigned int primType, int iPrimCount)
{
    switch (primType)
    {
        case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
            return iPrimCount;
        case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
            return iPrimCount * 2;
        case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
            return iPrimCount + 1;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
            return iPrimCount * 3;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN: // D3DPT_TRIANGLEFAN (0x6): Triangle fans are not supported in Direct3D 10 or later.
            return iPrimCount + 2;
        default:
            return -1;
    }
}

bool D3D11Shim::UpdateVBuffer(unsigned int iNumVerts, const void* pVData, unsigned int vertexStride)
{
    if (!m_pContext || !m_pVBuffer || !pVData || iNumVerts == 0 || vertexStride == 0 || vertexStride > sizeof(MDVERTEX))
        return false;

    D3D11_MAPPED_SUBRESOURCE res;
    if (!pVData || iNumVerts == 0)
        return false;

    const unsigned int numVerts = std::min(iNumVerts, MAX_VERTICES_COUNT);

    if (S_OK != m_pContext->Map(m_pVBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res))
        return false;

    if (vertexStride == sizeof(MDVERTEX))
    {
        memcpy(res.pData, pVData, numVerts * sizeof(MDVERTEX));
    }
    else if (vertexStride == sizeof(WFVERTEX))
    {
        auto* dst = static_cast<MDVERTEX*>(res.pData);
        const auto* src = static_cast<const WFVERTEX*>(pVData);
        for (unsigned int i = 0; i < numVerts; ++i)
            CopyVertexCommon(dst[i], src[i].x, src[i].y, src[i].z, src[i].r, src[i].g, src[i].b, src[i].a);
    }
    else if (vertexStride == sizeof(SPRITEVERTEX) || vertexStride == sizeof(HELPVERTEX))
    {
        static_assert(sizeof(SPRITEVERTEX) == sizeof(HELPVERTEX), "sprite and help vertices must stay layout-compatible");
        auto* dst = static_cast<MDVERTEX*>(res.pData);
        const auto* src = static_cast<const SPRITEVERTEX*>(pVData);
        for (unsigned int i = 0; i < numVerts; ++i)
            CopySpriteLikeVertex(dst[i], src[i]);
    }
    else if (vertexStride == sizeof(SIMPLEVERTEX))
    {
        auto* dst = static_cast<MDVERTEX*>(res.pData);
        const auto* src = static_cast<const SIMPLEVERTEX*>(pVData);
        for (unsigned int i = 0; i < numVerts; ++i)
            CopySimpleVertex(dst[i], src[i]);
    }
    else
    {
        m_pContext->Unmap(m_pVBuffer, 0);
        return false;
    }

    m_pContext->Unmap(m_pVBuffer, 0);
    return true;
}

bool D3D11Shim::UpdateIBuffer(unsigned int iNumIndices, const void* pIData)
{
    if (!m_pContext || !m_pIBuffer || !pIData || iNumIndices == 0)
        return false;

    D3D11_MAPPED_SUBRESOURCE res;
    if (S_OK == m_pContext->Map(m_pIBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &res))
    {
        memcpy(res.pData, pIData, std::min(iNumIndices, MAX_INDICES_COUNT) * sizeof(uint16_t));
        m_pContext->Unmap(m_pIBuffer, 0);
        return true;
    }

    return false;
}
