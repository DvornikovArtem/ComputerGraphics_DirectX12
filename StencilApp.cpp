#include "d3dApp.h"
#include "MathHelper.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

class StencilApp : public D3DApp
{
public:
    StencilApp(HINSTANCE hInstance) : D3DApp(hInstance) {}
    StencilApp(const StencilApp& rhs) = delete;
    StencilApp& operator=(const StencilApp& rhs) = delete;
    ~StencilApp() {};

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
    void OnKeyboardInput(const GameTimer& gt);

    void LoadShaders();
    void LoadTextures();
    void MakeMaterials();
    void LoadMeshes();
    void MakeDrawableObjects();
    void MakeLights();

};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        StencilApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

bool StencilApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    LoadShaders();
    LoadTextures();
    MakeMaterials();
    LoadMeshes();
    MakeDrawableObjects();
    MakeLights();

    mRenderingSystem->mCamera.SetPosition(-1.0f, 3.0f, 5.0f);
    mRenderingSystem->mCamera.RotateY(DirectX::XM_PI - 0.2f);
    mRenderingSystem->mCamera.Pitch(DirectX::XM_PI / 12.f);

    //Called after all assets, render items and lights are initialized
    mRenderingSystem->BuildFrameResources();

    return true;
}
 
void StencilApp::OnResize()
{
    D3DApp::OnResize();
}

void StencilApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    //Set NeedsUpdate for every object that changes its values at runtime
    mAllObjects["Head"]->WorldRotation.y = gt.TotalTime();
    mAllObjects["Head"]->NeedsUpdate = true;

    mAllLightObjects["Spot1"]->Color = { 0.5f + 0.5f * cos(gt.TotalTime()) , 0.5f + 0.5f * cos(gt.TotalTime() + 1) , 0.5f + 0.5f * cos(gt.TotalTime() + 4) };
    mAllLightObjects["Spot1"]->NeedsUpdate = true;

	mRenderingSystem->Update(mAllObjects);
}

void StencilApp::Draw(const GameTimer& gt)
{
	mRenderingSystem->Render();
}

void StencilApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mRenderingSystem->mLastMousePos.x = x;
	mRenderingSystem->mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void StencilApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void StencilApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mRenderingSystem->mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mRenderingSystem->mLastMousePos.y));

        mRenderingSystem->mCamera.Pitch(dy);
        mRenderingSystem->mCamera.RotateY(dx);
    }

    mRenderingSystem->mLastMousePos.x = x;
    mRenderingSystem->mLastMousePos.y = y;
}
 
void StencilApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        mRenderingSystem->mCamera.Walk(10.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        mRenderingSystem->mCamera.Walk(-10.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        mRenderingSystem->mCamera.Strafe(-10.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        mRenderingSystem->mCamera.Strafe(10.0f * dt);

    if(GetAsyncKeyState(VK_SPACE) & 0x8000)
        mRenderingSystem->MakeDecal();

    mRenderingSystem->mCamera.UpdateViewMatrix();
}

void StencilApp::LoadShaders()
{
    const D3D_SHADER_MACRO defines[] =
    {
        { "ROTATINGTILES", "1" },
        { "FOG", "1" },
        { NULL, NULL }
    };

    const D3D_SHADER_MACRO alphaTestDefines[] =
    {
        { "FOG", "1" },
        { "ALPHA_TEST", "1" },
        { NULL, NULL }
    };

    std::vector<ShaderDesc> ShaderDescs = 
    {
        //deferred shaders
        ShaderDesc("standardVS_deferred", L"../Shaders/DeferredGeometryPass.hlsl", "VS", nullptr, "vs_5_0"),
        ShaderDesc("standardPS_deferred", L"../Shaders/DeferredGeometryPass.hlsl", "PS", nullptr, "ps_5_0"),
        ShaderDesc("RotatingTilesPS_deferred", L"../Shaders/DeferredGeometryPass.hlsl", "PS", defines, "ps_5_0"),
        ShaderDesc("standardHS_deferred", L"../Shaders/DeferredGeometryPass.hlsl", "HSMain", nullptr, "hs_5_0"),
        ShaderDesc("standardDS_deferred", L"../Shaders/DeferredGeometryPass.hlsl", "DSMain", nullptr, "ds_5_0"),
        ShaderDesc("HSForDecals_deferred", L"../Shaders/DeferredGeometryPass.hlsl", "HSForDecals", nullptr, "hs_5_0"),
        ShaderDesc("DSForDecals_deferred", L"../Shaders/DeferredGeometryPass.hlsl", "DSForDecals", nullptr, "ds_5_0"),
    };

    mRenderingSystem->BuildShaders(ShaderDescs);
}

void StencilApp::LoadTextures()
{
    // First Texture in the list will be used as invalid texture

    std::vector<TextureDesc> TexDescs = 
    {
        TextureDesc("INVALID", L"../Textures/INVALID.dds", TextureDesc::Texture2D),
        TextureDesc("bricksTex", L"../Textures/bricks3.dds", TextureDesc::Texture2D),
        TextureDesc("checkboardTex", L"../Textures/checkboard.dds", TextureDesc::Texture2D),
        TextureDesc("iceTex", L"../Textures/ice.dds", TextureDesc::Texture2D),
        TextureDesc("white1x1Tex", L"../Textures/white1x1.dds", TextureDesc::Texture2D),
        TextureDesc("meshTex", L"../Textures/african_head_diffuse.dds", TextureDesc::Texture2D),
        TextureDesc("redTex", L"../Textures/rsq.dds", TextureDesc::Texture2D),
        TextureDesc("grassTex", L"../Textures/WoodCrate01.dds", TextureDesc::Texture2D),
        TextureDesc("PatrickTex", L"../Textures/patrickstar.dds", TextureDesc::Texture2D),
        TextureDesc("Semechki_Diffuse", L"../Textures/semente_BaseColor.dds", TextureDesc::Texture2D),
        TextureDesc("Semechki_NormalMap", L"../Textures/semente_Normal.dds", TextureDesc::Texture2D),
        TextureDesc("Semechki_HeightMap", L"../Textures/semente_Height.dds", TextureDesc::Texture2D),
        TextureDesc("ShinyStones_Diffuse", L"../Textures/ShinyStones_Diffuse.dds", TextureDesc::Texture2D),
        TextureDesc("ShinyStones_NormalMap", L"../Textures/ShinyStones_NormalMap.dds", TextureDesc::Texture2D),
        TextureDesc("ShinyStones_HeightMap", L"../Textures/ShinyStones_HeightMap.dds", TextureDesc::Texture2D),
        TextureDesc("SkyCubeMap", L"../Textures/snowcube1024.dds", TextureDesc::CubeMap),
    };

    mRenderingSystem->LoadTextures(TexDescs);
}

void StencilApp::MakeMaterials()
{
    std::vector<MaterialDesc> MaterialDescs =
    {
        MaterialDesc("bricks", "standardVS_deferred", "standardPS_deferred",  "", "", "bricksTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.25f, false),
        MaterialDesc("checkertile", "standardVS_deferred", "standardPS_deferred", "", "", "checkboardTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.07f, 0.07f, 0.07f), 0.3f, false),
        MaterialDesc("icemirror", "standardVS_deferred", "standardPS_deferred", "", "", "iceTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f), XMFLOAT3(0.1f, 0.1f, 0.1f), 0.5f, false),
        MaterialDesc("skullMat", "standardVS_deferred", "standardPS_deferred", "HSForDecals_deferred", "DSForDecals_deferred", "bricksTex", "ShinyStones_NormalMap", "ShinyStones_HeightMap", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, true),
        MaterialDesc("shadowMat", "standardVS_deferred", "standardPS_deferred", "", "", "redTex", "", "", XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f), XMFLOAT3(0.001f, 0.001f, 0.001f), 0.0f, false),
        MaterialDesc("mesh", "standardVS_deferred", "standardPS_deferred", "", "", "meshTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("grass", "standardVS_deferred", "RotatingTilesPS_deferred", "", "", "grassTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("PatrickMat", "standardVS_deferred", "standardPS_deferred", "", "", "PatrickTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("Semechki", "standardVS_deferred", "standardPS_deferred", "standardHS_deferred", "standardDS_deferred", "Semechki_Diffuse", "Semechki_NormalMap", "Semechki_HeightMap", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, true),
        MaterialDesc("ShinyStones", "standardVS_deferred", "standardPS_deferred", "standardHS_deferred", "standardDS_deferred", "ShinyStones_Diffuse", "ShinyStones_NormalMap", "ShinyStones_HeightMap", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, true),
        MaterialDesc("SkyBox", "SkyBoxVS", "SkyBoxPS",  "", "", "SkyCubeMap", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.1f, 0.1f, 0.1f), 1.f, false),
    };


    mRenderingSystem->BuildMaterials(MaterialDescs);
}

void StencilApp::LoadMeshes()
{
    std::vector<MeshDesc> MeshDescs =
    {
        MeshDesc("Head", "../Models/african_head.obj"),
        MeshDesc("PatrickStar", "../Models/patrickstarW3LODs.fbx")
    };

    mRenderingSystem->LoadMeshes(MeshDescs);
}

void StencilApp::MakeDrawableObjects()
{
    //Has prebuilt geometries: "Box", "Grid", "Sphere", "Cylinder"

    auto SkyBoxSphere = std::make_unique<DrawableObject>();
    SkyBoxSphere->Name = "SkyBoxSphere";
    SkyBoxSphere->GeometryName = "Sphere";
    SkyBoxSphere->MaterialName = "SkyBox";
    SkyBoxSphere->RenderLayer = (int)RenderLayer::Sky;
    SkyBoxSphere->Scale = XMFLOAT3(5000.0f, 5000.0f, 5000.0f);

    mAllObjects[SkyBoxSphere->Name] = std::move(SkyBoxSphere);

    auto TesselationTestSphere = std::make_unique<DrawableObject>();
    TesselationTestSphere->Name = "TesselationTestSphere";
    TesselationTestSphere->GeometryName = "Sphere";
    TesselationTestSphere->MaterialName = "Semechki";
    TesselationTestSphere->RenderLayer = (int)RenderLayer::Opaque;
    TesselationTestSphere->WorldLocation = XMFLOAT3(5.f, 3.f, -1.f);
    TesselationTestSphere->WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    TesselationTestSphere->Scale = XMFLOAT3(5.0f, 5.0f, 5.0f);
    TesselationTestSphere->TexTransform = XMMatrixScaling(5.0f, 5.0f, 1.0f);

    mAllObjects[TesselationTestSphere->Name] = std::move(TesselationTestSphere);

    auto DecalTestCube = std::make_unique<DrawableObject>();
    DecalTestCube->Name = "DecalTestCube";
    DecalTestCube->GeometryName = "Cylinder";
    DecalTestCube->MaterialName = "skullMat";
    DecalTestCube->RenderLayer = (int)RenderLayer::Opaque;
    DecalTestCube->WorldLocation = XMFLOAT3(10.f, 3.f, 5.f);
    DecalTestCube->WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    DecalTestCube->Scale = XMFLOAT3(5.0f, 5.0f, 5.0f);
    DecalTestCube->TexTransform = XMMatrixScaling(5.0f, 5.0f, 1.0f);

    mAllObjects[DecalTestCube->Name] = std::move(DecalTestCube);


    auto Floor = std::make_unique<DrawableObject>();
    Floor->Name = "Floor";
    Floor->GeometryName = "Grid";
    Floor->MaterialName = "grass";
    Floor->RenderLayer = (int)RenderLayer::Opaque;
    Floor->WorldLocation = XMFLOAT3(0.f, 0.f, 0.f);
    Floor->WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    Floor->Scale = XMFLOAT3(5.0f, 1.0f, 5.0f);
    Floor->TexTransform = XMMatrixScaling(50.0f, 50.0f, 1.0f);

    mAllObjects[Floor->Name] = std::move(Floor);

    auto Head = std::make_unique<DrawableObject>();
    Head->Name = "Head";
    Head->GeometryName = "Head";
    Head->MaterialName = "mesh";
    Head->RenderLayer = (int)RenderLayer::Opaque;
    Head->WorldLocation = XMFLOAT3(0.f, 2.f, 0.f);
    Head->WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    Head->Scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
    Head->TexTransform = XMMatrixScaling(1.f, 1.f, 1.f);

    mAllObjects[Head->Name] = std::move(Head);

    auto Patrick = std::make_unique<DrawableObject>();
    Patrick->Name = "Patrick";
    Patrick->GeometryName = "PatrickStar";
    Patrick->MaterialName = "PatrickMat";
    Patrick->RenderLayer = (int)RenderLayer::Opaque;
    Patrick->WorldLocation = XMFLOAT3(-3.f, 2.f, 0.f);
    Patrick->WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    Patrick->Scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
    Patrick->TexTransform = XMMatrixScaling(1.f, 1.f, 1.f);

    mAllObjects[Patrick->Name] = std::move(Patrick);

    mRenderingSystem->BuildRenderItems(mAllObjects);
}

void StencilApp::MakeLights()
{
    auto Direct1 = std::make_shared<LightObject>();
    Direct1->Name = "Direct1";
    Direct1->LightType = LightType::Directional;
    Direct1->WorldRotation = { 0.57735f, -0.57735f, 0.57735f };
    Direct1->Strength = 1.f;

    mAllLightObjects[Direct1->Name] = std::move(Direct1);

    auto Point1 = std::make_shared<LightObject>();
    Point1->Name = "Point1";
    Point1->LightType = LightType::Pointlight;
    Point1->WorldLocation = { 1.f, 1.f, 1.f };
    Point1->Strength = 2.f;
    Point1->Color = { 1.f, 0.f, 0.92f };
    Point1->FalloffStart = 1.f;
    Point1->FalloffEnd = 10.f;

    mAllLightObjects[Point1->Name] = std::move(Point1);

    auto Spot1 = std::make_shared<LightObject>();
    Spot1->Name = "Spot1";
    Spot1->LightType = LightType::Spotlight;
    Spot1->WorldLocation = { 4.f, 20.f, 5.f };
    Spot1->Strength = 0.3f;
    Spot1->Color = { 0.f, 1.f, 0.f };
    Spot1->FalloffStart = 1.f;
    Spot1->FalloffEnd = 100.f;
    Spot1->SpotPower = 20.f;
    Spot1->WorldRotation = { 0.5f, -1.f, 0.f };

    mAllLightObjects[Spot1->Name] = std::move(Spot1);

    mRenderingSystem->BuildLightItems(mAllLightObjects);
}




