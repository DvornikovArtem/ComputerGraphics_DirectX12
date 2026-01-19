#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include <wrl/client.h>
#include <d3d12.h>
#include <dstorage.h>

namespace DirectX
{
    class ResourceUploadBatch;
}

class DirectStorageLoader
{
public:
    DirectStorageLoader() = default;
    ~DirectStorageLoader();

    // Forbid copying to avoid double free and the multiplication of owners
    DirectStorageLoader(const DirectStorageLoader&) = delete;
    DirectStorageLoader& operator=(const DirectStorageLoader&) = delete;

    void Initialize(ID3D12Device* device);

    // Reads a file into memory: path - the file path, outData - the output buffer where the file bytes will be written
    void ReadFileToMemory(const std::wstring& path, std::vector<std::uint8_t>& outData);
    // Reads a DDS file into memory via DirectStorage, then creates a D3D12 texture resource from memory using a DDS loader
    void CreateDDSTextureFromFile_DS(DirectX::ResourceUploadBatch& upload, const std::wstring& ddsPath, ID3D12Resource** outTexture);

private:
    // Internal synchronization method
    void WaitForQueue();

private:
    Microsoft::WRL::ComPtr<ID3D12Device> mDevice;

    // Factory - is a DirectStorage's resource manager
    Microsoft::WRL::ComPtr<IDStorageFactory> mFactory;
    // Queue - is an IO request queue (it accepts IO requests, orders their execution, transfers data to RAM or directly to GPU memory (VRAM), and signals completion)
    Microsoft::WRL::ComPtr<IDStorageQueue> mQueue;

    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    HANDLE mFenceEvent = nullptr;
    UINT64 mFenceValue = 0;

    bool mInitialized = false;
};