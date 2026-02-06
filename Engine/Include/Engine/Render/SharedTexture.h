#pragma once
#include "../Core/d3dUtil.h"

class SharedTexture
{
public:
    SharedTexture();
    ~SharedTexture();

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

    ID3D12Resource* GetPrimaryResource() const { return mSharedFrameTexture.Get(); }
    ID3D12Resource* GetSecondaryResource() const { return mSharedFrameTextureOnDevice2.Get(); }
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }
    DXGI_FORMAT GetFormat() const { return mFormat; }
    const std::wstring& GetName() const { return mName; }

    D3D12_RESOURCE_DESC GetDesc() const;

    void PrepareForCopyFromPrimary(ID3D12GraphicsCommandList* commandList, ID3D12Resource* sourceResource);
    void FinishCopyOnPrimary(ID3D12GraphicsCommandList* commandList);
    void PrepareForCopyToSecondary(ID3D12GraphicsCommandList* commandList, ID3D12Resource* destResource);
    void FinishCopyOnSecondary(ID3D12GraphicsCommandList* commandList);

private:
    Microsoft::WRL::ComPtr<ID3D12Device> mPrimaryDevice;
    Microsoft::WRL::ComPtr<ID3D12Device> mSecondaryDevice;
    Microsoft::WRL::ComPtr<ID3D12Resource> mSharedFrameTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> mSharedFrameTextureOnDevice2;

    UINT mWidth;
    UINT mHeight;
    DXGI_FORMAT mFormat;
    std::wstring mName;
    bool mInitialized;

    void CreateSharedTexture();
    void CreateAndShareHandle();
    void SetDebugNames();
};