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

    mRenderingSystem->mCamera.SetPosition(-1.0f, 3.0f, 5.0f);
    mRenderingSystem->mCamera.RotateY(DirectX::XM_PI - 0.2f);
    mRenderingSystem->mCamera.Pitch(DirectX::XM_PI / 12.f);

    //Called after all assets and render items are initialized
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

    mAllObjects["Head"]->WorldRotation.y = gt.TotalTime();

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
        ShaderDesc("standardVS", L"../Shaders/Default.hlsl", "VS", nullptr, "vs_5_0"),
        ShaderDesc("standardPS", L"../Shaders/Default.hlsl", "PS", nullptr, "ps_5_0"),
        ShaderDesc("alphaTestedPS", L"../Shaders/Default.hlsl", "PS", alphaTestDefines, "ps_5_0"),
        ShaderDesc("RotatingTilesPS", L"../Shaders/Default.hlsl", "PS", defines, "ps_5_0"),
        ShaderDesc("standardHS", L"../Shaders/Default.hlsl", "HSMain", nullptr, "hs_5_0"),
        ShaderDesc("standardDS", L"../Shaders/Default.hlsl", "DSMain", nullptr, "ds_5_0")
    };

    mRenderingSystem->BuildShaders(ShaderDescs);
}

void StencilApp::LoadTextures()
{
    // First Texture in the list will be used as invalid texture

    std::vector<TextureDesc> TexDescs = 
    {
        TextureDesc("INVALID", L"../Textures/INVALID.dds"),
        TextureDesc("bricksTex", L"../Textures/bricks3.dds"),
        TextureDesc("checkboardTex", L"../Textures/checkboard.dds"),
        TextureDesc("iceTex", L"../Textures/ice.dds"),
        TextureDesc("white1x1Tex", L"../Textures/white1x1.dds"),
        TextureDesc("meshTex", L"../Textures/african_head_diffuse.dds"),
        TextureDesc("redTex", L"../Textures/rsq.dds"),
        TextureDesc("grassTex", L"../Textures/WoodCrate01.dds"),
        TextureDesc("PatrickTex", L"../Textures/patrickstar.dds"),
        TextureDesc("Semechki_Diffuse", L"../Textures/semente_BaseColor.dds"),
        TextureDesc("Semechki_NormalMap", L"../Textures/semente_Normal.dds"),
        TextureDesc("Semechki_HeightMap", L"../Textures/semente_Height.dds")
    };

    mRenderingSystem->LoadTextures(TexDescs);
}

void StencilApp::MakeMaterials()
{
    std::vector<MaterialDesc> MaterialDescs =
    {
        MaterialDesc("bricks", "standardVS", "standardPS", "bricksTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.25f, false),
        MaterialDesc("checkertile", "standardVS", "standardPS", "checkboardTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.07f, 0.07f, 0.07f), 0.3f, false),
        MaterialDesc("icemirror", "standardVS", "standardPS", "iceTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f), XMFLOAT3(0.1f, 0.1f, 0.1f), 0.5f, false),
        MaterialDesc("skullMat", "standardVS", "standardPS", "white1x1Tex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("shadowMat", "standardVS", "standardPS", "redTex", "", "", XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f), XMFLOAT3(0.001f, 0.001f, 0.001f), 0.0f, false),
        MaterialDesc("mesh", "standardVS", "standardPS", "meshTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("grass", "standardVS", "RotatingTilesPS", "grassTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("PatrickMat", "standardVS", "standardPS", "PatrickTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("Semechki", "standardVS", "standardPS", "Semechki_Diffuse", "Semechki_NormalMap", "Semechki_HeightMap", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, true)
    };

    mRenderingSystem->BuildMaterials(MaterialDescs);
}

void StencilApp::LoadMeshes()
{
    std::vector<MeshDesc> MeshDescs =
    {
        MeshDesc("Head", "../Models/african_head.obj"),
        MeshDesc("PatrickStar", "../Models/patrickstar.obj")
    };

    mRenderingSystem->LoadMeshes(MeshDescs);
}

void StencilApp::MakeDrawableObjects()
{
    //Has prebuilt geometries: "Box", "Grid", "Sphere", "Cylinder"

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

    auto Floor = std::make_unique<DrawableObject>();
    Floor->Name = "Floor";
    Floor->GeometryName = "Grid";
    Floor->MaterialName = "grass";
    Floor->RenderLayer = (int)RenderLayer::Opaque;
    Floor->WorldLocation = XMFLOAT3(0.f, 0.f, 0.f);
    Floor->WorldRotation = XMFLOAT3(0.f, 0.f, 0.f);
    Floor->Scale = XMFLOAT3(10.0f, 1.0f, 10.0f);
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




