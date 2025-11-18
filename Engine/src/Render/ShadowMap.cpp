#include <Engine/Render/ShadowMap.h>

ShadowMap::ShadowMap(ID3D12Device* device, UINT width, UINT height)
{
	md3dDevice = device;

	mWidth = width;
	mHeight = height;

	BuildResource();
}

UINT ShadowMap::Width()const
{
	return mWidth;
}

UINT ShadowMap::Height()const
{
	return mHeight;
}

ID3D12Resource* ShadowMap::Resource()
{
	return mShadowMapResource.Get();
}

ID3D12Resource* ShadowMap::BlurBufferA()
{
	return mBlurBufferA.Get();
}

ID3D12Resource* ShadowMap::BlurBufferB()
{
	return mBlurBufferB.Get();
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMap::Dsv()const
{
	return mCpuDSV;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMap::BlurSRVA() const
{
	return mSRVA;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMap::BlurSRVB() const
{
	return mSRVB;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMap::BlurUAVA() const
{
	return mUAVA;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMap::BlurUAVB() const
{
	return mUAVB;
}

void ShadowMap::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE mSRVA,
	CD3DX12_CPU_DESCRIPTOR_HANDLE mSRVB,
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDSV,
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSRV,
	CD3DX12_CPU_DESCRIPTOR_HANDLE mUAVA,
	CD3DX12_CPU_DESCRIPTOR_HANDLE mUAVB)
{
	// Save references to the descriptors. 
	this->mSRVA = mSRVA;
	this->mSRVB = mSRVB;
	this->mCpuDSV = hCpuDSV;
	this->mCpuSRV = hCpuSRV;
	this->mUAVA = mUAVA;
	this->mUAVB = mUAVB;
	//  Create the descriptors
	BuildDescriptors();
}

void ShadowMap::OnResize(UINT newWidth, UINT newHeight)
{
	mWidth = newWidth;
	mHeight = newHeight;

	BuildResource();

	// New resource, so we need new descriptors to that resource.
	BuildDescriptors();
}

void ShadowMap::BuildDescriptors()
{
	// Create SRV to resource so we can sample the shadow map in a shader program.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateShaderResourceView(mShadowMapResource.Get(), &srvDesc, mCpuSRV);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(mShadowMapResource.Get(), &dsvDesc, mCpuDSV);

	srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(mBlurBufferA.Get(), &srvDesc, mSRVA);

	md3dDevice->CreateShaderResourceView(mBlurBufferB.Get(), &srvDesc, mSRVB);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	uavDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateUnorderedAccessView(mBlurBufferA.Get(), nullptr, &uavDesc, mUAVA);

	md3dDevice->CreateUnorderedAccessView(mBlurBufferB.Get(), nullptr, &uavDesc, mUAVB);
}

void ShadowMap::BuildResource()
{
	// Note, compressed formats cannot be used for UAV.  We get error like:
	// ERROR: ID3D11Device::CreateTexture2D: The format (0x4d, BC3_UNORM) 
	// cannot be bound as an UnorderedAccessView, or cast to a format that
	// could be bound as an UnorderedAccessView.  Therefore this format 
	// does not support D3D11_BIND_UNORDERED_ACCESS.

	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = BufferCount;
	texDesc.MipLevels = 1;
	texDesc.Format = mFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&optClear,
		IID_PPV_ARGS(&mShadowMapResource)));

	texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&mBlurBufferA)));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&mBlurBufferB)));

}