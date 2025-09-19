#include <Engine/RHI/DX12/FrameResource.h>

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT LightCount, UINT particleSystemsCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

  //  FrameCB = std::make_unique<UploadBuffer<FrameConstants>>(device, 1, true);
    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    LightCB = std::make_unique<UploadBuffer<Light>>(device, LightCount, true);

    if (particleSystemsCount > 0)
    {
        ParticleCB = std::make_unique<UploadBuffer<ParticleConstants>>(device, particleSystemsCount, true);
        NullUploadBuffer = std::make_unique<UploadBuffer<UINT>>(device, 1, false);
        UINT zero = 0;
        NullUploadBuffer->CopyData(0, zero);
    }
}

FrameResource::~FrameResource()
{

}