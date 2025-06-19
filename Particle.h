#pragma once

#include "d3dUtil.h"
#include "DirectXMath.h"
#include <numeric> // Äëÿ std::iota

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct Particle
{
    XMFLOAT3 Position;
    float LifeTime;
    XMFLOAT3 Velocity;
    float Size;
    XMFLOAT4 Color;
};

//struct ParticleData
//{
//    float DeltaTime;
//    UINT NumEmit;
//    XMFLOAT2 Pad1;
//    XMFLOAT3 EmitterPos;
//    float Pad2;
//};

// Forward-äåêëàðàöèÿ
struct FrameResource;

class ParticleSystem
{
public:
    ParticleSystem(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT maxParticles);
    ~ParticleSystem() = default;

    void Update(ID3D12GraphicsCommandList* cmdList, float dt, FrameResource* currentFrameResource, const XMFLOAT3& emitterPos, UINT numToEmit);
    void Draw(ID3D12GraphicsCommandList* cmdList, D3D12_GPU_VIRTUAL_ADDRESS passCBAddress);

    MeshGeometry* GetQuadGeometry() const { return mQuadGeo.get(); }

    ID3D12Resource* GetAliveList() const { return mAliveList.Get(); }

private:
    void BuildResources(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void BuildRootSignatures(ID3D12Device* device);
    void BuildShadersAndPSOs(ID3D12Device* device);
    void BuildCommandSignature(ID3D12Device* device);

private:
    UINT mMaxParticles = 0;

    ComPtr<ID3D12RootSignature> mRootSignatureRender;
    ComPtr<ID3D12RootSignature> mRootSignatureCompute;
    ComPtr<ID3D12CommandSignature> mCommandSignature;

    ComPtr<ID3D12PipelineState> mPSORender;
    ComPtr<ID3D12PipelineState> mPSOEmit;
    ComPtr<ID3D12PipelineState> mPSOSimulate;

    ComPtr<ID3D12Resource> mParticlePool;
    ComPtr<ID3D12Resource> mAliveList;
    ComPtr<ID3D12Resource> mCounters;
    ComPtr<ID3D12Resource> mDrawArgs;

    ComPtr<ID3D12Resource> mDeadList[2];
    UINT mCurrentDeadList = 0;

    ComPtr<ID3D12DescriptorHeap> mUavSrvHeap;

    std::unique_ptr<MeshGeometry> mQuadGeo;
};