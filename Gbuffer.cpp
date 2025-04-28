// Gbuffer.cpp

#include "Gbuffer.h"
#include "d3dUtil.h"    // Предполагается, что здесь есть функция d3dUtil::GetDevice()
#include "d3dx12.h"     // Для использования обёрток CD3DX12_*
#include <stdexcept>

// Конструктор создает ресурсы (текстуры) и дескрипторные кучи для RTV и SRV.
Gbuffer::Gbuffer(int width, int height, Microsoft::WRL::ComPtr<ID3D12Device> device)
{
    //ID3D12Device* device = md3dDevice.Get(); // Получаем указатель на устройство

    // Создаем дескрипторную кучу для RTV (5 дескрипторов: Diffuse, Emissive, Normal, Accumulation, Bloom)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 7;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RTVDescriptorHeap))))
        throw std::runtime_error("Failed to create RTV Descriptor Heap");

    // Создаем дескрипторную кучу для SRV (5 дескрипторов) – с флагом видимости для шейдеров.
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 7;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SRVDescriptorHeap))))
        throw std::runtime_error("Failed to create SRV Descriptor Heap");

    // Получаем размеры дескрипторов для соответствующих куч
    UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Начальные CPU-хендлы для RTV и SRV
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    HRESULT hr = S_OK;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    // Создаем ресурс для DiffuseTex (формат 8-битный UNORM)
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        IID_PPV_ARGS(&DiffuseTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to create DiffuseTex");

    D3D12_CLEAR_VALUE clearValue8 = {};
    clearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    // Создаем ресурс для EmissiveTex (формат 8-битный UNORM)
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&EmissiveTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to create EmissiveTex");

    // Создаем ресурс для NormalTex (формат 8-битный UNORM)
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_SNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&NormalTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to create NormalTex");

    // Создаем ресурс для MaterialAlbedoTex (формат 8-битный UNORM)
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&MaterialAlbedoTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to create MaterialAlbedoTex");

    // Создаем ресурс для MaterialFresnelRoughnessTex (формат 8-битный UNORM)
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&MaterialFresnelRoughnessTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to create MaterialFresnelRoughnessTex");

    D3D12_CLEAR_VALUE clearValue2 = {};
    clearValue2.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearValue2.Color[0] = 0.0f;
    clearValue2.Color[1] = 0.0f;
    clearValue2.Color[2] = 0.0f;
    clearValue2.Color[3] = 1.0f;

    // Создаем ресурс для AccumulationBuf (формат 16-битный FLOAT)
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue2,
        IID_PPV_ARGS(&AccumulationBuf)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to create AccumulationBuf");

    // Создаем ресурс для BloomTex (формат 8-битный UNORM)
    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&BloomTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to create BloomTex");

    // Создаем RTV для DiffuseTex
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateRenderTargetView(DiffuseTex.Get(), &rtvDesc, rtvHandle);
    DiffuseRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // Создаем RTV для EmissiveTex
    rtvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    device->CreateRenderTargetView(EmissiveTex.Get(), &rtvDesc, rtvHandle);
    EmissiveRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // Создаем RTV для NormalTex
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
    device->CreateRenderTargetView(NormalTex.Get(), &rtvDesc, rtvHandle);
    NormalRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // Создаем RTV для MaterialAlbedoTex
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateRenderTargetView(MaterialAlbedoTex.Get(), &rtvDesc, rtvHandle);
    MaterialAlbedoRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // Создаем RTV для MaterialFresnelRoughnessTex
    device->CreateRenderTargetView(MaterialFresnelRoughnessTex.Get(), &rtvDesc, rtvHandle);
    MaterialFresnelRoughnessRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для AccumulationBuf (формат FLOAT)
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    device->CreateRenderTargetView(AccumulationBuf.Get(), &rtvDesc, rtvHandle);
    AccumulationRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для BloomTex (возвращаем формат UNORM)
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateRenderTargetView(BloomTex.Get(), &rtvDesc, rtvHandle);
    BloomRTV = rtvHandle;
    // rtvHandle далее не используется

    // Создаем SRV для DiffuseTex
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(DiffuseTex.Get(), &srvDesc, srvHandle);
    DiffuseSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для EmissiveTex
    srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    device->CreateShaderResourceView(EmissiveTex.Get(), &srvDesc, srvHandle);
    EmissiveSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для NormalTex
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
    device->CreateShaderResourceView(NormalTex.Get(), &srvDesc, srvHandle);
    NormalSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для MaterialAlbedoTex
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(MaterialAlbedoTex.Get(), &srvDesc, srvHandle);
    MaterialAlbedoSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для MaterialFresnelRoughnessTex
    device->CreateShaderResourceView(MaterialFresnelRoughnessTex.Get(), &srvDesc, srvHandle);
    MaterialFresnelRoughnessSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для AccumulationBuf (FLOAT)
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    device->CreateShaderResourceView(AccumulationBuf.Get(), &srvDesc, srvHandle);
    AccumulationSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для BloomTex
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(BloomTex.Get(), &srvDesc, srvHandle);
    BloomSRV = srvHandle;
}

// Копирование дескрипторов RTV в заданное расположение (например, в объединенную дескрипторную кучу).
// В этом примере копируются только RTV-дескрипторы.
void Gbuffer::CopyDescriptors(D3D12_CPU_DESCRIPTOR_HANDLE otherStart)
{
    ID3D12Device* device = md3dDevice.Get();
    UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    // Копируем все 5 RTV-дескрипторов из нашей кучки в предоставленное место.
    device->CopyDescriptorsSimple(7, otherStart,
        m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

// Переход к состоянию для отрисовки непрозрачных объектов.
// Здесь переводим текстуры G-buffer (Diffuse, Emissive, Normal) в состояние RENDER_TARGET.
void Gbuffer::TransitToOpaqueRenderingState(ComPtr<ID3D12GraphicsCommandList>& cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[5];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        DiffuseTex.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        //D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // предполагаем, что до этого текстура использовалась как SRV
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        EmissiveTex.Get(),
        //D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        NormalTex.Get(),
        //D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialAlbedoTex.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialFresnelRoughnessTex.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(5, barriers);
}

// Переход к состоянию для отрисовки освещения.
// Здесь переводим Diffuse, Emissive и Normal из состояния RENDER_TARGET в SRV,
// а также подготавливаем буферы Accumulation и Bloom для записи (RENDER_TARGET).
void Gbuffer::TransitToLightsRenderingState(ComPtr<ID3D12GraphicsCommandList>& cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[7];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        DiffuseTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        EmissiveTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        NormalTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialAlbedoTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialFresnelRoughnessTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[5] = CD3DX12_RESOURCE_BARRIER::Transition(
        AccumulationBuf.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    barriers[6] = CD3DX12_RESOURCE_BARRIER::Transition(
        BloomTex.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(7, barriers);
}

// Переход к состоянию для тонемаппинга.
// Переводим буферы Accumulation и Bloom из состояния RENDER_TARGET в SRV для последующей выборки.
void Gbuffer::TransitToTonemappingState(ComPtr<ID3D12GraphicsCommandList>& cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[2];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        AccumulationBuf.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        BloomTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(2, barriers);
}

void Gbuffer::TransitToCommon(ComPtr<ID3D12GraphicsCommandList>& cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[7];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        DiffuseTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        EmissiveTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        NormalTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialAlbedoTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialFresnelRoughnessTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[5] = CD3DX12_RESOURCE_BARRIER::Transition(
        AccumulationBuf.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[6] = CD3DX12_RESOURCE_BARRIER::Transition(
        BloomTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(7, barriers);
}

void Gbuffer::TransitFromRenderTargetToCommon(ComPtr<ID3D12GraphicsCommandList>& cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[5];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        DiffuseTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        EmissiveTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        NormalTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialAlbedoTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialFresnelRoughnessTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(5, barriers);
}

void Gbuffer::TransitFromShaderResourceToCommon(ComPtr<ID3D12GraphicsCommandList>& cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[7];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
        DiffuseTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
        EmissiveTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
        NormalTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialAlbedoTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
        MaterialFresnelRoughnessTex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[5] = CD3DX12_RESOURCE_BARRIER::Transition(
        AccumulationBuf.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    barriers[6] = CD3DX12_RESOURCE_BARRIER::Transition(
        BloomTex.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(7, barriers);
}

// Функция изменения размеров: освобождает текущие ресурсы и воссоздает их с новыми размерами.
void Gbuffer::Resize(int width, int height, Microsoft::WRL::ComPtr<ID3D12Device> device)
{
    // Освобождаем старые ресурсы
    DiffuseTex.Reset();
    EmissiveTex.Reset();
    NormalTex.Reset();
    MaterialAlbedoTex.Reset();
    MaterialFresnelRoughnessTex.Reset();
    AccumulationBuf.Reset();
    BloomTex.Reset();

    HRESULT hr = S_OK;

    // Воссоздаем ресурсы с новыми размерами аналогично конструктору

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&DiffuseTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to recreate DiffuseTex during Resize");

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&EmissiveTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to recreate EmissiveTex during Resize");

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_SNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&NormalTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to recreate NormalTex during Resize");

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&MaterialAlbedoTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to recreate MaterialAlbedoTex during Resize");

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&MaterialFresnelRoughnessTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to recreate MaterialFresnelRoughnessTex during Resize");

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&AccumulationBuf)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to recreate AccumulationBuf during Resize");

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&BloomTex)
    );
    if (FAILED(hr))
        throw std::runtime_error("Failed to recreate BloomTex during Resize");

    // Обновляем дескрипторы RTV и SRV. Предполагается, что дескрипторные кучи уже созданы.
    UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    // RTV для DiffuseTex
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateRenderTargetView(DiffuseTex.Get(), &rtvDesc, rtvHandle);
    DiffuseRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для EmissiveTex
    rtvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    device->CreateRenderTargetView(EmissiveTex.Get(), &rtvDesc, rtvHandle);
    EmissiveRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для NormalTex
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
    device->CreateRenderTargetView(NormalTex.Get(), &rtvDesc, rtvHandle);
    NormalRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для MaterialAlbedoTex
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateRenderTargetView(MaterialAlbedoTex.Get(), &rtvDesc, rtvHandle);
    MaterialAlbedoRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для MaterialFresnelRoughnessTex
    device->CreateRenderTargetView(MaterialFresnelRoughnessTex.Get(), &rtvDesc, rtvHandle);
    MaterialFresnelRoughnessRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для AccumulationBuf
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    device->CreateRenderTargetView(AccumulationBuf.Get(), &rtvDesc, rtvHandle);
    AccumulationRTV = rtvHandle;
    rtvHandle.ptr += rtvDescriptorSize;

    // RTV для BloomTex
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateRenderTargetView(BloomTex.Get(), &rtvDesc, rtvHandle);
    BloomRTV = rtvHandle;

    // SRV для DiffuseTex
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(DiffuseTex.Get(), &srvDesc, srvHandle);
    DiffuseSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для EmissiveTex
    srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    device->CreateShaderResourceView(EmissiveTex.Get(), &srvDesc, srvHandle);
    EmissiveSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для NormalTex
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
    device->CreateShaderResourceView(NormalTex.Get(), &srvDesc, srvHandle);
    NormalSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для MaterialAlbedoTex
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(MaterialAlbedoTex.Get(), &srvDesc, srvHandle);
    MaterialAlbedoSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для MaterialFresnelRoughnessTex
    device->CreateShaderResourceView(MaterialFresnelRoughnessTex.Get(), &srvDesc, srvHandle);
    MaterialFresnelRoughnessSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для AccumulationBuf
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    device->CreateShaderResourceView(AccumulationBuf.Get(), &srvDesc, srvHandle);
    AccumulationSRV = srvHandle;
    srvHandle.ptr += srvDescriptorSize;

    // SRV для BloomTex
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(BloomTex.Get(), &srvDesc, srvHandle);
    BloomSRV = srvHandle;
}

// Освобождаем все ресурсы
void Gbuffer::Dispose()
{
    DiffuseTex.Reset();
    EmissiveTex.Reset();
    NormalTex.Reset();
    MaterialAlbedoTex.Reset();
    MaterialFresnelRoughnessTex.Reset();
    AccumulationBuf.Reset();
    BloomTex.Reset();

    m_RTVDescriptorHeap.Reset();
    m_SRVDescriptorHeap.Reset();
}
