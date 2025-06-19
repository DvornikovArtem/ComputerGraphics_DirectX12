#include "Particle.h"
#include "d3dUtil.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include <numeric> // Ðåøàåò îøèáêó C3861 'iota': identifier not found

ParticleSystem::ParticleSystem(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT maxParticles)
    : mMaxParticles(maxParticles)
{
    BuildResources(device, cmdList);
    BuildRootSignatures(device);
    BuildCommandSignature(device);
    BuildShadersAndPSOs(device);
}

void ParticleSystem::BuildResources(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    bufferDesc.Width = mMaxParticles * sizeof(Particle);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mParticlePool)));

    bufferDesc.Width = mMaxParticles * sizeof(UINT);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mDeadList[0])));
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mDeadList[1])));
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mAliveList)));

    bufferDesc.Width = sizeof(UINT) * 4;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mCounters)));

    bufferDesc.Width = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, nullptr, IID_PPV_ARGS(&mDrawArgs)));

    std::vector<UINT> deadIndices(mMaxParticles);
    std::iota(deadIndices.begin(), deadIndices.end(), 0);

    ComPtr<ID3D12Resource> uploadDeadList;
    auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    bufferDesc.Width = mMaxParticles * sizeof(UINT);
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadDeadList)));

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = deadIndices.data();
    subData.RowPitch = bufferDesc.Width;
    subData.SlicePitch = subData.RowPitch;
    cmdList->CopyBufferRegion(mDeadList[0].Get(), 0, uploadDeadList.Get(), 0, bufferDesc.Width);

    // --- Ñîçäàíèå êó÷è äåñêðèïòîðîâ ---
    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
    uavHeapDesc.NumDescriptors = 5; // Pool, Dead0, Dead1, Alive, DrawArgs
    uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&mUavSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(mUavSrvHeap->GetCPUDescriptorHandleForHeapStart());
    UINT uavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = mMaxParticles;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    uavDesc.Buffer.StructureByteStride = sizeof(Particle);
    device->CreateUnorderedAccessView(mParticlePool.Get(), nullptr, &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.StructureByteStride = sizeof(UINT);
    // Dead List 0 (ñ÷åò÷èê ïî ñìåùåíèþ 0)
    device->CreateUnorderedAccessView(mDeadList[0].Get(), mCounters.Get(), &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);
    // Dead List 1 (ñ÷åò÷èê ïî ñìåùåíèþ 4)
    device->CreateUnorderedAccessView(mDeadList[1].Get(), mCounters.Get(), &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);
    // Alive List (ñ÷åò÷èê ïî ñìåùåíèþ 8)
    device->CreateUnorderedAccessView(mAliveList.Get(), mCounters.Get(), &uavDesc, uavHandle);
    uavHandle.Offset(1, uavDescriptorSize);

    uavDesc.Buffer.StructureByteStride = sizeof(UINT);
    uavDesc.Buffer.NumElements = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(UINT);
    device->CreateUnorderedAccessView(mDrawArgs.Get(), nullptr, &uavDesc, uavHandle);

    // --- Ãåîìåòðèÿ ---
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

    // Ýòî èñïðàâëÿåò îøèáêó C2061: syntax error: identifier 'Blob'
    ThrowIfFailed(D3DCreateBlob(vertices.size() * sizeof(Vertex), &mQuadGeo->VertexBufferCPU));
    CopyMemory(mQuadGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertices.size() * sizeof(Vertex));

    ThrowIfFailed(D3DCreateBlob(indices.size() * sizeof(uint16_t), &mQuadGeo->IndexBufferCPU));
    CopyMemory(mQuadGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), indices.size() * sizeof(uint16_t));

    mQuadGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, vertices.data(), vertices.size() * sizeof(Vertex), mQuadGeo->VertexBufferUploader);
    mQuadGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, indices.data(), indices.size() * sizeof(uint16_t), mQuadGeo->IndexBufferUploader);

    mQuadGeo->VertexByteStride = sizeof(Vertex);
    mQuadGeo->VertexBufferByteSize = (UINT)vertices.size() * sizeof(Vertex);
    mQuadGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    mQuadGeo->IndexBufferByteSize = (UINT)indices.size() * sizeof(uint16_t);

    mQuadGeo->DrawArgs["quad"] = { (UINT)indices.size(), 0, 0, {} };

    // Ýòî èñïðàâëÿåò îøèáêó "class "MeshGeometry" has no member "InputLayout""
    mQuadGeo->InputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void ParticleSystem::BuildRootSignatures(ID3D12Device* device)
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[3];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsShaderResourceView(0);
    slotRootParameter[2].InitAsShaderResourceView(1);

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(3, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignatureRender)));

    CD3DX12_DESCRIPTOR_RANGE uavTable;
    uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0);
    CD3DX12_ROOT_PARAMETER computeSlotRootParameter[2];
    computeSlotRootParameter[0].InitAsConstantBufferView(0);
    computeSlotRootParameter[1].InitAsDescriptorTable(1, &uavTable);

    CD3DX12_ROOT_SIGNATURE_DESC computeRsDesc(2, computeSlotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
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
    wchar_t buffer[MAX_PATH];
    GetCurrentDirectory(MAX_PATH, buffer);
    OutputDebugString(buffer); // èëè èñïîëüçóéòå std::wcout
    OutputDebugString(L"\nPRIKOOOOOOOL\n");

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
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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
    // 1. Îáíîâëÿåì êîíñòàíòû
    ParticleConstants pConsts;
    pConsts.DeltaTime = dt;
    pConsts.EmitterPos = emitterPos;
    pConsts.NumEmit = numToEmit;
    frameResource->ParticleCB->CopyData(0, pConsts);

    cmdList->SetComputeRootSignature(mRootSignatureCompute.Get());

    ID3D12DescriptorHeap* computeHeaps[] = { mUavSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(computeHeaps), computeHeaps);

    // 2. Ñáðàñûâàåì ñ÷åò÷èê AliveList íà 0.
    //cmdList->CopyBufferRegion(mCounters.Get(), 8, frameResource->UploadCounter.Get(), 0, 4); // Ñìåùåíèå 8 - äëÿ AliveList

    // 3. Çàïóñêàåì EmitCS äëÿ ñîçäàíèÿ íîâûõ ÷àñòèö
    cmdList->SetPipelineState(mPSOEmit.Get());
    cmdList->SetComputeRootConstantBufferView(0, frameResource->ParticleCB->Resource()->GetGPUVirtualAddress());
    cmdList->SetComputeRootDescriptorTable(1, mUavSrvHeap->GetGPUDescriptorHandleForHeapStart());
    cmdList->Dispatch(numToEmit / 256 + 1, 1, 1);

    // 4. Áàðüåðû, ÷òîáû çàâåðøèòü çàïèñü â áóôåðû ïåðåä ñèìóëÿöèåé
    auto barriers = {
        CD3DX12_RESOURCE_BARRIER::UAV(mParticlePool.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mDeadList[mCurrentDeadList].Get())
    };
    cmdList->ResourceBarrier(2, barriers.begin());

    // 5. Çàïóñêàåì SimulateCS äëÿ îáíîâëåíèÿ è îòáîðà
    cmdList->SetPipelineState(mPSOSimulate.Get());
    cmdList->Dispatch(mMaxParticles / 256 + 1, 1, 1);

    // 6. Êîïèðóåì êîëè÷åñòâî æèâûõ ÷àñòèö (ñ÷åò÷èê èç AliveList) â áóôåð äëÿ DrawIndirect
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mDrawArgs.Get(),
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
        D3D12_RESOURCE_STATE_COPY_DEST));
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
    cmdList->CopyBufferRegion(mDrawArgs.Get(), 4, mCounters.Get(), 8, 4); // Ñìåùåíèå 4 - InstanceCount â DrawArgs
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mCounters.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mDrawArgs.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));

    // 7. Ïåðåêëþ÷àåì "ìåðòâûå" ñïèñêè äëÿ ñëåäóþùåãî êàäðà
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