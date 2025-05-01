// IRenderTargetProvider.h
#pragma once
#include <d3d12.h>

class IRenderTargetProvider {
public:
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const = 0;
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const = 0;

    virtual ~IRenderTargetProvider() = default;
};
