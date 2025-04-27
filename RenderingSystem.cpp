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
	CreateRtvAndDsvDescriptorHeaps();

	mGbuffer = std::make_unique<Gbuffer>(mClientWidth, mClientHeight, md3dDevice);

	OnResize();

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	BuildRootSignature();
	BuildInputLayout();
	BuildBasicGeometry();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();
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

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
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
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

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
}

void RenderingSystem::Render()
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	/*mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));*/
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSwapChainBuffer[mCurrBackBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	//mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
	//mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	//mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Draw opaque items--floors, walls, skull.
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque], "opaque");

	//// Mark the visible mirror pixels in the stencil buffer with the value 1
	//mCommandList->OMSetStencilRef(1);
	//mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Mirrors]);

	//// Draw the reflection into the mirror only (only for pixels where the stencil buffer is 1).
	//// Note that we must supply a different per-pass constant buffer--one with the lights reflected.
	//mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
	//mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Reflected]);

	// Restore main pass constants and stencil ref.
	//mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	//mCommandList->OMSetStencilRef(0);

	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent], "transparent");

	//// Draw shadows
	//mCommandList->SetPipelineState(mPSOs["shadow"].Get());
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Shadow], "shadow");

	// Indicate a state transition on the resource usage.
	//mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
	//	D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
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

void RenderingSystem::BuildRenderItems(std::unordered_map<std::string, std::unique_ptr<DrawableObject>>& Objects)
{
	int k = 0;
	for (auto& pair : Objects)
	{
		auto i = pair.second.get();

		auto t = std::make_unique<RenderItem>();
		t->World = MathHelper::Identity4x4();
		XMStoreFloat4x4(&t->World, XMMatrixScaling(i->Scale.x, i->Scale.y, i->Scale.z) 
			* XMMatrixRotationRollPitchYaw(i->WorldRotation.z, i->WorldRotation.y, i->WorldRotation.x) 
			* XMMatrixTranslation(i->WorldLocation.x, i->WorldLocation.y, i->WorldLocation.z));
		t->TexTransform = MathHelper::Identity4x4();
		XMStoreFloat4x4(&t->TexTransform, i->TexTransform);
		t->ObjCBIndex = k;
		t->Mat = mMaterials[i->MaterialName].get();
		t->Geo = mGeometries[i->GeometryName].get();
		t->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		t->IndexCount = t->Geo->DrawArgs[i->GeometryName].IndexCount;
		t->StartIndexLocation = t->Geo->DrawArgs[i->GeometryName].StartIndexLocation;
		t->BaseVertexLocation = t->Geo->DrawArgs[i->GeometryName].BaseVertexLocation;

		mRitemLayer[i->RenderLayer].push_back(t.get());
		mAllRitems.push_back(std::move(t));
		
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

		LogOutputDisplayModes(output, mBackBufferFormat);

		ReleaseCom(output);

		++i;
	}
}

void RenderingSystem::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
{
	UINT count = 0;
	UINT flags = 0;

	// Call with nullptr to get list count.
	output->GetDisplayModeList(format, flags, &count, nullptr);

	std::vector<DXGI_MODE_DESC> modeList(count);
	output->GetDisplayModeList(format, flags, &count, &modeList[0]);

	for (auto& x : modeList)
	{
		UINT n = x.RefreshRate.Numerator;
		UINT d = x.RefreshRate.Denominator;
		std::wstring text =
			L"Width = " + std::to_wstring(x.Width) + L" " +
			L"Height = " + std::to_wstring(x.Height) + L" " +
			L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
			L"\n";

		::OutputDebugString(text.c_str());
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


	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void RenderingSystem::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
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
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));




	CD3DX12_DESCRIPTOR_RANGE lightPassTexTable;
	lightPassTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER lightPassSlotRootParameter[3];

	// Perfomance TIP: Order from most frequent to least frequent.
	lightPassSlotRootParameter[0].InitAsDescriptorTable(1, &lightPassTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	lightPassSlotRootParameter[1].InitAsConstantBufferView(0);
	lightPassSlotRootParameter[2].InitAsConstantBufferView(1);

	auto lightPassStaticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC lightPassRootSigDesc(3, lightPassSlotRootParameter,
		(UINT)lightPassStaticSamplers.size(), lightPassStaticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedLightPassRootSig = nullptr;
	ComPtr<ID3DBlob> lightPassErrorBlob = nullptr;
	HRESULT lightPassHr = D3D12SerializeRootSignature(&lightPassRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedLightPassRootSig.GetAddressOf(), lightPassErrorBlob.GetAddressOf());

	if (lightPassErrorBlob != nullptr)
	{
		::OutputDebugStringA((char*)lightPassErrorBlob->GetBufferPointer());
	}
	ThrowIfFailed(lightPassHr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedLightPassRootSig->GetBufferPointer(),
		serializedLightPassRootSig->GetBufferSize(),
		IID_PPV_ARGS(mLightPassRootSignature.GetAddressOf())));
}

void RenderingSystem::BuildDescriptorHeap(Material* t)
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 3;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&t->mSrvDescriptorHeap)));


	//Copying Descriptors from global SRVHeap

	D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptors[] = 
	{
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + t->DiffuseSrvHeapIndex * mCbvSrvDescriptorSize,
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + t->NormalSrvHeapIndex * mCbvSrvDescriptorSize,
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + t->HeightSrvHeapIndex * mCbvSrvDescriptorSize
	};

	D3D12_CPU_DESCRIPTOR_HANDLE destDescriptors[] =
	{
		t->mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr,
		t->mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + 1 * mCbvSrvDescriptorSize,
		t->mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + 2 * mCbvSrvDescriptorSize
	};

	md3dDevice->CopyDescriptors( 3, destDescriptors, nullptr, 3, srcDescriptors, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

}

void RenderingSystem::Update(std::unordered_map<std::string, std::unique_ptr<DrawableObject>>& mAllObjects)
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
	UpdateRenderItems(mAllObjects);
	UpdateObjectCBs(*gt);
	UpdateMaterialCBs(*gt);
	UpdateMainPassCB(*gt);
	UpdateReflectedPassCB(*gt);
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

void RenderingSystem::BuildMeshGeometry(std::string Name, const std::string & filename) {
	Assimp::Importer importer;

	// Загружаем сцену
	const aiScene* scene = importer.ReadFile(filename,
		aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_CalcTangentSpace);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		MessageBoxW(0, L"Model not found.", 0, 0);
		//std::cerr << "Ошибка загрузки файла: " << importer.GetErrorString() << std::endl;
		return;
	}

	std::vector<Vertex> vertices;
	std::vector<std::int32_t> indices;

	// Проходим по всем мешам сцены
	for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
		aiMesh* mesh = scene->mMeshes[i];

		for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
			Vertex vertex;

			// Позиция вершины
			vertex.Pos.x = mesh->mVertices[j].x;
			vertex.Pos.y = mesh->mVertices[j].y;
			vertex.Pos.z = mesh->mVertices[j].z;

			// Нормали
			if (mesh->HasNormals()) {
				vertex.Normal.x = mesh->mNormals[j].x;
				vertex.Normal.y = mesh->mNormals[j].y;
				vertex.Normal.z = mesh->mNormals[j].z;
			}

			// Текстурные координаты (если есть)
			if (mesh->HasTextureCoords(0)) {
				vertex.TexC.x = mesh->mTextureCoords[0][j].x;
				vertex.TexC.y = mesh->mTextureCoords[0][j].y;
			}
			else {
				vertex.TexC.x = 0.0f;
				vertex.TexC.y = 0.0f;
			}

			// Тангенты
			if (mesh->HasTangentsAndBitangents()) {
				vertex.Tangent.x = mesh->mTangents[j].x;
				vertex.Tangent.y = mesh->mTangents[j].y;
				vertex.Tangent.z = mesh->mTangents[j].z;
			}

			vertices.push_back(vertex);
			indices.push_back(static_cast<std::int32_t>(vertices.size() - 1));
		}
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = Name;

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

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs[Name] = submesh;

	mGeometries[geo->Name] = std::move(geo);

}

void RenderingSystem::LoadMeshes(std::vector<MeshDesc>& MeshDescs)
{
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	for (MeshDesc& i : MeshDescs)
	{
		BuildMeshGeometry(i.Name, i.Path);
	}

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
}

void RenderingSystem::BuildPSOs(MaterialDesc& MDesc, std::unordered_map<std::string, ComPtr<ID3D12PipelineState>>& mPSOs)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders[MDesc.VertexShaderName]->GetBufferPointer()),
		mShaders[MDesc.VertexShaderName]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders[MDesc.PixelShaderName]->GetBufferPointer()),
		mShaders[MDesc.PixelShaderName]->GetBufferSize()
	};
	if (MDesc.UseTesselation)
	{
		opaquePsoDesc.HS =
		{
			reinterpret_cast<BYTE*>(mShaders[MDesc.HullShaderName]->GetBufferPointer()),
			mShaders[MDesc.HullShaderName]->GetBufferSize()
		};
		opaquePsoDesc.DS =
		{
			reinterpret_cast<BYTE*>(mShaders[MDesc.DomainShaderName]->GetBufferPointer()),
			mShaders[MDesc.DomainShaderName]->GetBufferSize()
		};
		opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	}
	else opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	//opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for transparent objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	//
	// PSO for marking stencil mirrors.
	//

	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
	mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;

	D3D12_DEPTH_STENCIL_DESC mirrorDSS;
	mirrorDSS.DepthEnable = true;
	mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDSS.StencilEnable = true;
	mirrorDSS.StencilReadMask = 0xff;
	mirrorDSS.StencilWriteMask = 0xff;

	mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// We are not rendering backfacing polygons, so these settings do not matter.
	mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
	markMirrorsPsoDesc.BlendState = mirrorBlendState;
	markMirrorsPsoDesc.DepthStencilState = mirrorDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

	//
	// PSO for stencil reflections.
	//

	D3D12_DEPTH_STENCIL_DESC reflectionsDSS;
	reflectionsDSS.DepthEnable = true;
	reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionsDSS.StencilEnable = true;
	reflectionsDSS.StencilReadMask = 0xff;
	reflectionsDSS.StencilWriteMask = 0xff;

	reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	// We are not rendering backfacing polygons, so these settings do not matter.
	reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
	drawReflectionsPsoDesc.DepthStencilState = reflectionsDSS;
	drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

	//
	// PSO for shadow objects
	//

	// We are going to draw shadows with transparency, so base it off the transparency description.
	D3D12_DEPTH_STENCIL_DESC shadowDSS;
	shadowDSS.DepthEnable = true;
	shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDSS.StencilEnable = true;
	shadowDSS.StencilReadMask = 0xff;
	shadowDSS.StencilWriteMask = 0xff;

	shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	// We are not rendering backfacing polygons, so these settings do not matter.
	shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
	shadowPsoDesc.DepthStencilState = shadowDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));

	//
	// PSO for GBuffer Light Pass
	//

	// Здесь можно создать дополнительные PSO для отложенного освещения и тонемаппинга.
	// Например, PSO для расчёта освещения на основе G-buffer:
	D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredPsoDesc = {};
	// Поскольку для полноэкранного квадрата не нужен входной layout, оставляем его пустым:
	deferredPsoDesc.InputLayout = { nullptr, 0 };
	deferredPsoDesc.pRootSignature = mLightPassRootSignature.Get(); // либо создайте отдельную корневую сигнатуру для deferred рендера

	// Загрузка шейдеров deferred освещения (предварительно скомпилированных, например, "DeferredLightVS.cso" и "DeferredLightPS.cso")
	deferredPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["DeferredLightPassVS"]->GetBufferPointer()),
		mShaders["DeferredLightPassVS"]->GetBufferSize()
	};
	deferredPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["DeferredLightPassPS"]->GetBufferPointer()),
		mShaders["DeferredLightPassPS"]->GetBufferSize()
	};

	deferredPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);


	CD3DX12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	/*blendDesc.RenderTarget[0].BlendEnable = true;
	blendDesc.RenderTarget[0].LogicOpEnable = false;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;*/

	deferredPsoDesc.BlendState = blendDesc;

	deferredPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	deferredPsoDesc.DepthStencilState.DepthEnable = false;
	deferredPsoDesc.DepthStencilState.StencilEnable = false;
	deferredPsoDesc.SampleMask = UINT_MAX;
	deferredPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	deferredPsoDesc.NumRenderTargets = 1;
	deferredPsoDesc.RTVFormats[0] = mBackBufferFormat;
	deferredPsoDesc.SampleDesc.Count = 1;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&mPSOs["DeferredLightPass"])));

	//
	// PSO for GBuffer Geometry Pass
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC descPipelineState;
	ZeroMemory(&descPipelineState, sizeof(descPipelineState));
	descPipelineState.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS_deferred"]->GetBufferPointer()),
		mShaders["standardVS_deferred"]->GetBufferSize()
	};
	descPipelineState.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardPS_deferred"]->GetBufferPointer()),
		mShaders["standardPS_deferred"]->GetBufferSize()
	};
	if (MDesc.UseTesselation)
	{
		descPipelineState.HS =
		{
			reinterpret_cast<BYTE*>(mShaders["standardHS_deferred"]->GetBufferPointer()),
			mShaders["standardHS_deferred"]->GetBufferSize()
		};
		descPipelineState.DS =
		{
			reinterpret_cast<BYTE*>(mShaders["standardDS_deferred"]->GetBufferPointer()),
			mShaders["standardDS_deferred"]->GetBufferSize()
		};
		descPipelineState.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	}
	else descPipelineState.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	descPipelineState.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	descPipelineState.pRootSignature = mRootSignature.Get();
	descPipelineState.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	descPipelineState.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	descPipelineState.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	descPipelineState.SampleMask = UINT_MAX;
	descPipelineState.NumRenderTargets = 5;
	descPipelineState.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	descPipelineState.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	descPipelineState.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_SNORM;
	descPipelineState.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;
	descPipelineState.RTVFormats[4] = DXGI_FORMAT_R8G8B8A8_UNORM;
	descPipelineState.DSVFormat = mDepthStencilFormat;
	descPipelineState.SampleDesc.Count = 1;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&descPipelineState, IID_PPV_ARGS(&mPSOs["GBufferGeometryPass"])));
}

void RenderingSystem::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			2, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}

void RenderingSystem::BuildMaterials(std::vector<MaterialDesc>& MaterialDescs)
{
	for (int i = 0; i < MaterialDescs.size(); i++)
	{
		auto t = std::make_unique<Material>();
		t->Name = MaterialDescs[i].Name;
		t->MatCBIndex = i;
		t->DiffuseSrvHeapIndex = ((mTextures.find(MaterialDescs[i].DiffuseTexName) == mTextures.end())) ? 0 : mTextures[MaterialDescs[i].DiffuseTexName].get()->srvHeapIndex;
		t->NormalSrvHeapIndex = ((mTextures.find(MaterialDescs[i].NormalMapName) == mTextures.end())) ? 0 : mTextures[MaterialDescs[i].NormalMapName].get()->srvHeapIndex;
		t->HeightSrvHeapIndex = ((mTextures.find(MaterialDescs[i].HeightMapName) == mTextures.end())) ? 0 : mTextures[MaterialDescs[i].HeightMapName].get()->srvHeapIndex;
		t->DiffuseAlbedo = MaterialDescs[i].DiffuseAlbedo;
		t->FresnelR0 = MaterialDescs[i].FresnelR0;
		t->Roughness = MaterialDescs[i].Roughness;
		t->UseTesselation = MaterialDescs[i].UseTesselation;

		BuildPSOs(MaterialDescs[i], t->PSOs);
		BuildDescriptorHeap(t.get());

		mMaterials[t->Name] = std::move(t);
	}
}

void RenderingSystem::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, std::string RenderLayerName)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->Mat->UseTesselation ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmdList->SetPipelineState(ri->Mat->PSOs[RenderLayerName].Get());

		ID3D12DescriptorHeap* descriptorHeaps[] = { ri->Mat->mSrvDescriptorHeap.Get() };
		cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		CD3DX12_GPU_DESCRIPTOR_HANDLE TextureDescs(ri->Mat->mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, TextureDescs);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void RenderingSystem::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void RenderingSystem::LoadTextures(std::vector<TextureDesc>& TexDescs)
{

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	for (int i = 0; i < TexDescs.size() ; i++)
	{
		auto t = std::make_unique<Texture>();
		t->srvHeapIndex = i;
		t->Name = TexDescs[i].Name;
		t->Filename = TexDescs[i].Path;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), t->Filename.c_str(),
			t->Resource, t->UploadHeap));

		mTextures[t->Name] = std::move(t);
	}
	
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = TexDescs.size();
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (TextureDesc& i : TexDescs) {
		auto it = mTextures.find(i.Name);
		if (it == mTextures.end()) {
			// Обработка ошибки: текстура не найдена
			OutputDebugStringA(("Texture not found: " + i.Name + "\n").c_str());
			continue;
		}

		auto& tex = it->second->Resource;
		if (!tex) {
			// Обработка ошибки: ресурс текстуры не инициализирован
			OutputDebugStringA(("Texture resource is null: " + i.Name + "\n").c_str());
			continue;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = tex->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = tex->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(tex.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	}

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
}

void RenderingSystem::UpdateRenderItems(std::unordered_map<std::string, std::unique_ptr<DrawableObject>>& mAllObjects)
{
	int k = 0;
	for (auto& pair : mAllObjects)
	{
		auto i = pair.second.get();

		auto t = mAllRitems[k].get();
		XMStoreFloat4x4(&t->World, XMMatrixScaling(i->Scale.x, i->Scale.y, i->Scale.z)
			* XMMatrixRotationRollPitchYaw(i->WorldRotation.z, i->WorldRotation.y, i->WorldRotation.x)
			* XMMatrixTranslation(i->WorldLocation.x, i->WorldLocation.y, i->WorldLocation.z));
		XMStoreFloat4x4(&t->TexTransform, i->TexTransform);
		t->NumFramesDirty = gNumFrameResources;
		k++;
	}
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
}

void RenderingSystem::BuildBasicGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	std::vector<GeometryGenerator::MeshData*> Objects = { &box, &grid, &sphere, &cylinder };
	std::vector<std::string> Names = { "Box", "Grid", "Sphere", "Cylinder" };

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

		std::vector<std::uint16_t> indices;
		indices.insert(indices.end(), std::begin(Objects[k]->GetIndices16()), std::end(Objects[k]->GetIndices16()));

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

		auto geo = std::make_unique<MeshGeometry>();
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

		geo->DrawArgs[Names[k]] = *Submesh;

		mGeometries[geo->Name] = std::move(geo);
	}
}

void RenderingSystem::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

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
	mMainPassCB.AmbientLight = { 0.55f, 0.55f, 0.55f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };


	//mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 1.f, 1.f, 1.f };
	mMainPassCB.Lights[2].FalloffStart = 0.f;
	mMainPassCB.Lights[2].FalloffEnd = 100.f;
	mMainPassCB.Lights[2].Position = { 1.f, 1.f, 1.f };


	// Main pass stored in index 2
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void RenderingSystem::UpdateReflectedPassCB(const GameTimer& gt)
{
	mReflectedPassCB = mMainPassCB;

	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);

	// Reflect the lighting.
	for (int i = 0; i < 3; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
	}

	// Reflected pass stored in index 1
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(1, mReflectedPassCB);
}

void RenderingSystem::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
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