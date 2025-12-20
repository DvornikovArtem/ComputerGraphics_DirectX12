// Gbuffer.cpp

#include <Engine/Render/Gbuffer.h>
#include <Engine/Core/d3dUtil.h>    
#include <Engine/RHI/DX12/d3dx12.h>     
#include <stdexcept>

Gbuffer::Gbuffer(int width, int height, Microsoft::WRL::ComPtr<ID3D12Device> device)
{
    mDevice = device;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = NumBuffers;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RTVDescriptorHeap))))
        throw std::runtime_error("Failed to create RTV Descriptor Heap");

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = NumBuffers;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SRVDescriptorHeap))))
        throw std::runtime_error("Failed to create SRV Descriptor Heap");

    RTVDescSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    SRVDescSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void Gbuffer::TransitCommonToRTV(ComPtr<ID3D12GraphicsCommandList4>& cmdList)
{
    std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
    for (GBufferChannel* i : ChannelPTRs)
    {
        Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            i->Resource.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_RENDER_TARGET));
    }
    cmdList->ResourceBarrier(NumBuffers, Barriers.data());
}

void Gbuffer::TransitToLightsRenderingState(ComPtr<ID3D12GraphicsCommandList4>& cmdList)
{
    std::vector<GBufferChannel*> ChannelsToTransit =
    { &Diffuse, &DepthStencils, &Normal, &MatFresnelRoughness, &VelocityBuffer, &ObjectOutlines };

    std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;

    for (GBufferChannel* i : ChannelsToTransit)
    {
        Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            i->Resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }
    cmdList->ResourceBarrier(ChannelsToTransit.size(), Barriers.data());
}

void Gbuffer::TransitToTonemappingState(ComPtr<ID3D12GraphicsCommandList4>& cmdList)
{
    std::vector<GBufferChannel*> ChannelsToTransit =
    { &Accumulation };

    std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;

    for (GBufferChannel* i : ChannelsToTransit)
    {
        Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            i->Resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }
    cmdList->ResourceBarrier(ChannelsToTransit.size(), Barriers.data());
}

void Gbuffer::TransitSRVToCommon(ComPtr<ID3D12GraphicsCommandList4>& cmdList)
{
    std::vector<CD3DX12_RESOURCE_BARRIER> Barriers;
    for (GBufferChannel* i : ChannelPTRs)
    {
        Barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            i->Resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COMMON));
    }
    cmdList->ResourceBarrier(NumBuffers, Barriers.data());
}

void Gbuffer::Clear(ComPtr<ID3D12GraphicsCommandList4>& cmdList)
{
    PIXBeginEvent(cmdList.Get(), 0x00FF00, "Clear GBuffer");
    for (GBufferChannel* i : ChannelPTRs) cmdList->ClearRenderTargetView(i->RTV, i->ClearValue.Color, 0, nullptr);
    PIXEndEvent(cmdList.Get());
}

void Gbuffer::Resize(int width, int height)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    for (GBufferChannel* i : ChannelPTRs)
    {
        i->Resource.Reset();

        mDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(i->Format, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
            D3D12_RESOURCE_STATE_COMMON,
            &i->ClearValue,
            IID_PPV_ARGS(&i->Resource));

        rtvDesc.Format = i->Format;
        mDevice->CreateRenderTargetView(i->Resource.Get(), &rtvDesc, rtvHandle);
        i->RTV = rtvHandle;
        rtvHandle.ptr += RTVDescSize;

        srvDesc.Format = i->Format;
        mDevice->CreateShaderResourceView(i->Resource.Get(), &srvDesc, srvHandle);
        i->SRV = srvHandle;
        srvHandle.ptr += SRVDescSize;
    }
}

void Gbuffer::Dispose()
{
    for (GBufferChannel* i : ChannelPTRs) i->Resource.Reset();

    m_RTVDescriptorHeap.Reset();
    m_SRVDescriptorHeap.Reset();
}
