#include <Engine/RHI/DX12/FrameResource.h>

FrameResource::FrameResource(ID3D12Device* device, ID3D12Device* device2, UINT passCount, UINT objectCount, UINT materialCount, UINT LightCount, UINT particleSystemsCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    ThrowIfFailed(device2->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc2.GetAddressOf())));

  //  FrameCB = std::make_unique<UploadBuffer<FrameConstants>>(device, 1, true);
    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    LightCB = std::make_unique<UploadBuffer<Light>>(device, LightCount, true);

    PassCB2 = std::make_unique<UploadBuffer<PassConstants>>(device2, passCount, true);
    MaterialCB2 = std::make_unique<UploadBuffer<MaterialConstants>>(device2, materialCount, true);
    ObjectCB2 = std::make_unique<UploadBuffer<ObjectConstants>>(device2, objectCount, true);
    LightCB2 = std::make_unique<UploadBuffer<Light>>(device2, LightCount, true);

    if (particleSystemsCount > 0)
    {
        ParticleCB = std::make_unique<UploadBuffer<ParticleConstants>>(device, particleSystemsCount, true);
        NullUploadBuffer = std::make_unique<UploadBuffer<UINT>>(device, 1, false);
        UINT zero = 0;
        NullUploadBuffer->CopyData(0, zero);

        ParticleCB2 = std::make_unique<UploadBuffer<ParticleConstants>>(device2, particleSystemsCount, true);
        NullUploadBuffer2 = std::make_unique<UploadBuffer<UINT>>(device2, 1, false);
        NullUploadBuffer2->CopyData(0, zero);
    }
}

FrameResource::~FrameResource()
{

}