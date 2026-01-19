#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include <wrl/client.h>
#include <d3d12.h>

namespace DirectX
{
    class ResourceUploadBatch; // forward (у тебя он уже используется)
}

class DirectStorageLoader
{
public:
    DirectStorageLoader() = default;
    ~DirectStorageLoader();

    DirectStorageLoader(const DirectStorageLoader&) = delete;
    DirectStorageLoader& operator=(const DirectStorageLoader&) = delete;

    // Вызывать один раз после создания md3dDevice
    void Initialize(ID3D12Device* device);

    // Прочитать файл через DirectStorage в RAM (vector<byte>)
    void ReadFileToMemory(const std::wstring& path, std::vector<std::uint8_t>& outData);

    // Хелпер для DDS: DirectStorage -> RAM -> CreateDDSTextureFromMemory
    void CreateDDSTextureFromFile_DS(
        ID3D12Device* device,
        DirectX::ResourceUploadBatch& upload,
        const std::wstring& ddsPath,
        ID3D12Resource** outTexture);

private:
    void WaitForQueue();

private:
    Microsoft::WRL::ComPtr<ID3D12Device> mDevice;

    // DirectStorage COM
    Microsoft::WRL::ComPtr<IUnknown> mFactory; // будет QueryInterface в cpp на IDStorageFactory
    Microsoft::WRL::ComPtr<IUnknown> mQueue;   // будет QueryInterface в cpp на IDStorageQueue

    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    HANDLE mFenceEvent = nullptr;
    UINT64 mFenceValue = 0;

    bool mInitialized = false;
};