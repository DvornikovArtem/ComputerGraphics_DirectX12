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
    ~StencilApp() {
        for (auto& pair : mAllDrawableObjects)
            delete pair.second;
    };

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
    virtual void OnMouseWheelMove(WPARAM rotation) override;
    void OnKeyboardInput(const GameTimer& gt);

    void LoadShaders();
    void LoadTextures();
    void MakeMaterials();
    void LoadMeshes();
    void MakeDrawableObjects();
    void MakeLights();

    float mCameraMoveSpeed = 10.0f;
    POINT mLastMousePos;

    std::unordered_map<std::string, std::vector<MeshParsingResult>> MeshParsingResults;
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
    LoadMeshes();
    MakeLights();
    LoadTextures();
    MakeMaterials();
    MakeDrawableObjects();

    mRenderingSystem->mCamera.SetPosition(-1.0f, 3.0f, 5.0f);
    mRenderingSystem->mCamera.RotateY(DirectX::XM_PI - 0.2f);
    mRenderingSystem->mCamera.Pitch(DirectX::XM_PI / 12.f);

    //Called after all assets, render items and lights are initialized
    mRenderingSystem->FinishInitialize();

    //init update of all objects
    for (auto& i : mAllDrawableObjects) { DrawableObjectUpdateList.push_back(i.second); }
    for (auto& i : mAllLightObjects) { LightObjectUpdateList.push_back(i.second); }


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

    mAllDrawableObjects["Head"]->WorldRotation.y = gt.TotalTime();
    DrawableObjectUpdateList.push_back(mAllDrawableObjects["Head"]);

    mAllLightObjects["Spot1"]->Color = { 0.5f + 0.5f * cos(gt.TotalTime()) , 0.5f + 0.5f * cos(gt.TotalTime() + 1) , 0.5f + 0.5f * cos(gt.TotalTime() + 4) };
    LightObjectUpdateList.push_back(mAllLightObjects["Spot1"]);

    LightObjectUpdateList.push_back(mAllLightObjects["Direct1"]);

    mRenderingSystem->Update(DrawableObjectUpdateList, LightObjectUpdateList);
}

void StencilApp::Draw(const GameTimer& gt)
{
	mRenderingSystem->Render();
}

void StencilApp::OnMouseDown(WPARAM btnState, int x, int y)
{
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
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mRenderingSystem->mCamera.Pitch(dy);
        mRenderingSystem->mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void StencilApp::OnMouseWheelMove(WPARAM btnState)
{
    short wheelDelta = GET_WHEEL_DELTA_WPARAM(btnState);

    float& speed = mCameraMoveSpeed;
    if (wheelDelta > 0)
        speed = std::min(speed + 4.0f, 200.0f);
    else if (wheelDelta < 0)
        speed = (speed - 4.0f) > 1.0f ? (speed - 1.0f) : 1.0f;
}
 
void StencilApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        mRenderingSystem->mCamera.Walk(mCameraMoveSpeed * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        mRenderingSystem->mCamera.Walk(-mCameraMoveSpeed * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        mRenderingSystem->mCamera.Strafe(-mCameraMoveSpeed * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        mRenderingSystem->mCamera.Strafe(mCameraMoveSpeed * dt);

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
        //has prebuilt shaders "standardVS(PS/HS/DS)", "SkyBoxVS(PS)"
        ShaderDesc("RotatingTilesPS", L"../Shaders/DeferredGeometryPass.hlsl", "PS", defines, "ps_5_1"),
        ShaderDesc("HSForDecals", L"../Shaders/DeferredGeometryPass.hlsl", "HSForDecals", nullptr, "hs_5_1"),
        ShaderDesc("DSForDecals", L"../Shaders/DeferredGeometryPass.hlsl", "DSForDecals", nullptr, "ds_5_1"),
    };

    mRenderingSystem->BuildShaders(ShaderDescs);
}

void StencilApp::LoadMeshes()
{
    //Imports geometry and textures from 3D model file such as .fbx

    //ImportType::LODed
    //Will load textures from first submesh with name "Name_Diffuse(Normal, etc)" that you can use later
    //Other submeshes are assumed to be LODs and their textures are ignored
    //Returns std::vector of single parsing result with geometry and texture name
    //Use for models with LOD submeshes or single mesh models

    //ImportType::Complex
    //Will load every submesh as individual geometry with name "Name_SubmeshName"
    //All textures per submesh are imported with name "SubmeshName_Diffuse(Normal, etc)"
    //No LODs support(yet)
    //Returns std::vector of parsing results with geometry and texture names
    //Use for models that consist of multiple submeshes

    //If GenerateMaterials is set, the importer will generate a material decriptor that you can use later

    //FOR NOW UNABLE TO LOAD DDS TEXTURES FROM MESHES (use manual texture import)

    MeshParsingResults["Head"] = mRenderingSystem->LoadMesh(MeshDesc("Head", "../Models/african_head.obj", MeshDesc::ImportType::LODed), false);
    MeshParsingResults["PatrickStar"] = mRenderingSystem->LoadMesh(MeshDesc("PatrickStar", "../Models/patrickstarW5LODs.fbx", MeshDesc::ImportType::LODed), false);
    MeshParsingResults["Svidetel"] = mRenderingSystem->LoadMesh(MeshDesc("Svidetel", "../Models/Svidetel.fbx", MeshDesc::ImportType::LODed), true);
}

void StencilApp::LoadTextures()
{
    // has texture named INVALID (full black color) that is used whenever a texture is inaccessible

    // DDS textures only
    std::vector<TextureDesc> TexDescs = 
    {
        TextureDesc("bricksTex", L"../Textures/bricks3.dds", TextureDesc::Texture2D),
        TextureDesc("checkboardTex", L"../Textures/checkboard.dds", TextureDesc::Texture2D),
        TextureDesc("iceTex", L"../Textures/ice.dds", TextureDesc::Texture2D),
        TextureDesc("white1x1Tex", L"../Textures/white1x1.dds", TextureDesc::Texture2D),
        TextureDesc("AH_Diffuse", L"../Textures/african_head_diffuse.dds", TextureDesc::Texture2D),
        TextureDesc("redTex", L"../Textures/rsq.dds", TextureDesc::Texture2D),
        TextureDesc("woodCrateTex", L"../Textures/WoodCrate01.dds", TextureDesc::Texture2D),
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
    //You can modify Mesh Parsing Result materials here, before they are fully initialized
    std::vector<MaterialDesc> MaterialDescs =
    {
        MaterialDesc("bricks", "standardVS", "standardPS",  "", "", "bricksTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.25f, false),
        MaterialDesc("Bricks_DecalTesting", "standardVS", "standardPS", "HSForDecals", "DSForDecals", "bricksTex", "ShinyStones_NormalMap", "ShinyStones_HeightMap", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, true),
        MaterialDesc("AH", "standardVS", "standardPS", "", "", "AH_Diffuse", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("woodCrate", "standardVS", "RotatingTilesPS", "", "", "woodCrateTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("PatrickMat", "standardVS", "standardPS", "", "", "PatrickTex", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, false),
        MaterialDesc("Semechki", "standardVS", "standardPS", "standardHS", "standardDS", "Semechki_Diffuse", "Semechki_NormalMap", "Semechki_HeightMap", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, true),
        MaterialDesc("ShinyStones", "standardVS", "standardPS", "standardHS", "standardDS", "ShinyStones_Diffuse", "ShinyStones_NormalMap", "ShinyStones_HeightMap", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f, true),
        MaterialDesc("SkyBox", "SkyBoxVS", "SkyBoxPS",  "", "", "SkyCubeMap", "", "", XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.1f, 0.1f, 0.1f), 1.f, false),
    };

    for (auto& i : MeshParsingResults)
    {
        for (auto& j : i.second)
        {
            if (j.GenerateMaterial) MaterialDescs.push_back(j.GeneratedMaterial);
        }
    }

    mRenderingSystem->BuildMaterials(MaterialDescs);
}

void StencilApp::MakeDrawableObjects()
{
    //Has prebuilt geometries: "Box", "Grid", "Sphere", "Cylinder", "Cone"

    DrawableObject* Svidetel = new DrawableObject();
    Svidetel->Name = "Svidetel";
    Svidetel->GeometryName = MeshParsingResults["Svidetel"][0].GeometryName;
    Svidetel->MaterialName = MeshParsingResults["Svidetel"][0].GeneratedMaterial.Name;
    Svidetel->renderLayer = RenderLayer::Opaque;
    Svidetel->WorldLocation = XMFLOAT3(1.5f, 0.f, 0.f);
    Svidetel->Scale = XMFLOAT3(2.f, 2.f, 2.f);

    mAllDrawableObjects[Svidetel->Name] = Svidetel;

    DrawableObject* SkyBoxSphere = new DrawableObject();
    SkyBoxSphere->Name = "SkyBoxSphere";
    SkyBoxSphere->GeometryName = "Sphere";
    SkyBoxSphere->MaterialName = "SkyBox";
    SkyBoxSphere->renderLayer = RenderLayer::Sky;
    SkyBoxSphere->Scale = XMFLOAT3(5000.0f, 5000.0f, 5000.0f);

    mAllDrawableObjects[SkyBoxSphere->Name] = SkyBoxSphere;

    DrawableObject* TesselationTestSphere = new DrawableObject();
    TesselationTestSphere->Name = "TesselationTestSphere";
    TesselationTestSphere->GeometryName = "Sphere";
    TesselationTestSphere->MaterialName = "Semechki";
    TesselationTestSphere->renderLayer = RenderLayer::Opaque;
    TesselationTestSphere->WorldLocation = XMFLOAT3(5.f, 3.f, -1.f);
    TesselationTestSphere->Scale = XMFLOAT3(2.5f, 2.5f, 2.5f);
    TesselationTestSphere->TexTransform = XMMatrixScaling(5.0f, 5.0f, 1.0f);

    mAllDrawableObjects[TesselationTestSphere->Name] = TesselationTestSphere;

    DrawableObject* DecalTestCylinder = new DrawableObject();
    DecalTestCylinder->Name = "DecalTestCylinder";
    DecalTestCylinder->GeometryName = "Cylinder";
    DecalTestCylinder->MaterialName = "bricks";
    DecalTestCylinder->renderLayer = RenderLayer::Opaque;
    DecalTestCylinder->WorldLocation = XMFLOAT3(10.f, 3.f, 5.f);
    DecalTestCylinder->Scale = XMFLOAT3(5.0f, 5.0f, 5.0f);
    DecalTestCylinder->TexTransform = XMMatrixScaling(5.0f, 5.0f, 1.0f);

    mAllDrawableObjects[DecalTestCylinder->Name] = DecalTestCylinder;


    DrawableObject* Floor = new DrawableObject();
    Floor->Name = "Floor";
    Floor->GeometryName = "Grid";
    Floor->MaterialName = "woodCrate";
    Floor->renderLayer = RenderLayer::Opaque;
    Floor->Scale = XMFLOAT3(5.0f, 1.0f, 5.0f);
    Floor->TexTransform = XMMatrixScaling(50.0f, 50.0f, 1.0f);

    mAllDrawableObjects[Floor->Name] = Floor;

    DrawableObject* Head = new DrawableObject();
    Head->Name = "Head";
    Head->GeometryName = "Head";
    Head->MaterialName = "AH";
    Head->renderLayer = RenderLayer::Opaque;
    Head->WorldLocation = XMFLOAT3(0.f, 2.f, 0.f);

    mAllDrawableObjects[Head->Name] = Head;

    DrawableObject* Patrick = new DrawableObject();
    Patrick->Name = "Patrick";
    Patrick->GeometryName = MeshParsingResults["PatrickStar"][0].GeometryName;
    Patrick->MaterialName = "PatrickMat";
    Patrick->renderLayer = RenderLayer::Opaque;
    Patrick->WorldLocation = XMFLOAT3(-4.0f, 2.0f, 0.0f);

    mAllDrawableObjects[Patrick->Name] = Patrick;

    mRenderingSystem->BuildRenderItems(mAllDrawableObjects);
}

void StencilApp::MakeLights()
{
    auto Direct1 = new LightObject;
    Direct1->Name = "Direct1";
    Direct1->LightType = LightType::Directional;
    Direct1->WorldDirection = { 0.57735f, -0.57735f, 0.57735f };
    Direct1->Strength = 1.f;

    mAllLightObjects[Direct1->Name] = Direct1;

    auto Point1 = new LightObject;
    Point1->Name = "Point1";
    Point1->LightType = LightType::Pointlight;
    Point1->WorldLocation = { 1.f, 1.f, 1.f };
    Point1->Strength = 1.f;
    Point1->Color = { 1.f, 0.f, 0.92f };
    Point1->FalloffStart = 1.f;
    Point1->FalloffEnd = 9.f;

    mAllLightObjects[Point1->Name] = Point1;

    auto Spot1 = new LightObject;
    Spot1->Name = "Spot1";
    Spot1->LightType = LightType::Spotlight;
    Spot1->WorldLocation = { 4.f, 20.f, 5.f };
    Spot1->Strength = 0.3f; //0.3 //probably defines the brightness
    Spot1->Color = { 0.f, 1.f, 0.f };
    Spot1->FalloffStart = 1.f;
    Spot1->FalloffEnd = 100.f; //100 //defines how far it lights
    Spot1->SpotPower = 20.f; //20 //defines how sharp it is
    Spot1->WorldDirection = { 0.5f, -1.f, 0.f };

    mAllLightObjects[Spot1->Name] = Spot1;

    mRenderingSystem->BuildLightItems(mAllLightObjects);
}