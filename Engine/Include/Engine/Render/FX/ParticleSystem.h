#pragma once

#include <numeric>
#include <string.h>

#include "DirectXMath.h"
#include "../../Math/GeometryGenerator.h"
#include "../../RHI/DX12/FrameResource.h"

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

struct FrameResource;

enum PARTICLE_SHAPE {
    QUAD    = 0,
    CIRCLE  = 1
};

enum PARTICLE_SYSTEM_EFFECT {
    FIREWORK    = 0,
    FIRE        = 1,
    SMOKE       = 2
};

struct ParticleSystemDescriptor
{
    std::string name;
    XMFLOAT3 emitterPosition = { 0.0f, 0.0f, 0.0f };
    UINT numParticlesToEmit;
    UINT maxParticles = 1000;
    float particleSize = 0.2f;
    PARTICLE_SHAPE particleShape = PARTICLE_SHAPE::QUAD;
    std::string emitComputeShaderName;
    std::string simulateComputeShaderName;
    ComPtr<ID3DBlob> emitComputeShader;
    ComPtr<ID3DBlob> simulateComputeShader;
    UINT CBIndex;
    //PARTICLE_SYSTEM_EFFECT effect = PARTICLE_SYSTEM_EFFECT::"; // for the future
};


class ParticleSystem
{
public:
    //ParticleSystem(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT maxParticles);
    ~ParticleSystem() = default;

    void Initialize(const ParticleSystemDescriptor& particleSystemDesc);
    void Build(ComPtr<ID3D12Device> device, ComPtr<ID3D12GraphicsCommandList> cmdList);

    void Update(float dt, FrameResource* currentFrameResource);
    void Draw(D3D12_GPU_VIRTUAL_ADDRESS passCBAddress);

    MeshGeometry* GetQuadGeometry() const { return mQuadGeo.get(); }
    ID3D12Resource* GetAliveList() const { return mAliveList.Get(); }
    ID3D12Resource* GetParticlePool() const { return mParticlePool.Get(); }
    XMFLOAT3 getEmitterPosition() const { return mEmitterPosition; }
    UINT getMaxParticles() const { return mMaxParticles; }
    UINT getNumParticlesToEmit() const { return mNumParticlesToEmit; }
    
    void setEmissiveTex(ComPtr<ID3D12Resource> emissiveTex, ComPtr<ID3D12Resource> normalTex);

private:
    void BuildResources();
    void BuildRootSignatures();
    void BuildShadersAndPSOs();
    void BuildCommandSignature();

private:
    XMFLOAT3 mEmitterPosition;
    UINT mMaxParticles;
    UINT mNumParticlesToEmit;
    float mParticleSize;
    PARTICLE_SHAPE mParticleShape;
    ComPtr<ID3DBlob> mEmitComputeShader;
    ComPtr<ID3DBlob> mSimulateComputeShader;
    UINT mCBIndex;

    ComPtr<ID3D12RootSignature> mRootSignatureRender;
    ComPtr<ID3D12RootSignature> mRootSignatureCompute;
    ComPtr<ID3D12CommandSignature> mCommandSignature;

    ComPtr<ID3D12PipelineState> mPSORender;
    ComPtr<ID3D12PipelineState> mPSOEmit;
    ComPtr<ID3D12PipelineState> mPSOSimulate;
    ComPtr<ID3D12PipelineState> mPSOSort;

    ComPtr<ID3D12Resource> mParticlePool;
    ComPtr<ID3D12Resource> mAliveList;
    ComPtr<ID3D12Resource> mCounters;
    ComPtr<ID3D12Resource> mDrawArgs;

    ComPtr<ID3D12Resource> mDeadList[2];
    UINT mCurrentDeadList = 0;

    ComPtr<ID3D12DescriptorHeap> mUavSrvHeap;

    std::unique_ptr<MeshGeometry> mQuadGeo;

    ComPtr<ID3D12Resource> mDrawArgsUpload;
    ComPtr<ID3D12Resource> mCounterUpload;
    ComPtr<ID3D12Resource> mDeadListUpload;

    ID3D12Device* mDevice;
    ID3D12GraphicsCommandList* mCommandList;

    float mTime = 0.0f;
    static UINT sGlobalFrame;

    ComPtr<ID3D12Resource> mEmissiveTex = nullptr;
    ComPtr<ID3D12Resource> mNormalTex = nullptr;

public:
    XMFLOAT3 CameraPos;
    XMFLOAT3 CameraDir;
};