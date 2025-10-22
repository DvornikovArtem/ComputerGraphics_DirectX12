#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include <DirectXMath.h>
#include "../RHI/DX12/UploadBuffer.h"

struct VoxelChunkCB
{
    DirectX::XMFLOAT3 worldOrigin;
    float voxelSize;
    UINT dimX;
    UINT dimY;
    UINT dimZ;
    float isoLevel;
};

struct VoxelSettings
{
    UINT dimX = 33;
    UINT dimY = 33;
    UINT dimZ = 33;
    float voxelSize = 1.0f;
    float isoLevel  = 0.0f;
};

struct DensityFieldCPU
{
    std::vector<uint16_t> data;
    UINT dimX, dimY, dimZ;
};

class VoxelWorld
{
public:
    VoxelWorld() = default;
    ~VoxelWorld() = default;

    void Initialize(ID3D12Device* device,
                    ID3D12RootSignature* mcRootSig,
                    ID3D12PipelineState*  mcPSO,
                    ID3D12RootSignature* gbufferRootSig,
                    ID3D12PipelineState*  gbufferVoxelPSO);

    void CreateOneChunk(const DirectX::XMFLOAT3& origin, const VoxelSettings& settings, ID3D12GraphicsCommandList* cmd);
    void UpdateAndDispatch(ID3D12GraphicsCommandList* cmd, UploadBuffer<UINT>* zeroUploadBuffer);
    void Draw(ID3D12GraphicsCommandList* cmd);
    void ReleaseUploadsAfterGPU();
    void ScheduleUploadsRelease(UINT64 fenceValue);
    void CollectGarbage(UINT64 completedFence);

    void InitTablesCB(ID3D12Device* device, const int* edge256, const int* tri256x16_flat);

    void InitTablesCB(ID3D12Device* device, const int* edge256, const int(*tri256x16)[16]);

    void MarkAllChunksDirty();
    void MarkChunkDirty(size_t index);
    void DigSphere(const DirectX::XMFLOAT3& center, float radius);
    void DigRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir, float maxDist, float radius);

private:
    struct GPUResources {
        Microsoft::WRL::ComPtr<ID3D12Resource> densityTex;
        Microsoft::WRL::ComPtr<ID3D12Resource> densityUpload;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertices;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertCounter;
        Microsoft::WRL::ComPtr<ID3D12Resource> drawArgs;
        Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsUpload;
        UINT drawArgsOffset = 0;
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        UINT maxVertices = 0;
        //UINT vertexStride = sizeof(float)*(3+3+2+3);
        UINT vertexStride = 64;
    };

    struct Chunk {
        VoxelChunkCB cb;
        GPUResources gpu;
        Microsoft::WRL::ComPtr<ID3D12Resource> cbUpload;
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = 0;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        D3D12_CPU_DESCRIPTOR_HANDLE srvDensity{};
        D3D12_CPU_DESCRIPTOR_HANDLE uavVertices{};
        D3D12_CPU_DESCRIPTOR_HANDLE uavCounter{};

        DensityFieldCPU cpuDensity;
        bool densityReady = false;
        bool descriptorsReady = false;

        VoxelSettings contentSettings;
        DirectX::XMFLOAT3 contentOrigin;
    };

private:
    ID3D12Device* mDevice = nullptr;
    ID3D12RootSignature* mMCRootSig = nullptr;
    ID3D12PipelineState* mMCPSO = nullptr;
    ID3D12RootSignature* mGbufRootSig = nullptr;
    ID3D12PipelineState* mGbufVoxelPSO = nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> mDrawCmdSig;

    std::vector<Chunk> mChunks;

    struct PendingUploads {
        UINT64 fence = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> densityUpload;
        Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsUpload;
    };
    std::vector<PendingUploads> mPending;

private:
    DensityFieldCPU GenerateDensityCPU(const VoxelChunkCB& info);
    void UploadDensity3D(ID3D12GraphicsCommandList* cmd, Chunk& c, const DensityFieldCPU& df);
    void CreateDescriptors(Chunk& c);
    void CreateVertexUAV(Chunk& c, UINT maxVertices, ID3D12GraphicsCommandList* cmd);
};
