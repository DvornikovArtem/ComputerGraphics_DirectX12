#include "Particle.h"
#include "d3dUtil.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include <numeric> // Ðåøàåò îøèáêó C3861 'iota': identifier not found

static constexpr UINT64 kCounterAlignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT; // 4096
static constexpr UINT kNumCounters = 3; // Dead0, Dead1, Alive
static constexpr UINT64 kAliveCounterOffset = kCounterAlignment * 2; // 8192

ParticleSystem::ParticleSystem(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT maxParticles)
{
    mMaxParticles = maxParticles;

    BuildResources(device, cmdList);
    BuildRootSignatures(device);
    BuildCommandSignature(device);
    BuildShadersAndPSOs(device);
}

void ParticleSystem::BuildResources(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT); // GPU-accessible memory (VRAM) -> to storage resources for GPU-rendering (VB, textures, UAV e.c.)
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); // To storage data in default heap, u need to storage it first in upload heap (CPU-accessible) and then copy to default heap


    // Geometry for particle (square) ===========================================================================================================================================
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

    std::vector<Vertex> vertices(quad.Vertices.size());
    for (size_t i = 0; i < quad.Vertices.size(); ++i)
    {
        vertices[i].Pos = quad.Vertices[i].Position;
        vertices[i].Normal = quad.Vertices[i].Normal;
        vertices[i].TexC = quad.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices = quad.GetIndices16();
    mQuadGeo = std::make_unique<MeshGeometry>();
    mQuadGeo->Name = "particle_quad";
    mQuadGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, vertices.data(), (UINT)vertices.size() * sizeof(Vertex), mQuadGeo->VertexBufferUploader);
    mQuadGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, indices.data(), (UINT)indices.size() * sizeof(uint16_t), mQuadGeo->IndexBufferUploader);
    mQuadGeo->VertexByteStride = sizeof(Vertex);
    mQuadGeo->VertexBufferByteSize = (UINT)vertices.size() * sizeof(Vertex);
    mQuadGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    mQuadGeo->IndexBufferByteSize = (UINT)indices.size() * sizeof(uint16_t);
    mQuadGeo->DrawArgs["quad"] = { (UINT)indices.size(), 0, 0, {} };
    mQuadGeo->InputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    // ==========================================================================================================================================================================


    // Create base buffers
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(mMaxParticles * sizeof(Particle), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    
    bufferDesc.Width = mMaxParticles * sizeof(Particle);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mParticlePool)));

    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mDeadList[0])));
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mDeadList[1])));
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mAliveList)));

    bufferDesc.Width = kCounterAlignment * kNumCounters; // 4096 * 3 = 12 КБ
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mCounters)));

    bufferDesc.Width = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, nullptr, IID_PPV_ARGS(&mDrawArgs)));


    // Data initialization
    // DrawArgs
    D3D12_DRAW_INDEXED_ARGUMENTS initArgs = {};
    initArgs.IndexCountPerInstance = mQuadGeo->DrawArgs["quad"].IndexCount;

    CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(initArgs));
    
    uploadDesc.Width = sizeof(initArgs);
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mDrawArgsUpload)));

    void* mapped = nullptr;
    mDrawArgsUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, &initArgs, sizeof(initArgs));
    mDrawArgsUpload->Unmap(0, nullptr);

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST));
    cmdList->CopyBufferRegion(mDrawArgs.Get(), 0, mDrawArgsUpload.Get(), 0, sizeof(initArgs));
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));


    // Counters
    UINT initCounters[4] = { mMaxParticles, 0, 0, 0 };
    uploadDesc.Width = sizeof(initCounters);

    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mCounterUpload)));

    mCounterUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, initCounters, sizeof(initCounters));
    mCounterUpload->Unmap(0, nullptr);

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
    cmdList->CopyBufferRegion(mCounters.Get(), 0, mCounterUpload.Get(), 0, sizeof(initCounters));
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));


    // List of dead particles
    std::vector<UINT> deadIndices(mMaxParticles);
    std::iota(deadIndices.begin(), deadIndices.end(), 0);

    uploadDesc.Width = mMaxParticles * sizeof(UINT);

    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mDeadListUpload)));

    mDeadListUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, deadIndices.data(), mMaxParticles * sizeof(UINT));
    mDeadListUpload->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(mDeadList[0].Get(), 0, mDeadListUpload.Get(), 0, uploadDesc.Width);

    CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    cmdList->ResourceBarrier(_countof(barriers), barriers);


    // Create UAV-descriptors
    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
    uavHeapDesc.NumDescriptors = 5;
    uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&mUavSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(mUavSrvHeap->GetCPUDescriptorHandleForHeapStart());
    UINT uavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = mMaxParticles;

    uavDesc.Buffer.StructureByteStride = sizeof(Particle);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    device->CreateUnorderedAccessView(mParticlePool.Get(), nullptr, &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);
    
    uavDesc.Buffer.StructureByteStride = sizeof(UINT);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    device->CreateUnorderedAccessView(mDeadList[0].Get(), mCounters.Get(), &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.CounterOffsetInBytes = kCounterAlignment; // 4096
    device->CreateUnorderedAccessView(mDeadList[1].Get(), mCounters.Get(), &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.CounterOffsetInBytes = kCounterAlignment * 2; // 8192
    device->CreateUnorderedAccessView(mAliveList.Get(), mCounters.Get(), &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.NumElements = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(UINT);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    device->CreateUnorderedAccessView(mDrawArgs.Get(), nullptr, &uavDesc, uavHandle);
}

void ParticleSystem::BuildRootSignatures(ID3D12Device* device)
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[3] = {};
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsShaderResourceView(0);
    slotRootParameter[2].InitAsShaderResourceView(1);

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(_countof(slotRootParameter), slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignatureRender)));

    // For compute shaders
    CD3DX12_DESCRIPTOR_RANGE uavTable = {};
    uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0);

    CD3DX12_ROOT_PARAMETER computeSlotRootParameter[2] = {};
    computeSlotRootParameter[0].InitAsConstantBufferView(0);
    computeSlotRootParameter[1].InitAsDescriptorTable(1, &uavTable);

    CD3DX12_ROOT_SIGNATURE_DESC computeRsDesc(_countof(computeSlotRootParameter), computeSlotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ThrowIfFailed(D3D12SerializeRootSignature(&computeRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignatureCompute)));
}

void ParticleSystem::BuildCommandSignature(ID3D12Device* device)
{
    D3D12_INDIRECT_ARGUMENT_DESC argDescs[1] = {};
    argDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC cmdSignatureDesc = {};
    cmdSignatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    cmdSignatureDesc.NumArgumentDescs = 1;
    cmdSignatureDesc.pArgumentDescs = argDescs;
    cmdSignatureDesc.NodeMask = 0;

    ThrowIfFailed(device->CreateCommandSignature(&cmdSignatureDesc, nullptr, IID_PPV_ARGS(&mCommandSignature)));
}

void ParticleSystem::BuildShadersAndPSOs(ID3D12Device* device)
{
    auto vsByteCode = d3dUtil::CompileShader(L"../Shaders/Particle.hlsl", nullptr, "VS", "vs_5_1");
    auto psByteCode = d3dUtil::CompileShader(L"../Shaders/Particle.hlsl", nullptr, "PS", "ps_5_1");
    auto emitCSByteCode = d3dUtil::CompileShader(L"../Shaders/ParticleCS.hlsl", nullptr, "EmitCS", "cs_5_1");
    auto simulateCSByteCode = d3dUtil::CompileShader(L"../Shaders/ParticleCS.hlsl", nullptr, "SimulateCS", "cs_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mQuadGeo->InputLayout.data(), (UINT)mQuadGeo->InputLayout.size() };
    psoDesc.pRootSignature = mRootSignatureRender.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vsByteCode.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(psByteCode.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSORender)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = mRootSignatureCompute.Get();
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(emitCSByteCode.Get());
    ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mPSOEmit)));

    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(simulateCSByteCode.Get());
    ThrowIfFailed(device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mPSOSimulate)));
}

void ParticleSystem::Update(ID3D12GraphicsCommandList* cmdList, float dt, FrameResource* frameResource, const XMFLOAT3& emitterPos, UINT numToEmit)
{
    // Update constants
    ParticleConstants pConsts;
    pConsts.EmitterPos = emitterPos;
    pConsts.DeltaTime = dt;
    pConsts.NumEmit = numToEmit;
    frameResource->ParticleCB->CopyData(0, pConsts);

    cmdList->SetComputeRootSignature(mRootSignatureCompute.Get());

    ID3D12DescriptorHeap* computeHeaps[] = { mUavSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(computeHeaps), computeHeaps);

    // Bariers for all UAVs before use
    CD3DX12_RESOURCE_BARRIER uavBarriers[] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(mParticlePool.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mCounters.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mDeadList[0].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mDeadList[1].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mAliveList.Get()),
    };
    cmdList->ResourceBarrier(_countof(uavBarriers), uavBarriers);


    // Resetting the AliveList counter
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mCounters.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST));

    cmdList->CopyBufferRegion(
        mCounters.Get(),
        kAliveCounterOffset,
        frameResource->NullUploadBuffer->Resource(),
        0,
        sizeof(UINT));

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mCounters.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS));


    // Launch EmitCS for creating new particles
    cmdList->SetPipelineState(mPSOEmit.Get());
    cmdList->SetComputeRootConstantBufferView(0, frameResource->ParticleCB->Resource()->GetGPUVirtualAddress());
    cmdList->SetComputeRootDescriptorTable(1, mUavSrvHeap->GetGPUDescriptorHandleForHeapStart());
    cmdList->Dispatch(numToEmit / 256 + 1, 1, 1);
    

    // Barier for synchronization between EmitCS and SimulateCS
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(mParticlePool.Get()));


    // Launch SimulateCS for updating and selection
    cmdList->SetPipelineState(mPSOSimulate.Get());
    cmdList->Dispatch(mMaxParticles / 256 + 1, 1, 1);


    // Copy the number of alive particles (counter from AliveList) to the buffer for DrawIndirect
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST));
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));

    cmdList->CopyBufferRegion(mDrawArgs.Get(), 4, mCounters.Get(), 8, 4); // offset 4 - InstanceCount, offset 8 - counter Alive

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));


    // Convert resources to a read-only state in the vertex shader
    CD3DX12_RESOURCE_BARRIER toSrv[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(mParticlePool.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(mAliveList.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    cmdList->ResourceBarrier(_countof(toSrv), toSrv);


    // Switch the deadlists for the next frame
    mCurrentDeadList = 1 - mCurrentDeadList;
}

void ParticleSystem::Draw(ID3D12GraphicsCommandList* cmdList, D3D12_GPU_VIRTUAL_ADDRESS passCBAddress)
{
    cmdList->SetPipelineState(mPSORender.Get());
    cmdList->SetGraphicsRootSignature(mRootSignatureRender.Get());

    cmdList->IASetVertexBuffers(0, 1, &mQuadGeo->VertexBufferView());
    cmdList->IASetIndexBuffer(&mQuadGeo->IndexBufferView());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->SetGraphicsRootConstantBufferView(0, passCBAddress);
    cmdList->SetGraphicsRootShaderResourceView(1, mParticlePool->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootShaderResourceView(2, mAliveList->GetGPUVirtualAddress());

    cmdList->ExecuteIndirect(mCommandSignature.Get(), 1, mDrawArgs.Get(), 0, nullptr, 0);
}