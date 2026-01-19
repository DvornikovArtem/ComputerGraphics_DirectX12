#include <Engine/RHI/DX12/DirectStorageLoader.h>

#include <stdexcept>
#include <Windows.h>

#include <dstorage.h>
#include <Engine/RHI/DX12/DDSTextureLoader.h>

#include <Engine/RHI/DX12/DirectXHelpers.h>
#include <Engine/Math/MathHelper.h>

#include <Engine/RHI/DX12/ResourceUploadBatch.h>

#pragma comment(lib, "dstorage.lib")


using Microsoft::WRL::ComPtr;


static inline void ThrowIfFailedHR(HRESULT hr, const char* msg)
{
    if (FAILED(hr))
    {
        throw std::runtime_error(msg);
    }
}


DirectStorageLoader::~DirectStorageLoader()
{
    if (mFenceEvent)
    {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}


void DirectStorageLoader::Initialize(ID3D12Device* device)
{
    if (mInitialized) return;
    if (!device) throw std::runtime_error("DirectStorageLoader::Initialize: device is null");

    mDevice = device;

    // Factory
    ComPtr<IDStorageFactory> factory;
    ThrowIfFailedHR(DStorageGetFactory(IID_PPV_ARGS(&factory)), "DStorageCreateFactory failed");

    // Set staging buffer size to 128MB to handle large textures
    // Default is 32MB, which is not enough for some of our DDS files
    ThrowIfFailedHR(factory->SetStagingBufferSize(128 * 1024 * 1024), "IDStorageFactory::SetStagingBufferSize failed");

    // Queue
    DSTORAGE_QUEUE_DESC qdesc = {};
    qdesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    qdesc.Capacity = 1024;
    qdesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    qdesc.Device = device;
    qdesc.Name = "Engine DirectStorage Queue";

    ComPtr<IDStorageQueue> queue;
    ThrowIfFailedHR(factory->CreateQueue(&qdesc, IID_PPV_ARGS(&queue)), "IDStorageFactory::CreateQueue failed");

    // Fence for sync
    ThrowIfFailedHR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)), "CreateFence failed");
    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!mFenceEvent) throw std::runtime_error("CreateEvent failed");

    mFactory = factory;
    mQueue = queue;

    mFenceValue = 0;
    mInitialized = true;
}

void DirectStorageLoader::WaitForQueue()
{
    if (mFence->GetCompletedValue() < mFenceValue)
    {
        ThrowIfFailedHR(mFence->SetEventOnCompletion(mFenceValue, mFenceEvent), "SetEventOnCompletion failed");
        WaitForSingleObject(mFenceEvent, INFINITE);
    }
}

void DirectStorageLoader::ReadFileToMemory(const std::wstring& path, std::vector<std::uint8_t>& outData)
{
    if (!mInitialized) throw std::runtime_error("DirectStorageLoader not initialized");

    // Convert relative path to absolute path for DirectStorage
    wchar_t fullPath[MAX_PATH];
    if (GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr) == 0)
        throw std::runtime_error("GetFullPathNameW failed");

    // Get file size
    LARGE_INTEGER fileSize = {};
    {
        HANDLE h = CreateFileW(fullPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            throw std::runtime_error("CreateFileW failed (file not found or path invalid)");

        BOOL ok = GetFileSizeEx(h, &fileSize);
        CloseHandle(h);
        if (!ok || fileSize.QuadPart <= 0)
            throw std::runtime_error("GetFileSizeEx failed");
    }

    const size_t sizeBytes = static_cast<size_t>(fileSize.QuadPart);
    outData.assign(sizeBytes, 0);

    ComPtr<IDStorageFactory> factory;
    ThrowIfFailedHR(mFactory.As(&factory), "Factory As<IDStorageFactory> failed");

    ComPtr<IDStorageQueue> queue;
    ThrowIfFailedHR(mQueue.As(&queue), "Queue As<IDStorageQueue> failed");

    // Open DirectStorage file object
    ComPtr<IDStorageFile> dsFile;
    ThrowIfFailedHR(factory->OpenFile(fullPath, IID_PPV_ARGS(&dsFile)), "IDStorageFactory::OpenFile failed");

    DSTORAGE_REQUEST req = {};
    req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    req.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    req.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;

    req.Source.File.Source = dsFile.Get();
    req.Source.File.Offset = 0;
    req.Source.File.Size = static_cast<UINT32>(sizeBytes);

    req.Destination.Memory.Buffer = outData.data();
    req.Destination.Memory.Size = static_cast<UINT32>(sizeBytes);

    req.UncompressedSize = static_cast<UINT32>(sizeBytes);

    queue->EnqueueRequest(&req);

    // signal fence
    ++mFenceValue;
    queue->EnqueueSignal(mFence.Get(), mFenceValue);

    queue->Submit();
    WaitForQueue();

    // Check for DirectStorage errors
    DSTORAGE_ERROR_RECORD errorRecord = {};
    queue->RetrieveErrorRecord(&errorRecord);
    if (FAILED(errorRecord.FirstFailure.HResult))
    {
        char buf[512];
        sprintf_s(buf, "DirectStorage request failed. HRESULT: 0x%08X", (unsigned)errorRecord.FirstFailure.HResult);
        throw std::runtime_error(buf);
    }
}

void DirectStorageLoader::CreateDDSTextureFromFile_DS(
    ID3D12Device* device,
    DirectX::ResourceUploadBatch& upload,
    const std::wstring& ddsPath,
    ID3D12Resource** outTexture)
{
    if (!device || !outTexture)
        throw std::runtime_error("CreateDDSTextureFromFile_DS: bad args");

    std::vector<std::uint8_t> bytes;
    ReadFileToMemory(ddsPath, bytes);

    if (bytes.size() < 4 || memcmp(bytes.data(), "DDS ", 4) != 0)
        throw std::runtime_error("Loaded data is not a DDS (missing 'DDS ' magic).");

    HRESULT hr = DirectX::CreateDDSTextureFromMemory(
        device,
        upload,
        bytes.data(),
        bytes.size(),
        outTexture);

    ThrowIfFailedHR(hr, "CreateDDSTextureFromMemory(ResourceUploadBatch) failed");
}