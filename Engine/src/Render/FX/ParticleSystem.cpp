#include <Engine/Render/FX/ParticleSystem.h>

static constexpr UINT64 kCounterAlignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT; // 4096
static constexpr UINT kNumCounters = 3; // Dead0, Dead1, Alive
static constexpr UINT64 kAliveCounterOffset = kCounterAlignment * 2; // 8192

UINT ParticleSystem::sGlobalFrame = 0;

void ParticleSystem::Initialize(const ParticleSystemDescriptor& particleSystemDesc)
{
    mEmitterPosition = particleSystemDesc.emitterPosition;
    mMaxParticles = particleSystemDesc.maxParticles;
    mNumParticlesToEmit = particleSystemDesc.numParticlesToEmit;
    mParticleSize = particleSystemDesc.particleSize;
    mEmitComputeShader = particleSystemDesc.emitComputeShader;
    mSimulateComputeShader = particleSystemDesc.simulateComputeShader;
    mCBIndex = particleSystemDesc.CBIndex;
    mInputLayout = particleSystemDesc.InputLayout;
    IsBillboard = particleSystemDesc.IsBillboard;
}

void ParticleSystem::Build(ComPtr<ID3D12Device> device, ComPtr<ID3D12GraphicsCommandList> cmdList)
{
    mDevice = device.Get();
    mCommandList = cmdList.Get();

    BuildResources();
    BuildRootSignatures();
    BuildCommandSignature();
    BuildShadersAndPSOs();
}

void ParticleSystem::SetResources(ComPtr<ID3D12Resource> DepthTex, ComPtr<ID3D12Resource> NormalTex)
{
    if (mDepthStencilsTex.Get() == DepthTex.Get()) return;
    if (mNormalTex.Get() == NormalTex.Get()) return;

    mDepthStencilsTex = DepthTex;
    mNormalTex = NormalTex;

    UINT descriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    srvDesc.Format = mDepthStencilsTex->GetDesc().Format;
    CD3DX12_CPU_DESCRIPTOR_HANDLE DepthHeapHandle(
        mUavSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        5,
        descriptorSize);

    mDevice->CreateShaderResourceView(mDepthStencilsTex.Get(), &srvDesc, DepthHeapHandle);

    srvDesc.Format = mNormalTex->GetDesc().Format;
    CD3DX12_CPU_DESCRIPTOR_HANDLE normalHandle(
        DepthHeapHandle,
        1,
        descriptorSize);

    mDevice->CreateShaderResourceView(mNormalTex.Get(), &srvDesc, normalHandle);
}

void ParticleSystem::BuildResources()
{
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT); // GPU-accessible memory (VRAM) -> to storage resources for GPU-rendering (VB, textures, UAV e.c.)
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); // To storage data in default heap, u need to storage it first in upload heap (CPU-accessible) and then copy to default heap

    // Create base buffers
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(mMaxParticles * sizeof(Particle), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    
    bufferDesc.Width = mMaxParticles * sizeof(Particle);
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mParticlePool)));

    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mDeadList[0])));
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mDeadList[1])));
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mAliveList)));

    bufferDesc.Width = kCounterAlignment * kNumCounters; // 4096 * 3 = 12 ??
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mCounters)));

    bufferDesc.Width = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mDrawArgs)));


    // Data initialization
    // DrawArgs
    D3D12_DRAW_INDEXED_ARGUMENTS initArgs = {};
    initArgs.IndexCountPerInstance = mGeometry->DrawArgs["LOD0"].IndexCount;

    CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(initArgs));
    
    uploadDesc.Width = sizeof(initArgs);
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mDrawArgsUpload)));

    void* mapped = nullptr;
    mDrawArgsUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, &initArgs, sizeof(initArgs));
    mDrawArgsUpload->Unmap(0, nullptr);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    mCommandList->CopyBufferRegion(mDrawArgs.Get(), 0, mDrawArgsUpload.Get(), 0, sizeof(initArgs));
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));


    // Counters
    UINT initCounters[4] = { mMaxParticles, 0, 0, 0 };
    uploadDesc.Width = sizeof(initCounters);

    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mCounterUpload)));

    mCounterUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, initCounters, sizeof(initCounters));
    mCounterUpload->Unmap(0, nullptr);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    mCommandList->CopyBufferRegion(mCounters.Get(), 0, mCounterUpload.Get(), 0, sizeof(initCounters));
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));


    // List of dead particles
    std::vector<UINT> deadIndices(mMaxParticles);
    std::iota(deadIndices.begin(), deadIndices.end(), 0);

    uploadDesc.Width = mMaxParticles * sizeof(UINT);

    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mDeadListUpload)));

    mDeadListUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, deadIndices.data(), mMaxParticles * sizeof(UINT));
    mDeadListUpload->Unmap(0, nullptr);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[0].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    mCommandList->CopyBufferRegion(mDeadList[0].Get(), 0, mDeadListUpload.Get(), 0, uploadDesc.Width);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON));

    CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[0].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[1].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    mCommandList->ResourceBarrier(_countof(barriers), barriers);


    // Create UAV-descriptors
    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
    uavHeapDesc.NumDescriptors = 7;
    uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(mDevice->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&mUavSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(mUavSrvHeap->GetCPUDescriptorHandleForHeapStart());
    UINT uavDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = mMaxParticles;

    uavDesc.Buffer.StructureByteStride = sizeof(Particle);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    mDevice->CreateUnorderedAccessView(mParticlePool.Get(), nullptr, &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);
    
    uavDesc.Buffer.StructureByteStride = sizeof(UINT);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    mDevice->CreateUnorderedAccessView(mDeadList[0].Get(), mCounters.Get(), &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.CounterOffsetInBytes = kCounterAlignment; // 4096
    mDevice->CreateUnorderedAccessView(mDeadList[1].Get(), mCounters.Get(), &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.CounterOffsetInBytes = kCounterAlignment * 2; // 8192
    mDevice->CreateUnorderedAccessView(mAliveList.Get(), mCounters.Get(), &uavDesc, uavHandle); uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.NumElements = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(UINT);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    mDevice->CreateUnorderedAccessView(mDrawArgs.Get(), nullptr, &uavDesc, uavHandle); //uavHandle.Offset(1, uavDescriptorSize);
}

void ParticleSystem::BuildRootSignatures()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[3] = {};
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsShaderResourceView(0);
    slotRootParameter[2].InitAsShaderResourceView(1);

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(_countof(slotRootParameter), slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignatureRender)));


    // For compute shaders
    CD3DX12_DESCRIPTOR_RANGE uavTable = {};
    uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0);

    CD3DX12_DESCRIPTOR_RANGE srvTable = {};
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);

    CD3DX12_ROOT_PARAMETER computeSlotRootParameter[4] = {};
    computeSlotRootParameter[0].InitAsConstantBufferView(0);
    computeSlotRootParameter[1].InitAsConstantBufferView(1); // PassConstants
    computeSlotRootParameter[2].InitAsDescriptorTable(1, &uavTable);
    computeSlotRootParameter[3].InitAsDescriptorTable(1, &srvTable);

    CD3DX12_ROOT_SIGNATURE_DESC computeRsDesc(_countof(computeSlotRootParameter), computeSlotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ThrowIfFailed(D3D12SerializeRootSignature(&computeRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignatureCompute)));


    // For sort shader
    //auto sortCS = d3dUtil::CompileShader(L"../Shaders/ParticleSortCS.hlsl", nullptr, "CS", "cs_5_1");
    //D3D12_COMPUTE_PIPELINE_STATE_DESC sortDesc{};
    //sortDesc.pRootSignature = mRootSignatureCompute.Get();
    //sortDesc.CS = CD3DX12_SHADER_BYTECODE(sortCS.Get());
    //ThrowIfFailed(mDevice->CreateComputePipelineState(&sortDesc, IID_PPV_ARGS(&mPSOSort)));

}

void ParticleSystem::BuildCommandSignature()
{
    D3D12_INDIRECT_ARGUMENT_DESC argDescs[1] = {};
    argDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC cmdSignatureDesc = {};
    cmdSignatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    cmdSignatureDesc.NumArgumentDescs = 1;
    cmdSignatureDesc.pArgumentDescs = argDescs;
    cmdSignatureDesc.NodeMask = 0;

    ThrowIfFailed(mDevice->CreateCommandSignature(&cmdSignatureDesc, nullptr, IID_PPV_ARGS(&mCommandSignature)));
}

void ParticleSystem::SetGeometry(MeshGeometry* NewGeometry) { mGeometry = NewGeometry; }

void ParticleSystem::BuildShadersAndPSOs()
{
    const D3D_SHADER_MACRO defines[] =
    {
        { "BILLBOARDGEOMETRY", "1" },
        { NULL, NULL }
    };

    auto vsByteCode = d3dUtil::CompileShader(SHADERS_ENGINE_DIR L"\\Particle.hlsl", IsBillboard ? defines : nullptr, "VS", "vs_5_1");
    auto psByteCode = d3dUtil::CompileShader(SHADERS_ENGINE_DIR L"\\Particle.hlsl", nullptr, "PS", "ps_5_1");
    auto sortCS = d3dUtil::CompileShader(SHADERS_ENGINE_DIR L"\\ParticleSortCS.hlsl", nullptr, "CS", "cs_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
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
    //psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA; // D3D12_BLEND_ONE
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

    // mPSORender
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSORender)));

    // mPSOEmit
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = mRootSignatureCompute.Get();
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(mEmitComputeShader.Get());
    ThrowIfFailed(mDevice->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mPSOEmit)));

    // mPSOSimulate
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(mSimulateComputeShader.Get());
    ThrowIfFailed(mDevice->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mPSOSimulate)));

    // mPSOSort
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(sortCS.Get());
    ThrowIfFailed(mDevice->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mPSOSort)));
}

void ParticleSystem::Update(float dt, FrameResource* frameResource)
{
    mTime += dt;

    // Update constants
    ParticleConstants pConsts = {};
    pConsts.EmitterPos = mEmitterPosition;
    pConsts.DeltaTime = dt;
    pConsts.NumEmit = mNumParticlesToEmit;
    pConsts.CurrentDeadList = mCurrentDeadList;
    pConsts.MaxParticles = mMaxParticles;
    pConsts.particleSize = mParticleSize;
    pConsts.Time = mTime;
    pConsts.FrameIndex = sGlobalFrame++;
    pConsts.CameraPos = CameraPos;
    pConsts.CameraDir = CameraDir;
    frameResource->ParticleCB->CopyData(mCBIndex, pConsts);

    mCommandList->SetComputeRootSignature(mRootSignatureCompute.Get());

    ID3D12DescriptorHeap* computeHeaps[] = { mUavSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(computeHeaps), computeHeaps);

    // Bariers for all UAVs before use
    CD3DX12_RESOURCE_BARRIER uavBarriers[] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(mParticlePool.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mCounters.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mDeadList[0].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mDeadList[1].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mAliveList.Get()),
    };
    mCommandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);


    // Resetting the AliveList counter
    mCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            mCounters.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST
        )
    );

    mCommandList->CopyBufferRegion(
        mCounters.Get(),
        kAliveCounterOffset,
        frameResource->NullUploadBuffer->Resource(),
        0,
        sizeof(UINT));

    UINT64 deadCounterOffset = (mCurrentDeadList == 0)
        ? D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT
        : 0;


    mCommandList->CopyBufferRegion(
        mCounters.Get(),
        deadCounterOffset,
        frameResource->NullUploadBuffer->Resource(),
        0,
        sizeof(UINT));

    mCommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            mCounters.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        )
    );


    // Launch EmitCS for creating new particles
    mCommandList->SetPipelineState(mPSOEmit.Get());

    UINT alignedSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ParticleConstants));
    mCommandList->SetComputeRootConstantBufferView(0, frameResource->ParticleCB->Resource()->GetGPUVirtualAddress() + mCBIndex * alignedSize);

    // PassConstants
    auto passCB = frameResource->PassCB->Resource();
    mCommandList->SetComputeRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    mCommandList->SetComputeRootDescriptorTable(2, mUavSrvHeap->GetGPUDescriptorHandleForHeapStart());

    CD3DX12_GPU_DESCRIPTOR_HANDLE DepthTexDescriptorGPU(mUavSrvHeap->GetGPUDescriptorHandleForHeapStart());
    DepthTexDescriptorGPU.Offset(5, mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

    mCommandList->SetComputeRootDescriptorTable(3, DepthTexDescriptorGPU);

    UINT groups = (mNumParticlesToEmit + 255) / 256;
    if (groups == 0) return;
    mCommandList->Dispatch(groups, 1, 1);
    

    // Barier for synchronization between EmitCS and SimulateCS
    CD3DX12_RESOURCE_BARRIER postEmitBarriers[] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(mParticlePool.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mDeadList[0].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mDeadList[1].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mAliveList.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mCounters.Get())
    };
    mCommandList->ResourceBarrier(_countof(postEmitBarriers), postEmitBarriers);


    // Launch SimulateCS for updating and selection
    mCommandList->SetPipelineState(mPSOSimulate.Get());
    mCommandList->Dispatch(mMaxParticles / 256 + 1, 1, 1);


    // Copy the number of alive particles (counter from AliveList) to the buffer for DrawIndirect
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST));
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));

    mCommandList->CopyBufferRegion(mDrawArgs.Get(), offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, InstanceCount), mCounters.Get(), kAliveCounterOffset, sizeof(UINT));

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));


    // Sorting
    // Barrier fir gAliveList cause SimulateCS just now wrote inside gAliveList
    CD3DX12_RESOURCE_BARRIER sortBarrier = CD3DX12_RESOURCE_BARRIER::UAV(mAliveList.Get());
    mCommandList->ResourceBarrier(1, &sortBarrier);

    mCommandList->SetPipelineState(mPSOSort.Get());
    mCommandList->SetComputeRootSignature(mRootSignatureCompute.Get());

    UINT numGroups = (mMaxParticles + 255) / 256;
    mCommandList->Dispatch(numGroups, 1, 1);



    // Switch the deadlists for the next frame
    mCurrentDeadList = 1 - mCurrentDeadList;
}

void ParticleSystem::Draw(D3D12_GPU_VIRTUAL_ADDRESS passCBAddress)
{
    mCommandList->SetPipelineState(mPSORender.Get());
    mCommandList->SetGraphicsRootSignature(mRootSignatureRender.Get());

    mCommandList->IASetVertexBuffers(0, 1, &mGeometry->VertexBufferView());
    mCommandList->IASetIndexBuffer(&mGeometry->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->SetGraphicsRootConstantBufferView(0, passCBAddress);
    mCommandList->SetGraphicsRootShaderResourceView(1, mParticlePool->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootShaderResourceView(2, mAliveList->GetGPUVirtualAddress());

    mCommandList->ExecuteIndirect(mCommandSignature.Get(), 1, mDrawArgs.Get(), 0, nullptr, 0);
}