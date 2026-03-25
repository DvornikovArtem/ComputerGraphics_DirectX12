#pragma once
#include "../Core/d3dUtil.h"

class SharedTexture
{
public:
    SharedTexture();
    ~SharedTexture();

    SharedTexture(const SharedTexture&) = delete;
    SharedTexture& operator=(const SharedTexture&) = delete;

    void Initialize(
        ID3D12Device* primaryDevice,
        ID3D12Device* secondaryDevice,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        const std::wstring& name = L"SharedTexture");

    void Resize(
        ID3D12Device* primaryDevice,
        ID3D12Device* secondaryDevice,
        UINT width,
        UINT height);

    void Release();
    bool IsInitialized() const { return mInitialized; }

    ID3D12Resource* GetPrimaryResource() const { return mSharedTexturePrimary.Get(); }
    ID3D12Resource* GetSecondaryResource() const { return mSharedTextureSecondary.Get(); }
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }
    DXGI_FORMAT GetFormat() const { return mFormat; }
    const std::wstring& GetName() const { return mName; }

    D3D12_RESOURCE_DESC GetDesc() const;
    UINT64 GetSizeInBytes() const;

    D3D12_RESOURCE_STATES CopyFromPrimaryDevice(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* sourceResource,
        D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COMMON);

    D3D12_RESOURCE_STATES CopyToSecondaryDevice(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* destResource,
        D3D12_RESOURCE_STATES destState = D3D12_RESOURCE_STATE_COMMON);
    D3D12_RESOURCE_STATES SharedTexture::CopyFromSecondaryDevice(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* sourceResource,
        D3D12_RESOURCE_STATES destState = D3D12_RESOURCE_STATE_COMMON);
    D3D12_RESOURCE_STATES SharedTexture::CopyToPrimaryDevice(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* destResource,
        D3D12_RESOURCE_STATES destState = D3D12_RESOURCE_STATE_COMMON);

private:
    Microsoft::WRL::ComPtr<ID3D12Device> mPrimaryDevice;
    Microsoft::WRL::ComPtr<ID3D12Device> mSecondaryDevice;

    Microsoft::WRL::ComPtr<ID3D12Heap> mSharedHeap;

    Microsoft::WRL::ComPtr<ID3D12Resource> mSharedTexturePrimary;
    Microsoft::WRL::ComPtr<ID3D12Resource> mSharedTextureSecondary;

    UINT mWidth;
    UINT mHeight;
    DXGI_FORMAT mFormat;
    std::wstring mName;
    bool mInitialized;
    UINT64 mHeapSize;

    void CreateSharedHeap();
    void CreatePlacedResources();
    void ShareResources();
};