#include "ParticleSystem.h"

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
    mParticleShape = particleSystemDesc.particleShape;
    mEmitComputeShader = particleSystemDesc.emitComputeShader;
    mSimulateComputeShader = particleSystemDesc.simulateComputeShader;
    mCBIndex = particleSystemDesc.CBIndex;
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


void ParticleSystem::setEmissiveTex(ComPtr<ID3D12Resource> emissiveTex, ComPtr<ID3D12Resource> normalTex)
{
    if (mEmissiveTex.Get() == emissiveTex.Get()) return;
    if (mNormalTex.Get() == normalTex.Get()) return;

    mEmissiveTex = emissiveTex;
    mNormalTex = normalTex;

    UINT descriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    srvDesc.Format = mEmissiveTex->GetDesc().Format;
    CD3DX12_CPU_DESCRIPTOR_HANDLE emissiveHandle(mUavSrvHeap->GetCPUDescriptorHandleForHeapStart(), 5, descriptorSize);

    mDevice->CreateShaderResourceView(mEmissiveTex.Get(), &srvDesc, emissiveHandle);

    srvDesc.Format = mNormalTex->GetDesc().Format;
    CD3DX12_CPU_DESCRIPTOR_HANDLE normalHandle(emissiveHandle, 1, descriptorSize);

    mDevice->CreateShaderResourceView(mNormalTex.Get(), &srvDesc, normalHandle);
}


void ParticleSystem::BuildResources()
{
    // GPU-accessible memory (VRAM) -> to storage resources for GPU-rendering (VB, textures, UAV e.c.).
    // Resources in GPU-accessible memory are not directly accessible by the CPU, so they will be filled via the upload heap
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    // To storage data in default heap, u need to storage it first in upload heap (CPU-accessible) and then copy to default heap
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);



    // Geometry for particle ====================================================================================================================================================
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData particleGeometry;

    switch (mParticleShape)
    {
        case PARTICLE_SHAPE::QUAD:
            particleGeometry = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
            break;

        case PARTICLE_SHAPE::CIRCLE:
            particleGeometry = geoGen.CreateCircle(0.5f, 16);
            break;

        default:
            throw std::runtime_error("Unknown PARTICLE_SHAPE");
    }


    // Copy the generated vertices from the GeometryGenerator::Vertex format into our engine's Vertex format (position/normal/UV).
    // This aligns the data with input layout.
    std::vector<Vertex> vertices(particleGeometry.Vertices.size());
    for (size_t i = 0; i < particleGeometry.Vertices.size(); ++i)
    {
        vertices[i].Pos = particleGeometry.Vertices[i].Position;
        vertices[i].Normal = particleGeometry.Vertices[i].Normal;
        vertices[i].TexC = particleGeometry.Vertices[i].TexC;
    }

    // Use 16-bit indices (more memory-efficient, sufficient for small meshes)
    std::vector<std::uint16_t> indices = particleGeometry.GetIndices16();


    // Create a MeshGeometry wrapper object to store all GPU resources of a specific mesh
    mQuadGeo = std::make_unique<MeshGeometry>();
    mQuadGeo->Name = "particleGeometry";

    // Upload vertices and indices into GPU memory (default heap) via upload buffers -> the result is ready-to-use VB/IB for rendering
    mQuadGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(mDevice, mCommandList, vertices.data(), (UINT)vertices.size() * sizeof(Vertex), mQuadGeo->VertexBufferUploader);
    mQuadGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(mDevice, mCommandList, indices.data(), (UINT)indices.size() * sizeof(uint16_t), mQuadGeo->IndexBufferUploader);

    // Define the buffer metadata: vertex stride, total VB/IB size, index format (R16_UINT)
    mQuadGeo->VertexByteStride = sizeof(Vertex);
    mQuadGeo->VertexBufferByteSize = (UINT)vertices.size() * sizeof(Vertex);
    mQuadGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    mQuadGeo->IndexBufferByteSize = (UINT)indices.size() * sizeof(uint16_t);

    // Add a draw item: how many indices to render and the offsets
    mQuadGeo->DrawArgs["particleGeometry"] = { (UINT)indices.size(), 0, 0, {} };

    // Define InputLayout
    mQuadGeo->InputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    // ==========================================================================================================================================================================



    // Create base buffers (GPU-resources)
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(mMaxParticles * sizeof(Particle), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    
    bufferDesc.Width = mMaxParticles * sizeof(Particle);
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mParticlePool)));

    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mDeadList[0])));
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mDeadList[1])));
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mAliveList)));

    bufferDesc.Width = kCounterAlignment * kNumCounters; // 4096 * 3 = 12288
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mCounters)));

    bufferDesc.Width = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, nullptr, IID_PPV_ARGS(&mDrawArgs)));



    // Data initialization
    // DrawArgs
    D3D12_DRAW_INDEXED_ARGUMENTS initArgs = {};

    // Fill in the index count per instance from a prebuilt submesh.
    // The remaining fields are left zero for now (e.g., InstanceCount = 0 — it is usually overwritten later by a compute shader before ExecuteIndirect,
    // setting the actual number of live particles)
    initArgs.IndexCountPerInstance = mQuadGeo->DrawArgs["particleGeometry"].IndexCount;


    // Prepare the buffer resource description for our arguments (size = structure size)
    CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(initArgs));
    
    // Create an upload buffer (in a CPU-writeable heap) — convenient for writing data from the CPU and
    // then copying it into the actual GPU buffer mDrawArgs (which resides in the default heap)
    uploadDesc.Width = sizeof(initArgs);
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mDrawArgsUpload)));


    // Map is a way to obtain a pointer to a resource's memory if the resource resides in the UPLOAD heap (CPU-accessible).
    // Essentially, the driver 'opens' direct access to this buffer’s memory for the CPU. The mapped value is just a regular void* pointing
    // to the memory region corresponding to the resource buffer in VRAM (or, more precisely, in system memory accessible by both GPU and CPU).
    
    // Map the upload buffer, copy our argument structure into it, and unmap. Now the upload buffer holds the initial DrawArgs.
    void* mapped = nullptr;
    mDrawArgsUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, &initArgs, sizeof(initArgs));
    mDrawArgsUpload->Unmap(0, nullptr);


    // Transition the target buffer mDrawArgs (which will be read by ExecuteIndirect) from the INDIRECT_ARGUMENT state to a temporary COPY_DEST state to allow copying into it.
    // With a copy command, we transfer the data from the upload buffer -> into mDrawArgs (default heap).
    // Transition mDrawArgs back to the INDIRECT_ARGUMENT state so that the GPU can later read it inside ExecuteIndirect
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST));
    mCommandList->CopyBufferRegion(mDrawArgs.Get(), 0, mDrawArgsUpload.Get(), 0, sizeof(initArgs));
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDrawArgs.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));



    // Counters
    
    // In D3D12 things are stricter than in D3D11: if you use a UAV as an AppendStructuredBuffer or ConsumeStructuredBuffer,
    // it must have a separate UAV counter resource so the GPU can atomically increment/decrement its length.
    // In this case, the dead list and alive list are exactly like that: they are declared in HLSL as AppendStructuredBuffer<uint> / ConsumeStructuredBuffer<uint>.
    // In such buffers, the shader does not write the size manually — the hardware counter does the job.
    // Even if you don’t access the counter directly in shaders (.IncrementCounter() or .DecrementCounter()),
    // the driver and GPU internally still rely on it to know where to place the next Append() and whether there's anything to return from Consume().

    // Prepare the initial values of the UAV counters. The idea is as follows:
    //  - the first counter = mMaxParticles (everything is free in the dead list at the very beginning);
    //  - the second and third = 0 (the second dead list is empty, the alive list is empty);
    //  - the fourth UINT is just padding/alignment in this array.
    UINT initCounters[4] = { mMaxParticles, 0, 0, 0 };

    uploadDesc.Width = sizeof(initCounters);
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mCounterUpload)));

    mCounterUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, initCounters, sizeof(initCounters));
    mCounterUpload->Unmap(0, nullptr);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
    mCommandList->CopyBufferRegion(mCounters.Get(), 0, mCounterUpload.Get(), 0, sizeof(initCounters));
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));



    // List of dead particles
    std::vector<UINT> deadIndices(mMaxParticles);
    std::iota(deadIndices.begin(), deadIndices.end(), 0);

    uploadDesc.Width = mMaxParticles * sizeof(UINT);
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mDeadListUpload)));

    mDeadListUpload->Map(0, nullptr, &mapped);
    memcpy(mapped, deadIndices.data(), mMaxParticles * sizeof(UINT));
    mDeadListUpload->Unmap(0, nullptr);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
    mCommandList->CopyBufferRegion(mDeadList[0].Get(), 0, mDeadListUpload.Get(), 0, uploadDesc.Width);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDeadList[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));



    // Create UAV-descriptors

    // Prepare the descriptor heap description for CBV/SRV/UAV
    // 7 slots are enough for all required UAVs (pool, lists, counters/args, etc.).
    // SHADER_VISIBLE — so that the heap can be bound in the root signature and accessed from shaders.
    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
    uavHeapDesc.NumDescriptors = 7; // 5 - below + 2 - EmissiveTex and NormalTex
    uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    // Create the heap
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&mUavSrvHeap)));

    // Take the CPU handle to the start of the heap and the increment size (the number of bytes between neighboring descriptors of this type)
    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(mUavSrvHeap->GetCPUDescriptorHandleForHeapStart());
    UINT uavDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


    // Prepare a common UAV description for the buffers.
    // DXGI_FORMAT_UNKNOWN — since this is a structured buffer (we specify StructureByteStride instead of a pixel format).
    // NumElements is initially set to the number of elements in the particle array.
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = mMaxParticles;

    // UAV for the particle pool (RWStructuredBuffer<Particle>).
    // StructureByteStride = sizeof(Particle).
    // CounterResource = nullptr -> no counter is needed (we don't use Append/Consume on the pool).
    // Place the descriptor in the first slot and advance uavHandle to the next one.
    uavDesc.Buffer.StructureByteStride = sizeof(Particle);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    mDevice->CreateUnorderedAccessView(mParticlePool.Get(), nullptr, &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);
    
    uavDesc.Buffer.StructureByteStride = sizeof(UINT);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    mDevice->CreateUnorderedAccessView(mDeadList[0].Get(), mCounters.Get(), &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.CounterOffsetInBytes = kCounterAlignment; // 4096
    mDevice->CreateUnorderedAccessView(mDeadList[1].Get(), mCounters.Get(), &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.CounterOffsetInBytes = kCounterAlignment * 2; // 8192
    mDevice->CreateUnorderedAccessView(mAliveList.Get(), mCounters.Get(), &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.StructureByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    uavDesc.Buffer.NumElements = 1;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    mDevice->CreateUnorderedAccessView(mDrawArgs.Get(), nullptr, &uavDesc, uavHandle);
}


void ParticleSystem::BuildRootSignatures()
{
    // Create an array of 3 root parameters and zero-initialize it.
    // Each element describes what and how will be bound to the shaders (CBV/SRV/UAV/descriptor tables, etc.)
    // 
    // Prepare 3 root parameters:
    //  - b0: CBV with frame/pass constants
    //  - t0: SRV for ParticlePool (the vertex shader reads the particle array).
    //  - t1: SRV for AliveList (indices of live particles for instancing).
    CD3DX12_ROOT_PARAMETER slotRootParameter[3] = {};
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsShaderResourceView(0);
    slotRootParameter[2].InitAsShaderResourceView(1);


    // Define the root signature and enable the Input Assembler layout (required since we are rendering indexed geometry)
    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(_countof(slotRootParameter), slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // Buffers for the root signature serialization result and potential compilation errors
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;

    // Serialization of the description into a binary blob. If something is invalid (e.g., incompatible flags/parameters), the driver will write the error text into errorBlob
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));

    // Create an ID3D12RootSignature object on the device from the serialized blob. This root signature is then set before rendering
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignatureRender)));



    // For compute shaders
    // Create a descriptor range for UAVs. Type: UAV, 5 entries starting from register u0 (i.e., this will cover the range u0...u4).
    // This range will then be placed into a descriptor table so that the shader can access multiple UAVs through a single root parameter
    CD3DX12_DESCRIPTOR_RANGE uavTable = {};
    uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0);

    // Similarly, create a descriptor range for SRVs
    CD3DX12_DESCRIPTOR_RANGE srvTable = {};
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);

    // Root parameter #2 is a descriptor table with a single range — the uavTable (5 consecutive UAVs: u0...u4).
    // In the command list, the shader-visible heap will then be bound and the GPU handle to the start of this table specified ->
    // -> the shader will gain access to all 5 resources.
    //
    // Root parameter #3 is the second descriptor table, this time for SRVs (range t0...t1) — analogous to the previous one
    CD3DX12_ROOT_PARAMETER computeSlotRootParameter[4] = {};
    computeSlotRootParameter[0].InitAsConstantBufferView(0); // ParticleConstants
    computeSlotRootParameter[1].InitAsConstantBufferView(1); // PassConstants
    computeSlotRootParameter[2].InitAsDescriptorTable(1, &uavTable);
    computeSlotRootParameter[3].InitAsDescriptorTable(1, &srvTable);


    // Assemble the compute root signature description:
    //  - 4 root parameters — b0, b1, the table u0...u4, and the table t0...t1
    //  - no static samplers
    //  - flags = NONE (the IA flag is not required for compute)
    CD3DX12_ROOT_SIGNATURE_DESC computeRsDesc(_countof(computeSlotRootParameter), computeSlotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    // Serialize the description into a binary blob. If there is an error (mismatched registers, invalid ranges), the error text will appear in errorBlob
    ThrowIfFailed(D3D12SerializeRootSignature(&computeRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignatureCompute)));
}


void ParticleSystem::BuildCommandSignature()
{
    // Describe which specific indirect argument the GPU will read from the argument buffer. Here the only argument is the DRAW_INDEXED command.
    // That means a row in the argument buffer must match the D3D12_DRAW_INDEXED_ARGUMENTS structure format:
    //  - IndexCountPerInstance
    //  - InstanceCount
    //  - StartIndexLocation
    //  - BaseVertexLocation
    //  - StartInstanceLocation
    D3D12_INDIRECT_ARGUMENT_DESC argDescs[1] = {};
    argDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;


    // Assemble the command signature description:
    //  - ByteStride — the size of one argument record in the buffer. Here it equals the size of D3D12_DRAW_INDEXED_ARGUMENTS, since each record = one DrawIndexed call
    //  - NumArgumentDescs — how many heterogeneous arguments a record contains.Here it is 1 (just the DRAW_INDEXED)
    //  - pArgumentDescs — the array of argument descriptions (defined above)
    //  - NodeMask — for multi - adapter setups (usually 0).
    D3D12_COMMAND_SIGNATURE_DESC cmdSignatureDesc = {};
    cmdSignatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    cmdSignatureDesc.NumArgumentDescs = 1;
    cmdSignatureDesc.pArgumentDescs = argDescs;
    cmdSignatureDesc.NodeMask = 0;


    // Create an ID3D12CommandSignature object. It tells the driver how to interpret the bytes in the argument buffer when ExecuteIndirect is called
    ThrowIfFailed(mDevice->CreateCommandSignature(&cmdSignatureDesc, nullptr, IID_PPV_ARGS(&mCommandSignature)));
}


void ParticleSystem::BuildShadersAndPSOs()
{
    // Compile shaders for particles
    auto vsByteCode = d3dUtil::CompileShader(L"../Shaders/Particle.hlsl", nullptr, "VS", "vs_5_1");
    auto psByteCode = d3dUtil::CompileShader(L"../Shaders/Particle.hlsl", nullptr, "PS", "ps_5_1");
    auto sortCS = d3dUtil::CompileShader(L"../Shaders/ParticleSortCS.hlsl", nullptr, "CS", "cs_5_1");



    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mQuadGeo->InputLayout.data(), (UINT)mQuadGeo->InputLayout.size() };
    psoDesc.pRootSignature = mRootSignatureRender.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vsByteCode.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(psByteCode.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // <- The issue with disappearing particles might be here
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
    ThrowIfFailed(mDevice->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mPSOSimulate)))

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
    
    // Writes new values of the ParticleConstants structure into the constant buffer (CB)
    frameResource->ParticleCB->CopyData(mCBIndex, pConsts);

    mCommandList->SetComputeRootSignature(mRootSignatureCompute.Get());

    ID3D12DescriptorHeap* computeHeaps[] = { mUavSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(computeHeaps), computeHeaps);



    // Bariers for all UAVs before use
    //
    // This is UAV barriers. Unlike Transition barriers, they do not change the resource state (the resource remains a UAV).
    // They are needed to synchronize the order of access to a UAV inside the GPU. But for UAVs this is dangerous:
    //  - two compute dispatches may write to the same UAV
    //  - or you may first write to a UAV and then want to read its data.
    // Without a barrier, the GPU may reorder these operations.
    // A UAV barrier says: 'All previous writes to this UAV must be completed and visible before anything else that uses the same UAV can begin'
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
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));

    // Resets the AliveList counter to 0 to start the frame from a clean slate
    mCommandList->CopyBufferRegion(mCounters.Get(), kAliveCounterOffset, frameResource->NullUploadBuffer->Resource(), 0, sizeof(UINT));

    // If Dead0 is active, the other counter (Dead1 - offset 4096) must be reset so that this frame writes there. If Dead1 is active, we reset Dead0 - offset 0.
    UINT64 deadCounterOffset = (mCurrentDeadList == 0) ? D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT : 0;

    // Copy a UINT(0) from a small upload buffer into the selected counter offset — this resets the counter of the dead list active for this frame to zero
    mCommandList->CopyBufferRegion(mCounters.Get(), deadCounterOffset, frameResource->NullUploadBuffer->Resource(), 0, sizeof(UINT));
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));



    // Launch EmitCS for creating new particles
    mCommandList->SetPipelineState(mPSOEmit.Get());

    // Set ParticleConstants
    UINT alignedSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ParticleConstants));
    mCommandList->SetComputeRootConstantBufferView(0, frameResource->ParticleCB->Resource()->GetGPUVirtualAddress() + mCBIndex * alignedSize);

    // Set PassConstants
    auto passCB = frameResource->PassCB->Resource();
    mCommandList->SetComputeRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    // Set UAV's
    mCommandList->SetComputeRootDescriptorTable(2, mUavSrvHeap->GetGPUDescriptorHandleForHeapStart());

    // Set SRV's
    CD3DX12_GPU_DESCRIPTOR_HANDLE emissiveTexDescriptorGPU(mUavSrvHeap->GetGPUDescriptorHandleForHeapStart());
    emissiveTexDescriptorGPU.Offset(5, mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    mCommandList->SetComputeRootDescriptorTable(3, emissiveTexDescriptorGPU);


    // Calculate how many 256-thread groups are needed to cover all particles for the frame.
    // mNumParticlesToEmit is the number of new particles that need to be created this frame.
    // To cover them all, we do (mNumParticlesToEmit + 255) / 256
    UINT groups = (mNumParticlesToEmit + 255) / 256;
    if (groups == 0) return; // may break resource types
    mCommandList->Dispatch(groups, 1, 1);
    

    // Barier for synchronization between EmitCS and SimulateCS
    //
    // Guarantee that all writes from EmitCS are completed and visible before SimulateCS starts reading/writing the same resources
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
    //
    // Switch to the simulation PSO and dispatch a compute with the number of groups covering the entire pool (ceil(mMaxParticles / 256), written as mMaxParticles / 256 + 1)
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

    mCommandList->SetComputeRootSignature(mRootSignatureCompute.Get());
    mCommandList->SetPipelineState(mPSOSort.Get());
    

    UINT numGroups = (mMaxParticles + 255) / 256;
    mCommandList->Dispatch(numGroups, 1, 1);

    //mCommandList->ResourceBarrier(1, &sortBarrier);

    // Switch the deadlists for the next frame
    mCurrentDeadList = 1 - mCurrentDeadList;
}


void ParticleSystem::Draw(D3D12_GPU_VIRTUAL_ADDRESS passCBAddress)
{
    mCommandList->SetGraphicsRootSignature(mRootSignatureRender.Get());
    mCommandList->SetPipelineState(mPSORender.Get());

    mCommandList->IASetVertexBuffers(0, 1, &mQuadGeo->VertexBufferView());
    mCommandList->IASetIndexBuffer(&mQuadGeo->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->SetGraphicsRootConstantBufferView(0, passCBAddress);
    mCommandList->SetGraphicsRootShaderResourceView(1, mParticlePool->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootShaderResourceView(2, mAliveList->GetGPUVirtualAddress());

    mCommandList->ExecuteIndirect(mCommandSignature.Get(), 1, mDrawArgs.Get(), 0, nullptr, 0);
}