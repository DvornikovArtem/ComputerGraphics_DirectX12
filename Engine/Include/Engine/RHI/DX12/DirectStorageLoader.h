#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include <wrl/client.h>
#include <d3d12.h>

namespace DirectX
{
    class ResourceUploadBatch;
}

class DirectStorageLoader
{
public:
    DirectStorageLoader() = default;
    ~DirectStorageLoader();

    DirectStorageLoader(const DirectStorageLoader&) = delete;
    DirectStorageLoader& operator=(const DirectStorageLoader&) = delete;

    void Initialize(ID3D12Device* device);

    void ReadFileToMemory(const std::wstring& path, std::vector<std::uint8_t>& outData);

    void CreateDDSTextureFromFile_DS(
        ID3D12Device* device,
        DirectX::ResourceUploadBatch& upload,
        const std::wstring& ddsPath,
        ID3D12Resource** outTexture);

private:
    void WaitForQueue();

private:
    Microsoft::WRL::ComPtr<ID3D12Device> mDevice;

    Microsoft::WRL::ComPtr<IUnknown> mFactory;
    Microsoft::WRL::ComPtr<IUnknown> mQueue;

    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    HANDLE mFenceEvent = nullptr;
    UINT64 mFenceValue = 0;

    bool mInitialized = false;
};