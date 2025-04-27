// RenderingSystem.h
#pragma once

#ifndef GBUFFER_H
#define GBUFFER_H


#include "d3dUtil.h"

using Microsoft::WRL::ComPtr;

class Gbuffer {

    Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice;

    ComPtr<ID3D12Resource> DiffuseTex       = nullptr;
    ComPtr<ID3D12Resource> EmissiveTex      = nullptr;
    ComPtr<ID3D12Resource> NormalTex        = nullptr;
    ComPtr<ID3D12Resource> MaterialAlbedoTex                = nullptr;
    ComPtr<ID3D12Resource> MaterialFresnelRoughnessTex      = nullptr;

    ComPtr<ID3D12Resource> AccumulationBuf  = nullptr;
    ComPtr<ID3D12Resource> BloomTex         = nullptr; // это потом (про блики)


public:
    D3D12_CPU_DESCRIPTOR_HANDLE DiffuseSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE EmissiveSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE NormalSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE MaterialAlbedoSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE MaterialFresnelRoughnessSRV;

    D3D12_CPU_DESCRIPTOR_HANDLE DiffuseRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE EmissiveRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE NormalRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE MaterialAlbedoRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE MaterialFresnelRoughnessRTV;

    D3D12_CPU_DESCRIPTOR_HANDLE AccumulationSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE BloomSRV;

    D3D12_CPU_DESCRIPTOR_HANDLE AccumulationRTV;
    D3D12_CPU_DESCRIPTOR_HANDLE BloomRTV;

    ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
    ComPtr<ID3D12DescriptorHeap> m_SRVDescriptorHeap;

public:
    Gbuffer(int width, int height, Microsoft::WRL::ComPtr<ID3D12Device> device);

    void setDevice(Microsoft::WRL::ComPtr<ID3D12Device> newDevice) { md3dDevice = newDevice; };

    ComPtr<ID3D12DescriptorHeap> getSRVDescriptorHeap() const { return m_SRVDescriptorHeap; }

    DXGI_FORMAT getRTVFormat() {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    };

    void CopyDescriptors(D3D12_CPU_DESCRIPTOR_HANDLE otherStart);

    /*void TransitToOpaqueRenderingState(ComPtr<ID3D12GraphicsCommandList2>& c);
    void TransitToLightsRenderingState(ComPtr<ID3D12GraphicsCommandList2>& c);
    void TransitToTonemappingState(ComPtr<ID3D12GraphicsCommandList2>& c);*/
    void TransitToOpaqueRenderingState(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitToLightsRenderingState(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitToTonemappingState(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitToCommon(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitFromRenderTargetToCommon(ComPtr<ID3D12GraphicsCommandList>& c);
    void TransitFromShaderResourceToCommon(ComPtr<ID3D12GraphicsCommandList>& c);

    void Resize(int width, int height);

    void Dispose();
};

#endif // GBUFFER_H