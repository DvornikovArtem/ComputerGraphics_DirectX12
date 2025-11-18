#pragma once

#ifndef SHADOWMAP_H
#define SHADOWMAP_H

#include "../Core/d3dUtil.h"

using Microsoft::WRL::ComPtr;

enum class CubeMapFace : int
{
	PositiveX = 0,
	NegativeX = 1,
	PositiveY = 2,
	NegativeY = 3,
	PositiveZ = 4,
	NegativeZ = 5
};

class ShadowMap
{
public:
	ShadowMap(ID3D12Device* device,
		UINT width, UINT height);

	ShadowMap(const ShadowMap& rhs) = delete;
	ShadowMap& operator=(const ShadowMap& rhs) = delete;
	~ShadowMap() = default;

	UINT Width()const;
	UINT Height()const;
	ID3D12Resource* Resource();
	ID3D12Resource* BlurBufferA();
	ID3D12Resource* BlurBufferB();
	CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv()const;

	CD3DX12_CPU_DESCRIPTOR_HANDLE BlurSRVA()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE BlurSRVB()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE BlurUAVA()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE BlurUAVB()const;

	D3D12_VIEWPORT Viewport()const;
	D3D12_RECT ScissorRect()const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE mSRVA,
		CD3DX12_CPU_DESCRIPTOR_HANDLE mSRVB,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSRV,
		CD3DX12_CPU_DESCRIPTOR_HANDLE mUAVA,
		CD3DX12_CPU_DESCRIPTOR_HANDLE mUAVB);

	void OnResize(UINT newWidth, UINT newHeight);

	int SRVHeapIndex = 0;
	int BufferCount = 6;

private:
	void BuildDescriptors();
	void BuildResource();

	ID3D12Device* md3dDevice = nullptr;

	UINT mWidth = 0;
	UINT mHeight = 0;
	DXGI_FORMAT mFormat = DXGI_FORMAT_R24G8_TYPELESS;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mCpuDSV;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mCpuSRV;

	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMapResource = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> mBlurBufferA;
	Microsoft::WRL::ComPtr<ID3D12Resource> mBlurBufferB;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mSRVA;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mSRVB;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mUAVA;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mUAVB;

};

#endif // SHADOWMAP_H