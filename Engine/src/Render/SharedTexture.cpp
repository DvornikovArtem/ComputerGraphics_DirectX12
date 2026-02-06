#include "Engine/Render/SharedTexture.h"
#include <comdef.h>

SharedTexture::SharedTexture()
    : mWidth(0)
    , mHeight(0)
    , mFormat(DXGI_FORMAT_UNKNOWN)
    , mInitialized(false)
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

    CreateSharedTexture();
    CreateAndShareHandle();
    SetDebugNames();

    mInitialized = true;
}

void SharedTexture::Resize(
    ID3D12Device* primaryDevice,
    ID3D12Device* secondaryDevice,
    UINT width,
    UINT height)
{
    Release();
    Initialize(primaryDevice, secondaryDevice, width, height, mFormat, mName);
}

void SharedTexture::Release()
{
    mSharedFrameTextureOnDevice2.Reset();
    mSharedFrameTexture.Reset();
    mSecondaryDevice.Reset();
    mPrimaryDevice.Reset();
    mInitialized = false;
}

D3D12_RESOURCE_DESC SharedTexture::GetDesc() const
{
    if (!mInitialized) throw std::runtime_error("SharedTexture is not initialized");
    return mSharedFrameTexture->GetDesc();
}

void SharedTexture::CreateSharedTexture()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    ThrowIfFailed(mPrimaryDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&mSharedFrameTexture)));
}

void SharedTexture::CreateAndShareHandle()
{
    HANDLE textureHandle = nullptr;

    ThrowIfFailed(mPrimaryDevice->CreateSharedHandle(
        mSharedFrameTexture.Get(),
        nullptr,
        GENERIC_ALL,
        nullptr,
        &textureHandle),
        "Failed to create shared handle");

    if (!textureHandle)
    {
        throw std::runtime_error("Failed to create shared handle (handle is null)");
    }

    HRESULT hr = mSecondaryDevice->OpenSharedHandle(
        textureHandle,
        IID_PPV_ARGS(&mSharedFrameTextureOnDevice2));

    CloseHandle(textureHandle);

    ThrowIfFailed(hr, "Failed to open shared handle on secondary device");
}

void SharedTexture::SetDebugNames()
{
    if (mSharedFrameTexture)
    {
        std::wstring primaryName = mName + L" (Primary)";
        mSharedFrameTexture->SetName(primaryName.c_str());
    }

    if (mSharedFrameTextureOnDevice2)
    {
        std::wstring secondaryName = mName + L" (Secondary)";
        mSharedFrameTextureOnDevice2->SetName(secondaryName.c_str());
    }
}

void SharedTexture::PrepareForCopyFromPrimary(ID3D12GraphicsCommandList* commandList, ID3D12Resource* sourceResource)
{
    if (!mInitialized || !commandList || !sourceResource)
    {
        throw std::invalid_argument("Invalid arguments for PrepareForCopyFromPrimary");
    }

    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            sourceResource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_SOURCE));

    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedFrameTexture.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST));
}

void SharedTexture::FinishCopyOnPrimary(ID3D12GraphicsCommandList* commandList)
{
    if (!mInitialized || !commandList)
    {
        throw std::invalid_argument("Invalid arguments for FinishCopyOnPrimary");
    }

    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedFrameTexture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COMMON));
}

void SharedTexture::PrepareForCopyToSecondary(ID3D12GraphicsCommandList* commandList, ID3D12Resource* destResource)
{
    if (!mInitialized || !commandList || !destResource)
    {
        throw std::invalid_argument("Invalid arguments for PrepareForCopyToSecondary");
    }

    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            destResource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST));

    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedFrameTextureOnDevice2.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_SOURCE));
}

void SharedTexture::FinishCopyOnSecondary(ID3D12GraphicsCommandList* commandList)
{
    if (!mInitialized || !commandList)
    {
        throw std::invalid_argument("Invalid arguments for FinishCopyOnSecondary");
    }

    commandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            mSharedFrameTextureOnDevice2.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_COMMON));
}