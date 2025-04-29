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
    enum TextureType { Texture2D, CubeMap };
    TextureDesc() {}

    TextureDesc(std::string Name, std::wstring Path, TextureType TexType)
    {
        this->Name = Name;
        this->Path = Path;
        this->TexType = TexType;
    }
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
    std::string Name;
    std::string Path;
    std::string TextureName = "";
};

// Lightweight structure stores parameters to draw a shape.
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

struct LightObject
{
    LightObject() {}

    float Strength = 0.5f;
    float FalloffStart = 1.0f;                          // point/spot light only
    XMFLOAT3 WorldRotation = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    XMFLOAT3 WorldLocation = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;                            // spot light only
    XMFLOAT3 Color = { 1.f, 1.f, 1.f };
    LightType LightType = LightType::Pointlight;
    std::string Name = "";
    int LightCBIndex = 0; // DONT CHANGE ME
    bool NeedsUpdate = true;
    int NumFramesDirty = gNumFrameResources;
};

struct DrawableObject
{
    DrawableObject() {}

    DrawableObject(std::string Name, std::string GeometryName, std::string MaterialName, int RenderLayer)
    {
        this->Name = Name;
        this->GeometryName = GeometryName;
        this->MaterialName = MaterialName;
        this->RenderLayer = RenderLayer;
    }
    DrawableObject(std::string Name, std::string GeometryName, std::string MaterialName, int RenderLayer, XMFLOAT3 WorldLocation, XMFLOAT3 WorldRotation, XMFLOAT3 Scale)
    {
        this->Name = Name;
        this->GeometryName = GeometryName;
        this->MaterialName = MaterialName;
        this->RenderLayer = RenderLayer;
        this->WorldLocation = WorldLocation;
        this->WorldRotation = WorldRotation;
        this->Scale = Scale;
    }

    std::string Name;
    std::string GeometryName;
    std::string MaterialName;
    int RenderLayer = 0;

    XMFLOAT3 WorldLocation = XMFLOAT3(0.f, 0.f, 0.f);
    XMFLOAT3 WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    XMFLOAT3 Scale = XMFLOAT3(1.f, 1.f, 1.f);
    XMMATRIX TexTransform = XMMatrixIdentity();
    bool NeedsUpdate = true;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Mirrors,
    Reflected,
    Transparent,
    Shadow,
    Sky,
    Count,
};

class RenderingSystem {
public:
    RenderingSystem();

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
    void UpdateRenderItems(std::unordered_map<std::string, std::unique_ptr<DrawableObject>>& mAllObjects);

    void BuildMeshGeometry(std::string Name, const std::string& filename);
    void LoadMeshes(std::vector<MeshDesc>& MeshDescs);
    void BuildPSOs(MaterialDesc& MDesc, std::unordered_map<std::string, ComPtr<ID3D12PipelineState>>& mPSOs);
    void BuildGlobalPSOs();
    void BuildFrameResources();
    void BuildMaterials(std::vector<MaterialDesc>& MaterialDescs);
    void BuildRenderItems(std::unordered_map<std::string, std::unique_ptr<DrawableObject>>& Objects);
    void BuildLightItems(std::unordered_map<std::string, std::shared_ptr<LightObject>>& Objects);

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, std::string PSOName);

    void GBufferGeometryPass();
    void GBufferLightPass();
    
    void DrawSkyBox();

    void Render();

    void Update(std::unordered_map<std::string, std::unique_ptr<DrawableObject>>& mAllObjects);

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

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<std::shared_ptr<LightObject>> mAllLights;
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
};




#endif // RENDERINGSYSTEM_H