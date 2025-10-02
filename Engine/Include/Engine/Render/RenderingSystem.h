#pragma once

#ifndef RENDERINGSYSTEM_H
#define RENDERINGSYSTEM_H

#include "../RHI/DX12/UploadBuffer.h"
#include "../Math/GeometryGenerator.h"
#include "../RHI/DX12/FrameResource.h"
#include "../Math/MathHelper.h"
#include "../Scene/Camera.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "../Core/GameTimer.h"
#include <d3dcommon.h>
#include <wrl/client.h> 
#include <unordered_map>
#include <string>
#include "Gbuffer.h"
#include "DirectXCollision.h"
#include "../RHI/DX12/ResourceUploadBatch.h"

//#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_STATIC

#include <Engine/Render/RenderItem.h>
//#include <Engine/RHI/DX12/stb_image.h>
#include <Engine/Render/IRenderTargetProvider.h>
#include <Engine/Debug/DebugRenderSysImpl.h>
#include <Engine/Core/OctTree.h>
#include <Engine/Render/FX/ParticleSystem.h>
#include <Engine/Terrain/TerrainRenderer.h>
#include <Engine/Render/Descriptors.h>
//#include "../Terrain/TerrainRenderer.h"

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/dx12/ffx_api_dx12.h>


#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;



// Already Initialized In RenderItem.h =========
//const int gNumFrameResources = 3;
// =============================================





class RenderingSystem : public IRenderTargetProvider {
//class RenderingSystem {
public:
    RenderingSystem();

    ~RenderingSystem()
    {
        if (mOctTree) delete mOctTree;

        if (terrainRenderer) delete terrainRenderer;

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

        for (auto& ps : mAllParticleSystems)
            delete ps;

        mFrameResources.clear();

        if(mDebugDrawer) delete mDebugDrawer;

        for (auto& i : terrainDrawableObjects)
            delete i;

        for (auto& i : mAllTerrainRitems)
            delete i;

        if(mFFXContext) ffxDestroyContext(&mFFXContext, nullptr);
    }


    void Initialize(HWND mhMainWnd, HINSTANCE mhAppInst, GameTimer* gt);
    void FinishInitialize();
    void OnResize();

    void LogAdapters();
    void LogAdapterOutputs(IDXGIAdapter* adapter);
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();
    void BuildRootSignatures();

    void UpdateObjectCBs(const GameTimer& gt);

    void UpdateLightItems(std::vector<LightObject*>& mAllLightObjectsToUpdate);
    void UpdateLightCBs(const GameTimer& gt);

    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);
    void BuildInputLayout();
    void BuildShaders(std::vector<ShaderDesc>& ShaderDescs);
    void BuildBasicGeometry();
    void LoadTextures(std::vector<TextureDesc>& TexDescs);
    void ProcessEmbeddedTexture(const aiTexture* texture, std::string TextureName);

    void CollectVisibleRenderItems();
    void CollectVisibleLightItems();

    void UpdateRenderItems(std::vector<DrawableObject*>& mAllObjectsToUpdate);

    std::vector<MeshParsingResult> BuildMeshGeometry(std::string Name, const std::string& filename);
    std::vector<MeshParsingResult> LoadMesh(MeshDesc& meshDesc, bool GenerateMaterial);
    void BuildPSOs(MaterialDesc& MDesc, std::unordered_map<std::string, ComPtr<ID3D12PipelineState>>& mPSOs);
    void BuildGlobalPSOs();
    void BuildFrameResources();
    void BuildMaterials(std::vector<MaterialDesc>& MaterialDescs);
    void BuildRenderItems(std::unordered_map<std::string, DrawableObject*>& Objects);
    void BuildLightItems(std::unordered_map<std::string, LightObject*>& Objects);
    void BuildParticleSystems(std::unordered_map<std::string, ParticleSystemDescriptor> ParticleSystemDescriptors);
    void BuildTerrain();
    void BuildSceneGrid();
    void DrawSceneGrid();
    void BuildFSRContext();
    void FSRUpscale();
    static void FSRMessageCallback(uint32_t type, const wchar_t* message);

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, std::string PSOName);

    void GBufferGeometryPass();
    void GBufferLightPass();
    void DrawParticleSystems();

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index)const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE RenderingSystem::GetGpuSrv(int index)const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetDsv(int index)const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetRtv(int index)const;

    // For Debug System ===============================================================================
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const override { return CurrentBackBufferView(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const override { return DepthStencilView(); }

    gfw::DebugRenderSysImpl* mDebugDrawer = nullptr;
    // =================================================================================================

    void DrawSkyBox();
    void DrawShadowMaps();
    void PostProcessingPass();
    void DrawDebugTexture(CD3DX12_GPU_DESCRIPTOR_HANDLE SRVHandle);

    void Render();

    void Update(std::vector<DrawableObject*>& mAllObjectsToUpdate, std::vector<LightObject*>& mAllLightObjectsToUpdate);

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

    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthBufferSRV() const { return mDepthBufferSRV; }

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

    D3D12_CPU_DESCRIPTOR_HANDLE mDepthBufferSRV;

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
    std::vector<LightObject*> mAllVisibleLitems;
    std::unordered_set<LightObject*> alreadyCheckedLitems;
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    PassConstants mMainPassCB;

    float mTheta = 1.24f * XM_PI;
    float mPhi = 0.42f * XM_PI;
    float mRadius = 12.0f;

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();
    
    GameTimer* gt = nullptr;

    std::unique_ptr<Gbuffer> mGBuffer;

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> GlobalPSOs;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> RootSignatures;

    BoundingFrustum ViewFrustum;

    OctTree* mOctTree = nullptr;

    //holds generated textures to be added in main texture pipeline later
    std::vector<Texture*> MPRTextures;
    std::vector<TextureDesc> MPRTerrainTextures;

    std::vector<MaterialDesc> TerrainMaterialDescs;

    UINT TexDescsLength;

    std::vector <ParticleSystem*> mAllParticleSystems;

    TerrainRenderer* terrainRenderer = nullptr;

    std::vector <DrawableObject*> terrainDrawableObjects;

    std::vector<RenderItem*> mAllTerrainRitems;

    std::vector<RenderItem*> mChosenTerrainRitems;
    std::vector<RenderItem*> mVisibleTerrainRitems;

    //FSR sctructures and resources
    ffxContext mFFXContext;
    UINT mRecommendedRenderResolutionX = 0;
    UINT mRecommendedRenderResolutionY = 0;
    FfxApiUpscaleQualityMode mFSRQualityMode = FFX_UPSCALE_QUALITY_MODE_QUALITY;
    Microsoft::WRL::ComPtr<ID3D12Resource> mFSROutput;
};


#endif // RENDERINGSYSTEM_H