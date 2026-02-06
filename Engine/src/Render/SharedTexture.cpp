#include "Engine/Render/SharedTexture.h"
#include <comdef.h>

SharedTexture::SharedTexture()
    : mWidth(0)
    , mHeight(0)
    , mFormat(DXGI_FORMAT_UNKNOWN)
    , mInitialized(false)
    , mHeapSize(0)
{
}

SharedTexture::~SharedTexture()
{
    Release();
}

void SharedTexture::Initialize(
    ID3D12Device* primaryDevice,
    ID3D12Device* secondaryDevice,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    const std::wstring& name)
{
    if (mInitialized) Release();

    mPrimaryDevice = primaryDevice;
    mSecondaryDevice = secondaryDevice;
    mWidth = width;
    mHeight = height;
    mFormat = format;
    mName = name;

    CreateSharedHeap();
    CreatePlacedResources();
    ShareResources();
    SetDebugNames();

    mInitialized = true;
}

void SharedTexture::Resize(
    ID3D12Device* primaryDevice,
    ID3D12Device* secondaryDevice,
    UINT width,
    UINT height)
{
    if (!mInitialized) return;

    Release();
    Initialize(primaryDevice, secondaryDevice, width, height, mFormat, mName);
}

void SharedTexture::Release()
{
    mSharedTextureSecondary.Reset();
    mSharedTexturePrimary.Reset();
    mSharedHeap.Reset();
    mSecondaryDevice.Reset();
    mPrimaryDevice.Reset();

    mWidth = 0;
    mHeight = 0;
    mFormat = DXGI_FORMAT_UNKNOWN;
    mName.clear();
    mInitialized = false;
    mHeapSize = 0;
}

D3D12_RESOURCE_DESC SharedTexture::GetDesc() const
{
    if (!mInitialized) throw std::runtime_error("SharedTexture is not initialized");

    return mSharedTexturePrimary->GetDesc();
}

UINT64 SharedTexture::GetSizeInBytes() const
{
    if (!mInitialized) return 0;

    D3D12_RESOURCE_DESC desc = GetDesc();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;

    mPrimaryDevice->GetCopyableFootprints(
        &desc,
        0, 1, 0,
        &layout,
        &numRows,
        &rowSizeInBytes,
        &totalBytes);

    return totalBytes;
}

UINT64 SharedTexture::CalculateHeapSize(const D3D12_RESOURCE_DESC& desc) const
{
    D3D12_RESOURCE_ALLOCATION_INFO allocInfo = mPrimaryDevice->GetResourceAllocationInfo(0, 1, &desc);

    return allocInfo.SizeInBytes;
}

void SharedTexture::CreateSharedHeap()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = mFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    mHeapSize = CalculateHeapSize(desc);

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = mHeapSize;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapDesc.Properties.CreationNodeMask = 1;
    heapDesc.Properties.VisibleNodeMask = 1;
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Flags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;

    ThrowIfFailed(mPrimaryDevice->CreateHeap(
        &heapDesc,
        IID_PPV_ARGS(&mSharedHeap)),
        "Failed to create shared heap for cross-adapter texture");
}

void SharedTexture::CreatePlacedResources()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = mFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    ThrowIfFailed(mPrimaryDevice->CreatePlacedResource(
        mSharedHeap.Get(),
        0,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&mSharedTexturePrimary)),
        "Failed to create placed resource on primary device");
}

void SharedTexture::ShareResources()
{
    HANDLE heapHandle = nullptr;
    ThrowIfFailed(mPrimaryDevice->CreateSharedHandle(
        mSharedHeap.Get(),
        nullptr,
        GENERIC_ALL,
        nullptr,
        &heapHandle),
        "Failed to create shared handle for heap");

    if (!heapHandle) throw std::runtime_error("Failed to create shared handle (handle is null)");

    Microsoft::WRL::ComPtr<ID3D12Heap> sharedHeapOnSecondary;
    HRESULT hr = mSecondaryDevice->OpenSharedHandle(
        heapHandle,
        IID_PPV_ARGS(&sharedHeapOnSecondary));

    CloseHandle(heapHandle);
    ThrowIfFailed(hr, "Failed to open shared heap handle on secondary device");

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = mFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    ThrowIfFailed(mSecondaryDevice->CreatePlacedResource(
        sharedHeapOnSecondary.Get(),
        0,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&mSharedTextureSecondary)),
        "Failed to create placed resource on secondary device");
}

void SharedTexture::SetDebugNames()
{
    if (mSharedHeap)
    {
        std::wstring heapName = mName + L" Heap";
        mSharedHeap->SetName(heapName.c_str());
    }

    if (mSharedTexturePrimary)
    {
        std::wstring primaryName = mName + L" (Primary)";
        mSharedTexturePrimary->SetName(primaryName.c_str());
    }

    if (mSharedTextureSecondary)
    {
        std::wstring secondaryName = mName + L" (Secondary)";
        mSharedTextureSecondary->SetName(secondaryName.c_str());
    }
}

D3D12_RESOURCE_STATES SharedTexture::CopyFromPrimaryDevice(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* sourceResource,
    D3D12_RESOURCE_STATES sourceState)
{
    CD3DX12_RESOURCE_BARRIER preCopyBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            sourceResource,
            sourceState,
            D3D12_RESOURCE_STATE_COPY_SOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedTexturePrimary.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST)
    };

    commandList->ResourceBarrier(_countof(preCopyBarriers), preCopyBarriers);
    commandList->CopyResource(mSharedTexturePrimary.Get(), sourceResource);

    CD3DX12_RESOURCE_BARRIER postCopyBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedTexturePrimary.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(
            sourceResource,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            sourceState)
    };

    commandList->ResourceBarrier(_countof(postCopyBarriers), postCopyBarriers);

    return sourceState;
}

D3D12_RESOURCE_STATES SharedTexture::CopyToSecondaryDevice(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* destResource,
    D3D12_RESOURCE_STATES destState)
{

    CD3DX12_RESOURCE_BARRIER preCopyBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            destResource,
            destState,
            D3D12_RESOURCE_STATE_COPY_DEST),
        CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedTextureSecondary.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_SOURCE)
    };

    commandList->ResourceBarrier(_countof(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(destResource, mSharedTextureSecondary.Get());

    CD3DX12_RESOURCE_BARRIER postCopyBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedTextureSecondary.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(
            destResource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            destState)
    };

    commandList->ResourceBarrier(_countof(postCopyBarriers), postCopyBarriers);

    return destState;
}