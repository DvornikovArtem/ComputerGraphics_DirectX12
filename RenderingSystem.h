#pragma once

#ifndef RENDERINGSYSTEM_H
#define RENDERINGSYSTEM_H

#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "FrameResource.h"
#include "MathHelper.h"
#include "Camera.h"
#include "assimp/Importer.hpp"
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "GameTimer.h"
#include <d3dcommon.h> 
#include <wrl/client.h> 
#include <unordered_map>
#include <string>
#include "Gbuffer.h"
#include "DirectXCollision.h"
#include "directx/ResourceUploadBatch.h"
#include "RenderItem.h"

#include "IRenderTargetProvider.h"
#include "old/DebugRenderSysImpl.h"
#include "OctTree.h"

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// Already Initialized In RenderItem.h =========
//const int gNumFrameResources = 3;
// =============================================

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

    ~ShaderDesc() = default;

    std::string Name;
    std::wstring Path;
    std::string FunctionName;
    const D3D_SHADER_MACRO* ShaderDefines;
    std::string ShaderProfile;
};

struct TextureDesc
{
    enum TextureType { Texture2D, CubeMap };
    TextureDesc() {}

    TextureDesc(std::string Name, std::wstring Path, TextureType TexType)
    {
        this->Name = Name;
        this->Path = Path;
        this->TexType = TexType;
    }

    ~TextureDesc() = default;

    std::string Name;
    std::wstring Path;
    TextureType TexType;
};

struct MaterialDesc
{
    MaterialDesc() {}

    MaterialDesc(std::string Name, std::string VertexShaderName, std::string PixelShaderName, std::string HullShaderName, std::string DomainShaderName, std::string DiffuseTexName, std::string NormalMapName, std::string HeightMapName, XMFLOAT4 DiffuseAlbedo, XMFLOAT3 FresnelR0, float Roughness, bool UseTesselation)
    {
        this->Name = Name;
        this->VertexShaderName = VertexShaderName;
        this->PixelShaderName = PixelShaderName;
        this->HullShaderName = HullShaderName;
        this->DomainShaderName = DomainShaderName;
        this->DiffuseTexName = DiffuseTexName;
        this->NormalMapName = NormalMapName;
        this->HeightMapName = HeightMapName;
        this->DiffuseAlbedo = DiffuseAlbedo;
        this->FresnelR0 = FresnelR0;
        this->Roughness = Roughness;
        this->UseTesselation = UseTesselation;
    }

    ~MaterialDesc() = default;

    std::string Name = "";
    std::string DiffuseTexName = "";
    std::string NormalMapName = "";
    std::string HeightMapName = "";
    XMFLOAT4 DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    XMFLOAT3 FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    float Roughness = 0.f;
    std::string PixelShaderName = "";
    std::string VertexShaderName = "";
    std::string HullShaderName = "";
    std::string DomainShaderName = "";
    bool UseTesselation = false;
};

struct MeshDesc
{
    MeshDesc() {}

    MeshDesc(std::string Name, std::string Path)
    {
        this->Name = Name;
        this->Path = Path;
    }
    MeshDesc(std::string Name, std::string Path, std::string TextureName)
    {
        this->Name = Name;
        this->Path = Path;
        this->TextureName = TextureName;
    }

    ~MeshDesc() = default;

    std::string Name;
    std::string Path;
    std::string TextureName = "";
};



struct LightObject
{
    LightObject() {}

    ~LightObject() = default;

    float Strength = 0.5f;
    float FalloffStart = 1.0f;                          // point/spot light only
    XMFLOAT3 WorldDirection = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    XMFLOAT3 WorldLocation = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;                            // spot light only
    XMFLOAT3 Color = { 1.f, 1.f, 1.f };
    LightType LightType = LightType::Pointlight;
    std::string Name = "";
    int LightCBIndex = 0; // auto generated value
    bool NeedsUpdate = true;
    int NumFramesDirty = gNumFrameResources; // auto generated value
    MeshGeometry* Geo = nullptr; // auto generated value
    XMFLOAT4X4 World = MathHelper::Identity4x4();
};

class RenderingSystem : public IRenderTargetProvider {
//class RenderingSystem {
public:
    RenderingSystem();

    ~RenderingSystem()
    {
        delete mOctTree;

        for (auto& pair : mGeometries)
            delete pair.second;

        for (auto& pair : mMaterials)
            delete pair.second;

        for (auto& pair : mTextures)
            delete pair.second;

        for (auto& light : mAllLights)
            delete light;

        for (auto& ri : mAllRitems)
            delete ri;

        for (auto& layer : mRitemLayer)
        {
            layer.clear();
        }

        mFrameResources.clear();

        delete mDebugDrawer;
    }


    void Initialize(HWND mhMainWnd, HINSTANCE mhAppInst, GameTimer* gt);
    void OnResize();

    void LogAdapters();
    void LogAdapterOutputs(IDXGIAdapter* adapter);
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();
    void BuildRootSignatures();
    void BuildDescriptorHeap(Material* t);

    //DELETE AFTER TESTING
    void MakeDecal() { XMStoreFloat4(&mMainPassCB.Decals[0], mCamera.GetPosition() + mCamera.GetLook() * 4); }

    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateLightCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateReflectedPassCB(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);
    void BuildInputLayout();
    void BuildShaders(std::vector<ShaderDesc>& ShaderDescs);
    void BuildBasicGeometry();
    void LoadTextures(std::vector<TextureDesc>& TexDescs);

    void CollectVisibleRenderItems(OctTreeNode* node);

    void UpdateRenderItems(std::unordered_map<std::string, DrawableObject*>& mAllObjects);

    void BuildMeshGeometry(std::string Name, const std::string& filename);
    void LoadMeshes(std::vector<MeshDesc>& MeshDescs);
    void BuildPSOs(MaterialDesc& MDesc, std::unordered_map<std::string, ComPtr<ID3D12PipelineState>>& mPSOs);
    void BuildGlobalPSOs();
    void BuildFrameResources();
    void BuildMaterials(std::vector<MaterialDesc>& MaterialDescs);
    void BuildRenderItems(std::unordered_map<std::string, DrawableObject*>& Objects);
    void BuildLightItems(std::unordered_map<std::string, LightObject*>& Objects);

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, std::string PSOName);

    void GBufferGeometryPass();
    void GBufferLightPass();

    // For Debug System ===============================================================================
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const override { return CurrentBackBufferView(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const override { return DepthStencilView(); }

    gfw::DebugRenderSysImpl* mDebugDrawer;
    // =================================================================================================

    void DrawSkyBox();

    void Render();

    void Update(std::unordered_map<std::string, DrawableObject*>& mAllObjects);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    void FlushCommandQueue();
    D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::DepthStencilView() const;
    ID3D12Resource* RenderingSystem::CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::CurrentBackBufferView() const;

    float AspectRatio()const
    {
        return static_cast<float>(mClientWidth) / mClientHeight;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> getd3dDevice() { return md3dDevice; };

    void setScreenParams(int NewWidth, int NewHeight) { mClientWidth = NewWidth; mClientHeight = NewHeight; }

    Camera mCamera;
    POINT mLastMousePos;

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

    std::wstring mMainWndCaption = L"Renderer";
    D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int mClientWidth = 800;
    int mClientHeight = 600;

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, MeshGeometry*> mGeometries;
    std::unordered_map<std::string, Material*> mMaterials;
    std::unordered_map<std::string, Texture*> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<RenderItem*> mAllRitems;
    std::vector<RenderItem*> mAllVisibleRitems;
    std::vector<LightObject*> mAllLights;
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    PassConstants mMainPassCB;
    PassConstants mReflectedPassCB;

    float mTheta = 1.24f * XM_PI;
    float mPhi = 0.42f * XM_PI;
    float mRadius = 12.0f;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();
    
    GameTimer* gt = nullptr;

    std::unique_ptr<Gbuffer> mGbuffer;

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> GlobalPSOs;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> RootSignatures;

    BoundingFrustum ViewFrustum;

    OctTree* mOctTree;
};




#endif // RENDERINGSYSTEM_H