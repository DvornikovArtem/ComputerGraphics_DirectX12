// RenderingSystem.h
#pragma once

#ifndef RENDERINGSYSTEM_H
#define RENDERINGSYSTEM_H

#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
//#include "d3dUtil.h"
#include "MathHelper.h"

#include "assimp/Importer.hpp"
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "GameTimer.h"

#include <d3dcommon.h> // Äëÿ ID3DBlob
#include <wrl/client.h> // Äëÿ ComPtr
#include <unordered_map> // Äëÿ std::unordered_map
#include <string> // Äëÿ std::string

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct ShaderDesc
{
    ShaderDesc() {}

    ShaderDesc(std::string Name, std::wstring Path, std::string FunctionName, const D3D_SHADER_MACRO* ShaderDefines, std::string ShaderProfile)
    {
        this->Name = Name;
        this->Path = Path;
        this->FunctionName = FunctionName;
        this->ShaderDefines = ShaderDefines;
        this->ShaderProfile = ShaderProfile;
    }

    std::string Name;
    std::wstring Path;
    std::string FunctionName;
    const D3D_SHADER_MACRO* ShaderDefines;
    std::string ShaderProfile;
};

struct TextureDesc
{
    TextureDesc() {}

    TextureDesc(std::string Name, std::wstring Path)
    {
        this->Name = Name;
        this->Path = Path;
    }
    std::string Name;
    std::wstring Path;
};

struct MaterialDesc
{
    MaterialDesc() {}

    MaterialDesc(std::string Name, std::string DiffuseTexName, XMFLOAT4 DiffuseAlbedo, XMFLOAT3 FresnelR0, float Roughness)
    {
        this->Name = Name;
        this->DiffuseTexName = DiffuseTexName;
        this->DiffuseAlbedo = DiffuseAlbedo;
        this->FresnelR0 = FresnelR0;
        this->Roughness = Roughness;
    }

    std::string Name;
    std::string DiffuseTexName;
    XMFLOAT4 DiffuseAlbedo;
    XMFLOAT3 FresnelR0;
    float Roughness;
};

struct MeshDesc
{
    std::string Name;
    std::wstring Path;

    bool LoadTexture;
    std::string TextureName;
};

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
    RenderItem() = default;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    int NumFramesDirty = gNumFrameResources;

    // Index into GPU constant buffer corresponding to the ObjectCB for this render item.
    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Mirrors,
    Reflected,
    Transparent,
    Shadow,
    Count
};

class RenderingSystem {
public:
    RenderingSystem();

    void Initialize(HWND mhMainWnd, HINSTANCE mhAppInst, GameTimer* gt);
    void OnResize();

    void LogAdapters();
    void LogAdapterOutputs(IDXGIAdapter* adapter);
    void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();
    void BuildRootSignature();

    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateReflectedPassCB(const GameTimer& gt);
    void BuildDescriptorHeaps();
    void BuildInputLayout();
    void BuildShaders(std::vector<ShaderDesc>& ShaderDescs);
    void BuildBasicGeometry();
    void LoadTextures(std::vector<TextureDesc>& TexDescs);

    void BuildMeshGeometry(const std::string& filename);
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials(std::vector<MaterialDesc>& MaterialDescs);
    void BuildRenderItems();

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

    void Render(const GameTimer& gt);

    void Update();

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    void FlushCommandQueue();
    D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::DepthStencilView() const;
    ID3D12Resource* RenderingSystem::CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::CurrentBackBufferView() const;

    float AspectRatio()const
    {
        return static_cast<float>(mClientWidth) / mClientHeight;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory4> getdxgiFactory() { return mdxgiFactory; };
    Microsoft::WRL::ComPtr<ID3D12Device> getd3dDevice() { return md3dDevice; };
    Microsoft::WRL::ComPtr<ID3D12Fence> getFence() { return mFence; };

    void setScreenParams(int NewWidth, int NewHeight) { mClientWidth = NewWidth; mClientHeight = NewHeight; }

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> getCommandQueue() { return mCommandQueue; };
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> getDirectCmdListAlloc() { return mDirectCmdListAlloc; };
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> getCommandList() { return mCommandList; };

    Microsoft::WRL::ComPtr<IDXGISwapChain> getSwapChain() { return mSwapChain; };
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> getRtvHeap() { return mRtvHeap; };
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> getDsvHeap() { return mDsvHeap; };

    Microsoft::WRL::ComPtr<ID3D12RootSignature> getRootSignature() { return mRootSignature; };

    UINT64 getCurrentFence() { return mCurrentFence; };

    //static int getSwapChainBufferCount() { return SwapChainBufferCount; };
    int getCurrBackBuffer() const { return mCurrBackBuffer; };

    UINT getRtvDescriptorSize() { return mRtvDescriptorSize; };
    UINT getDsvDescriptorSize() { return mDsvDescriptorSize; };
    UINT getCbvSrvUavDescriptorSize() { return mCbvSrvUavDescriptorSize; };

    DXGI_FORMAT getBackBufferFormat() { return mBackBufferFormat; };
    DXGI_FORMAT getDepthStencilFormat() { return mDepthStencilFormat; };

    Microsoft::WRL::ComPtr<ID3D12Resource>& getSwapChainBuffer(int index) {
        if (index >= 0 && index < SwapChainBufferCount) {
            return mSwapChainBuffer[index];
        }
        throw std::out_of_range("Index is out of range");
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> getDepthStencilBuffer() { return mDepthStencilBuffer; };

    D3D12_VIEWPORT getScreenViewport() { return mScreenViewport; };
    D3D12_RECT getScissorRect() { return mScissorRect; };
    int getClientWidth() { return mClientWidth; };
    int getClientHeight() { return mClientHeight; };


    void setCurrBackBuffer(int mCurrBackBuffer) { this->mCurrBackBuffer = mCurrBackBuffer; };

    POINT mLastMousePos;
    float mTheta = 1.24f * XM_PI;
    float mPhi = 0.42f * XM_PI;
    float mRadius = 12.0f;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

protected:
    HINSTANCE mhAppInst = nullptr; // application instance handle
    HWND      mhMainWnd = nullptr; // main window handle
    bool      mAppPaused = false;  // is the application paused?
    bool      mMinimized = false;  // is the application minimized?
    bool      mMaximized = false;  // is the application maximized?
    bool      mResizing = false;   // are the resize bars being dragged?
    bool      mFullscreenState = false;// fullscreen enabled

    bool      m4xMsaaState = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

    Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
    Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice;

    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    UINT64 mCurrentFence = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;

    static const int SwapChainBufferCount = 2;
    int mCurrBackBuffer = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;

    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    std::wstring mMainWndCaption = L"prikol";
    D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int mClientWidth = 800;
    int mClientHeight = 600;

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    PassConstants mMainPassCB;
    PassConstants mReflectedPassCB;
    
    GameTimer* gt = nullptr;
};




#endif // RENDERINGSYSTEM_H