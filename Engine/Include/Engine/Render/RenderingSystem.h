// RenderingSystem.h

#pragma once

#ifndef RENDERINGSYSTEM_H
#define RENDERINGSYSTEM_H


#include <unordered_map>
#include <string>

#include <d3dcommon.h>
#include <wrl/client.h> 
#include <DirectXCollision.h>

#include <Engine/Core/GameTimer.h>
#include <Engine/Core/OctTree.h>

#include <Engine/Math/MathHelper.h>
#include <Engine/Math/GeometryGenerator.h>

#include <Engine/Debug/DebugRenderSysImpl.h>

#include <Engine/Render/RenderItem.h>
#include <Engine/Render/Descriptors.h>
#include <Engine/Render/Gbuffer.h>
#include <Engine/Render/IRenderTargetProvider.h>
#include <Engine/Render/FX/ParticleSystem.h>

#include <Engine/RHI/DX12/ResourceUploadBatch.h>
#include <Engine/RHI/DX12/FrameResource.h>
#include <Engine/RHI/DX12/UploadBuffer.h>

#include <Engine/Terrain/TerrainRenderer.h>

#include <Engine/Scene/Camera.h>

#include <Engine/Render/SharedTexture.h>

#include <Engine/UI/ImGui_Layer.h>
#include <Engine/UI/DebugOutputHook.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/dx12/ffx_api_dx12.h>

#include <dxil/dxcapi.h>
#include <dxil/d3d12shader.h>

#include <pix3.h>

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")



using Microsoft::WRL::ComPtr;



class RenderingSystem final : public IRenderTargetProvider {
public:
    RenderingSystem() = default;
    ~RenderingSystem();


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

    std::vector<MeshParsingResult> BuildMeshGeometry(std::string Name, const std::string& filename, MeshDesc::ImportType importType);
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
    void DrawUI();
    void PreRender();

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, std::string PSOName);

    void GBufferGeometryPass(ComPtr<ID3D12GraphicsCommandList4>& cmdList);
    void GBufferLightPass(ComPtr<ID3D12GraphicsCommandList4>& cmdList);
    void DrawParticleSystems(ComPtr<ID3D12GraphicsCommandList4>& cmdList);

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index)const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE RenderingSystem::GetGpuSrv(int index)const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetDsv(int index)const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE RenderingSystem::GetRtv(int index)const;

    // For Debug System ================================================================================
public:
    //D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const override { return CurrentBackBufferView(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const override { return mSceneColorRTV; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const override { return DepthStencilView(); }

    gfw::DebugRenderSysImpl* GetDebugDrawer() const { return mDebugDrawer; }
    void SetDebugDrawer(gfw::DebugRenderSysImpl* newDebugDrawer) { mDebugDrawer = newDebugDrawer; }

protected:
    gfw::DebugRenderSysImpl* mDebugDrawer = nullptr;
    // =================================================================================================

public:
    void DrawSkyBox(ComPtr<ID3D12GraphicsCommandList4>& cmdList);
    void DrawShadowMaps(ComPtr<ID3D12GraphicsCommandList4>& cmdList);
    void PostProcessingPass(ComPtr<ID3D12GraphicsCommandList4>& cmdList);

    void Render();

    void Update(std::vector<DrawableObject*>& mAllObjectsToUpdate, std::vector<LightObject*>& mAllLightObjectsToUpdate);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    void FlushCommandQueue();
    void FlushCommandQueue2();
    D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::DepthStencilView() const;
    ID3D12Resource* RenderingSystem::CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::CurrentBackBufferView() const;

    float AspectRatio() const { return static_cast<float>(mClientWidth) / mClientHeight; }

    Microsoft::WRL::ComPtr<ID3D12Device5> getd3dDevice() { return md3dDevice; };

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

public:
    int GetScreenWidth() const { return mClientWidth; }
    int GetScreenHeight() const { return mClientHeight; }

    bool GetVSync() const { return mVSync; }
    void SetVSync(bool v) { mVSync = v; }

    bool GetWireframe() const { return mWireframe; }
    void SetWireframe(bool v) { mWireframe = v; }

    bool GetShowBounds() const { return mShowBounds; }
    void SetShowBounds(bool v) { mShowBounds = v; }

    bool MultiGPUMode() { return !mUseSingleGPU; }

protected:
    bool mVSync = false;
    bool mWireframe = false;
    bool mShowBounds = false;

    Microsoft::WRL::ComPtr<IDXGIFactory6> mdxgiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
    Microsoft::WRL::ComPtr<ID3D12Device5> md3dDevice;

    D3D12_CPU_DESCRIPTOR_HANDLE mDepthBufferSRV;

    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    UINT64 mCurrentFence = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> mCommandList;

    static const int SwapChainBufferCount = 2;
    int mCurrBackBuffer = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;

    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;

    UINT mRtvDescriptorSize = 0;
    UINT mRtvDescriptorSize2 = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    std::wstring mMainWndCaption = L"Renderer";
    D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int mClientWidth;
    int mClientHeight;

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;
    UINT mCbvSrvDescriptorSize2 = 0;

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

    int SRVHeapHeadIndex = 0;

// For Post Effects ================================================================================
public:


protected:
    float mPostEffectsExposure = 0.0f;
// =================================================================================================


// For ImGui =======================================================================================
public:
    void CreateOrResizeSceneColor(int width, int height);

    D3D12_GPU_DESCRIPTOR_HANDLE GetSceneColorSRV() const { return mSceneColorSRV; }
    void SetSceneViewHovered(bool isSceneViewHovered) { mSceneUI.hovered = isSceneViewHovered; }
    void SetSceneViewFocused(bool isSceneViewFocused) { mSceneUI.focused = isSceneViewFocused; }
    void SetSceneRMBDown(bool isSceneRMBDown) { mSceneUI.rmbDown = isSceneRMBDown; }
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetCommandQueue() const { return mCommandQueue; }
    void SetImGuiLayer(Engine::UI::ImGuiLayer* imguiLayer) { mImGui = imguiLayer; }


    bool IsSceneInputActive() const { return (mSceneUI.hovered && (mSceneUI.rmbDown || mSceneUI.mouseLookActive)) || mSceneUI.mouseLookActive; }
    bool IsSceneViewHovered() const { return mSceneUI.hovered; }
    bool IsSceneViewFocused() const { return mSceneUI.focused; }

    bool IsMouseLookActive() const { return mSceneUI.mouseLookActive; }
    int GetMouseSkipFrames() const { return mSceneUI.skipFrames; }

    void BeginMouseLook();
    void UpdateMouseLook();
    void EndMouseLook();

    void RegisterScenePanels();

    std::wstring PrimaryDeviceName = L"";
    std::wstring SecondaryDeviceName = L"";

protected:
    // SRV for imgui
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mImGuiSrvHeap = nullptr;

    Engine::UI::ImGuiLayer* mImGui = nullptr;
    Engine::UI::UIPanelRegistry mPanelRegistry;
    std::vector<Engine::UI::UILayerKind> mActiveUILayers = { Engine::UI::UILayerKind::Editor };

    Microsoft::WRL::ComPtr<ID3D12Resource> mSceneColor = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE mSceneColorRTV{};
    D3D12_GPU_DESCRIPTOR_HANDLE mSceneColorSRV{};

    struct SceneViewUIState {
        bool hovered = false;
        bool focused = false;
        bool rmbDown = false;

        ImVec2 imgRectMin{ 0,0 };
        ImVec2 imgRectMax{ 0,0 };
        ImGuiViewport* viewportForImg = nullptr;

        bool  mouseLookActive = false;
        HWND  mouseLookHwnd = nullptr;
        POINT savedCursorPos{ 0,0 };
        POINT lockCenterPos{ 0,0 };
        RECT  lockRect{ 0,0,0,0 };
        int   skipFrames = 0;
    } mSceneUI;

    ImVec2 mSceneImgRectMin{ 0,0 };
    ImVec2 mSceneImgRectMax{ 0,0 };
    ImGuiViewport* mSceneViewportForImg = nullptr;

    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mGbufferImguiSlots;
// ================================================================================================= 

// For FSR =========================================================================================
protected:
    ffxContext mFFXContext;
    UINT mRecommendedRenderResolutionX = 0;
    UINT mRecommendedRenderResolutionY = 0;
    FfxApiUpscaleQualityMode mFSRQualityMode = FFX_UPSCALE_QUALITY_MODE_QUALITY;
    Microsoft::WRL::ComPtr<ID3D12Resource> mFSROutput;
    D3D12_VIEWPORT mDownscaledScreenViewport;
    D3D12_RECT mDownscaledScissorRect;
    int mFSROutputSRVHeapIndex;
    bool mFSREnabled = false;
    bool mFSRSwitchFlag = false;
    bool mFSREnabledDisplayValue = mFSREnabled;

    void BuildFSRContext();
    void FSRUpscale();
// =================================================================================================
    DirectX::XMFLOAT4 ClearValue = { 0.f, 0.f, 0.f, 1.f };

// For TAA =========================================================================================
    bool mTAAEnabled = true;
    bool mTAAEnabledDisplayValue = mTAAEnabled;
    bool mTAASwitchFlag = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrevFrameTex; //Traditional Render only frame(no anti-aliasing, upscaling, or post-processing)
    Microsoft::WRL::ComPtr<ID3D12Resource> mTAAResolvedAccBuffer; //Result of resolving mGBuffer->AccumulationBuf + mPrevFrameTex
    int mPrevFrameSRVHeapIndex = 0;
    int mResolvedAccBufferSRVHeapIndex = 0;
    int mResolvedAccBufferRTVHeapIndex = SwapChainBufferCount + 1;
    float mJitterX;
    float mJitterY;
    int mJitterIndex = 0;

    float HaltonSequence(uint32_t index, uint32_t base);
    void SaveFrameAsPrevious(ComPtr<ID3D12GraphicsCommandList4>& cmdList);
    void CalculateJitter();
    void TAAResolve(ComPtr<ID3D12GraphicsCommandList4>& cmdList);
// =================================================================================================

// For RT ==========================================================================================
    bool RTSupport = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTLASResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTLASScratchResource;
    int mTLASSRVHeapIndex;
    Microsoft::WRL::ComPtr<ID3D12Resource> mInstanceDescsResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> mInstanceDescsUploadResource;

    void BuildBLASForGeometries();
    void BuildTLAS();
    void RefitTLAS();
// =================================================================================================

// For DXC Shader compilation ======================================================================
	ComPtr<IDxcCompiler3> mDxcCompiler;
	ComPtr<IDxcUtils> mDxcUtils;
	ComPtr<IDxcIncludeHandler> mDxcIncludeHandler;
    D3D_SHADER_MODEL MaxSupportedShaderModel;
    std::wstring mAdapterName;

    std::string GetShaderTargetForModel(const std::string& shaderType);
    void InitializeDXC();
    ComPtr<ID3DBlob> DXCCompileShader(const std::wstring& filename, const D3D_SHADER_MACRO* defines, const std::string& entrypoint, const std::string& shaderType);
// =================================================================================================

// For mGPU ========================================================================================
    Microsoft::WRL::ComPtr<ID3D12Device5> md3dDevice2;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue2;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc2;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> mCommandList2;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence2;
    UINT64 mCurrentFence2 = 0;

    Microsoft::WRL::ComPtr<ID3D12Fence> mSharedFence;
    Microsoft::WRL::ComPtr<ID3D12Fence> mSharedFenceOnDevice2;
    UINT64 mSharedFenceValue = 0;
    HANDLE mSharedFenceHandle = nullptr;
    std::wstring mAdapterName2;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap2;

    SharedTexture mSharedAccBuffer;
    SharedTexture mSharedVelocityBuffer;
    void InitializeSharedResources();

    //Secondary Device Textures
    ComPtr<ID3D12Resource> mDevice2AccBuffer;
    ComPtr<ID3D12Resource> mDevice2VelocityBuffer;
    ComPtr<ID3D12Resource> mDevice2PrevFrame;
    ComPtr<ID3D12Resource> mDevice2ResolvedAccBuffer;

    ComPtr<ID3D12DescriptorHeap> mSrvHeapDevice2;

    int mDevice2AccBufferSRVIndex = -1;
    int mDevice2VelocityBufferSRVIndex = -1;
    int mDevice2PrevFrameSRVIndex = -1;
    int mDevice2ResolvedAccBufferSRVIndex = -1;

    ComPtr<ID3D12DescriptorHeap> mRtvHeapDevice2;
    int mDevice2ResolvedAccBufferRTVIndex = -1;

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> GlobalPSOs2;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> RootSignatures2;

public:
    float GetPrimaryGPUMspf() const { return mPrimaryGPUMspf; }
    bool GetRenderSwappedDevices() const { return mSwapDevices; }
    bool GetCopyTest() const { return mCopyTest; }
    int GetCopyTestType() const { return (int)mCopyTestType; }

private:
    float mPrimaryGPUMspf = 0.0f;
    UINT64 mLastPrimaryFenceValue = 0;
    LARGE_INTEGER mLastPrimaryTime = { 0 };

    void CopyTest();
    void CreateCopyTestResources();
    ComPtr<ID3D12Resource> mCopySource;
    SharedTexture mCopyDestShared;
    ComPtr<ID3D12Resource> mCopyDestLocal;

    enum class CopyTestLoadType { ZeroLoad, CopyFromLocal, CopyFromShared };

    const bool mUseSingleGPU = true;
    const bool mSwapDevices = false;
    const bool mCopyTest = false;
    const CopyTestLoadType mCopyTestType = CopyTestLoadType::CopyFromShared;
// =================================================================================================
};


#endif // RENDERINGSYSTEM_H