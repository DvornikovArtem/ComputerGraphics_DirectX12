// RenderingSystem.cpp

#include <Engine/Render/RenderingSystem.h>
#include <Engine/RHI/DX12/stb_image.h>
#include <shobjidl.h>


RenderingSystem::~RenderingSystem()
{
	if (mOctTree) delete mOctTree;

	if (terrainRenderer) delete terrainRenderer;

	if (mDebugDrawer) delete mDebugDrawer;

	for (auto& pair : mGeometries) delete pair.second;

	for (auto& pair : mMaterials) delete pair.second;

	for (auto& pair : mTextures) delete pair.second;

	for (auto& light : mAllLights) delete light;

	for (auto& ri : mAllRitems) delete ri;

	for (auto& i : terrainDrawableObjects) delete i;

	for (auto& i : mAllTerrainRitems) delete i;

	for (auto& layer : mRitemLayer) layer.clear();

	for (auto& ps : mAllParticleSystems) delete ps;

	mFrameResources.clear();

	if (mFFXContext && mFSREnabled) ffxDestroyContext(&mFFXContext, nullptr);

	UninstallDebugOutputHooks();
}


void RenderingSystem::Initialize(HWND mhMainWnd, HINSTANCE mhAppInst, GameTimer* gt) {
#if defined(DEBUG) || defined(_DEBUG) 
	// Enable the D3D12 debug layer.
	ComPtr<ID3D12Debug> debugController;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
	debugController->EnableDebugLayer();
#endif

	InstallDebugOutputHooks();

	this->mhMainWnd = mhMainWnd;
	this->mhAppInst = mhAppInst;
	this->gt = gt;

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

	// Try to create hardware device.
	HRESULT hardwareResult = D3D12CreateDevice(/*default adapter*/nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&md3dDevice));

	// Fallback to WARP device.
	if (FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(pWarpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&md3dDevice)));
	}

	mDirectStorage.Initialize(md3dDevice.Get());

	IDXGIAdapter* currentAdapter;
	LUID deviceLuid = md3dDevice->GetAdapterLuid();
	mdxgiFactory->EnumAdapterByLuid(deviceLuid, IID_PPV_ARGS(&currentAdapter));
	DXGI_ADAPTER_DESC adapterDesc;
	currentAdapter->GetDesc(&adapterDesc);
	mAdapterName = adapterDesc.Description;
	OutputDebugStringA("\n\n");
	OutputDebugStringW(adapterDesc.Description);
	OutputDebugStringA("\n\n");

	//check RT support
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	HRESULT hr = md3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5,
		&options5,
		sizeof(options5));

	if (SUCCEEDED(hr) && options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) RTSupport = true;
	else RTSupport = false;

	//check Highest Supported Shader Model
	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel;
	D3D_SHADER_MODEL testModels[] = {
		D3D_SHADER_MODEL_6_8,
		D3D_SHADER_MODEL_6_7,
		D3D_SHADER_MODEL_6_6,
		D3D_SHADER_MODEL_6_5,
		D3D_SHADER_MODEL_6_4,
		D3D_SHADER_MODEL_6_3,
		D3D_SHADER_MODEL_6_2,
		D3D_SHADER_MODEL_6_1,
		D3D_SHADER_MODEL_6_0,
		D3D_SHADER_MODEL_5_1 };

	for (auto model : testModels)
	{
		shaderModel.HighestShaderModel = model;
		if (SUCCEEDED(md3dDevice->CheckFeatureSupport(
			D3D12_FEATURE_SHADER_MODEL,
			&shaderModel,
			sizeof(shaderModel))))
		{
			MaxSupportedShaderModel = model;
			break;
		}
	}

	ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	#ifdef _DEBUG
	LogAdapters();
	#endif

	CreateCommandObjects();
	CreateSwapChain();
	BuildFSRContext();
	InitializeDXC();

	mGBuffer = std::make_unique<Gbuffer>(mClientWidth, mClientHeight, md3dDevice);

	// For Debug System =========================================================
	mDebugDrawer = new gfw::DebugRenderSysImpl(md3dDevice);

	mDebugDrawer->SetCamera(&mCamera);
	// ==========================================================================

	OnResize();

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	/*mParticleSystem = std::make_unique<ParticleSystem>(
		md3dDevice.Get(),
		mCommandList.Get(),
		5000);*/

		// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
		// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	BuildRootSignatures();
	BuildInputLayout();
	BuildBasicGeometry();
	BuildSceneGrid();
	//BuildTerrain();


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
	CreateOrResizeSceneColor(mClientWidth, mClientHeight);

	mGBuffer->Channel0SRVHeapIndex = static_cast<int>(TexDescsLength + MPRTextures.size() + MPRTerrainTextures.size() + 1);
	for (int i = 0; i < mGBuffer->ChannelPTRs.size(); i++) mGBuffer->ChannelPTRs[i]->SRVHeapIndex = mGBuffer->Channel0SRVHeapIndex + i;

	//copy GBuffer SRVs into main SRVHeap
	md3dDevice->CopyDescriptorsSimple(mGBuffer->NumBuffers, GetCpuSrv(mGBuffer->Channel0SRVHeapIndex),
		mGBuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	int k = 0;
	const int shadowBase =
		static_cast<int>(TexDescsLength)
		+ static_cast<int>(MPRTextures.size())
		+ static_cast<int>(MPRTerrainTextures.size())
		+ 1
		+ mGBuffer->NumBuffers;
	for (auto& litem : mAllLights) {
		litem->shadowMap->BuildDescriptors(GetCpuSrv(shadowBase + k), GetGpuSrv(shadowBase + k), GetDsv(1 + k));
		litem->shadowMap->SRVHeapIndex = shadowBase + k;
		k++;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mBackBufferFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	if (mFSREnabled)
	{
		md3dDevice->CreateShaderResourceView(mFSROutput.Get(), &srvDesc,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mFSROutputSRVHeapIndex, mCbvSrvDescriptorSize));
	}
	if (mTAAEnabled)
	{
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		md3dDevice->CreateShaderResourceView(mPrevFrameTex.Get(), &srvDesc,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mPrevFrameSRVHeapIndex, mCbvSrvDescriptorSize));
		md3dDevice->CreateShaderResourceView(mTAAResolvedAccBuffer.Get(), &srvDesc,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mResolvedAccBufferSRVHeapIndex, mCbvSrvDescriptorSize));

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		rtvDesc.Texture2D.PlaneSlice = 0;
		rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		md3dDevice->CreateRenderTargetView(mTAAResolvedAccBuffer.Get(), &rtvDesc, 
			CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), mResolvedAccBufferRTVHeapIndex, mRtvDescriptorSize));
	}


	BuildFrameResources();
	if (RTSupport)
	{
		BuildBLASForGeometries();
		BuildTLAS();
	}

	mGbufferImguiSlots.resize(mGBuffer->NumBuffers);
	for (int i = 0; i < mGBuffer->NumBuffers; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE dummyCPU{};
		D3D12_GPU_DESCRIPTOR_HANDLE slotGPU{};
		mImGui->AllocSrv(dummyCPU, slotGPU);
		mGbufferImguiSlots[i] = slotGPU;
	}

	auto gbCPU = mGBuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	UINT stride = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	for (int i = 0; i < mGBuffer->NumBuffers && i < (int)mGbufferImguiSlots.size(); ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE src = gbCPU;
		src.ptr += SIZE_T(i) * stride;
		mImGui->CopySrvIntoSlot(mGbufferImguiSlots[i], src);
	}

	for (ParticleSystem* particleSystem : mAllParticleSystems)
	{
		particleSystem->SetResources(mGBuffer->DepthStencils.Resource, mGBuffer->Normal.Resource);
	}
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
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));

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

	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
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

	if (mRtvHeap) CreateOrResizeSceneColor(mClientWidth, mClientHeight);

	mScissorRect = { 0, 0, mClientWidth, mClientHeight };

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 100000.0f);

	if (mFSREnabled)
	{
		//Ask FFX_API for render resolution
		if (mFFXContext) ffxDestroyContext(&mFFXContext, nullptr);
		BuildFSRContext();

		ffxQueryDescUpscaleGetRenderResolutionFromQualityMode queryDesc = {};
		queryDesc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
		queryDesc.displayHeight = mClientHeight;
		queryDesc.displayWidth = mClientWidth;
		queryDesc.qualityMode = mFSRQualityMode;
		queryDesc.pOutRenderHeight = &mRecommendedRenderResolutionY;
		queryDesc.pOutRenderWidth = &mRecommendedRenderResolutionX;
		ffxQuery(&mFFXContext, &queryDesc.header);

		//Resize FSROutput && MotionVector textures
		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = mBackBufferFormat;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(mBackBufferFormat, mClientWidth, mClientHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			D3D12_RESOURCE_STATE_COMMON,
			&clearValue,
			IID_PPV_ARGS(&mFSROutput));

		mDownscaledScreenViewport.TopLeftX = 0;
		mDownscaledScreenViewport.TopLeftY = 0;
		mDownscaledScreenViewport.Width = static_cast<float>(mRecommendedRenderResolutionX);
		mDownscaledScreenViewport.Height = static_cast<float>(mRecommendedRenderResolutionY);
		mDownscaledScreenViewport.MinDepth = 0.0f;
		mDownscaledScreenViewport.MaxDepth = 1.0f;

		mDownscaledScissorRect = { 0, 0, (long)mRecommendedRenderResolutionX, (long)mRecommendedRenderResolutionY };
	}

	mGBuffer->Resize(mFSREnabled ? mRecommendedRenderResolutionX : mClientWidth,
		mFSREnabled ? mRecommendedRenderResolutionY : mClientHeight);

	if (mTAAEnabled)
	{
		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, mFSREnabled ? mRecommendedRenderResolutionX : mClientWidth, mFSREnabled ? mRecommendedRenderResolutionY : mClientHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			D3D12_RESOURCE_STATE_COMMON,
			&clearValue,
			IID_PPV_ARGS(&mPrevFrameTex));

		md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, mFSREnabled ? mRecommendedRenderResolutionX : mClientWidth, mFSREnabled ? mRecommendedRenderResolutionY : mClientHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			D3D12_RESOURCE_STATE_COMMON,
			&clearValue,
			IID_PPV_ARGS(&mTAAResolvedAccBuffer));

		if (mRtvHeap)
		{
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;
			rtvDesc.Texture2D.PlaneSlice = 0;
			rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			md3dDevice->CreateRenderTargetView(mTAAResolvedAccBuffer.Get(), &rtvDesc, 
				CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), mResolvedAccBufferRTVHeapIndex, mRtvDescriptorSize));
		}

		mJitterIndex = 0;
		mCamera.ResetJitter();
	}

	if (mSrvDescriptorHeap)
	{
		md3dDevice->CopyDescriptorsSimple(mGBuffer->NumBuffers, GetCpuSrv(mGBuffer->Channel0SRVHeapIndex),
			mGBuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = mBackBufferFormat;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		if (mFSREnabled)
		{
			md3dDevice->CreateShaderResourceView(mFSROutput.Get(), &srvDesc,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mFSROutputSRVHeapIndex, mCbvSrvDescriptorSize));
		}
		if (mTAAEnabled)
		{
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			md3dDevice->CreateShaderResourceView(mPrevFrameTex.Get(), &srvDesc,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mPrevFrameSRVHeapIndex, mCbvSrvDescriptorSize));
			md3dDevice->CreateShaderResourceView(mTAAResolvedAccBuffer.Get(), &srvDesc,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mResolvedAccBufferSRVHeapIndex, mCbvSrvDescriptorSize));
		}
	}

	if (!mGbufferImguiSlots.empty()) {
		auto gbCPU = mGBuffer->m_SRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		const UINT stride = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		for (int i = 0; i < mGBuffer->NumBuffers && i < (int)mGbufferImguiSlots.size(); ++i) {
			D3D12_CPU_DESCRIPTOR_HANDLE src = gbCPU;
			src.ptr += SIZE_T(i) * stride;
			mImGui->CopySrvIntoSlot(mGbufferImguiSlots[i], src);
		}
	}

	for (ParticleSystem* particleSystem : mAllParticleSystems)
	{
		particleSystem->SetResources(mGBuffer->DepthStencils.Resource, mGBuffer->Normal.Resource);
	}
}

std::string RenderingSystem::GetShaderTargetForModel(const std::string& shaderType)
{
	UINT modelMajor = (MaxSupportedShaderModel >> 4) & 0xF;
	UINT modelMinor = MaxSupportedShaderModel & 0xF;

	std::string targetVersion;

	switch (modelMajor) {
	case 6:
		if (modelMinor >= 8 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_8) {
			targetVersion = "6_8";
		}
		else if (modelMinor >= 7 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_7) {
			targetVersion = "6_7";
		}
		else if (modelMinor >= 6 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_6) {
			targetVersion = "6_6";
		}
		else if (modelMinor >= 5 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_5) {
			targetVersion = "6_5";
		}
		else if (modelMinor >= 4 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_4) {
			targetVersion = "6_4";
		}
		else if (modelMinor >= 3 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_3) {
			targetVersion = "6_3";
		}
		else if (modelMinor >= 2 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_2) {
			targetVersion = "6_2";
		}
		else if (modelMinor >= 1 && MaxSupportedShaderModel >= D3D_SHADER_MODEL_6_1) {
			targetVersion = "6_1";
		}
		else {
			targetVersion = "6_0";
		}
		break;
	case 5:
		targetVersion = "5_1";
		break;
	default:
		targetVersion = "5_1";
		break;
	}

	return shaderType + "_" + targetVersion;
}

void RenderingSystem::CreateOrResizeSceneColor(int width, int height)
{
	mSceneColor.Reset();

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mBackBufferFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clear = {};
	clear.Format = texDesc.Format;
	clear.Color[0] = ClearValue.x;
	clear.Color[1] = ClearValue.y;
	clear.Color[2] = ClearValue.z;
	clear.Color[3] = ClearValue.w;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&clear,
		IID_PPV_ARGS(&mSceneColor)));

	auto rtvStart = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = rtvStart;
	sceneRTV.ptr += (mRtvDescriptorSize * SwapChainBufferCount);
	md3dDevice->CreateRenderTargetView(mSceneColor.Get(), nullptr, sceneRTV);
	mSceneColorRTV = sceneRTV;

	//mSceneColorSRV = mImGui->CreateTextureSRV(mSceneColor.Get(), mBackBufferFormat);
	mSceneColorSRV = mImGui->CreateOrOverwriteTextureSRV(mSceneColor.Get(), mBackBufferFormat, mSceneColorSRV);
}

void RenderingSystem::Render()
{
	PreRender();

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

	if (mTAAEnabled) SaveFrameAsPrevious();

	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "Clear Back Buffer");
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSceneColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

	mCommandList->ClearRenderTargetView(mSceneColorRTV, reinterpret_cast<FLOAT*>(&ClearValue), 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &mSceneColorRTV, FALSE, &DepthStencilView());
	PIXEndEvent(mCommandList.Get());

	DrawShadowMaps();

	mGBuffer->TransitCommonToRTV(mCommandList);
	mGBuffer->Clear(mCommandList);
	GBufferGeometryPass();

	mGBuffer->TransitToLightsRenderingState(mCommandList);
	GBufferLightPass();

	DrawSkyBox();

	DrawParticleSystems();

	if (mTAAEnabled) TAAResolve();

	if (mFSREnabled) FSRUpscale();

	mGBuffer->TransitToTonemappingState(mCommandList);
	PostProcessingPass();

	if (mShowBounds && mOctTree) mOctTree->Draw(mDebugDrawer);

	// Draw debug primitives
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "UI&Debug Pass");
	mDebugDrawer->Draw(mCommandQueue, mCommandList, &mScreenViewport, &mScissorRect, this, mCurrFrameResourceIndex);
	mDebugDrawer->Clear();
	//DrawSceneGrid();
	mGBuffer->TransitSRVToCommon(mCommandList);
	DrawUI();
	PIXEndEvent(mCommandList.Get());

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(mVSync ? 1u : 0u, mVSync ? 0 : DXGI_PRESENT_ALLOW_TEARING));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Notify the fence when the GPU completes commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}


inline static std::wstring WOpenIniDialog(HWND owner)
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool needUninit = SUCCEEDED(hr);

	std::wstring result;

	IFileOpenDialog* pDlg = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))))
	{
		DWORD opts = 0;
		if (SUCCEEDED(pDlg->GetOptions(&opts)))
			pDlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

		COMDLG_FILTERSPEC types[] = {
			{ L"ImGui Layout (*.ini)", L"*.ini" },
			{ L"All Files (*.*)",      L"*.*"   }
		};
		pDlg->SetFileTypes(ARRAYSIZE(types), types);
		pDlg->SetFileTypeIndex(1);
		pDlg->SetDefaultExtension(L"ini");

		if (SUCCEEDED(pDlg->Show(owner)))
		{
			IShellItem* pItem = nullptr;
			if (SUCCEEDED(pDlg->GetResult(&pItem)))
			{
				PWSTR path = nullptr;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				{
					result = path;
					CoTaskMemFree(path);
				}
				pItem->Release();
			}
		}
		pDlg->Release();
	}

	if (needUninit) CoUninitialize();
	return result;
}

inline static std::wstring WSaveIniDialog(HWND owner)
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool needUninit = SUCCEEDED(hr);

	std::wstring result;

	IFileSaveDialog* pDlg = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))))
	{
		DWORD opts = 0;
		if (SUCCEEDED(pDlg->GetOptions(&opts)))
			pDlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT);

		COMDLG_FILTERSPEC types[] = {
			{ L"ImGui Layout (*.ini)", L"*.ini" },
			{ L"All Files (*.*)",      L"*.*"   }
		};
		pDlg->SetFileTypes(ARRAYSIZE(types), types);
		pDlg->SetFileTypeIndex(1);
		pDlg->SetDefaultExtension(L"ini");

		if (SUCCEEDED(pDlg->Show(owner)))
		{
			IShellItem* pItem = nullptr;
			if (SUCCEEDED(pDlg->GetResult(&pItem)))
			{
				PWSTR path = nullptr;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				{
					result = path;
					CoTaskMemFree(path);
				}
				pItem->Release();
			}
		}
		pDlg->Release();
	}

	if (needUninit) CoUninitialize();
	return result;
}


void RenderingSystem::RegisterScenePanels() {
	using Engine::UI::PanelEntry;
	using Engine::UI::UILayerKind;

	// Dockspace - the earliest one (order = -1000)
	{
		PanelEntry p{};
		p.id = "MainDockspaceHost";
		p.draw = [] {
			ImGuiWindowFlags f = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
			const ImGuiViewport* vp = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(vp->Pos);
			ImGui::SetNextWindowSize(vp->Size);
			ImGui::SetNextWindowViewport(vp->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::Begin("MainDockspaceHost", nullptr, f);
			ImGui::PopStyleVar(3);
			ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0, 0), 0);
			ImGui::End();
			};
		p.visible = true;
		p.order = -1000;
		p.layer = UILayerKind::Editor;
		p.tags = { "core","dock" };
		mPanelRegistry.register_panel(std::move(p));
	}


	// Scene View
	{
		Engine::UI::PanelEntry p{};
		p.id = "Scene View";
		p.draw = [this] {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			if (ImGui::Begin("Scene View")) {
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
				ImGui::BeginChild("##scene_img", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

				ImVec2 avail = ImGui::GetContentRegionAvail();
				ImTextureID texId = (ImTextureID)mSceneColorSRV.ptr;

				// COVER
				float texW = (float)mClientWidth;
				float texH = (float)mClientHeight;
				float r_tex = texW / texH;
				float r_av = (avail.y > 0.0f) ? (avail.x / avail.y) : r_tex;

				float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
				if (r_av > r_tex) {
					float newH = avail.x / r_tex;
					float excess = (newH - avail.y) / newH;
					float cut = 0.5f * excess; v0 = cut; v1 = 1.0f - cut;
				}
				else if (r_av < r_tex) {
					float newW = avail.y * r_tex;
					float excess = (newW - avail.x) / newW;
					float cut = 0.5f * excess; u0 = cut; u1 = 1.0f - cut;
				}

				ImGui::Image(texId, avail, ImVec2(u0, v0), ImVec2(u1, v1));

				mSceneImgRectMin = ImGui::GetItemRectMin();
				mSceneImgRectMax = ImGui::GetItemRectMax();
				mSceneViewportForImg = ImGui::GetWindowViewport();

				mSceneUI.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
				mSceneUI.focused = ImGui::IsWindowFocused();
				mSceneUI.rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);

				if (mSceneUI.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) BeginMouseLook();
				if (IsMouseLookActive() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) EndMouseLook();

				ImGui::EndChild();
				ImGui::PopStyleVar(1);
			}
			ImGui::End();
			ImGui::PopStyleVar(1);
			};
		p.visible = true;
		p.order = 0;
		p.layer = Engine::UI::UILayerKind::Editor;
		p.tags = { "scene" };
		mPanelRegistry.register_panel(std::move(p));
	}


	// Info panel
	{
		Engine::UI::PanelEntry p{};
		p.id = "Info";
		p.draw = [this] {
			if (ImGui::Begin("Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
				ImGui::Text("Frame: %.3f ms", (ImGui::GetIO().Framerate > 0.f) ? 1000.0f / ImGui::GetIO().Framerate : 0.0f);
				ImGui::Separator();
				ImGui::Text("Render Resolution: %dx%d", mFSREnabled ? mRecommendedRenderResolutionX : mClientWidth, mFSREnabled ? mRecommendedRenderResolutionY : mClientHeight);
				ImGui::Text("Viewport Resolution: %dx%d", mClientWidth, mClientHeight);
				ImGui::Text("UI Clipped Resolution: %dx%d", (int)(mSceneImgRectMax.x - mSceneImgRectMin.x), (int)(mSceneImgRectMax.y - mSceneImgRectMin.y));
				ImGui::Text("RayTracing Support: %s", RTSupport ? "ACTIVE" : "INACTIVE");
				ImGui::Text("Max Supported Shader Model: %d.%d", (MaxSupportedShaderModel >> 4) & 0xF, MaxSupportedShaderModel & 0xF);
				ImGui::Text("GPU: %ws", mAdapterName.c_str());
			}
			ImGui::End();
			};
		p.visible = true;
		p.order = -10;
		p.layer = Engine::UI::UILayerKind::Editor;
		p.tags = { "info", "perf" };
		mPanelRegistry.register_panel(std::move(p));
	}


	// Settings panel
	{
		Engine::UI::PanelEntry p{};
		p.id = "Settings";
		p.draw = [this] {
			if (ImGui::Begin("Settings")) {
				ImGui::BeginChild("##scroll_right", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);


				// UI | Docking & Layout				
				ImGui::SeparatorText("UI / Docking & Layout");
				static char iniPath[260] = "imgui.ini";
				if (ImGui::Button("Load Layout")) {
					std::wstring wpath = WOpenIniDialog(mhMainWnd);
					if (!wpath.empty()) {
						int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
						std::string utf8(len - 1, '\0');
						WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, utf8.data(), len, nullptr, nullptr);

						mImGui->queue_load_layout(utf8);

						std::snprintf(iniPath, IM_ARRAYSIZE(iniPath), "%s", utf8.c_str());
						Engine::UI::DebugConsole::Get().Info("%s", std::string(("Loaded layout from: ") + utf8).c_str());
					}
				}

				ImGui::SameLine();
				if (ImGui::Button("Save Layout")) {
					std::wstring wpath = WSaveIniDialog(mhMainWnd);
					if (!wpath.empty()) {
						int len = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
						std::string utf8(len - 1, '\0');
						WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, utf8.data(), len, nullptr, nullptr);

						mImGui->queue_save_layout(utf8);

						std::snprintf(iniPath, IM_ARRAYSIZE(iniPath), "%s", utf8.c_str());
						Engine::UI::DebugConsole::Get().Info("%s", std::string(("Saved layout to: ") + utf8).c_str());
					}
				}


				// Camera
				ImGui::SeparatorText("Camera");
				ImGui::SliderFloat("Camera speed", mCamera.GetMoveSpeedPtr(), mCamera.GetMinMoveSpeed(), mCamera.GetMaxMoveSpeed(), "%.2f");


				// PostProcess
				ImGui::SeparatorText("PostProcess");
				ImGui::SliderFloat("Exposure", &mPostEffectsExposure, 0.f, 10.0f, "%.3f");


				// Render
				ImGui::SeparatorText("Render");
				bool vSync = GetVSync();
				if (ImGui::Checkbox("VSync", &vSync)) {
					SetVSync(vSync);
					Engine::UI::DebugConsole::Get().Info("%s", std::string(("VSync: ") + std::string(vSync ? "On" : "Off")).c_str());
				}

				bool wire = GetWireframe();
				if (ImGui::Checkbox("Wireframe", &wire)) {
					SetWireframe(wire);
					Engine::UI::DebugConsole::Get().Info("%s", std::string(("Wireframe: ") + std::string(wire ? "On" : "Off")).c_str());
				}

				bool showBounds = GetShowBounds();
				if (ImGui::Checkbox("Show Bounds", &showBounds)) {
					SetShowBounds(showBounds);
					Engine::UI::DebugConsole::Get().Info("%s", std::string(("Show Bounds: ") + std::string(showBounds ? "On" : "Off")).c_str());
				}

				if (ImGui::Checkbox("TAA Enabled", &mTAAEnabledDisplayValue)) mTAASwitchFlag = true;
				if (ImGui::Checkbox("FSR Enabled", &mFSREnabledDisplayValue)) mFSRSwitchFlag = true;

				const char* modeNames[] = { "Native", "Quality", "Balanced", "Performance", "Ultra Performance"};
				const int FSRQualityModesCount = 5;
				
				if (ImGui::BeginCombo("FSR Quality Mode", modeNames[(int)mFSRQualityMode]))
				{
					for (int i = 0; i < FSRQualityModesCount; i++)
					{
						bool isSelected = ((int)mFSRQualityMode == i);
						if (ImGui::Selectable(modeNames[i], isSelected))
						{
							mFSRQualityMode = (FfxApiUpscaleQualityMode)i;
							mFSRSwitchFlag = true;
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				if (ImGui::Button("Print Debug Text")) {
					Engine::UI::DebugConsole::Get().Info("%s", "It is information");
					Engine::UI::DebugConsole::Get().Warn("%s", "It is warning");
					Engine::UI::DebugConsole::Get().Error("%s", "It is error");
					Engine::UI::DebugConsole::Get().Info("%s", "It is a really LOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOONG text");
				}


				// Lights
				const char* TypeToName[] = { "Directional Light", "Point Light", "Spot Light" };
				ImGui::SeparatorText("Lights");
				for (size_t i = 0; i < mAllLights.size(); ++i) {
					auto* L = mAllLights[i];
					ImGui::PushID((int)i);
					ImGui::Text("Light %d: %s", (int)i, TypeToName[(int)*(&L->LightType)]);
					ImGui::SliderFloat3("Pos", (float*)&L->WorldLocation, -200.f, 200.f);
					ImGui::SliderFloat3("Dir", (float*)&L->WorldDirection, -3.14f, 3.14f);
					ImGui::ColorEdit3("Color", (float*)&L->Color);
					ImGui::SliderFloat("Strength", &L->Strength, 0.0f, 50.0f);
					ImGui::Separator();
					ImGui::PopID();
				}

				
				// Resources
				ImGui::SeparatorText("Resources");
				if (ImGui::TreeNode("Geometries")) {
					for (auto& [name, geo] : mGeometries) ImGui::BulletText("%s", name.c_str());
					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Materials")) {
					for (auto& [name, mat] : mMaterials) ImGui::BulletText("%s", name.c_str());
					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Textures")) {
					for (auto& [name, tex] : mTextures) ImGui::BulletText("%s", name.c_str());
					ImGui::TreePop();
				}


				ImGui::EndChild();
			}
			ImGui::End();
			};
		p.visible = true;
		p.order = 10;
		p.layer = Engine::UI::UILayerKind::Editor;
		p.tags = { "settings" };
		mPanelRegistry.register_panel(std::move(p));
	}


	// GBuffer View panel
	Engine::UI::PanelEntry p{};
	p.id = "G-Buffer Viewer";
	p.draw = [this]
		{
			if (!mGBuffer) return;

			static int rows = 2;
			static int cols = 4;
			ImGui::Begin("G-Buffer Viewer Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
			ImGui::SliderInt("Rows", &rows, 1, 8);
			ImGui::SliderInt("Cols", &cols, 1, 8);
			ImGui::End();

			const ImVec2 sceneSize(
				(std::max)(0.0f, mSceneImgRectMax.x - mSceneImgRectMin.x),
				(std::max)(0.0f, mSceneImgRectMax.y - mSceneImgRectMin.y)
			);

			if (sceneSize.x <= 0.0f || sceneSize.y <= 0.0f)
				return;

			const float texW = float(mClientWidth);
			const float texH = float(mClientHeight);
			const float r_tex = (texH > 0.0f) ? (texW / texH) : 1.0f;
			const float r_av = (sceneSize.y > 0.0f) ? (sceneSize.x / sceneSize.y) : r_tex;

			float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
			if (r_av > r_tex) {
				float newH = sceneSize.x / r_tex;
				float excess = (newH - sceneSize.y) / newH;
				float cut = 0.5f * excess; v0 = cut; v1 = 1.0f - cut;
			}
			else if (r_av < r_tex) {
				float newW = sceneSize.y * r_tex;
				float excess = (newW - sceneSize.x) / newW;
				float cut = 0.5f * excess; u0 = cut; u1 = 1.0f - cut;
			}

			std::vector<std::pair<ImTextureID, std::string>> textures;
			//textures.reserve(mGbuffer->NumBuffers);

			textures.push_back({ (ImTextureID)mGbufferImguiSlots[/*Diffuse*/0].ptr, "Diffuse" });
			textures.push_back({ (ImTextureID)mGbufferImguiSlots[/*Depth Stencils*/1].ptr, "Depth Stencils" });
			textures.push_back({ (ImTextureID)mGbufferImguiSlots[/*Normal*/2].ptr, "Normal" });
			textures.push_back({ (ImTextureID)mGbufferImguiSlots[/*Mat Fresnel/Rough*/3].ptr, "Mat Fresnel/Rough" });
			textures.push_back({ (ImTextureID)mGbufferImguiSlots[/*Accumulation*/4].ptr, "Accumulation" });
			textures.push_back({ (ImTextureID)mGbufferImguiSlots[/*VelocityBuffer*/5].ptr, "VelocityBuffer" });
			textures.push_back({ (ImTextureID)mGbufferImguiSlots[/*Outlines*/6].ptr, "Object Outlines" });

			mImGui->DrawTextureGridFixedSize_ImTexID("G-Buffer Viewer", textures, rows, cols, sceneSize, ImVec2(u0, v0), ImVec2(u1, v1), 0.0f, true);
		};
	p.visible = true;
	p.order = 5;
	p.layer = Engine::UI::UILayerKind::Editor;
	p.tags = { "debug", "gbuffer", "textures" };
	mPanelRegistry.register_panel(std::move(p));
}

float RenderingSystem::HaltonSequence(uint32_t index, uint32_t base)
{
	float f = 1.0f;
	float result = 0.0f;

	while (index > 0)
	{
		f /= static_cast<float>(base);
		result += f * static_cast<float>(index % base);
		index = static_cast<uint32_t>(floorf(static_cast<float>(index) / static_cast<float>(base)));
	}

	return result * 2;
}




void RenderingSystem::BeginMouseLook()
{
	if (mSceneUI.mouseLookActive) return;

	ImGuiViewport* vp = mSceneViewportForImg ? mSceneViewportForImg : ImGui::GetWindowViewport();
	HWND hwnd = (HWND)vp->PlatformHandleRaw;
	if (!hwnd) return;
	mSceneUI.mouseLookHwnd = hwnd;

	GetCursorPos(&mSceneUI.savedCursorPos);

	RECT r{};
	r.left = (LONG)std::floor(mSceneImgRectMin.x);
	r.top = (LONG)std::floor(mSceneImgRectMin.y);
	r.right = (LONG)std::ceil(mSceneImgRectMax.x);
	r.bottom = (LONG)std::ceil(mSceneImgRectMax.y);
	mSceneUI.lockRect = r;

	// The center of the scene texture
	mSceneUI.lockCenterPos.x = (mSceneUI.lockRect.left + mSceneUI.lockRect.right) / 2;
	mSceneUI.lockCenterPos.y = (mSceneUI.lockRect.top + mSceneUI.lockRect.bottom) / 2;

	ClipCursor(&mSceneUI.lockRect);
	SetCapture(hwnd);
	ShowCursor(FALSE);

	SetCursorPos(mSceneUI.lockCenterPos.x, mSceneUI.lockCenterPos.y);

	ImGuiIO& io = ImGui::GetIO();
	io.MousePos = ImVec2((float)mSceneUI.lockCenterPos.x, (float)mSceneUI.lockCenterPos.y);
	io.MouseDelta = ImVec2(0, 0);
	ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);

	mSceneUI.mouseLookActive = true;
	mSceneUI.skipFrames = 1;
}

void RenderingSystem::UpdateMouseLook()
{
	if (!mSceneUI.mouseLookActive) return;

	SetCursorPos(mSceneUI.lockCenterPos.x, mSceneUI.lockCenterPos.y);

	ImGuiIO& io = ImGui::GetIO();
	io.MousePos = ImVec2((float)mSceneUI.lockCenterPos.x, (float)mSceneUI.lockCenterPos.y);

	if (mSceneUI.skipFrames > 0) {
		io.MouseDelta = ImVec2(0, 0);
		--mSceneUI.skipFrames;
	}
}

void RenderingSystem::EndMouseLook()
{
	if (!mSceneUI.mouseLookActive) return;

	ClipCursor(nullptr);
	ReleaseCapture();
	ShowCursor(TRUE);
	SetCursorPos(mSceneUI.savedCursorPos.x, mSceneUI.savedCursorPos.y);

	mSceneUI.mouseLookHwnd = nullptr;
	mSceneUI.mouseLookActive = false;
	mSceneUI.skipFrames = 0;
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
		t->numLODs = static_cast<UINT>(t->Geo->DrawArgs.size() - 1);

		t->renderLayer = i->renderLayer;
		t->drawableObject = i;

		mRitemLayer[(int)i->renderLayer].push_back(t);
		mAllRitems.push_back(t);

		i->renderItem = t;

		k++;

	}

	if (terrainRenderer) terrainRenderer->Quad().ForEachNode([&](TerrainNode& n)
		{
			const TerrainTile& t = n.tile;

			std::string materialName = "tile_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy);
			std::string geoName = "TerrainTile";

			DrawableObject* terrainTile = new DrawableObject();
			terrainTile->Name = materialName;
			terrainTile->GeometryName = geoName;
			terrainTile->MaterialName = materialName;
			terrainTile->renderLayer = RenderLayer::Landscape;
			terrainTile->WorldLocation = XMFLOAT3(0.f, 0.f, 0.f);
			terrainTile->Scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
			terrainTile->TexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);

			const float sx = t.worldRect.sizeX;
			const float sz = t.worldRect.sizeZ;
			const float cx = t.worldRect.x0 + sx * 0.5f;
			const float cz = t.worldRect.z0 + sz * 0.5f;

			terrainTile->WorldLocation = XMFLOAT3(cx, 0.0f, cz);
			terrainTile->Scale = XMFLOAT3(sx, 0.0f, sz);



			auto* ri = new RenderItem();
			ri->ObjCBIndex = k;
			ri->Mat = mMaterials[materialName];
			ri->Geo = mGeometries[geoName];
			ri->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			ri->IndexCount = ri->Geo->DrawArgs["LOD0"].IndexCount;
			ri->StartIndexLocation = ri->Geo->DrawArgs["LOD0"].StartIndexLocation;
			ri->BaseVertexLocation = ri->Geo->DrawArgs["LOD0"].BaseVertexLocation;

			XMStoreFloat4x4(&ri->World, XMMatrixScaling(sx, 1.0f, sz) * XMMatrixTranslation(cx, 0.0f, cz));
			XMStoreFloat4x4(&ri->TexTransform, XMMatrixScaling(1.0f, -1.0f, 1.0f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));

			float u0 = t.worldRect.x0 / terrainRenderer->Meta().worldSizeX;
			float v0 = t.worldRect.z0 / terrainRenderer->Meta().worldSizeZ;
			float u1 = (t.worldRect.x0 + t.worldRect.sizeX) / terrainRenderer->Meta().worldSizeX;
			float v1 = (t.worldRect.z0 + t.worldRect.sizeZ) / terrainRenderer->Meta().worldSizeZ;

			ri->bounds = t.bounds;

			ri->currentLOD = 0;
			ri->numLODs = static_cast<UINT>(ri->Geo->DrawArgs.size() - 1);

			ri->renderLayer = terrainTile->renderLayer;
			ri->drawableObject = terrainTile;

			mRitemLayer[(int)terrainTile->renderLayer].push_back(ri);
			mAllRitems.push_back(ri);

			terrainTile->renderItem = ri;
			n.terrainTileItem = ri;

			terrainDrawableObjects.push_back(terrainTile);

			k++;
		});




	//generate OctTree
	OctTreeDesc octTreeDesc;
	octTreeDesc.ritems = &mAllRitems;
	octTreeDesc.titemLOD0 = terrainRenderer ? terrainRenderer->Quad().Find(0, 0, 0)->terrainTileItem : nullptr;
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

void RenderingSystem::BuildParticleSystems(std::unordered_map<std::string, ParticleSystemDescriptor> ParticleSystemDescriptors)
{
	FlushCommandQueue();
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	UINT k = 0;
	for (auto& pair : ParticleSystemDescriptors)
	{
		auto& particleSystemName = pair.first;
		auto& particleSystemDescriptor = pair.second;

		particleSystemDescriptor.emitComputeShader = mShaders[particleSystemDescriptor.emitComputeShaderName];
		particleSystemDescriptor.simulateComputeShader = mShaders[particleSystemDescriptor.simulateComputeShaderName];
		particleSystemDescriptor.CBIndex = k;
		particleSystemDescriptor.InputLayout = mInputLayout;

		ParticleSystem* particleSystem = new ParticleSystem();

		//need to add warning if geometry not found later
		particleSystem->SetGeometry(mGeometries[particleSystemDescriptor.particleGeometryName]);

		particleSystem->Initialize(particleSystemDescriptor);
		particleSystem->Build(md3dDevice, mCommandList);



		mAllParticleSystems.push_back(particleSystem);

		k++;
	}

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();
}


void RenderingSystem::BuildTerrain()
{
	TerrainRendererDesc terrainRendererDesc;
	terrainRendererDesc.terrainName = L"Mountains";
	//terrainRendererDesc.pathToDiffuseMap = SOLUTION_DIR L"assets/textures/terrain/mountains_png_8bit/mountains_DiffuseMap.png";
	//terrainRendererDesc.pathToHeightMap = SOLUTION_DIR L"assets/textures/terrain/mountains_png_8bit/mountains_HeightMap.png";
	//terrainRendererDesc.pathToNormalMap = SOLUTION_DIR L"assets/textures/terrain/mountains_png_8bit/mountains_NormalMap.png";
	//terrainRendererDesc.pathToDiffuseMap = SOLUTION_DIR L"assets/textures/terrain/rugged_terrain_png_8bit/ruggedTerrain_DiffuseMap.png";
	//terrainRendererDesc.pathToHeightMap = SOLUTION_DIR L"assets/textures/terrain/rugged_terrain_png_8bit/ruggedTerrain_HeightMap.png";
	terrainRendererDesc.pathToDiffuseMap = SOLUTION_DIR L"assets/textures/terrain/mountain_8K_png_8bit/mountain_8K_DiffuseMap.png";
	terrainRendererDesc.pathToHeightMap = SOLUTION_DIR L"assets/textures/terrain/mountain_8K_png_8bit/mountain_8K_HeightMap.png";
	terrainRendererDesc.pathToNormalMap;
	terrainRendererDesc.quadTreeLevels = 6;
	terrainRendererDesc.heightMapScale = 3500.0f;
	terrainRendererDesc.enableWireFrame = false;
	terrainRendererDesc.skipTileReimportIfPresent = true;
	terrainRendererDesc.generateHeightWithPerlin = true;
	terrainRendererDesc.perlinSeed = 42;
	terrainRendererDesc.perlinFrequency = 0.00015f;
	terrainRendererDesc.perlinOctaves = 6;
	terrainRendererDesc.perlinPersistence = 0.5f;
	terrainRendererDesc.perlinLacunarity = 2.0f;
	terrainRendererDesc.perlinOffsetX = 0.f;
	terrainRendererDesc.perlinOffsetZ = 0.f;

	terrainRenderer = new TerrainRenderer();
	terrainRenderer->Initialize(terrainRendererDesc);


	// Create geometry for a single quadtree tile as a grid with 6 LODs 
	const std::vector<std::pair<std::string, UINT>> lodMeshes = {
		{"LOD0", 32},
		/*{"LOD1", 128},
		{"LOD2", 64},
		{"LOD3", 32},
		{"LOD4", 16},
		{"LOD5", 8},*/
	};

	GeometryGenerator geoGen;

	auto geo = new MeshGeometry;
	geo->Name = "TerrainTile";

	std::vector<Vertex> vertices;
	std::vector<std::int32_t> indices;


	for (const auto& pair : lodMeshes)
	{
		const std::string& lodName = pair.first;
		const UINT vertsPerSide = pair.second;

		auto mesh = geoGen.CreateGrid(1.0f, 1.0f, vertsPerSide, vertsPerSide);

		UINT baseVertexLocation = (UINT)vertices.size();
		UINT startIndexLocation = (UINT)indices.size();


		vertices.reserve(vertices.size() + mesh.Vertices.size());
		for (const auto& mv : mesh.Vertices)
		{
			Vertex v;
			v.Pos = mv.Position;
			//v.Normal = XMFLOAT3(0, 1, 0);
			v.Normal = mv.Normal;
			v.TexC = mv.TexC;
			//v.Tangent = XMFLOAT3(1, 0, 0);
			v.Tangent = mv.TangentU;
			vertices.push_back(v);
		}

		indices.reserve(indices.size() + mesh.Indices32.size());
		for (uint32_t idx : mesh.Indices32) indices.push_back(idx + baseVertexLocation);


		SubmeshGeometry sub;
		sub.IndexCount = (UINT)mesh.Indices32.size();
		sub.StartIndexLocation = startIndexLocation;
		sub.BaseVertexLocation = 0;

		std::vector<XMFLOAT3> positions; positions.reserve(vertices.size());
		for (auto& v : mesh.Vertices) positions.push_back(v.Position);
		BoundingBox::CreateFromPoints(sub.Bounds, (UINT)positions.size(), positions.data(), sizeof(XMFLOAT3));

		geo->DrawArgs[lodName] = sub;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

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



	if (terrainRenderer) terrainRenderer->ForEachTile([&](const TerrainTile& t)
		{
			MPRTerrainTextures.push_back(TextureDesc("tile_diffuse_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy), t.textures.diffusePath, TextureDesc::Texture2D, true));
			MPRTerrainTextures.push_back(TextureDesc("tile_normal_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy), t.textures.normalPath, TextureDesc::Texture2D, false));
			MPRTerrainTextures.push_back(TextureDesc("tile_height_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy), t.textures.heightPath, TextureDesc::Texture2D, false));


			MaterialDesc Tile;

			Tile.Name = "tile_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy);
			Tile.VertexShaderName = "TerrainVS";
			Tile.GeometryShaderName = "TerrainGS";
			Tile.PixelShaderName = "TerrainPS";

			Tile.DiffuseTexName = "tile_diffuse_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy);
			Tile.NormalMapName = "tile_normal_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy);
			Tile.HeightMapName = "tile_height_level" + std::to_string(t.lod) + "_" + std::to_string(t.ix) + "_" + std::to_string(t.iy);

			Tile.UseTesselation = false;
			Tile.bWireframe = terrainRendererDesc.enableWireFrame;

			Tile.DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			Tile.FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
			Tile.Roughness = 0.5f;
			Tile.Metallic = 0.f;

			TerrainMaterialDescs.push_back(Tile);
		});


	//terrainRenderer->BuildGeometry();
}

void RenderingSystem::BuildSceneGrid()
{
	mShaders["SceneGridVS"] = d3dUtil::CompileShader(
		SHADERS_ENGINE_DIR L"\\SceneGrid.hlsl", nullptr, "SceneGridVS", "vs_5_1");
	mShaders["SceneGridPS"] = d3dUtil::CompileShader(
		SHADERS_ENGINE_DIR L"\\SceneGrid.hlsl", nullptr, "SceneGridPS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = RootSignatures["PostProcessing"].Get();

	psoDesc.VS = {
		reinterpret_cast<BYTE*>(mShaders["SceneGridVS"]->GetBufferPointer()),
		mShaders["SceneGridVS"]->GetBufferSize()
	};
	psoDesc.PS = {
		reinterpret_cast<BYTE*>(mShaders["SceneGridPS"]->GetBufferPointer()),
		mShaders["SceneGridPS"]->GetBufferSize()
	};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	psoDesc.DSVFormat = mDepthStencilFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&GlobalPSOs["SceneGrid"])));
}

void RenderingSystem::DrawSceneGrid()
{
	mCommandList->OMSetRenderTargets(1, &mGBuffer->Accumulation.RTV, false, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(RootSignatures["PostProcessing"].Get());

	ID3D12DescriptorHeap* heaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

	mCommandList->SetPipelineState(GlobalPSOs["SceneGrid"].Get());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	auto passCB = mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(0, passCB);

	//mCommandList->DrawInstanced(6, 1, 0, 0);
	mCommandList->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::BuildFSRContext()
{
	ffxCreateBackendDX12Desc backendDesc = {};
	backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
	backendDesc.device = md3dDevice.Get();

	ffxCreateContextDescUpscale upscaleDesc = {};
	upscaleDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	upscaleDesc.header.pNext = &backendDesc.header;
	upscaleDesc.maxRenderSize = { static_cast<uint32_t>(mClientWidth), static_cast<uint32_t>(mClientHeight) };
	upscaleDesc.maxUpscaleSize = { static_cast<uint32_t>(mClientWidth), static_cast<uint32_t>(mClientHeight) };
	upscaleDesc.flags = FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
	upscaleDesc.fpMessage = [](uint32_t type, const wchar_t* message)
		{
			std::wstring wideMessage(message);
			std::string narrowMessage(wideMessage.begin(), wideMessage.end());

			std::string prefix;
			switch (type) {
			case FFX_API_MESSAGE_TYPE_ERROR:
				prefix = "[FFX ERROR] ";
				break;
			case FFX_API_MESSAGE_TYPE_WARNING:
				prefix = "[FFX WARNING] ";
				break;
			default:
				prefix = "[FFX DEBUG] ";
				break;
			}

			std::string fullMessage = prefix + narrowMessage + "\n";
			OutputDebugStringA(fullMessage.c_str());
		};

	ffxReturnCode_t errorCode = ffxCreateContext(&mFFXContext, &upscaleDesc.header, nullptr);
	if (errorCode != FFX_API_RETURN_OK)
	{
		std::string errorMsg = "ERROR: FSR3 CONTEXT NOT CREATED\n";
		OutputDebugStringA(errorMsg.c_str());
	}
}

void RenderingSystem::FSRUpscale()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "FSR Upscaling");
	ffxDispatchDescUpscale dispatchDesc;

	dispatchDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
	dispatchDesc.commandList = mCommandList.Get();
	dispatchDesc.color = ffxApiGetResourceDX12(mTAAEnabled ? mTAAResolvedAccBuffer.Get() : mGBuffer->Accumulation.Resource.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchDesc.depth = ffxApiGetResourceDX12(mDepthStencilBuffer.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchDesc.motionVectors = ffxApiGetResourceDX12(mGBuffer->VelocityBuffer.Resource.Get(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatchDesc.renderSize = { (UINT)mRecommendedRenderResolutionX, (UINT)mRecommendedRenderResolutionY };    // Resolution before upscaling
	dispatchDesc.motionVectorScale = { 1.f, 1.f };
	dispatchDesc.upscaleSize = { (UINT)mClientWidth, (UINT)mClientHeight };
	dispatchDesc.cameraNear = mCamera.GetNearZ();
	dispatchDesc.cameraFar = mCamera.GetFarZ();
	dispatchDesc.cameraFovAngleVertical = mCamera.GetFovY();

	dispatchDesc.jitterOffset.x = mTAAEnabled ? -mJitterX : 0;
	dispatchDesc.jitterOffset.y = mTAAEnabled ? -mJitterY : 0;

	dispatchDesc.enableSharpening = false;
	dispatchDesc.sharpness = 0.8f; // 0.0 - 1.0
	dispatchDesc.frameTimeDelta = gt->DeltaTime() * 1000.f; //expects milliseconds
	dispatchDesc.reset = false; // set to true if camera teleports or moves not smoothly

	dispatchDesc.preExposure = 1.f;
	dispatchDesc.viewSpaceToMetersFactor = 1.f;

	dispatchDesc.output = ffxApiGetResourceDX12(mFSROutput.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
	//not sure if RTV is even needed here

	dispatchDesc.exposure = ffxApiGetResourceDX12(nullptr, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchDesc.reactive = ffxApiGetResourceDX12(nullptr, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchDesc.transparencyAndComposition = ffxApiGetResourceDX12(nullptr, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	//dispatchDesc.flags = FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;


	ffxReturnCode_t dispatchError = ffxDispatch(&mFFXContext, &dispatchDesc.header);

	if (dispatchError != FFX_API_RETURN_OK)
	{
		std::string errorMsg = "FSR DISPATCH ERROR: " + std::to_string(dispatchError) + " \n";
		OutputDebugStringA(errorMsg.c_str());
	}
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::DrawUI()
{
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mSceneColor.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), reinterpret_cast<FLOAT*>(&ClearValue), 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), FALSE, &DepthStencilView());


	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);


	for (Engine::UI::UILayerKind activeLayer : mActiveUILayers) mPanelRegistry.draw_layer(activeLayer);
	mImGui->DrawBuiltins();

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), FALSE, &DepthStencilView());


	mImGui->RenderDrawData(mCommandList.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mSceneColor.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COMMON));


	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
}

void RenderingSystem::PreRender()
{
	//If something needs to be done before rendering(), do it here
	if (mFSRSwitchFlag)
	{
		mFSRSwitchFlag = false;
		mFSREnabled = mFSREnabledDisplayValue;
		OnResize();
	}
	if (mTAASwitchFlag)
	{
		mTAASwitchFlag = false;
		mTAAEnabled = mTAAEnabledDisplayValue;
		OnResize();

		if (!mTAAEnabledDisplayValue)
		{
			mJitterX = mJitterY = 0;
			mMainPassCB.PrevCameraJitter = { 0, 0 };
		}
	}
}

void RenderingSystem::SaveFrameAsPrevious()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "Saving Previous Frame");
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mPrevFrameTex.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_DEST));
	
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mTAAResolvedAccBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_SOURCE));

	mCommandList->CopyResource(mPrevFrameTex.Get(), mTAAResolvedAccBuffer.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mPrevFrameTex.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_COMMON));

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mTAAResolvedAccBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_COMMON));
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::CalculateJitter()
{
	mMainPassCB.PrevCameraJitter = { mJitterX, mJitterY };

	mJitterIndex++;

	//int32_t jitterPhaseCount;
	//ffxQueryDescUpscaleGetJitterPhaseCount getJitterPhaseDesc;
	//getJitterPhaseDesc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
	//getJitterPhaseDesc.displayWidth = mFSREnabled ? mRecommendedRenderResolutionX : mClientWidth;
	//getJitterPhaseDesc.renderWidth = mClientWidth;
	//getJitterPhaseDesc.pOutPhaseCount = &jitterPhaseCount;

	//ffxQuery(&mFFXContext, &getJitterPhaseDesc.header);

	//ffxQueryDescUpscaleGetJitterOffset getJitterOffsetDesc{};
	//getJitterOffsetDesc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
	//getJitterOffsetDesc.index = mJitterIndex;
	//getJitterOffsetDesc.phaseCount = jitterPhaseCount;
	//getJitterOffsetDesc.pOutX = &mJitterX;
	//getJitterOffsetDesc.pOutY = &mJitterY;

	//ffxQuery(&mFFXContext, &getJitterOffsetDesc.header);

	mJitterX = HaltonSequence(mJitterIndex, 2) / (float)mClientWidth;
	mJitterY = HaltonSequence(mJitterIndex, 3) / (float)mClientHeight;
	mCamera.SetJitter(mJitterX, mJitterY);
}

void RenderingSystem::TAAResolve()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "TAA Resolve Pass");
	CD3DX12_RESOURCE_BARRIER barriers[3];
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		mGBuffer->Accumulation.Resource.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mTAAResolvedAccBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, 
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(mPrevFrameTex.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(3, barriers);

	mCommandList->SetGraphicsRootSignature(RootSignatures["TAAResolve"].Get());
	mCommandList->OMSetRenderTargets(1, &CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), mResolvedAccBufferRTVHeapIndex, mRtvDescriptorSize), 
		FALSE, &DepthStencilView());
	mCommandList->SetPipelineState(GlobalPSOs["TAAResolve"].Get());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	auto passCB = mCurrFrameResource->PassCB->Resource();

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());

	mCommandList->SetGraphicsRootDescriptorTable(1, GetGpuSrv(mGBuffer->Accumulation.SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(mPrevFrameSRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrv(mGBuffer->DepthStencils.SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(4, GetGpuSrv(mGBuffer->VelocityBuffer.SRVHeapIndex));

	mCommandList->DrawInstanced(6, 1, 0, 0);

	CD3DX12_RESOURCE_BARRIER antibarriers[3];
	antibarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		mGBuffer->Accumulation.Resource.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	antibarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(mTAAResolvedAccBuffer.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COMMON);
	antibarriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(mPrevFrameTex.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COMMON);
	mCommandList->ResourceBarrier(3, antibarriers);
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::BuildBLASForGeometries()
{
	if (!RTSupport) return;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	for (auto& geoPair : mGeometries) 
	{
		geoPair.second->BuildBLAS(md3dDevice.Get(), mCommandList, true);
	}

}

void RenderingSystem::BuildTLAS()
{
	std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> instances;

	for (RenderItem* ri : mAllRitems) {
		if (ri->Geo && ri->Geo->BLASResource) {
			DirectX::XMMATRIX worldMatrix = DirectX::XMLoadFloat4x4(&ri->World);
			instances.emplace_back(ri->Geo->BLASResource, worldMatrix);
		}
	}

	UINT InstanceCount = static_cast<UINT>(instances.size());

	if (InstanceCount == 0) {
		OutputDebugStringA("BuildTLAS: No instances found\n");
		return;
	}

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(InstanceCount);

	for (UINT i = 0; i < InstanceCount; ++i) {
		D3D12_RAYTRACING_INSTANCE_DESC& desc = instanceDescs[i];
		desc.InstanceID = i;
		desc.InstanceContributionToHitGroupIndex = 0;
		desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		desc.AccelerationStructure = instances[i].first->GetGPUVirtualAddress();

		DirectX::XMMATRIX worldMatrix = instances[i].second;
		DirectX::XMFLOAT3X4 transform3x4;
		DirectX::XMStoreFloat3x4(&transform3x4, worldMatrix);
		memcpy(desc.Transform, &transform3x4, sizeof(desc.Transform));

		desc.InstanceMask = 0xFF;
	}

	UINT instanceDescsSize = InstanceCount * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(instanceDescsSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(mInstanceDescsUploadResource.GetAddressOf())));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(instanceDescsSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(mInstanceDescsResource.GetAddressOf())));

	void* pData;
	ThrowIfFailed(mInstanceDescsUploadResource->Map(0, nullptr, &pData));
	memcpy(pData, instanceDescs.data(), instanceDescsSize);
	mInstanceDescsUploadResource->Unmap(0, nullptr);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mInstanceDescsResource.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_DEST));

	mCommandList->CopyResource(mInstanceDescsResource.Get(), mInstanceDescsUploadResource.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mInstanceDescsResource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.InstanceDescs = mInstanceDescsResource->GetGPUVirtualAddress();
	inputs.NumDescs = InstanceCount;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
		           D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
	md3dDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	auto tlasDesc = CD3DX12_RESOURCE_DESC::Buffer(
		prebuildInfo.ResultDataMaxSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&tlasDesc,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nullptr,
		IID_PPV_ARGS(&mTLASResource)
	));
	mTLASResource->SetName(L"TLAS_Resource");

	auto scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(
		prebuildInfo.ScratchDataSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&scratchDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&mTLASScratchResource)
	));
	mTLASScratchResource->SetName(L"TLAS_Scratch");

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = inputs;
	buildDesc.ScratchAccelerationStructureData = mTLASScratchResource->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = mTLASResource->GetGPUVirtualAddress();
	buildDesc.SourceAccelerationStructureData = 0;

	mCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(mTLASResource.Get());
	mCommandList->ResourceBarrier(1, &barrier);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();


	//building TLAS SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = mTLASResource->GetGPUVirtualAddress();

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mTLASSRVHeapIndex,
		mCbvSrvUavDescriptorSize);

	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
}

void RenderingSystem::RefitTLAS()
{
	//Might require FlushCommandQueue() call, but it eats fps. A lot.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	std::vector<std::pair<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> instances;

	for (RenderItem* ri : mAllRitems) {
		if (ri->Geo && ri->Geo->BLASResource) {
			DirectX::XMMATRIX worldMatrix = DirectX::XMLoadFloat4x4(&ri->World);
			instances.emplace_back(ri->Geo->BLASResource, worldMatrix);
		}
	}

	UINT InstanceCount = static_cast<UINT>(instances.size());

	if (InstanceCount == 0) {
		OutputDebugStringA("RebuildTLAS: No instances found\n");
		return;
	}

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(InstanceCount);

	for (UINT i = 0; i < InstanceCount; ++i) {
		D3D12_RAYTRACING_INSTANCE_DESC& desc = instanceDescs[i];
		desc.InstanceID = i;
		desc.InstanceContributionToHitGroupIndex = 0;
		desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		desc.AccelerationStructure = instances[i].first->GetGPUVirtualAddress();

		DirectX::XMMATRIX worldMatrix = instances[i].second;
		DirectX::XMFLOAT3X4 transform3x4;
		DirectX::XMStoreFloat3x4(&transform3x4, worldMatrix);
		memcpy(desc.Transform, &transform3x4, sizeof(desc.Transform));

		desc.InstanceMask = 0xFF;
	}

	UINT instanceDescsSize = InstanceCount * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

	void* pData;
	ThrowIfFailed(mInstanceDescsUploadResource->Map(0, nullptr, &pData));
	memcpy(pData, instanceDescs.data(), instanceDescsSize);
	mInstanceDescsUploadResource->Unmap(0, nullptr);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mInstanceDescsResource.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_COPY_DEST));

	mCommandList->CopyResource(mInstanceDescsResource.Get(), mInstanceDescsUploadResource.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		mInstanceDescsResource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ));

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	buildDesc.Inputs.InstanceDescs = mInstanceDescsResource->GetGPUVirtualAddress();
	buildDesc.Inputs.NumDescs = InstanceCount;
	buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

	buildDesc.ScratchAccelerationStructureData = mTLASScratchResource->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = mTLASResource->GetGPUVirtualAddress();
	buildDesc.SourceAccelerationStructureData = mTLASResource->GetGPUVirtualAddress();

	mCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(mTLASResource.Get());
	mCommandList->ResourceBarrier(1, &barrier);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
}

void RenderingSystem::InitializeDXC()
{
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&mDxcUtils)));
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&mDxcCompiler)));
	ThrowIfFailed(mDxcUtils->CreateDefaultIncludeHandler(&mDxcIncludeHandler));
}

ComPtr<ID3DBlob> RenderingSystem::DXCCompileShader(const std::wstring& filename, const D3D_SHADER_MACRO* defines, const std::string& entrypoint, const std::string& shaderType)
{
	std::string target = GetShaderTargetForModel(shaderType);

	//Fallback to older shader compiler
	if (MaxSupportedShaderModel == D3D_SHADER_MODEL_5_1)
	{
		return d3dUtil::CompileShader(filename, defines, entrypoint, target);
	}

	ComPtr<IDxcBlobEncoding> sourceBlob;
	ThrowIfFailed(mDxcUtils->LoadFile(filename.c_str(), nullptr, &sourceBlob));

	std::vector<LPCWSTR> arguments;
	std::vector<std::wstring> storage;

	arguments.push_back(L"-E");
	std::wstring entrypointW(entrypoint.begin(), entrypoint.end());
	arguments.push_back(entrypointW.c_str());

	arguments.push_back(L"-T");
	std::wstring targetW(target.begin(), target.end());
	arguments.push_back(targetW.c_str());

	arguments.push_back(L"-I");
	arguments.push_back(SHADERS_ENGINE_DIR);

	arguments.push_back(L"-I");
	arguments.push_back(L"./");

	arguments.push_back(L"-I");
	arguments.push_back(L"../");

	arguments.push_back(L"-Zi");    // Generate debug information

	//in case we need no optimization
	//arguments.push_back(L"-Qembed_debug");
	//arguments.push_back(L"-Od");

	arguments.push_back(L"-O3"); // max optimization otherwise

	if (defines)
	{
		const D3D_SHADER_MACRO* define = defines;
		while (define->Name && define->Definition)
		{
			std::string defineStr = std::string(define->Name) + "=" + define->Definition;
			arguments.push_back(L"-D");
			std::wstring defineWide(defineStr.begin(), defineStr.end());
			storage.push_back(defineWide);
			arguments.push_back(storage.back().c_str());

			define++;
		}
	}

	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
	sourceBuffer.Size = sourceBlob->GetBufferSize();
	sourceBuffer.Encoding = DXC_CP_UTF8;

	ComPtr<IDxcResult> results;
	HRESULT hr = mDxcCompiler->Compile(
		&sourceBuffer,
		arguments.data(),
		(UINT32)arguments.size(),
		mDxcIncludeHandler.Get(),
		IID_PPV_ARGS(&results));

	ComPtr<IDxcBlobUtf8> errors;
	if (SUCCEEDED(hr)) results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

	if (errors != nullptr && errors->GetStringLength() > 0)
	{
		//print a bunch of info if shader doesnt want to compile
		OutputDebugStringA("Shader compilation warnings/errors:\n");
		OutputDebugStringA(errors->GetStringPointer());

		if (errors->GetStringLength() > 0) {
			OutputDebugStringA("\n=== Shader Compilation Details ===\n");
			OutputDebugStringA(("Shader: " + std::string(filename.begin(), filename.end()) + "\n").c_str());
			OutputDebugStringA(("Entry point: " + entrypoint + "\n").c_str());
			OutputDebugStringA(("Target: " + std::string(target.begin(), target.end()) + "\n").c_str());
		}
	}

	ComPtr<IDxcBlobUtf16> outputName;
	HRESULT compileStatus;
	if (SUCCEEDED(results->GetStatus(&compileStatus)) && FAILED(compileStatus))
	{
		if (errors != nullptr && errors->GetStringLength() > 0)
		{
			OutputDebugStringA("Shader compilation failed:\n");
			OutputDebugStringA(errors->GetStringPointer());
		}
		ThrowIfFailed(compileStatus);
	}

	//save .pdb file for debugging
	ComPtr<IDxcBlob> pdbBlob;
	ComPtr<IDxcBlobWide> pdbName;
	results->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdbBlob), &pdbName);
	const wchar_t* pdbNameStr = pdbName->GetStringPointer();
	std::wstring pdbFilePath = std::wstring(SHADERS_ENGINE_DIR) + L"/PDBs/" + pdbNameStr;
	std::ofstream pdbFile(pdbFilePath, std::ios::binary);
	if (pdbFile.is_open())
	{
		pdbFile.write(static_cast<const char*>(pdbBlob->GetBufferPointer()),
			pdbBlob->GetBufferSize());
		pdbFile.close();
	}

	//IDxcBlob to ID3DBlob conversion because im lazy to convert everything to IDxcBlob
	ComPtr<IDxcBlob> dxcBlob;
	ThrowIfFailed(results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxcBlob), &outputName));

	ComPtr<ID3DBlob> d3dBlob;
	D3DCreateBlob(dxcBlob->GetBufferSize(), &d3dBlob);
	memcpy(d3dBlob->GetBufferPointer(), dxcBlob->GetBufferPointer(), dxcBlob->GetBufferSize());

	return d3dBlob;
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
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = mhMainWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	// Note: Swap chain uses queue to perform flush.
	ThrowIfFailed(mdxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, mSwapChain.GetAddressOf()));
}

void RenderingSystem::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 1 + 1;
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
	dsvHeapDesc.NumDescriptors = static_cast<UINT>(1 + mAllLights.size());
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

	CD3DX12_ROOT_PARAMETER lightPassSlotRootParameter[10];

	lightPassSlotRootParameter[0].InitAsConstantBufferView(0); //MainPassCB
	lightPassSlotRootParameter[1].InitAsConstantBufferView(1); //LightCB

	lightPassSlotRootParameter[2].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_ALL); //GBufferChannels
	lightPassSlotRootParameter[3].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_ALL);
	lightPassSlotRootParameter[4].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_ALL);
	lightPassSlotRootParameter[5].InitAsDescriptorTable(1, &texTable4, D3D12_SHADER_VISIBILITY_ALL);

	lightPassSlotRootParameter[6].InitAsDescriptorTable(1, &texTable5, D3D12_SHADER_VISIBILITY_ALL); //ShadowMap

	lightPassSlotRootParameter[7].InitAsDescriptorTable(1, &texTable6, D3D12_SHADER_VISIBILITY_ALL); //IBL SkyMaps
	lightPassSlotRootParameter[8].InitAsDescriptorTable(1, &texTable7, D3D12_SHADER_VISIBILITY_ALL);
	lightPassSlotRootParameter[9].InitAsDescriptorTable(1, &texTable8, D3D12_SHADER_VISIBILITY_ALL);

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

	CD3DX12_ROOT_SIGNATURE_DESC lightPassRootSigDesc(10, lightPassSlotRootParameter,
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

	CD3DX12_ROOT_PARAMETER PPSlotRootParameter[5];

	PPSlotRootParameter[0].InitAsConstantBufferView(0); //MainPassCB
	PPSlotRootParameter[1].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_ALL); //GBufferChannels
	PPSlotRootParameter[2].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_ALL);
	PPSlotRootParameter[3].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_ALL);
	PPSlotRootParameter[4].InitAsDescriptorTable(1, &texTable4, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC PPRootSigDesc(5, PPSlotRootParameter,
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

	CD3DX12_ROOT_PARAMETER TAASlotRootParameter[5];

	TAASlotRootParameter[0].InitAsConstantBufferView(0); //MainPassCB
	TAASlotRootParameter[1].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_ALL); //CurrFrame
	TAASlotRootParameter[2].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_ALL); //PrevFrame
	TAASlotRootParameter[3].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_ALL); //DepthMap
	TAASlotRootParameter[4].InitAsDescriptorTable(1, &texTable4, D3D12_SHADER_VISIBILITY_ALL); //MotionVectors

	CD3DX12_ROOT_SIGNATURE_DESC TAARootSigDesc(5, TAASlotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedTAArootSig = nullptr;
	ComPtr<ID3DBlob> TAAErrorBlob = nullptr;
	HRESULT TAAHr = D3D12SerializeRootSignature(&TAARootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedTAArootSig.GetAddressOf(), TAAErrorBlob.GetAddressOf());

	if (TAAErrorBlob != nullptr)
	{
		OutputDebugStringA((char*)TAAErrorBlob->GetBufferPointer());
	}
	ThrowIfFailed(TAAHr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedTAArootSig->GetBufferPointer(),
		serializedTAArootSig->GetBufferSize(),
		IID_PPV_ARGS(RootSignatures["TAAResolve"].GetAddressOf())));

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

	if (mTAAEnabled) CalculateJitter();
	//if (RTSupport && mAllObjectsToUpdate.size() != 0) RefitTLAS();
	UpdateCamera(*gt);
	UpdateRenderItems(mAllObjectsToUpdate);
	UpdateObjectCBs(*gt);
	UpdateMaterialCBs(*gt);
	UpdateMainPassCB(*gt);
	UpdateLightItems(mAllLightObjectsToUpdate);
	UpdateLightCBs(*gt);
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

			if (e->InitFrame)
			{
				XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(world));
				e->InitFrame = false;
			}
			else XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(XMLoadFloat4x4(&e->PrevWorld)));
			XMStoreFloat4x4(&e->PrevWorld, world);


			XMVECTOR diff = XMVectorSubtract(XMLoadFloat4x4(&e->World).r[3], mCamera.GetPosition());

			objConstants.TesselationFactor = 50 / XMVectorGetX(XMVector3Length(diff));

			if (terrainRenderer) objConstants.HeightMapScale = terrainRenderer->Meta().heightScale;

			objConstants.HasOutline = e->drawableObject->HasOutline ? 1.f : 0.f;
			objConstants.OutlineColor = e->drawableObject->OutlineColor;

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

	std::unordered_set<RenderItem*> alreadyCheckedRitemsforLights;
	for (auto& i : mAllLights)
	{
		i->VisibleRitems.clear();
		alreadyCheckedRitemsforLights.clear();
		std::vector<OctTreeNode*> leaves = mOctTree->GetAllNodesAtLevel(static_cast<int>(mOctTree->getNumDivisions() - 1));

		if (i->LightType != LightType::Pointlight)
		{
			for (auto& leaf : leaves) {
				if (i->LightFrustum.Contains(leaf->bounds) != DirectX::ContainmentType::DISJOINT) {
					for (RenderItem* ri : leaf->OverlappedRitems) {
						if (ri->renderLayer == RenderLayer::Landscape || alreadyCheckedRitemsforLights.find(ri) != alreadyCheckedRitemsforLights.end()) continue;
						alreadyCheckedRitemsforLights.insert(ri);
						if (i->LightFrustum.Intersects(ri->bounds)) i->VisibleRitems.push_back(ri);
					}
				}
			}
		}
		else
		{
			for (auto& leaf : leaves) {
				if (i->bounds.Contains(leaf->bounds) != DirectX::ContainmentType::DISJOINT) {
					for (RenderItem* ri : leaf->OverlappedRitems) {
						if (ri->renderLayer == RenderLayer::Landscape || alreadyCheckedRitemsforLights.find(ri) != alreadyCheckedRitemsforLights.end()) continue;
						alreadyCheckedRitemsforLights.insert(ri);
						if (i->bounds.Intersects(ri->bounds)) i->VisibleRitems.push_back(ri);
					}
				}
			}
		}

	}

	mAllLightObjectsToUpdate.clear();
}

void RenderingSystem::UpdateLightCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->LightCB.get();
	//float lightAngle;

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
				float SphereRadiuses[5] = { 10, 50, 150, 400, 1500 };

				//for each cascade
				for (int i = 0; i < 5; i++)
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
					if (i == 4)
					{
						BoundingFrustum::CreateFromMatrix(e->LightFrustum, lightProj);
						e->LightFrustum.Transform(e->LightFrustum, XMMatrixInverse(nullptr, lightView));
					}
				}
			}
			break;

			case LightType::Spotlight:
				lightPos = XMLoadFloat3(&e->WorldLocation);
				lightDir = XMLoadFloat3(&e->WorldDirection);
				targetPos = lightPos + lightDir;

				//ADD FOV CALCULATION
				lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
				lightProj = XMMatrixPerspectiveFovLH(XM_PI / 6, 1.0f, 10.f, e->FalloffEnd);

				S = lightView * lightProj * T;

				XMStoreFloat4x4(&LightConstants.View[0], XMMatrixTranspose(lightView));
				XMStoreFloat4x4(&LightConstants.Proj[0], XMMatrixTranspose(lightProj));
				XMStoreFloat4x4(&LightConstants.ShadowTransform[0], XMMatrixTranspose(S));
				BoundingFrustum::CreateFromMatrix(e->LightFrustum, lightProj);
				e->LightFrustum.Transform(e->LightFrustum, XMMatrixInverse(nullptr, lightView));
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

std::vector<MeshParsingResult> RenderingSystem::BuildMeshGeometry(std::string Name, const std::string& filename, MeshDesc::ImportType meshType)
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

	//const aiScene* scene = importer.ReadFile(filename, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_CalcTangentSpace);

	// Use DirectStorage to load mesh data into memory
	std::wstring wFilename(filename.begin(), filename.end());
	// A buffer into which the raw bytes of the file will be loaded
	std::vector<std::uint8_t> fileData;
	try
	{
		mDirectStorage.ReadFileToMemory(wFilename, fileData);
	}
	catch (const std::exception& e)
	{
		std::string err = "DirectStorage failed to load mesh: " + std::string(e.what());
		MessageBoxA(0, err.c_str(), "DirectStorage Error", 0);
		return res;
	}

	// Get extension for Assimp hint
	std::string extension = "";
	size_t dotPos = filename.find_last_of('.');
	if (dotPos != std::string::npos) {
		extension = filename.substr(dotPos);
	}

	const aiScene* scene = importer.ReadFileFromMemory(fileData.data(), fileData.size(),
		aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_CalcTangentSpace,
		extension.c_str());

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		MessageBoxW(0, L"Model not found.", 0, 0);
		return res;
	}

	auto geo = new MeshGeometry;
	geo->Name = Name;

	std::vector<Vertex> vertices;
	std::vector<std::int32_t> indices;
	XMFLOAT3 vMinSingle = { FLT_MAX, FLT_MAX, FLT_MAX };
	XMFLOAT3 vMaxSingle = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	UINT totalIndexCount = 0;

	switch (meshType)
	{
	case MeshDesc::ImportType::LODed:
	{
		std::vector<Vertex> lodVertices;
		std::vector<std::int32_t> lodIndices;

		for (unsigned int i = 0; i < scene->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[i];
			UINT baseVertexLocation = (UINT)lodVertices.size();
			UINT startIndexLocation = (UINT)lodIndices.size();

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
						aiTexture* embeddedTexture = nullptr;
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

				lodVertices.push_back(vertex);
			}

			for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
				const aiFace& face = mesh->mFaces[f];
				for (unsigned int k = 0; k < face.mNumIndices; ++k) {
					lodIndices.push_back(static_cast<std::int32_t>(face.mIndices[k] + baseVertexLocation));
				}
			}

			SubmeshGeometry submesh;
			submesh.IndexCount = (UINT)lodIndices.size() - startIndexLocation;
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

		const UINT vbByteSize = (UINT)lodVertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)lodIndices.size() * sizeof(std::int32_t);

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), lodVertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), lodIndices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), lodVertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), lodIndices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		break;
	}

	case MeshDesc::ImportType::SingleMesh:
	{
		for (unsigned int i = 0; i < scene->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[i];
			UINT baseVertexLocation = (UINT)vertices.size();
			UINT startIndexLocation = (UINT)indices.size();

			if (i == 0)
			{
				res[0].GeneratedMaterial.Name = Name + "_" + mesh->mName.C_Str();
				aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
				for (auto& texType : textureTypes) {
					if (material->GetTextureCount(texType) > 0)
					{
						aiString texturePath;
						aiTexture* embeddedTexture = nullptr;
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

				vMinSingle.x = std::min(vMinSingle.x, vertex.Pos.x);
				vMinSingle.y = std::min(vMinSingle.y, vertex.Pos.y);
				vMinSingle.z = std::min(vMinSingle.z, vertex.Pos.z);

				vMaxSingle.x = (((vMaxSingle.x) > (vertex.Pos.x)) ? (vMaxSingle.x) : (vertex.Pos.x));
				vMaxSingle.y = (((vMaxSingle.y) > (vertex.Pos.y)) ? (vMaxSingle.y) : (vertex.Pos.y));
				vMaxSingle.z = (((vMaxSingle.z) > (vertex.Pos.z)) ? (vMaxSingle.z) : (vertex.Pos.z));

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

			totalIndexCount += (UINT)indices.size() - startIndexLocation;
		}

		SubmeshGeometry submesh;
		submesh.IndexCount = totalIndexCount;  
		submesh.StartIndexLocation = 0;        
		submesh.BaseVertexLocation = 0;

		XMFLOAT3 center = {
		  0.5f * (vMinSingle.x + vMaxSingle.x),
		  0.5f * (vMinSingle.y + vMaxSingle.y),
		  0.5f * (vMinSingle.z + vMaxSingle.z)
		};
		XMFLOAT3 extents = {
		  0.5f * (vMaxSingle.x - vMinSingle.x),
		  0.5f * (vMaxSingle.y - vMinSingle.y),
		  0.5f * (vMaxSingle.z - vMinSingle.z)
		};

		BoundingBox box(center, extents);
		submesh.Bounds = box;

		geo->DrawArgs["LOD0"] = submesh;

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

		break;
	}
	}

	mGeometries[geo->Name] = geo;
	res[0].GeometryName = Name;
	return res;
}

std::vector<MeshParsingResult> RenderingSystem::LoadMesh(MeshDesc& meshDesc, bool GenerateMaterial)
{
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	std::vector<MeshParsingResult> res;

	res = BuildMeshGeometry(meshDesc.Name, meshDesc.Path, meshDesc.importType);

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

	if (mShaders.find(MDesc.GeometryShaderName) != mShaders.end())
	{
		descPipelineState.GS =
		{
			reinterpret_cast<BYTE*>(mShaders[MDesc.GeometryShaderName]->GetBufferPointer()),
			mShaders[MDesc.GeometryShaderName]->GetBufferSize()
		};
	}

	descPipelineState.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	descPipelineState.pRootSignature = RootSignatures["Default"].Get();
	descPipelineState.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	descPipelineState.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	descPipelineState.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	/*if (MDesc.bWireframe)
		descPipelineState.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	else
		descPipelineState.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;*/
	descPipelineState.SampleMask = UINT_MAX;
	descPipelineState.NumRenderTargets = 6;
	descPipelineState.RTVFormats[0] = mGBuffer->Diffuse.Format;
	descPipelineState.RTVFormats[1] = mGBuffer->DepthStencils.Format;
	descPipelineState.RTVFormats[2] = mGBuffer->Normal.Format;
	descPipelineState.RTVFormats[3] = mGBuffer->MatFresnelRoughness.Format;
	descPipelineState.RTVFormats[4] = mGBuffer->VelocityBuffer.Format;
	descPipelineState.RTVFormats[5] = mGBuffer->ObjectOutlines.Format;
	descPipelineState.DSVFormat = mDepthStencilFormat;
	descPipelineState.SampleDesc.Count = 1;

	//ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&descPipelineState, IID_PPV_ARGS(&mPSOs["GBufferGeometryPass"])));

	// SOLID
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC solid = descPipelineState;
		solid.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&solid, IID_PPV_ARGS(&mPSOs["GBufferGeometryPass"])));
	}

	// WIREFRAME
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC wire = descPipelineState;
		wire.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		// For better grid vision
		wire.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&wire, IID_PPV_ARGS(&mPSOs["GBufferGeometryPass_WireFrame"])));
	}

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
	ShadowMapPSODesc.RTVFormats[5] = DXGI_FORMAT_UNKNOWN;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ShadowMapPSODesc, IID_PPV_ARGS(&mPSOs["ShadowOpaque"])));

	ShadowMapPSODesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["ShadowOpaqueVS_Terrain"]->GetBufferPointer()),
		mShaders["ShadowOpaqueVS_Terrain"]->GetBufferSize()
	};
	ShadowMapPSODesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["ShadowOpaqueGS_Terrain"]->GetBufferPointer()),
		mShaders["ShadowOpaqueGS_Terrain"]->GetBufferSize()
	};
	ShadowMapPSODesc.PS = { nullptr, 0 };
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ShadowMapPSODesc, IID_PPV_ARGS(&mPSOs["ShadowOpaque_terrain"])));
}

void RenderingSystem::BuildGlobalPSOs()
{
	//
	// PSO for GBuffer Light Pass
	//


	D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredPsoDesc = {};
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
	//deferredPsoDesc.RTVFormats[0] = mBackBufferFormat;
	deferredPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
	//skyPsoDesc.RTVFormats[0] = mBackBufferFormat;
	skyPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	skyPsoDesc.SampleDesc.Count = 1;
	skyPsoDesc.SampleDesc.Quality = 0;
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

	PPPsoDesc.pRootSignature = RootSignatures["TAAResolve"].Get();
	PPPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	PPPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["TAAResolveVS"]->GetBufferPointer()),
		mShaders["TAAResolveVS"]->GetBufferSize()
	};
	PPPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["TAAResolvePS"]->GetBufferPointer()),
		mShaders["TAAResolvePS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&PPPsoDesc, IID_PPV_ARGS(&GlobalPSOs["TAAResolve"])));

}

void RenderingSystem::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			2, (UINT)(mAllRitems.size()), (UINT)mMaterials.size(), (UINT)mAllLights.size(), (UINT)mAllParticleSystems.size()));
	}
}

void RenderingSystem::BuildMaterials(std::vector<MaterialDesc>& MaterialDescs)
{
	MaterialDescs.insert(MaterialDescs.end(), TerrainMaterialDescs.begin(), TerrainMaterialDescs.end());

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
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "GBuffer Geometry Pass");
	mCommandList->RSSetViewports(1, mFSREnabled ? &mDownscaledScreenViewport : &mScreenViewport);
	mCommandList->RSSetScissorRects(1, mFSREnabled ? &mDownscaledScissorRect : &mScissorRect);
	mCommandList->SetGraphicsRootSignature(RootSignatures["Default"].Get());

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[6] = {
		mGBuffer->Diffuse.RTV,
		mGBuffer->DepthStencils.RTV,
		mGBuffer->Normal.RTV,
		mGBuffer->MatFresnelRoughness.RTV,
		mGBuffer->VelocityBuffer.RTV,
		mGBuffer->ObjectOutlines.RTV
	};

	mCommandList->OMSetRenderTargets(6, rtvs, false, &DepthStencilView());

	//mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);


	const std::string psoName = GetWireframe() ? "GBufferGeometryPass_WireFrame" : "GBufferGeometryPass";

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque], psoName);

	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Landscape], "GBufferGeometryPass");
	DrawRenderItems(mCommandList.Get(), mVisibleTerrainRitems, psoName);
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::GBufferLightPass()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "GBuffer Light Pass");
	mCommandList->SetGraphicsRootSignature(RootSignatures["DeferredLightPass"].Get());
	//mCommandList->OMSetRenderTargets(1, &mGbuffer->BloomRTV, false, &DepthStencilView());
	mCommandList->OMSetRenderTargets(1, &mGBuffer->Accumulation.RTV, false, &DepthStencilView());

	UINT lightCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(Light));
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	auto lightCB = mCurrFrameResource->LightCB->Resource();
	auto passCB = mCurrFrameResource->PassCB->Resource();

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(mGBuffer->Diffuse.SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrv(mGBuffer->DepthStencils.SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(4, GetGpuSrv(mGBuffer->Normal.SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(5, GetGpuSrv(mGBuffer->MatFresnelRoughness.SRVHeapIndex));
	//mCommandList->SetGraphicsRootDescriptorTable(6, GetGpuSrv(mGBuffer->MatFresnelRoughness.SRVHeapIndex));

	mCommandList->SetGraphicsRootDescriptorTable(7, GetGpuSrv(mTextures["SkyIrradiance"]->srvHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(8, GetGpuSrv(mTextures["SkyPref"]->srvHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(9, GetGpuSrv(mTextures["SkyBRDF"]->srvHeapIndex));

	mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// For each light item...
	for (size_t i = 0; i < mAllLights.size(); ++i)
	{
		auto& li = mAllLights[i];

		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + li->LightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(1, lightCBAddress);

		mCommandList->SetGraphicsRootDescriptorTable(6, GetGpuSrv(li->shadowMap->SRVHeapIndex));

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
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::DrawSkyBox()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "Sky Box Rendering");
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
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::DrawParticleSystems()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "Particle System Updating");
	for (ParticleSystem* particleSystem : mAllParticleSystems)
	{
		particleSystem->CameraPos = mCamera.GetPosition3f();
		particleSystem->CameraDir = mCamera.GetLook3f();
		particleSystem->Update(gt->DeltaTime(), mCurrFrameResource);
	}
	PIXEndEvent(mCommandList.Get());

	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "Particle System Rendering");
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	auto passCBAddress = mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();

	for (ParticleSystem* particleSystem : mAllParticleSystems)
	{
		CD3DX12_RESOURCE_BARRIER toSrv[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(particleSystem->GetParticlePool(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(particleSystem->GetAliveList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		mCommandList->ResourceBarrier(_countof(toSrv), toSrv);
		particleSystem->Draw(passCBAddress);

		CD3DX12_RESOURCE_BARRIER barriers[2] = {
			CD3DX12_RESOURCE_BARRIER::Transition(particleSystem->GetAliveList(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(particleSystem->GetParticlePool(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};
		mCommandList->ResourceBarrier(_countof(barriers), barriers);
	}
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::DrawShadowMaps()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "Shadow Map Pass");
	mCommandList->SetGraphicsRootSignature(RootSignatures["Default"].Get());
	for (auto& i : mAllLights)
	{
		PIXBeginEvent(mCommandList.Get(), 0x00FF00, i->Name.c_str());
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

		ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress = lightCB->GetGPUVirtualAddress() + i->LightCBIndex * lightCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(4, lightCBAddress);

		for (size_t j = 0; j < i->VisibleRitems.size(); ++j)
		{
			auto& ri = i->VisibleRitems[j];

			mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
			mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			mCommandList->SetPipelineState(ri->Mat->PSOs["ShadowOpaque"].Get());

			mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrv(ri->Mat->DiffuseSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(1, GetGpuSrv(ri->Mat->NormalSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(ri->Mat->HeightSrvHeapIndex));


			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

			mCommandList->SetGraphicsRootConstantBufferView(3, objCBAddress);
			mCommandList->SetGraphicsRootConstantBufferView(5, matCBAddress);


			std::string subMeshName = "LOD" + std::to_string(ri->currentLOD);

			UINT IndexCount = ri->Geo->DrawArgs[subMeshName].IndexCount;
			UINT StartIndexLocation = ri->Geo->DrawArgs[subMeshName].StartIndexLocation;
			UINT BaseVertexLocation = ri->Geo->DrawArgs[subMeshName].BaseVertexLocation;

			mCommandList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);

		}

		//checking intesction with biggest tile of terrain, draw it into shadow map if true
		if (terrainRenderer)
		{
			auto& Terrain = terrainRenderer->Quad().Find(0, 0, 0)->terrainTileItem;
			if (i->LightFrustum.Intersects(Terrain->bounds))
			{
				mCommandList->SetPipelineState(Terrain->Mat->PSOs["ShadowOpaque_terrain"].Get());
				mCommandList->IASetVertexBuffers(0, 1, &Terrain->Geo->VertexBufferView());
				mCommandList->IASetIndexBuffer(&Terrain->Geo->IndexBufferView());
				mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(Terrain->Mat->HeightSrvHeapIndex));

				D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + Terrain->ObjCBIndex * objCBByteSize;
				D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + Terrain->Mat->MatCBIndex * matCBByteSize;

				mCommandList->SetGraphicsRootConstantBufferView(3, objCBAddress);
				mCommandList->SetGraphicsRootConstantBufferView(5, matCBAddress);

				std::string subMeshName = "LOD" + std::to_string(Terrain->currentLOD);
				UINT IndexCount = Terrain->Geo->DrawArgs[subMeshName].IndexCount;
				UINT StartIndexLocation = Terrain->Geo->DrawArgs[subMeshName].StartIndexLocation;
				UINT BaseVertexLocation = Terrain->Geo->DrawArgs[subMeshName].BaseVertexLocation;

				mCommandList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
			}
		}

		// Change back to GENERIC_READ so we can read the texture in a shader.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(i->shadowMap->Resource(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
		PIXEndEvent(mCommandList.Get());
	}
	PIXEndEvent(mCommandList.Get());
}

void RenderingSystem::PostProcessingPass()
{
	PIXBeginEvent(mCommandList.Get(), 0x00FF00, "Post Processing Pass");
	if (mFSREnabled)
	{
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mFSROutput.Get(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);
	}
	mCommandList->SetGraphicsRootSignature(RootSignatures["PostProcessing"].Get());
	//mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), false, &DepthStencilView());
	mCommandList->OMSetRenderTargets(1, &mSceneColorRTV, FALSE, &DepthStencilView());
	mCommandList->SetPipelineState(GlobalPSOs["PostProcessing"].Get());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	auto passCB = mCurrFrameResource->PassCB->Resource();

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootConstantBufferView(0, passCB->GetGPUVirtualAddress());

	mCommandList->SetGraphicsRootDescriptorTable(1, GetGpuSrv(mFSREnabled ? mFSROutputSRVHeapIndex : (mTAAEnabled ? mResolvedAccBufferSRVHeapIndex :  mGBuffer->Accumulation.SRVHeapIndex)));
	mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrv(mGBuffer->DepthStencils.SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrv(mGBuffer->Normal.SRVHeapIndex));
	mCommandList->SetGraphicsRootDescriptorTable(4, GetGpuSrv(mGBuffer->ObjectOutlines.SRVHeapIndex));


	mCommandList->DrawInstanced(6, 1, 0, 0);
	if (mFSREnabled) mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mFSROutput.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
	PIXEndEvent(mCommandList.Get());
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

	TexDescsLength = static_cast<UINT>(TexDescs.size());

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	DirectX::ResourceUploadBatch upload(md3dDevice.Get());
	upload.Begin();

	auto invalidTex = new Texture;
	invalidTex->srvHeapIndex = 0;
	invalidTex->Name = "INVALID";
	invalidTex->Filename = L"assets/textures/INVALID.dds";

	//ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
	//	md3dDevice.Get(),
	//	upload,
	//	invalidTex->Filename.c_str(),
	//	invalidTex->Resource.GetAddressOf()));

	mDirectStorage.CreateDDSTextureFromFile_DS(upload, invalidTex->Filename.c_str(), invalidTex->Resource.GetAddressOf());

	mTextures[invalidTex->Name] = invalidTex;

	for (int i = 0; i < TexDescs.size(); i++)
	{
		auto t = new Texture;
		t->srvHeapIndex = i + 1;
		t->Name = TexDescs[i].Name;
		t->Filename = TexDescs[i].Path;

		//ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
		//	md3dDevice.Get(),
		//	upload,
		//	t->Filename.c_str(),
		//	t->Resource.GetAddressOf()));

		mDirectStorage.CreateDDSTextureFromFile_DS(upload, t->Filename.c_str(), t->Resource.GetAddressOf());

		mTextures[t->Name] = t;
	}

	for (int i = 0; i < MPRTextures.size(); i++)
	{
		auto t = MPRTextures[i];
		t->srvHeapIndex = static_cast<int>(TexDescs.size() + i + 1);

		mTextures[t->Name] = t;
	}

	for (int i = 0; i < MPRTerrainTextures.size(); i++)
	{
		auto t = new Texture;
		t->srvHeapIndex = static_cast<int>(TexDescs.size() + MPRTextures.size() + i + 1);
		t->Name = MPRTerrainTextures[i].Name;
		t->Filename = MPRTerrainTextures[i].Path;

		//ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
		//	md3dDevice.Get(),
		//	upload,
		//	t->Filename.c_str(),
		//	t->Resource.GetAddressOf()));

		mDirectStorage.CreateDDSTextureFromFile_DS(upload, t->Filename.c_str(), t->Resource.GetAddressOf());

		mTextures[t->Name] = t;
	}

	auto finish = upload.End(mCommandQueue.Get());
	finish.get();


	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 100000;
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
	SRVHeapHeadIndex++;

	for (TextureDesc& i : TexDescs) {
		auto it = mTextures.find(i.Name);
		if (it == mTextures.end()) {
			OutputDebugStringA(("Texture not found: " + i.Name + "\n").c_str());
			continue;
		}

		auto& tex = it->second->Resource;
		if (!tex) {
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
		SRVHeapHeadIndex++;
	}

	for (Texture* i : MPRTextures) {
		auto it = mTextures.find(i->Name);
		if (it == mTextures.end()) {
			OutputDebugStringA(("Texture not found: " + i->Name + "\n").c_str());
			continue;
		}

		auto& tex = it->second->Resource;
		if (!tex) {
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
		SRVHeapHeadIndex++;
	}

	for (TextureDesc& i : MPRTerrainTextures) {
		auto it = mTextures.find(i.Name);
		if (it == mTextures.end()) {
			OutputDebugStringA(("Texture not found: " + i.Name + "\n").c_str());
			continue;
		}

		auto& tex = it->second->Resource;
		if (!tex) {
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
		SRVHeapHeadIndex++;
	}

	for (int i = 0; i < mGBuffer->NumBuffers; i++) {
		md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		SRVHeapHeadIndex++;
	}

	for (int i = 0; i < mAllLights.size(); i++) {
		md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		SRVHeapHeadIndex++;
	}
	mFSROutputSRVHeapIndex = SRVHeapHeadIndex;
	SRVHeapHeadIndex++;
	mPrevFrameSRVHeapIndex = SRVHeapHeadIndex;
	SRVHeapHeadIndex++;
	mResolvedAccBufferSRVHeapIndex = SRVHeapHeadIndex;
	SRVHeapHeadIndex++;
	mTLASSRVHeapIndex = SRVHeapHeadIndex;
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
		
		if (texture->mHeight == 0)
			stbi_image_free(imageData);
		else
			delete[] imageData;

	}
}

std::unordered_set<RenderItem*> alreadyCheckedRitems;
void RenderingSystem::CollectVisibleRenderItems()
{
	std::vector<OctTreeNode*> leaves = mOctTree->GetAllNodesAtLevel(static_cast<int>(mOctTree->getNumDivisions() - 1));


	for (auto& leaf : leaves) {
		if (ViewFrustum.Contains(leaf->bounds) != DirectX::ContainmentType::DISJOINT) {
			for (RenderItem* ri : leaf->OverlappedRitems) {
				if (ri->renderLayer == RenderLayer::Landscape || alreadyCheckedRitems.find(ri) != alreadyCheckedRitems.end()) continue;
				alreadyCheckedRitems.insert(ri);
				ri->IsInViewFrustum = ViewFrustum.Intersects(ri->bounds);
				if (ri->IsInViewFrustum) mAllVisibleRitems.push_back(ri);
			}
		}
	}
}

void RenderingSystem::CollectVisibleLightItems()
{
	std::vector<OctTreeNode*> leaves = mOctTree->GetAllNodesAtLevel(static_cast<int>(mOctTree->getNumDivisions() - 1));

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


	mChosenTerrainRitems.clear();
	mVisibleTerrainRitems.clear();
	if (terrainRenderer) terrainRenderer->SelectLOD(mCamera, mChosenTerrainRitems, 3.0f);

	// Iterate over all OctTree leaves containing the current terrain tile,
	// and if at least one leaf is inside the frustum ?> render this tile
	for (auto* terrainTile : mChosenTerrainRitems)
	{
		for (auto leaf : terrainTile->occupiedLeaves)
		{
			if (ViewFrustum.Contains(leaf->bounds) != DirectX::ContainmentType::DISJOINT)
			{
				terrainTile->IsInViewFrustum = true;
				mVisibleTerrainRitems.push_back(terrainTile);
				break;
			}
		}

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
	for (ShaderDesc& i : ShaderDescs) mShaders[i.Name] = DXCCompileShader(i.Path, i.ShaderDefines, i.FunctionName, i.ShaderProfile);

	// Deferred Geometry Rendering
	mShaders["standardVS"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredGeometryPass.hlsl", nullptr, "VS", "vs");
	mShaders["standardPS"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredGeometryPass.hlsl", nullptr, "PS", "ps");
	mShaders["standardHS"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredGeometryPass.hlsl", nullptr, "HSMain", "hs");
	mShaders["standardDS"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredGeometryPass.hlsl", nullptr, "DSMain", "ds");

	// Deferred Light Rendering
	mShaders["DeferredLightPassVS_FSQuad"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredLightPass.hlsl", nullptr, "VS_FSQuad", "vs");
	mShaders["DeferredLightPassVS_Bounded"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredLightPass.hlsl", nullptr, "VS_Bounded", "vs");
	mShaders["DeferredLightPassPS"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredLightPass.hlsl", nullptr, "PS", "ps");
	mShaders["DeferredLightPassPS_AddAmbient"] = DXCCompileShader(SHADERS_ENGINE_DIR "DeferredLightPass.hlsl", nullptr, "PS_AddAmbient", "ps");

	// Skybox rendering
	mShaders["SkyBoxVS"] = DXCCompileShader(SHADERS_ENGINE_DIR "SkyBox.hlsl", nullptr, "VS", "vs");
	mShaders["SkyBoxPS"] = DXCCompileShader(SHADERS_ENGINE_DIR "SkyBox.hlsl", nullptr, "PS", "ps");

	// Shadowmapping
	mShaders["ShadowOpaqueVS"] = DXCCompileShader(SHADERS_ENGINE_DIR "Shadows.hlsl", nullptr, "VS", "vs");
	mShaders["ShadowOpaquePS"] = DXCCompileShader(SHADERS_ENGINE_DIR "Shadows.hlsl", nullptr, "PS", "ps");
	mShaders["ShadowOpaqueGS"] = DXCCompileShader(SHADERS_ENGINE_DIR "Shadows.hlsl", nullptr, "GS", "gs");

	mShaders["ShadowOpaqueVS_Terrain"] = DXCCompileShader(SHADERS_ENGINE_DIR "Shadows_Terrain.hlsl", nullptr, "VS", "vs");
	mShaders["ShadowOpaqueGS_Terrain"] = DXCCompileShader(SHADERS_ENGINE_DIR "Shadows_Terrain.hlsl", nullptr, "GS", "gs");

	// Post-Processing
	mShaders["PPVS"] = DXCCompileShader(SHADERS_ENGINE_DIR "PostProcessing.hlsl", nullptr, "VS_FSQuad", "vs");
	mShaders["PPPS"] = DXCCompileShader(SHADERS_ENGINE_DIR "PostProcessing.hlsl", nullptr, "PS", "ps");

	// TAA Resolve
	mShaders["TAAResolveVS"] = DXCCompileShader(SHADERS_ENGINE_DIR "TAAResolve.hlsl", nullptr, "VS_FSQuad", "vs");
	mShaders["TAAResolvePS"] = DXCCompileShader(SHADERS_ENGINE_DIR "TAAResolve.hlsl", nullptr, "PS", "ps");
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
	GeometryGenerator::MeshData TwoDCircle = geoGen.CreateCircle(0.5f, 16);
	GeometryGenerator::MeshData TwoDQuad = geoGen.CreateQuad(0.f, 0.f, 1.f, 1.f, 0.f);

	//std::vector<GeometryGenerator::MeshData*> Objects = { &box, &grid, &sphere, &cylinder, &cone, &sphere_lp, &TwoDCircle, &TwoDQuad };
	//std::vector<std::string> Names = { "Box", "Grid", "Sphere", "Cylinder", "Cone", "Sphere_LowPoly", "2DCircle", "2DQuad"};

	std::vector<GeometryGenerator::MeshData*> Objects = { &box, &grid, &cylinder, &cone, &sphere_lp, &TwoDCircle, &TwoDQuad };
	std::vector<std::string> Names = { "Box", "Grid", "Cylinder", "Cone", "Sphere_LowPoly", "2DCircle", "2DQuad" };

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

		auto geo = new MeshGeometry;
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



	const std::vector<std::pair<std::string, UINT>> lodMeshes = {
		{"LOD0", 40},
		{"LOD1", 32},
		{"LOD2", 24},
		{"LOD3", 16},
		{"LOD4", 12},
		{"LOD5", 8},
	};

	auto geo = new MeshGeometry;
	geo->Name = "Sphere";

	std::vector<Vertex> vertices;
	std::vector<std::int32_t> indices;


	for (const auto& pair : lodMeshes)
	{
		const std::string& lodName = pair.first;
		const UINT vertsPerSide = pair.second;

		auto mesh = geoGen.CreateSphere(1.f, vertsPerSide, vertsPerSide);

		UINT baseVertexLocation = (UINT)vertices.size();
		UINT startIndexLocation = (UINT)indices.size();


		vertices.reserve(vertices.size() + mesh.Vertices.size());
		for (const auto& mv : mesh.Vertices)
		{
			Vertex v;
			v.Pos = mv.Position;
			v.Normal = mv.Normal;
			v.TexC = mv.TexC;
			v.Tangent = mv.TangentU;
			vertices.push_back(v);
		}


		indices.reserve(indices.size() + mesh.Indices32.size());
		for (uint32_t idx : mesh.Indices32) indices.push_back(idx + baseVertexLocation);


		SubmeshGeometry sub;
		sub.IndexCount = (UINT)mesh.Indices32.size();
		sub.StartIndexLocation = startIndexLocation;
		sub.BaseVertexLocation = 0;

		std::vector<XMFLOAT3> positions; positions.reserve(vertices.size());
		for (auto& v : mesh.Vertices) positions.push_back(v.Position);
		BoundingBox::CreateFromPoints(sub.Bounds, (UINT)positions.size(), positions.data(), sizeof(XMFLOAT3));

		geo->DrawArgs[lodName] = sub;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

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
}

void RenderingSystem::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	XMMATRIX projNoJitter = mCamera.GetProjNoJitter();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX viewProjNoJitter = XMMatrixMultiply(view, projNoJitter);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	static XMMATRIX prevViewProj = viewProj;
	static XMMATRIX prevViewProjNoJitter = viewProjNoJitter;
	static XMFLOAT3 prevCameraPos = mCamera.GetPosition3f();

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.PrevViewProj, XMMatrixTranspose(prevViewProj));
	XMStoreFloat4x4(&mMainPassCB.PrevViewProjNoJitter, XMMatrixTranspose(prevViewProjNoJitter));
	XMStoreFloat4x4(&mMainPassCB.ViewProjNoJitter, XMMatrixTranspose(viewProjNoJitter));
	mMainPassCB.PrevCameraPos = prevCameraPos;
	mMainPassCB.CameraPos = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = mFSREnabled ? XMFLOAT2((float)mRecommendedRenderResolutionX, (float)mRecommendedRenderResolutionY) : XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / (mFSREnabled ? mRecommendedRenderResolutionX : mClientWidth), 1.0f / (mFSREnabled ? mRecommendedRenderResolutionY : mClientHeight));
	mMainPassCB.ViewportSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.NearZ = mCamera.GetNearZ();
	mMainPassCB.FarZ = mCamera.GetFarZ();
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.4f, 0.4f, 0.4f, 1.0f };
	mMainPassCB.CameraDirection = mCamera.GetLook3f();
	mMainPassCB.postEffectsExposure = mPostEffectsExposure;
	mMainPassCB.CameraJitter = { mJitterX, mJitterY };
	//PrevCameraJitter is set in CalculateJitter()

	prevViewProj = viewProj;
	prevViewProjNoJitter = viewProjNoJitter;
	prevCameraPos = mCamera.GetPosition3f();

	// Main pass stored in index 2
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void RenderingSystem::UpdateCamera(const GameTimer& gt)
{
	mCamera.UpdateProjMatrix();
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