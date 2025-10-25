// RenderingSystem.h
#pragma once

#ifndef GBUFFER_H
#define GBUFFER_H


#include "../Core/d3dUtil.h"

using Microsoft::WRL::ComPtr;

struct GBufferChannel
{
    ComPtr<ID3D12Resource> Resource;
    DXGI_FORMAT Format;
    D3D12_CPU_DESCRIPTOR_HANDLE RTV;
    D3D12_CPU_DESCRIPTOR_HANDLE SRV;
    int SRVHeapIndex;
    D3D12_CLEAR_VALUE ClearValue;

    GBufferChannel(DXGI_FORMAT Format)
    {
        this->Format = ClearValue.Format = Format;
        ClearValue.Color[0] = 0.0f;
        ClearValue.Color[1] = 0.0f;
        ClearValue.Color[2] = 0.0f;
        ClearValue.Color[3] = 1.0f;
    }
};

class Gbuffer {

    Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
    UINT RTVDescSize;
    UINT SRVDescSize;

public:
    GBufferChannel Diffuse = GBufferChannel(DXGI_FORMAT_R8G8B8A8_UNORM);
    GBufferChannel DepthStencils = GBufferChannel(DXGI_FORMAT_R32G32B32A32_FLOAT);
    GBufferChannel Normal = GBufferChannel(DXGI_FORMAT_R16G16B16A16_SNORM);
    GBufferChannel MatFresnelRoughness = GBufferChannel(DXGI_FORMAT_R8G8B8A8_UNORM);
    GBufferChannel Accumulation = GBufferChannel(DXGI_FORMAT_R16G16B16A16_FLOAT);
    GBufferChannel VelocityBuffer = GBufferChannel(DXGI_FORMAT_R16G16_FLOAT);
    GBufferChannel ObjectOutlines = GBufferChannel(DXGI_FORMAT_R8G8B8A8_UNORM);

    std::vector<GBufferChannel*> ChannelPTRs = { &Diffuse, &DepthStencils, &Normal, &MatFresnelRoughness, 
        &Accumulation, &VelocityBuffer, &ObjectOutlines };

    const int NumBuffers = ChannelPTRs.size();
    int Channel0SRVHeapIndex;

    ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
    ComPtr<ID3D12DescriptorHeap> m_SRVDescriptorHeap;

    Gbuffer(int width, int height, Microsoft::WRL::ComPtr<ID3D12Device> device);

    ComPtr<ID3D12DescriptorHeap> getSRVDescriptorHeap() const { return m_SRVDescriptorHeap; }

    //All resources start as Common -> All resources transitted to RTV ->
    // -> resources are transited to SRVs as needed(all resources need to to be transited)  ->
    // -> All resources are transited from SRV to Common
    void TransitCommonToRTV(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitToLightsRenderingState(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitToTonemappingState(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitSRVToCommon(ComPtr<ID3D12GraphicsCommandList>& c);

    void Clear(ComPtr<ID3D12GraphicsCommandList>& cmdList);

    void Resize(int width, int height);

    void Dispose();
};

#endif // GBUFFER_H