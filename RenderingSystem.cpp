#include "RenderingSystem.h"

RenderingSystem::RenderingSystem() {}


void RenderingSystem::Initialize(HWND mhMainWnd, HINSTANCE mhAppInst, GameTimer* gt) {
#if defined(DEBUG) || defined(_DEBUG) 
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	this->mhMainWnd = mhMainWnd;
	this->mhAppInst = mhAppInst;
	this->gt = gt;
	
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

	// Try to create hardware device.
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             // default adapter
		D3D_FEATURE_LEVEL_12_0,
		IID_PPV_ARGS(&md3dDevice));

	// Fallback to WARP device.
	if (FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&md3dDevice)));
	}

	ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&mFence)));

	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Check 4X MSAA quality support for our back buffer format.
	// All Direct3D 11 capable devices support 4X MSAA for all render 
	// target formats, so we only need to check quality support.

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = mBackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(md3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

	#ifdef _DEBUG
	LogAdapters();
	#endif

	CreateCommandObjects();
	CreateSwapChain();

	mGbuffer = std::make_unique<Gbuffer>(mClientWidth, mClientHeight, md3dDevice);

	// For Debug System =========================================================
	mDebugDrawer = new gfw::DebugRenderSysImpl(md3dDevice);

	mDebugDrawer->SetCamera(&mCamera);
	// ==========================================================================

	OnResize();

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mParticleSystem = std::make_unique<ParticleSystem>(
		md3dDevice.Get(),
		mCommandList.Get(),
		5000);

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	BuildRootSignatures();
	BuildInputLayout();
	BuildBasicGeometry();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();
}

void RenderingSystem::FinishInitialize()
{
	CreateRtvAndDsvDescriptorHeaps();

	mGbuffer->Channel0SRVHeapIndex = TexDescsLength + MPRTextures.size() + 1;

	//copy GBuffer SRVs into main SRVHeap
	md3dDevice->CopyDescriptorsSimple(mGbuffer->NumBuffers, GetCpuSrv(mGbuffer->Channel0SRVHeapIndex),
		mGbuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	int k = 0;
	for (auto& litem : mAllLights) {
		litem->shadowMap->BuildDescriptors(GetCpuSrv(TexDescsLength + MPRTextures.size() + 1 + mGbuffer->NumBuffers + k), GetGpuSrv(TexDescsLength + MPRTextures.size() + 1 + mGbuffer->NumBuffers + k), GetDsv(1 + k));
		litem->shadowMap->SRVHeapIndex = TexDescsLength + MPRTextures.size() + 1 + mGbuffer->NumBuffers + k;
		k++;
	}

	BuildFrameResources();
}

void RenderingSystem::OnResize() {
	assert(md3dDevice);
	assert(mSwapChain);
	assert(mDirectCmdListAlloc);

	// Flush before changing any resources.
	FlushCommandQueue();

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Release the previous resources we will be recreating.
	for (int i = 0; i < SwapChainBufferCount; ++i)
		mSwapChainBuffer[i].Reset();
	mDepthStencilBuffer.Reset();

	// Resize the swap chain.
	ThrowIfFailed(mSwapChain->ResizeBuffers(
		SwapChainBufferCount,
		mClientWidth, mClientHeight,
		mBackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	mCurrBackBuffer = 0;

	if (mRtvHeap) {
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
		for (UINT i = 0; i < SwapChainBufferCount; i++)
		{
			ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
			md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
			rtvHeapHandle.Offset(1, mRtvDescriptorSize);
		}
	}

	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = mClientWidth;
	depthStencilDesc.Height = mClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;

	// Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
	// the depth buffer.  Therefore, because we need to create two views to the same resource:
	//   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
	//   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
	// we need to create the depth buffer resource with a typeless format.  
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

	depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	if (mDsvHeap) {
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = mDepthStencilFormat;
		dsvDesc.Texture2D.MipSlice = 0;
		md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());
	}

	// Transition the resource from its initial state to be used as a depth buffer.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Execute the resize commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until resize is complete.
	FlushCommandQueue();

	// Update the viewport transform to cover the client area.
	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width = static_cast<float>(mClientWidth);
	mScreenViewport.Height = static_cast<float>(mClientHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	mScissorRect = { 0, 0, mClientWidth, mClientHeight };

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 100000.0f);
	

	mGbuffer->Resize(mClientWidth, mClientHeight, md3dDevice.Get());

	if (mSrvDescriptorHeap)
	{
		md3dDevice->CopyDescriptorsSimple(mGbuffer->NumBuffers, GetCpuSrv(mGbuffer->Channel0SRVHeapIndex),
			mGbuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

}

void RenderingSystem::Render()
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mParticleSystem->Update(
		mCommandList.Get(),
		mDeltaTime,
		mCurrFrameResource,
		XMFLOAT3{ 0.0f, 1.0f, 0.0f },
		10);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSwapChainBuffer[mCurrBackBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Create frame shadow maps
	DrawShadowMaps();
	

	// Deferred Passes:
	
	//
	// 1. Geometry: draw scene into G-buffer.
	//
	mGbuffer->TransitToOpaqueRenderingState(mCommandList);
	mGbuffer->ClearRTVs(mCommandList);
	GBufferGeometryPass();

	//
	// 2. Light: calculate light into G-buffer.
	// 
	mGbuffer->TransitToLightsRenderingState(mCommandList);
	GBufferLightPass();


	//
	//Draw SkyBox
	//
	DrawSkyBox();


	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	auto passCBAddress = mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
	mParticleSystem->Draw(mCommandList.Get(), passCBAddress);

	CD3DX12_RESOURCE_BARRIER barriers[2] = {
		CD3DX12_RESOURCE_BARRIER::Transition(mParticleSystem->GetAliveList(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(mParticleSystem->GetParticlePool(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};
	mCommandList->ResourceBarrier(_countof(barriers), barriers);


	//
	// Post-Processing
	//
	mGbuffer->TransitToTonemappingState(mCommandList);
	PostProcessingPass();

	// Draw debug primitives
	mDebugDrawer->Draw(
		0.0f,
		mCommandQueue,
		mCommandList,
		&mScreenViewport,
		&mScissorRect,
		this,
		mCurrFrameResourceIndex
	);


	// Clear
	mDebugDrawer->Clear();

	mGbuffer->TransitFromShaderResourceToCommon(mCommandList);


	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;


	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Notify the fence when the GPU completes commands up to this fence point.
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void RenderingSystem::FlushCommandQueue()
{
	// Advance the fence value to mark commands up to this fence point.
	mCurrentFence++;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

	// Wait until the GPU has completed commands up to this fence point.
	if (mFence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::DepthStencilView() const {
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

ID3D12Resource* RenderingSystem::CurrentBackBuffer() const {
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::CurrentBackBufferView() const {
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrBackBuffer,
		mRtvDescriptorSize);
}

void RenderingSystem::LogAdapters()
{
	UINT i = 0;
	IDXGIAdapter* adapter = nullptr;
	std::vector<IDXGIAdapter*> adapterList;
	while (mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);

		std::wstring text = L"***Adapter: ";
		text += desc.Description;
		text += L"\n";

		OutputDebugString(text.c_str());

		adapterList.push_back(adapter);

		++i;
	}

	for (size_t i = 0; i < adapterList.size(); ++i)
	{
		LogAdapterOutputs(adapterList[i]);
		ReleaseCom(adapterList[i]);
	}
}

void RenderingSystem::BuildRenderItems(std::unordered_map<std::string, DrawableObject*>& Objects)
{
	int k = 0;
	for (auto& pair : Objects)
	{
		auto i = pair.second;

		auto t = new RenderItem;

		t->ObjCBIndex = k;
		t->Mat = mMaterials[i->MaterialName];
		t->Geo = mGeometries[i->GeometryName];
		t->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		t->IndexCount = t->Geo->DrawArgs[i->GeometryName].IndexCount;
		t->StartIndexLocation = t->Geo->DrawArgs[i->GeometryName].StartIndexLocation;
		t->BaseVertexLocation = t->Geo->DrawArgs[i->GeometryName].BaseVertexLocation;

		XMStoreFloat4x4(&t->World, XMMatrixScaling(i->Scale.x, i->Scale.y, i->Scale.z)
			* XMMatrixRotationRollPitchYaw(i->WorldRotation.z, i->WorldRotation.y, i->WorldRotation.x)
			* XMMatrixTranslation(i->WorldLocation.x, i->WorldLocation.y, i->WorldLocation.z));
		XMStoreFloat4x4(&t->TexTransform, i->TexTransform);
		t->Geo->DrawArgs["LOD0"].Bounds.Transform(t->bounds, XMLoadFloat4x4(&t->World));

		t->currentLOD = 0;
		t->numLODs = t->Geo->DrawArgs.size() - 1;

		t->renderLayer = i->renderLayer;
		t->drawableObject = i;

		mRitemLayer[(int)i->renderLayer].push_back(t);
		mAllRitems.push_back(t);

		i->renderItem = t;

		k++;

	}

	//generate OctTree
	OctTreeDesc octTreeDesc;
	octTreeDesc.ritems = &mAllRitems;
	octTreeDesc.litems = &mAllLights;
	octTreeDesc.numDivisions = 4;
	octTreeDesc.autoFitBox = true;

	mOctTree = new OctTree(octTreeDesc);
}

void RenderingSystem::BuildLightItems(std::unordered_map<std::string, LightObject*>& Objects)
{

	XMVECTOR RotationAxis;
	float RotAngle;
	float SphereRadius;
	XMFLOAT3 ConeScale;

	int k = 0;
	for (auto& pair : Objects)
	{
		auto& i = pair.second;

		i->LightCBIndex = k;

		i->shadowMap = new ShadowMap(md3dDevice.Get(), 2048, 2048);

		//generated bounding geometry and world matrix for light
		switch (i->LightType)
		{
		case LightType::Pointlight:
			i->Geo = mGeometries["Sphere_LowPoly"];
			SphereRadius = 7.f * i->Strength;
			XMStoreFloat4x4(&i->World, XMMatrixScaling(SphereRadius, SphereRadius, SphereRadius) *
				XMMatrixTranslation(i->WorldLocation.x, i->WorldLocation.y, i->WorldLocation.z));
			break;
		case LightType::Spotlight:
			i->Geo = mGeometries["Cone"];
			ConeScale.y = i->FalloffEnd / 5;
			ConeScale.x = 1.f / ConeScale.y;
			ConeScale.x = ConeScale.z = ConeScale.x * i->SpotPower * 8;
			//calculate rotation matrix from start and target direction vectors
			XMVECTOR StartDir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
			XMVECTOR TargetDir = XMVector3Normalize(XMLoadFloat3(&i->WorldDirection));
			RotationAxis = XMVector3Cross(StartDir, TargetDir);
			RotAngle = acosf(XMVectorGetX(XMVector3Dot(StartDir, TargetDir)));

			XMStoreFloat4x4(&i->World, XMMatrixScaling(ConeScale.x, ConeScale.y, ConeScale.z) *
				XMMatrixRotationAxis(XMVector3Normalize(RotationAxis), RotAngle) *
				XMMatrixTranslation(i->WorldLocation.x, i->WorldLocation.y, i->WorldLocation.z));
			break;
		}

		if (i->LightType != LightType::Directional) {
			i->Geo->DrawArgs["LOD0"].Bounds.Transform(i->bounds, XMLoadFloat4x4(&i->World));
		}

		mAllLights.push_back(i);

		k++;
	}
}

void RenderingSystem::LogAdapterOutputs(IDXGIAdapter* adapter)
{
	UINT i = 0;
	IDXGIOutput* output = nullptr;
	while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);

		std::wstring text = L"***Output: ";
		text += desc.DeviceName;
		text += L"\n";
		OutputDebugString(text.c_str());

		ReleaseCom(output);

		++i;
	}
}


void RenderingSystem::CreateCommandObjects() {
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

	ThrowIfFailed(md3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

	ThrowIfFailed(md3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		mDirectCmdListAlloc.Get(), // Associated command allocator
		nullptr,                   // Initial PipelineStateObject
		IID_PPV_ARGS(mCommandList.GetAddressOf())));

	// Start off in a closed state.  This is because the first time we refer 
	// to the command list we will Reset it, and it needs to be closed before
	// calling Reset.
	mCommandList->Close();
}

void RenderingSystem::CreateSwapChain()
{
	// Release the previous swapchain we will be recreating.
	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = mClientWidth;
	sd.BufferDesc.Height = mClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = mBackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = mhMainWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Note: Swap chain uses queue to perform flush.
	ThrowIfFailed(mdxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, mSwapChain.GetAddressOf()));
}

void RenderingSystem::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}


	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + mAllLights.size();
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));


	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(DepthStencilView());
	

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, hDescriptor);
	hDescriptor.Offset(1, mDsvDescriptorSize);
	
	for (int i = 0; i < mAllLights.size(); i++) {
		md3dDevice->CreateDepthStencilView(nullptr, &dsvDesc, hDescriptor);
		hDescriptor.Offset(1, mDsvDescriptorSize);
	}
}

CD3DX12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetCpuSrv(int index)const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE RenderingSystem::GetGpuSrv(int index)const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetDsv(int index)const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
	dsv.Offset(index, mDsvDescriptorSize);
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetRtv(int index)const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtv.Offset(index, mRtvDescriptorSize);
	return rtv;
}

void RenderingSystem::BuildRootSignatures()
{

	//default

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable2;
	texTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_DESCRIPTOR_RANGE texTable3;
	texTable3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

	CD3DX12_ROOT_PARAMETER slotRootParameter[6];

	// Texture resources
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_ALL); // Diffuse texture
	slotRootParameter[1].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_ALL); // Normal map
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_ALL); // Height map

	// Constant buffers
	slotRootParameter[3].InitAsConstantBufferView(0); // ObjectCB
	slotRootParameter[4].InitAsConstantBufferView(1); // MainPassCB 
	slotRootParameter[5].InitAsConstantBufferView(2); // MaterialCB

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(6, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(RootSignatures["Default"].GetAddressOf())));


	// for deferred light pass

	CD3DX12_DESCRIPTOR_RANGE texTable4;
	texTable4.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

	CD3DX12_DESCRIPTOR_RANGE texTable5;
	texTable5.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

	CD3DX12_DESCRIPTOR_RANGE texTable6;
	texTable6.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);

	CD3DX12_DESCRIPTOR_RANGE texTable7;
	texTable7.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);

	CD3DX12_DESCRIPTOR_RANGE texTable8;
	texTable8.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);

	CD3DX12_DESCRIPTOR_RANGE texTable9;
	texTable9.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);

	CD3DX12_ROOT_PARAMETER lightPassSlotRootParameter[11];

	lightPassSlotRootParameter[0].InitAsConstantBufferView(0); //MainPassCB
	lightPassSlotRootParameter[1].InitAsConstantBufferView(1); //LightCB

	lightPassSlotRootParameter[2].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_ALL); //GBufferChannels
	lightPassSlotRootParameter[3].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_ALL);
	lightPassSlotRootParameter[4].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_ALL);
	lightPassSlotRootParameter[5].InitAsDescriptorTable(1, &texTable4, D3D12_SHADER_VISIBILITY_ALL);
	lightPassSlotRootParameter[6].InitAsDescriptorTable(1, &texTable5, D3D12_SHADER_VISIBILITY_ALL);

	lightPassSlotRootParameter[7].InitAsDescriptorTable(1, &texTable6, D3D12_SHADER_VISIBILITY_ALL); //ShadowMap

	lightPassSlotRootParameter[8].InitAsDescriptorTable(1, &texTable7, D3D12_SHADER_VISIBILITY_ALL); //IBL SkyMaps
	lightPassSlotRootParameter[9].InitAsDescriptorTable(1, &texTable8, D3D12_SHADER_VISIBILITY_ALL); 
	lightPassSlotRootParameter[10].InitAsDescriptorTable(1, &texTable9, D3D12_SHADER_VISIBILITY_ALL); 

	//ShadowMap ComparisonSampler
	const CD3DX12_STATIC_SAMPLER_DESC StaticSamplers[2] = 
	{
		CD3DX12_STATIC_SAMPLER_DESC(
		0, // register(s0)
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.0f,
		16,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE),

		CD3DX12_STATIC_SAMPLER_DESC(
		1, // register(s1)
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, 
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP) 
	};

	CD3DX12_ROOT_SIGNATURE_DESC lightPassRootSigDesc(11, lightPassSlotRootParameter,
		2, StaticSamplers,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	
	ComPtr<ID3DBlob> serializedLightPassRootSig = nullptr;
	ComPtr<ID3DBlob> lightPassErrorBlob = nullptr;
	HRESULT lightPassHr = D3D12SerializeRootSignature(&lightPassRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedLightPassRootSig.GetAddressOf(), lightPassErrorBlob.GetAddressOf());

	if (lightPassErrorBlob != nullptr)
	{
		OutputDebugStringA((char*)lightPassErrorBlob->GetBufferPointer());
	}
	ThrowIfFailed(lightPassHr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedLightPassRootSig->GetBufferPointer(),
		serializedLightPassRootSig->GetBufferSize(),
		IID_PPV_ARGS(RootSignatures["DeferredLightPass"].GetAddressOf())));


	//for post-processing

	CD3DX12_ROOT_PARAMETER PPSlotRootParameter[4];

	PPSlotRootParameter[0].InitAsConstantBufferView(0); //MainPassCB
	PPSlotRootParameter[1].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_ALL); //GBufferChannels
	PPSlotRootParameter[2].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_ALL);
	PPSlotRootParameter[3].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC PPRootSigDesc(4, PPSlotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedPProotSig = nullptr;
	ComPtr<ID3DBlob> PPErrorBlob = nullptr;
	HRESULT PPHr = D3D12SerializeRootSignature(&PPRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedPProotSig.GetAddressOf(), PPErrorBlob.GetAddressOf());

	if (PPErrorBlob != nullptr)
	{
		OutputDebugStringA((char*)PPErrorBlob->GetBufferPointer());
	}
	ThrowIfFailed(PPHr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedPProotSig->GetBufferPointer(),
		serializedPProotSig->GetBufferSize(),
		IID_PPV_ARGS(RootSignatures["PostProcessing"].GetAddressOf())));

}

void RenderingSystem::Update(std::vector<DrawableObject*>& mAllObjectsToUpdate, std::vector<LightObject*>& mAllLightObjectsToUpdate)
{
	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateCamera(*gt);
	UpdateRenderItems(mAllObjectsToUpdate);
	UpdateObjectCBs(*gt);
	UpdateMaterialCBs(*gt);
	UpdateMainPassCB(*gt);
	UpdateLightItems(mAllLightObjectsToUpdate);
	UpdateLightCBs(*gt);


	mDeltaTime = gt->DeltaTime();
	//XMFLOAT3 emitterPos = { 0.0f, 5.0f, 0.0f }; // Позиция эмиттера
	//UINT numToEmit = 10; // Сколько частиц создавать каждый кадр

	//mParticleSystem->Update(mCommandList.Get(), gt->DeltaTime(), mCurrFrameResource, emitterPos, numToEmit);

}

void RenderingSystem::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));


			XMVECTOR diff = XMVectorSubtract(XMLoadFloat4x4(&e->World).r[3], mCamera.GetPosition());

			objConstants.TesselationFactor = 50 / XMVectorGetX(XMVector3Length(diff));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}

}

void RenderingSystem::UpdateLightItems(std::vector<LightObject*>& mAllLightObjectsToUpdate)
{

	XMVECTOR RotationAxis;
	float RotAngle;
	float SphereRadius;
	XMFLOAT3 ConeScale;

	for (auto& e : mAllLightObjectsToUpdate) {

		switch (e->LightType)
		{
		case LightType::Pointlight:
			SphereRadius = 10.f * e->Strength;
			XMStoreFloat4x4(&e->World, XMMatrixScaling(SphereRadius, SphereRadius, SphereRadius) *
				XMMatrixTranslation(e->WorldLocation.x, e->WorldLocation.y, e->WorldLocation.z));
			break;
		case LightType::Spotlight:
			ConeScale.y = e->FalloffEnd / 5;
			ConeScale.x = 1.f / ConeScale.y;
			ConeScale.x = ConeScale.z = ConeScale.x * e->SpotPower * 8;
			//calculate rotation matrix from start and target direction vectors
			XMVECTOR StartDir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
			XMVECTOR TargetDir = XMVector3Normalize(XMLoadFloat3(&e->WorldDirection));
			RotationAxis = XMVector3Cross(StartDir, TargetDir);
			RotAngle = acosf(XMVectorGetX(XMVector3Dot(StartDir, TargetDir)));

			XMStoreFloat4x4(&e->World, XMMatrixScaling(ConeScale.x, ConeScale.y, ConeScale.z) *
				XMMatrixRotationAxis(XMVector3Normalize(RotationAxis), RotAngle) *
				XMMatrixTranslation(e->WorldLocation.x, e->WorldLocation.y, e->WorldLocation.z));
			break;
		}

		if (e->LightType != LightType::Directional) {
			e->Geo->DrawArgs["LOD0"].Bounds.Transform(e->bounds, XMLoadFloat4x4(&e->World));
		}

		e->NumFramesDirty = gNumFrameResources;

		mOctTree->UpdateLightItemTreeLocation(e);
	}

	mAllVisibleLitems.clear();
	alreadyCheckedLitems.clear();
	CollectVisibleLightItems();

	mAllLightObjectsToUpdate.clear();
}

void RenderingSystem::UpdateLightCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->LightCB.get();
	float lightAngle;

	XMVECTOR lightDir;
	XMVECTOR lightPos;
	XMVECTOR targetPos;
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX lightView = XMMatrixIdentity();
	XMMATRIX lightProj = XMMatrixIdentity();
	XMFLOAT3 sphereCenterLS;
	float l, b, n, r, t, f;
	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	XMMATRIX S = XMMatrixIdentity();

	for (auto& e : mAllLights)
	{
		if (e->NumFramesDirty > 0)
		{
			Light LightConstants;
			LightConstants.Position = e->WorldLocation;
			LightConstants.Direction = e->WorldDirection;
			LightConstants.Color = e->Color;
			LightConstants.FalloffStart = e->FalloffStart;
			LightConstants.FalloffEnd = e->FalloffEnd;
			LightConstants.LightType = (int)e->LightType;
			LightConstants.SpotPower = e->SpotPower;
			LightConstants.Strength = XMFLOAT3(e->Strength, e->Strength, e->Strength);

			switch (e->LightType)
			{
			case LightType::Directional:
			{
				float SphereRadiuses[4] = { 10, 50, 150, 400 };
				//for each cascade
				for (int i = 0; i < 4; i++)
				{
					lightDir = XMLoadFloat3(&e->WorldDirection);
					lightPos = mCamera.GetPosition() - 2.0f * SphereRadiuses[i] * lightDir;
					targetPos = mCamera.GetPosition();
					lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

					// Transform bounding sphere to light space.
					sphereCenterLS;
					XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

					// Ortho frustum in light space encloses cascade.
					l = sphereCenterLS.x - SphereRadiuses[i];
					b = sphereCenterLS.y - SphereRadiuses[i];
					n = sphereCenterLS.z - SphereRadiuses[i];
					r = sphereCenterLS.x + SphereRadiuses[i];
					t = sphereCenterLS.y + SphereRadiuses[i];
					f = sphereCenterLS.z + SphereRadiuses[i];

					lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

					S = lightView * lightProj * T;
					XMStoreFloat4x4(&LightConstants.View[i], XMMatrixTranspose(lightView));
					XMStoreFloat4x4(&LightConstants.Proj[i], XMMatrixTranspose(lightProj));
					XMStoreFloat4x4(&LightConstants.ShadowTransform[i], XMMatrixTranspose(S));
					XMStoreFloat4(&LightConstants.CascadeDistances, XMVectorSet(SphereRadiuses[0], SphereRadiuses[1], SphereRadiuses[2], SphereRadiuses[3]));
				}
			}
				break;

			case LightType::Spotlight:
				lightPos = XMLoadFloat3(&e->WorldLocation);
				lightDir = XMLoadFloat3(&e->WorldDirection);
				targetPos = lightPos + lightDir;

				//ADD FOV CALCULATION
				lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
				lightProj = XMMatrixPerspectiveFovLH(XM_PI/6, 1.0f, 10.f, e->FalloffEnd);

				S = lightView * lightProj * T;

				XMStoreFloat4x4(&LightConstants.View[0], XMMatrixTranspose(lightView));
				XMStoreFloat4x4(&LightConstants.Proj[0], XMMatrixTranspose(lightProj));
				XMStoreFloat4x4(&LightConstants.ShadowTransform[0], XMMatrixTranspose(S));
				break;

			case LightType::Pointlight:
				lightPos = XMLoadFloat3(&e->WorldLocation);

				lightProj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, 0.1f, e->FalloffEnd);

				static const XMVECTOR directions[6] =
				{
					XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),  // +X
					XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f), // -X
					XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // +Y
					XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), // -Y
					XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),  // +Z
					XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)  // -Z
				};

				static const XMVECTOR ups[6] =
				{
					XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // +X
					XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // -X
					XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), // +Y
					XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),  // -Y
					XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),  // +Z
					XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)   // -Z
				};

				for (int i = 0; i < 6; ++i)
				{
					targetPos = lightPos + directions[i];
					lightView = XMMatrixLookAtLH(lightPos, targetPos, ups[i]);
					S = lightView * lightProj * T;
					XMStoreFloat4x4(&LightConstants.View[i], XMMatrixTranspose(lightView));
					XMStoreFloat4x4(&LightConstants.Proj[i], XMMatrixTranspose(lightProj));
					XMStoreFloat4x4(&LightConstants.ShadowTransform[i], XMMatrixTranspose(S));
				}
				break;
			}

			XMStoreFloat4x4(&LightConstants.World, XMMatrixTranspose(XMLoadFloat4x4(&e->World)));

			currObjectCB->CopyData(e->LightCBIndex, LightConstants);

			e->NumFramesDirty--;
		}
	}
}

std::vector<MeshParsingResult> RenderingSystem::BuildMeshGeometry(std::string Name, const std::string& filename) 
{
	Assimp::Importer importer;


	//select texture types we're looking for
	const std::vector<aiTextureType> textureTypes = 
	{
		aiTextureType_DIFFUSE,
		aiTextureType_NORMALS,
		aiTextureType_DIFFUSE_ROUGHNESS
	};

	std::vector<MeshParsingResult> res;
	res.resize(1);

	const aiScene* scene = importer.ReadFile(filename,
		aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_CalcTangentSpace);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		MessageBoxW(0, L"Model not found.", 0, 0);
		return res;
	}

	std::vector<Vertex> vertices;
	std::vector<std::int32_t> indices;

	auto geo = new MeshGeometry;
	geo->Name = Name;

	for (unsigned int i = 0; i < scene->mNumMeshes; i++) 
	{
		aiMesh* mesh = scene->mMeshes[i];
		UINT baseVertexLocation = (UINT)vertices.size();
		UINT startIndexLocation = (UINT)indices.size();

		XMFLOAT3 vMin = { FLT_MAX, FLT_MAX, FLT_MAX };
		XMFLOAT3 vMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		//read diffuse texture from first submesh
		if (i == 0)
		{
			res[0].GeneratedMaterial.Name = Name + "_" + mesh->mName.C_Str();
			aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
			for (auto& texType : textureTypes) {
				if (material->GetTextureCount(texType) > 0)
				{
					aiString texturePath;
					aiTexture* embeddedTexture;
					if (material->GetTexture(texType, 0, &texturePath) == AI_SUCCESS)
					{
						for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
							if (scene->mTextures[i]->mFilename == texturePath) {
								embeddedTexture = scene->mTextures[i];
								break;
							}
						}

						//Get texture name
						std::string TextureName = std::string(texturePath.C_Str());
						size_t lastSlash = TextureName.find_last_of("\\/");
						if (lastSlash != std::string::npos) { TextureName = TextureName.substr(lastSlash + 1); }
						size_t dotPos = TextureName.find_last_of('.');
						if (dotPos != std::string::npos) { TextureName = TextureName.substr(0, dotPos); }

						ProcessEmbeddedTexture(embeddedTexture, TextureName);

						switch (texType)
						{
						case aiTextureType_DIFFUSE:
							res[0].DiffuseTextureName = TextureName;
							res[0].GeneratedMaterial.DiffuseTexName = TextureName;
							break;
						case aiTextureType_NORMALS:
							res[0].NormalMapName = TextureName;
							//res[0].GeneratedMaterial.NormalMapName = TextureName;
							break;
						case aiTextureType_DIFFUSE_ROUGHNESS:
							res[0].RoughnessMapName = TextureName;
							break;
						}
					}
				}
			}
		}

		for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
			Vertex vertex;

			vertex.Pos.x = mesh->mVertices[j].x;
			vertex.Pos.y = mesh->mVertices[j].y;
			vertex.Pos.z = mesh->mVertices[j].z;



			vMin.x = std::min(vMin.x, vertex.Pos.x);
			vMin.y = std::min(vMin.y, vertex.Pos.y);
			vMin.z = std::min(vMin.z, vertex.Pos.z);

			vMax.x = (((vMax.x) > (vertex.Pos.x)) ? (vMax.x) : (vertex.Pos.x));
			vMax.y = (((vMax.y) > (vertex.Pos.y)) ? (vMax.y) : (vertex.Pos.y));
			vMax.z = (((vMax.z) > (vertex.Pos.z)) ? (vMax.z) : (vertex.Pos.z));



			if (mesh->HasNormals()) {
				vertex.Normal.x = mesh->mNormals[j].x;
				vertex.Normal.y = mesh->mNormals[j].y;
				vertex.Normal.z = mesh->mNormals[j].z;
			}

			if (mesh->HasTextureCoords(0)) {
				vertex.TexC.x = mesh->mTextureCoords[0][j].x;
				vertex.TexC.y = mesh->mTextureCoords[0][j].y;
			}
			else {
				vertex.TexC.x = 0.0f;
				vertex.TexC.y = 0.0f;
			}

			if (mesh->HasTangentsAndBitangents()) {
				vertex.Tangent.x = mesh->mTangents[j].x;
				vertex.Tangent.y = mesh->mTangents[j].y;
				vertex.Tangent.z = mesh->mTangents[j].z;
			}

			vertices.push_back(vertex);
		}

		for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
			const aiFace& face = mesh->mFaces[f];
			for (unsigned int k = 0; k < face.mNumIndices; ++k) {
				indices.push_back(static_cast<std::int32_t>(face.mIndices[k] + baseVertexLocation));
			}
		}

		SubmeshGeometry submesh;
		submesh.IndexCount = (UINT)indices.size() - startIndexLocation;
		submesh.StartIndexLocation = startIndexLocation;
		submesh.BaseVertexLocation = 0;

		// create bounding box
		XMFLOAT3 center = {
		  0.5f * (vMin.x + vMax.x),
		  0.5f * (vMin.y + vMax.y),
		  0.5f * (vMin.z + vMax.z)
		};
		XMFLOAT3 extents = {
		  0.5f * (vMax.x - vMin.x),
		  0.5f * (vMax.y - vMin.y),
		  0.5f * (vMax.z - vMin.z)
		};

		BoundingBox box(center, extents);
		submesh.Bounds = box;

		std::string submeshName = "LOD" + std::to_string(i);

		geo->DrawArgs[submeshName] = submesh;

	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);


	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	mGeometries[geo->Name] = geo;

	res[0].GeometryName = Name;
	return res;
}

std::vector<MeshParsingResult> RenderingSystem::LoadMesh(MeshDesc& meshDesc, bool GenerateMaterial)
{
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	std::vector<MeshParsingResult> res;

	res = BuildMeshGeometry(meshDesc.Name, meshDesc.Path);

	for (auto& i : res) i.GenerateMaterial = GenerateMaterial;

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return res;
}

void RenderingSystem::BuildPSOs(MaterialDesc& MDesc, std::unordered_map<std::string, ComPtr<ID3D12PipelineState>>& mPSOs)
{

	//
	// PSO for GBuffer Geometry Pass
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC descPipelineState;
	ZeroMemory(&descPipelineState, sizeof(descPipelineState));
	descPipelineState.VS =
	{
		reinterpret_cast<BYTE*>(mShaders[MDesc.VertexShaderName]->GetBufferPointer()),
		mShaders[MDesc.VertexShaderName]->GetBufferSize()
	};
	descPipelineState.PS =
	{
		reinterpret_cast<BYTE*>(mShaders[MDesc.PixelShaderName]->GetBufferPointer()),
		mShaders[MDesc.PixelShaderName]->GetBufferSize()
	};
	if (MDesc.UseTesselation)
	{
		descPipelineState.HS =
		{
			reinterpret_cast<BYTE*>(mShaders[MDesc.HullShaderName]->GetBufferPointer()),
			mShaders[MDesc.HullShaderName]->GetBufferSize()
		};
		descPipelineState.DS =
		{
			reinterpret_cast<BYTE*>(mShaders[MDesc.DomainShaderName]->GetBufferPointer()),
			mShaders[MDesc.DomainShaderName]->GetBufferSize()
		};
		descPipelineState.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	}
	else descPipelineState.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	descPipelineState.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	descPipelineState.pRootSignature = RootSignatures["Default"].Get();
	descPipelineState.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	descPipelineState.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	descPipelineState.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	descPipelineState.SampleMask = UINT_MAX;
	descPipelineState.NumRenderTargets = 5;
	descPipelineState.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	descPipelineState.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	descPipelineState.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_SNORM;
	descPipelineState.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;
	descPipelineState.RTVFormats[4] = DXGI_FORMAT_R8G8B8A8_UNORM;
	descPipelineState.DSVFormat = mDepthStencilFormat;
	descPipelineState.SampleDesc.Count = 1;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&descPipelineState, IID_PPV_ARGS(&mPSOs["GBufferGeometryPass"])));

	///
	/// PSO for ShadowMap generation (opaque geometry)
	/// 

	D3D12_GRAPHICS_PIPELINE_STATE_DESC ShadowMapPSODesc = descPipelineState;
	ShadowMapPSODesc.RasterizerState.DepthBias = 1000;
	ShadowMapPSODesc.RasterizerState.DepthBiasClamp = 0.0f;
	ShadowMapPSODesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	ShadowMapPSODesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["ShadowOpaqueVS"]->GetBufferPointer()),
		mShaders["ShadowOpaqueVS"]->GetBufferSize()
	};
	ShadowMapPSODesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["ShadowOpaquePS"]->GetBufferPointer()),
		mShaders["ShadowOpaquePS"]->GetBufferSize()
	};
	ShadowMapPSODesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["ShadowOpaqueGS"]->GetBufferPointer()),
		mShaders["ShadowOpaqueGS"]->GetBufferSize()
	};
	ShadowMapPSODesc.HS = { nullptr, 0 };
	ShadowMapPSODesc.DS = { nullptr, 0 };
	ShadowMapPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// Shadow map pass does not have a render target.
	ShadowMapPSODesc.NumRenderTargets = 0;
	ShadowMapPSODesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	ShadowMapPSODesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
	ShadowMapPSODesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
	ShadowMapPSODesc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;
	ShadowMapPSODesc.RTVFormats[4] = DXGI_FORMAT_UNKNOWN;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ShadowMapPSODesc, IID_PPV_ARGS(&mPSOs["ShadowOpaque"])));
}

void RenderingSystem::BuildGlobalPSOs()
{
	//
	// PSO for GBuffer Light Pass
	//


	D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredPsoDesc = {};
	// Ïîñêîëüêó äëÿ ïîëíîýêðàííîãî êâàäðàòà íå íóæåí âõîäíîé layout, îñòàâëÿåì åãî ïóñòûì:
	deferredPsoDesc.InputLayout = { nullptr, 0 };
	deferredPsoDesc.pRootSignature = RootSignatures["DeferredLightPass"].Get();

	deferredPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["DeferredLightPassVS_FSQuad"]->GetBufferPointer()),
		mShaders["DeferredLightPassVS_FSQuad"]->GetBufferSize()
	};
	deferredPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["DeferredLightPassPS"]->GetBufferPointer()),
		mShaders["DeferredLightPassPS"]->GetBufferSize()
	};

	deferredPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);


	CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	blendDesc.RenderTarget[0].BlendEnable = true;
	blendDesc.RenderTarget[0].LogicOpEnable = false;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	deferredPsoDesc.BlendState = blendDesc;

	deferredPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	deferredPsoDesc.DepthStencilState.DepthEnable = false;
	deferredPsoDesc.DepthStencilState.StencilEnable = false;
	deferredPsoDesc.SampleMask = UINT_MAX;
	deferredPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	deferredPsoDesc.NumRenderTargets = 1;
	deferredPsoDesc.RTVFormats[0] = mBackBufferFormat;
	deferredPsoDesc.SampleDesc.Count = 1;
	deferredPsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&GlobalPSOs["DeferredLightPass_FSQuad"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredAddAmbientPsoDesc = deferredPsoDesc;

	deferredAddAmbientPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["DeferredLightPassPS_AddAmbient"]->GetBufferPointer()),
		mShaders["DeferredLightPassPS_AddAmbient"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredAddAmbientPsoDesc, IID_PPV_ARGS(&GlobalPSOs["DeferredLightPass_AddAmbient"])));

	deferredPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	deferredPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["DeferredLightPassVS_Bounded"]->GetBufferPointer()),
		mShaders["DeferredLightPassVS_Bounded"]->GetBufferSize()
	};

	deferredPsoDesc.DepthStencilState.DepthEnable = true;
	deferredPsoDesc.DepthStencilState.StencilEnable = true;
	deferredPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	deferredPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
	deferredPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&GlobalPSOs["DeferredLightPass_Bounded"])));


	//
	// PSO for skybox
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc;

	ZeroMemory(&skyPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	skyPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	skyPsoDesc.pRootSignature = RootSignatures["Default"].Get();
	skyPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	skyPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	skyPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	skyPsoDesc.SampleMask = UINT_MAX;
	skyPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	skyPsoDesc.NumRenderTargets = 1;
	skyPsoDesc.RTVFormats[0] = mBackBufferFormat;
	skyPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	skyPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	skyPsoDesc.DSVFormat = mDepthStencilFormat;

	// The camera is inside the sky sphere, so just turn off culling.
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["SkyBoxVS"]->GetBufferPointer()),
		mShaders["SkyBoxVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["SkyBoxPS"]->GetBufferPointer()),
		mShaders["SkyBoxPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&GlobalPSOs["SkyBox"])));

	//
	// PSO for Post-Processing
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PPPsoDesc;

	ZeroMemory(&PPPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	PPPsoDesc.InputLayout = { nullptr, 0 };
	PPPsoDesc.pRootSignature = RootSignatures["PostProcessing"].Get();
	PPPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	PPPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	PPPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	PPPsoDesc.DepthStencilState.DepthEnable = false;
	PPPsoDesc.DepthStencilState.StencilEnable = false;
	PPPsoDesc.SampleMask = UINT_MAX;
	PPPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PPPsoDesc.NumRenderTargets = 1;
	PPPsoDesc.RTVFormats[0] = mBackBufferFormat;
	PPPsoDesc.SampleDesc.Count = 1;
	PPPsoDesc.SampleDesc.Quality = 0;
	PPPsoDesc.DSVFormat = mDepthStencilFormat;

	PPPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["PPVS"]->GetBufferPointer()),
		mShaders["PPVS"]->GetBufferSize()
	};
	PPPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["PPPS"]->GetBufferPointer()),
		mShaders["PPPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&PPPsoDesc, IID_PPV_ARGS(&GlobalPSOs["PostProcessing"])));

}

void RenderingSystem::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			2, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), (UINT)mAllLights.size(), 1));
	}
}

void RenderingSystem::BuildMaterials(std::vector<MaterialDesc>& MaterialDescs)
{
	for (int i = 0; i < MaterialDescs.size(); i++)
	{
		auto t = new Material;
		t->Name = MaterialDescs[i].Name;
		t->MatCBIndex = i;
		t->DiffuseSrvHeapIndex = ((mTextures.find(MaterialDescs[i].DiffuseTexName) == mTextures.end())) ? 0 : mTextures[MaterialDescs[i].DiffuseTexName]->srvHeapIndex;
		t->NormalSrvHeapIndex = ((mTextures.find(MaterialDescs[i].NormalMapName) == mTextures.end())) ? 0 : mTextures[MaterialDescs[i].NormalMapName]->srvHeapIndex;
		t->HeightSrvHeapIndex = ((mTextures.find(MaterialDescs[i].HeightMapName) == mTextures.end())) ? 0 : mTextures[MaterialDescs[i].HeightMapName]->srvHeapIndex;
		t->DiffuseAlbedo = MaterialDescs[i].DiffuseAlbedo;
		t->FresnelR0 = MaterialDescs[i].FresnelR0;
		t->Roughness = MaterialDescs[i].Roughness;
		t->Metallic = MaterialDescs[i].Metallic;
		t->UseTesselation = MaterialDescs[i].UseTesselation;

		BuildPSOs(MaterialDescs[i], t->PSOs);

		mMaterials[t->Name] = t;
	}
	BuildGlobalPSOs();
}

void RenderingSystem::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, std::string PSOName)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(4, passCB->GetGPUVirtualAddress());

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		if (!ri->IsInViewFrustum) continue;
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->Mat->UseTesselation ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmdList->SetPipelineState(ri->Mat->PSOs[PSOName].Get());

		ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
		cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, GetGpuSrv(ri->Mat->DiffuseSrvHeapIndex));
		cmdList->SetGraphicsRootDescriptorTable(1, GetGpuSrv(ri->Mat->NormalSrvHeapIndex));
		cmdList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(ri->Mat->HeightSrvHeapIndex));

		cmdList->SetGraphicsRootConstantBufferView(3, objCBAddress);
		//cmdList->SetGraphicsRootConstantBufferView(4, passCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(5, matCBAddress);



		std::string subMeshName = "LOD" + std::to_string(ri->currentLOD);

		UINT IndexCount = ri->Geo->DrawArgs[subMeshName].IndexCount;
		UINT StartIndexLocation = ri->Geo->DrawArgs[subMeshName].StartIndexLocation;
		UINT BaseVertexLocation = ri->Geo->DrawArgs[subMeshName].BaseVertexLocation;

		cmdList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);

		ri->IsInViewFrustum = false;
	}
}

void RenderingSystem::GBufferGeometryPass()
{
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);
	mCommandList->SetGraphicsRootSignature(RootSignatures["Default"].Get());

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[5] = {
		mGbuffer->DiffuseRTV,
		mGbuffer->EmissiveRTV,
		mGbuffer->NormalRTV,
		mGbuffer->MaterialAlbedoRTV,
		mGbuffer->MaterialFresnelRoughnessRTV
	};

	mCommandList->OMSetRenderTargets(5, rtvs, false, &DepthStencilView());

	// clear G-buffer
	float clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
	for (int i = 0; i < 5; ++i)
	{
		mCommandList->ClearRenderTargetView(rtvs[i], clearColor, 0, nullptr);
	}

	//mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);


	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque], "GBufferGeometryPass");
}

void RenderingSystem::GBufferLightPass()
{
	mCommandList->SetGraphicsRootSignature(RootSignatures["DeferredLightPass"].Get());
	mCommandList->OMSetRenderTargets(1, &mGbuffer->BloomRTV, false, &DepthStencilView());

	UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(Light));
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	auto lightCB = mCurrFrameResource->LightCB->Resource();
	auto passCB = mCurrFrameResource->PassCB->Resource();

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex + 1));
	mCommandList->SetGraphicsRootDescriptorTable(4, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex + 2));
	mCommandList->SetGraphicsRootDescriptorTable(5, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex + 3));
	mCommandList->SetGraphicsRootDescriptorTable(6, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex + 4));

	mCommandList->SetGraphicsRootDescriptorTable(8, GetGpuSrv(mTextures["SkyIrradiance"]->srvHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(9, GetGpuSrv(mTextures["SkyPref"]->srvHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(10, GetGpuSrv(mTextures["SkyBRDF"]->srvHeapIndex));

	mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// For each light item...
	for (size_t i = 0; i < mAllLights.size(); ++i)
	{
		auto& li = mAllLights[i];

		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + li->LightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(1, lightCBAddress);

		mCommandList->SetGraphicsRootDescriptorTable(7, GetGpuSrv(li->shadowMap->SRVHeapIndex));
		
		if (li->LightType == LightType::Directional)
		{
			mCommandList->SetPipelineState(GlobalPSOs["DeferredLightPass_FSQuad"].Get());
			mCommandList->DrawInstanced(6, 1, 0, 0);
		}
		else
		{
			if (!li->IsInViewFrustum) continue;

			mCommandList->SetPipelineState(GlobalPSOs["DeferredLightPass_Bounded"].Get());
			mCommandList->IASetVertexBuffers(0, 1, &li->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&li->Geo->IndexBufferView());

			UINT IndexCount = li->Geo->DrawArgs["LOD0"].IndexCount;
			UINT StartIndexLocation = li->Geo->DrawArgs["LOD0"].StartIndexLocation;
			UINT BaseVertexLocation = li->Geo->DrawArgs["LOD0"].BaseVertexLocation;

			mCommandList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);

			li->IsInViewFrustum = false;
		}
	}

	//Add ambient light on screen
	mCommandList->SetPipelineState(GlobalPSOs["DeferredLightPass_AddAmbient"].Get());
	mCommandList->DrawInstanced(6, 1, 0, 0);

}

void RenderingSystem::DrawSkyBox()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootSignature(RootSignatures["Default"].Get());
	mCommandList->SetPipelineState(GlobalPSOs["SkyBox"].Get());
	mCommandList->SetGraphicsRootConstantBufferView(4, passCB->GetGPUVirtualAddress());

	// For each render item...
	for (size_t i = 0; i < mRitemLayer[(int)RenderLayer::Sky].size(); ++i)
	{
		auto ri = mRitemLayer[(int)RenderLayer::Sky][i];

		mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrv(ri->Mat->DiffuseSrvHeapIndex));
		mCommandList->SetGraphicsRootConstantBufferView(3, objCBAddress);
		mCommandList->SetGraphicsRootConstantBufferView(5, matCBAddress);

		UINT IndexCount = ri->Geo->DrawArgs["LOD0"].IndexCount;
		UINT StartIndexLocation = ri->Geo->DrawArgs["LOD0"].StartIndexLocation;
		UINT BaseVertexLocation = ri->Geo->DrawArgs["LOD0"].BaseVertexLocation;

		mCommandList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
	}
}

void RenderingSystem::DrawShadowMaps()
{
	mCommandList->SetGraphicsRootSignature(RootSignatures["Default"].Get());
	for (auto& i : mAllLights)
	{
		//if (i->LightType == LightType::Pointlight) continue;

		mCommandList->RSSetViewports(1, &i->shadowMap->Viewport());
		mCommandList->RSSetScissorRects(1, &i->shadowMap->ScissorRect());

		// Change to DEPTH_WRITE.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(i->shadowMap->Resource(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));


		// Clear the back buffer and depth buffer.
		mCommandList->ClearDepthStencilView(i->shadowMap->Dsv(),
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		// Set null render target because we are only going to draw to
		// depth buffer.  Setting a null render target will disable color writes.
		// Note the active PSO also must specify a render target count of 0.
		mCommandList->OMSetRenderTargets(0, nullptr, false, &i->shadowMap->Dsv());

		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(Light));
		auto lightCB = mCurrFrameResource->LightCB->Resource();

		UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
		UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
		auto objectCB = mCurrFrameResource->ObjectCB->Resource();
		auto matCB = mCurrFrameResource->MaterialCB->Resource();

		for (size_t j = 0; j < mRitemLayer[(int)RenderLayer::Opaque].size(); ++j)
		{
			auto& ri = mRitemLayer[(int)RenderLayer::Opaque][j];

			mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->SetPipelineState(ri->Mat->PSOs["ShadowOpaque"].Get());

			ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
			mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);


			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
			D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + i->LightCBIndex * lightCBByteSize;

			mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrv(ri->Mat->DiffuseSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(1, GetGpuSrv(ri->Mat->NormalSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(ri->Mat->HeightSrvHeapIndex));

			mCommandList->SetGraphicsRootConstantBufferView(3, objCBAddress);
			mCommandList->SetGraphicsRootConstantBufferView(4, lightCBAddress);
			mCommandList->SetGraphicsRootConstantBufferView(5, matCBAddress);


			std::string subMeshName = "LOD" + std::to_string(ri->currentLOD);

			UINT IndexCount = ri->Geo->DrawArgs[subMeshName].IndexCount;
			UINT StartIndexLocation = ri->Geo->DrawArgs[subMeshName].StartIndexLocation;
			UINT BaseVertexLocation = ri->Geo->DrawArgs[subMeshName].BaseVertexLocation;

			mCommandList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);

		}

		// Change back to GENERIC_READ so we can read the texture in a shader.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(i->shadowMap->Resource(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
	}
}

void RenderingSystem::PostProcessingPass()
{
	mCommandList->SetGraphicsRootSignature(RootSignatures["PostProcessing"].Get());
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), false, &DepthStencilView());
	mCommandList->SetPipelineState(GlobalPSOs["PostProcessing"].Get());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	auto passCB = mCurrFrameResource->PassCB->Resource();

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());

	mCommandList->SetGraphicsRootDescriptorTable(1, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex + 6));
	mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex + 1));
	mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrv(mGbuffer->Channel0SRVHeapIndex + 2));


	mCommandList->DrawInstanced(6, 1, 0, 0);
}

void RenderingSystem::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second;
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			matConstants.Metallic = mat->Metallic;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void RenderingSystem::LoadTextures(std::vector<TextureDesc>& TexDescs)
{

	TexDescsLength = TexDescs.size();

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	DirectX::ResourceUploadBatch upload(md3dDevice.Get());
	upload.Begin();

	auto invalidTex = new Texture;
	invalidTex->srvHeapIndex = 0;
	invalidTex->Name = "INVALID";
	invalidTex->Filename = L"../Textures/INVALID.dds";

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
		md3dDevice.Get(),
		upload,
		invalidTex->Filename.c_str(),
		invalidTex->Resource.GetAddressOf()));

	mTextures[invalidTex->Name] = invalidTex;

	for (int i = 0; i < TexDescs.size(); i++)
	{
		auto t = new Texture;
		t->srvHeapIndex = i + 1;
		t->Name = TexDescs[i].Name;
		t->Filename = TexDescs[i].Path;

		ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
			md3dDevice.Get(),
			upload,
			t->Filename.c_str(),
			t->Resource.GetAddressOf()));

		mTextures[t->Name] = t;
	}

	for (int i = 0; i < MPRTextures.size(); i++)
	{
		auto t = MPRTextures[i];
		t->srvHeapIndex = i + TexDescs.size() + 1;

		mTextures[t->Name] = t;
	}

	auto finish = upload.End(mCommandQueue.Get());
	finish.get();

	
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = TexDescs.size() + MPRTextures.size() + 1 + mAllLights.size() + mGbuffer->NumBuffers;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = invalidTex->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = invalidTex->Resource->GetDesc().MipLevels;

	md3dDevice->CreateShaderResourceView(invalidTex->Resource.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);


	for (TextureDesc& i : TexDescs) {
		auto it = mTextures.find(i.Name);
		if (it == mTextures.end()) {
			// Îáðàáîòêà îøèáêè: òåêñòóðà íå íàéäåíà
			OutputDebugStringA(("Texture not found: " + i.Name + "\n").c_str());
			continue;
		}

		auto& tex = it->second->Resource;
		if (!tex) {
			// Îáðàáîòêà îøèáêè: ðåñóðñ òåêñòóðû íå èíèöèàëèçèðîâàí
			OutputDebugStringA(("Texture resource is null: " + i.Name + "\n").c_str());
			continue;
		}
		
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = tex->GetDesc().Format;
		if (i.UseSRGB)
		{
			switch (srvDesc.Format)
			{
			case DXGI_FORMAT_R8G8B8A8_UNORM:
				srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC1_UNORM:
				srvDesc.Format = DXGI_FORMAT_BC1_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC2_UNORM:
				srvDesc.Format = DXGI_FORMAT_BC2_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC3_UNORM:
				srvDesc.Format = DXGI_FORMAT_BC3_UNORM_SRGB;
				break;
			case DXGI_FORMAT_B8G8R8A8_UNORM:
				srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_B8G8R8X8_UNORM:
				srvDesc.Format = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_BC7_UNORM:
				srvDesc.Format = DXGI_FORMAT_BC7_UNORM_SRGB;
				break;
			}
		}
		switch (i.TexType)
		{
		case TextureDesc::Texture2D:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = tex->GetDesc().MipLevels;
			break;
		case TextureDesc::CubeMap:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = tex->GetDesc().MipLevels;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
			break;
		}

		md3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	}

	for (Texture* i : MPRTextures) {
		auto it = mTextures.find(i->Name);
		if (it == mTextures.end()) {
			// Îáðàáîòêà îøèáêè: òåêñòóðà íå íàéäåíà
			OutputDebugStringA(("Texture not found: " + i->Name + "\n").c_str());
			continue;
		}

		auto& tex = it->second->Resource;
		if (!tex) {
			// Îáðàáîòêà îøèáêè: ðåñóðñ òåêñòóðû íå èíèöèàëèçèðîâàí
			OutputDebugStringA(("Texture resource is null: " + i->Name + "\n").c_str());
			continue;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = tex->GetDesc().Format;
		switch (srvDesc.Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			break;
		case DXGI_FORMAT_BC1_UNORM:
			srvDesc.Format = DXGI_FORMAT_BC1_UNORM_SRGB;
			break;
		case DXGI_FORMAT_BC2_UNORM:
			srvDesc.Format = DXGI_FORMAT_BC2_UNORM_SRGB;
			break;
		case DXGI_FORMAT_BC3_UNORM:
			srvDesc.Format = DXGI_FORMAT_BC3_UNORM_SRGB;
			break;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			break;
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			srvDesc.Format = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
			break;
		case DXGI_FORMAT_BC7_UNORM:
			srvDesc.Format = DXGI_FORMAT_BC7_UNORM_SRGB;
			break;
		}
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = tex->GetDesc().MipLevels;

		md3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	}

	for (int i = 0; i < mGbuffer->NumBuffers; i++) {
		md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	for (int i = 0; i < mAllLights.size(); i++) {
		md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
}

void RenderingSystem::ProcessEmbeddedTexture(const aiTexture* texture, std::string TextureName)
{
	//can be done with either DirectXTex or stb_image(we're going for option #2)

	int width, height, channels;
	unsigned char* imageData;

	if (texture->mHeight == 0) 
	{
		// Compressed data
		imageData = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc*>(texture->pcData),
			texture->mWidth,
			&width, &height, &channels, STBI_rgb_alpha);
	}
	else 
	{
		// Uncompressed data
		width = texture->mWidth;
		height = texture->mHeight;
		channels = 4;
		imageData = new unsigned char[width * height * 4];
		memcpy(imageData, texture->pcData, width * height * 4);
	}

	// need to convert RGBA to BGRA for whatever reason
	if (channels >= 3) {
		for (int i = 0; i < width * height; i++) {
			std::swap(imageData[i * 4], imageData[i * 4 + 2]);
		}
	}

	if (imageData) 
	{

		auto* generatedTex = new Texture;
		generatedTex->Name = TextureName;

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&generatedTex->Resource)));

		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(generatedTex->Resource.Get(), 0, 1));
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&generatedTex->UploadHeap)));

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = imageData;
		textureData.RowPitch = width * 4;
		textureData.SlicePitch = textureData.RowPitch * height;

		UpdateSubresources(mCommandList.Get(),
			generatedTex->Resource.Get(),
			generatedTex->UploadHeap.Get(),
			0, 0, 1, &textureData);

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			generatedTex->Resource.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		mCommandList->ResourceBarrier(1, &barrier);

		MPRTextures.push_back(generatedTex);
		stbi_image_free(imageData);

	}
}



std::unordered_set<RenderItem*> alreadyCheckedRitems;

void RenderingSystem::CollectVisibleRenderItems()
{

	std::vector<OctTreeNode*> leaves = mOctTree->GetAllNodesAtLevel(mOctTree->getNumDivisions() - 1);


	for (auto& leaf : leaves) {
		if (ViewFrustum.Contains(leaf->bounds) != DirectX::ContainmentType::DISJOINT) {
			for (RenderItem* ri : leaf->OverlappedRitems) {
				if (alreadyCheckedRitems.find(ri) != alreadyCheckedRitems.end()) continue;
				alreadyCheckedRitems.insert(ri);
				ri->IsInViewFrustum = ViewFrustum.Intersects(ri->bounds);
				if (ri->IsInViewFrustum) {
					//if (ri->Name.rfind("Patrick", 0) == std::string::npos) {
					mAllVisibleRitems.push_back(ri);
				}
			}
		}
	}
}

void RenderingSystem::CollectVisibleLightItems()
{
	std::vector<OctTreeNode*> leaves = mOctTree->GetAllNodesAtLevel(mOctTree->getNumDivisions() - 1);

	for (auto& leaf : leaves) {
		if (ViewFrustum.Contains(leaf->bounds) != DirectX::ContainmentType::DISJOINT) {
			for (LightObject* li : leaf->OverlappedLitems) {
				if (alreadyCheckedLitems.find(li) != alreadyCheckedLitems.end()) continue;
				alreadyCheckedLitems.insert(li);
				li->IsInViewFrustum = ViewFrustum.Intersects(li->bounds);
				if (li->IsInViewFrustum) {
					mAllVisibleLitems.push_back(li);
				}
			}
		}
	}
}


void RenderingSystem::UpdateRenderItems(std::vector<DrawableObject*>& mAllObjectsToUpdate)
{
	XMVECTOR cameraPos = mCamera.GetPosition();


	for (auto& ri : mAllVisibleRitems) {
		auto& i = ri->drawableObject;

		float dx = i->WorldLocation.x - XMVectorGetX(cameraPos);
		float dy = i->WorldLocation.y - XMVectorGetY(cameraPos);
		float dz = i->WorldLocation.z - XMVectorGetZ(cameraPos);
		float DistanceToObject = sqrtf(dx * dx + dy * dy + dz * dz);

		if (DistanceToObject < 10.f) ri->currentLOD = 0;
		else if (DistanceToObject < 20.f) ri->currentLOD = std::min(ri->numLODs - 1, (UINT)1);
		else if (DistanceToObject < 30.f) ri->currentLOD = std::min(ri->numLODs - 1, (UINT)2);
		else if (DistanceToObject < 40.f) ri->currentLOD = std::min(ri->numLODs - 1, (UINT)3);
		else ri->currentLOD = std::min(ri->numLODs - 1, (UINT)4);
	}

	for (auto& i : mAllObjectsToUpdate) {
		auto& ri = i->renderItem;

		XMStoreFloat4x4(&ri->World, XMMatrixScaling(i->Scale.x, i->Scale.y, i->Scale.z)
			* XMMatrixRotationRollPitchYaw(i->WorldRotation.z, i->WorldRotation.y, i->WorldRotation.x)
			* XMMatrixTranslation(i->WorldLocation.x, i->WorldLocation.y, i->WorldLocation.z));
		XMStoreFloat4x4(&ri->TexTransform, i->TexTransform);
		ri->Geo->DrawArgs["LOD0"].Bounds.Transform(ri->bounds, XMLoadFloat4x4(&ri->World));
		ri->NumFramesDirty = gNumFrameResources;

		mOctTree->UpdateRenderItemTreeLocation(ri);
	}

	mAllVisibleRitems.clear();
	alreadyCheckedRitems.clear();
	CollectVisibleRenderItems();

	mAllObjectsToUpdate.clear();
}

void RenderingSystem::BuildInputLayout()
{
	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void RenderingSystem::BuildShaders(std::vector<ShaderDesc>& ShaderDescs)
{
	for (ShaderDesc& i : ShaderDescs)
	{
		mShaders[i.Name] = d3dUtil::CompileShader(i.Path, i.ShaderDefines, i.FunctionName, i.ShaderProfile);
	}

	//standard shaders for deferred geometry rendering
	mShaders["standardVS"] = d3dUtil::CompileShader(L"../Shaders/DeferredGeometryPass.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["standardPS"] = d3dUtil::CompileShader(L"../Shaders/DeferredGeometryPass.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["standardHS"] = d3dUtil::CompileShader(L"../Shaders/DeferredGeometryPass.hlsl", nullptr, "HSMain", "hs_5_1");
	mShaders["standardDS"] = d3dUtil::CompileShader(L"../Shaders/DeferredGeometryPass.hlsl", nullptr, "DSMain", "ds_5_1");

	//standard shaders for deferred light rendering
	mShaders["DeferredLightPassVS_FSQuad"] = d3dUtil::CompileShader(L"../Shaders/DeferredLightPass.hlsl", nullptr, "VS_FSQuad", "vs_5_1");
	mShaders["DeferredLightPassVS_Bounded"] = d3dUtil::CompileShader(L"../Shaders/DeferredLightPass.hlsl", nullptr, "VS_Bounded", "vs_5_1");
	mShaders["DeferredLightPassPS"] = d3dUtil::CompileShader(L"../Shaders/DeferredLightPass.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["DeferredLightPassPS_AddAmbient"] = d3dUtil::CompileShader(L"../Shaders/DeferredLightPass.hlsl", nullptr, "PS_AddAmbient", "ps_5_1");

	//for skybox rendering
	mShaders["SkyBoxVS"] = d3dUtil::CompileShader(L"../Shaders/SkyBox.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["SkyBoxPS"] = d3dUtil::CompileShader(L"../Shaders/SkyBox.hlsl", nullptr, "PS", "ps_5_1");

	//for shadowmap geometry generation
	mShaders["ShadowOpaqueVS"] = d3dUtil::CompileShader(L"../Shaders/Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ShadowOpaquePS"] = d3dUtil::CompileShader(L"../Shaders/Shadows.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["ShadowOpaqueGS"] = d3dUtil::CompileShader(L"../Shaders/Shadows.hlsl", nullptr, "GS", "gs_5_1");

	//for post-processing
	mShaders["PPVS"] = d3dUtil::CompileShader(L"../Shaders/PostProcessing.hlsl", nullptr, "VS_FSQuad", "vs_5_1");
	mShaders["PPPS"] = d3dUtil::CompileShader(L"../Shaders/PostProcessing.hlsl", nullptr, "PS", "ps_5_1");
}

void RenderingSystem::BuildBasicGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(1.f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(2.f, 3.f, 20, 20);
	GeometryGenerator::MeshData sphere_lp = geoGen.CreateSphere(1.f, 10, 10);

	std::vector<GeometryGenerator::MeshData*> Objects = { &box, &grid, &sphere, &cylinder, &cone, &sphere_lp };
	std::vector<std::string> Names = { "Box", "Grid", "Sphere", "Cylinder", "Cone", "Sphere_LowPoly"};

	for (int k = 0; k < Objects.size(); k++)
	{
		auto Submesh = std::make_unique<SubmeshGeometry>();
		Submesh->IndexCount = (UINT)Objects[k]->Indices32.size();
		Submesh->StartIndexLocation = 0;
		Submesh->BaseVertexLocation = 0;

		std::vector<Vertex> vertices(Objects[k]->Vertices.size());

		for (size_t i = 0; i < Objects[k]->Vertices.size(); ++i)
		{
			vertices[i].Pos = Objects[k]->Vertices[i].Position;
			vertices[i].Normal = Objects[k]->Vertices[i].Normal;
			vertices[i].TexC = Objects[k]->Vertices[i].TexC;
			vertices[i].Tangent = Objects[k]->Vertices[i].TangentU;
		}

		std::vector<DirectX::XMFLOAT3> positions;
		for (size_t i = 0; i < vertices.size(); ++i)
			positions.push_back(vertices[i].Pos);

		std::vector<std::uint16_t> indices;
		indices.insert(indices.end(), std::begin(Objects[k]->GetIndices16()), std::end(Objects[k]->GetIndices16()));

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

		auto geo =  new MeshGeometry;
		geo->Name = Names[k];

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R16_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		// Create bounding box
		BoundingBox::CreateFromPoints(Submesh->Bounds, positions.size(), positions.data(), sizeof(XMFLOAT3));

		geo->DrawArgs["LOD0"] = *Submesh;

		mGeometries[geo->Name] = geo;
	}
}

void RenderingSystem::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.4f, 0.4f, 0.4f, 1.0f };

	// Main pass stored in index 2
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void RenderingSystem::UpdateCamera(const GameTimer& gt)
{
	//Update ViewFrustum
	BoundingFrustum::CreateFromMatrix(ViewFrustum, mCamera.GetProj());
	XMMATRIX invView = XMMatrixInverse(nullptr, mCamera.GetView());
	ViewFrustum.Transform(ViewFrustum, invView);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> RenderingSystem::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}