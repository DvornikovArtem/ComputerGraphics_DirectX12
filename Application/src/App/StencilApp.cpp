#include <App/d3dApp.h>
#include <Engine/Math/MathHelper.h>


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

struct CameraFlyParams
{
    XMFLOAT3 StartPos;
    XMFLOAT3 EndPos;
    bool FocusOnPoint;
    XMFLOAT3 FocusCoordinate;
    float TransitionSpeed;
    bool SmoothTransitFocusPoint;

    CameraFlyParams(const XMFLOAT3& start, const XMFLOAT3& end,
        bool focus = false, const XMFLOAT3& focusPoint = { 0,0,0 },
        float speed = 5.0f, bool smoothFocus = false)
        : StartPos(start), EndPos(end), FocusOnPoint(focus),
        FocusCoordinate(focusPoint), TransitionSpeed(speed),
        SmoothTransitFocusPoint(smoothFocus) {
    }
};

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
    void MakeParticleSystems();

    float EaseInEaseOut(float t);
    void UpdateFlyCamera(const GameTimer& gt);
    void InitializeFlyRoutes();

    POINT mLastMousePos;

    std::unordered_map<std::string, std::vector<MeshParsingResult>> MeshParsingResults;

    std::vector<CameraFlyParams> mFlyRoutes;
    size_t mCurrentRouteIndex = 0;
    float mFlyProgress = 0.0f;
    float mFlyTimer = 0.0f;
    bool mIsFlying = false;
    XMFLOAT3 mCurrentFocusPoint;
    XMFLOAT3 mTargetFocusPoint;
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
        if (!theApp.Initialize()) return 0;

        return theApp.Run();
    }
    catch (DxException& ex)
    {
        MessageBoxW(nullptr, ex.WideMessage(L"Error").c_str(), L"Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        return 0;
    }
    catch (const std::exception& ex)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, nullptr, 0);
        std::wstring w(n ? n - 1 : 0, L'\0');
        if (n) MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, &w[0], n);
        MessageBoxW(nullptr, (L"[Unhandled]\n\n" + w).c_str(), L"Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
    }
}

float StencilApp::EaseInEaseOut(float t)
{
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

void StencilApp::InitializeFlyRoutes()
{
    mFlyRoutes.push_back(CameraFlyParams(
        XMFLOAT3(10.57f, 11.61f, -7.82f),
        XMFLOAT3(8.66f, 11.34f, 16.97f),
        true, XMFLOAT3(-5.52f, 9.21f, 3.73f), 5.0f, false));

    mFlyRoutes.push_back(CameraFlyParams(
        XMFLOAT3(8.66f, 11.34f, 16.97f),
        XMFLOAT3(23.11f, 14.41f, -13.76f),
        true, XMFLOAT3(-5.52f, 9.21f, 3.73f), 5.0f, false));

    mFlyRoutes.push_back(CameraFlyParams(
        XMFLOAT3(23.11f, 14.41f, -13.76f),
        XMFLOAT3(-5.53f, 5.89f, 16.41f),
        true, XMFLOAT3(25.66f, 14.98f, 5.38f), 5.0f, true));

    mFlyRoutes.push_back(CameraFlyParams(
        XMFLOAT3(-5.53f, 5.89f, 16.41f),
        XMFLOAT3(16.11f, 2.73f, -9.70f),
        true, XMFLOAT3(19.08f, 8.35f, 8.44f), 5.0f, true));

    mFlyRoutes.push_back(CameraFlyParams(
        XMFLOAT3(16.11f, 2.73f, -9.70f),
        XMFLOAT3(10.41f, 6.38f, 2.51f),
        true, XMFLOAT3(-5.02f, 2.38f, 1.20f), 5.0f, true));

    mFlyRoutes.push_back(CameraFlyParams(
        XMFLOAT3(10.41f, 6.38f, 2.51f),
        XMFLOAT3(10.57f, 11.61f, -7.82f),
        true, XMFLOAT3(-5.52f, 9.21f, 3.73f), 5.0f, true));
}

void StencilApp::UpdateFlyCamera(const GameTimer& gt)
{
    if (!mIsFlying) return;

    float dt = gt.DeltaTime();
    mFlyTimer += dt;

    const CameraFlyParams& currentRoute = mFlyRoutes[mCurrentRouteIndex];
    float currentDuration = currentRoute.TransitionSpeed;
    mFlyProgress = mFlyTimer / currentDuration;

    if (mFlyProgress >= 1.0f)
    {
        mCurrentRouteIndex = (mCurrentRouteIndex + 1) % mFlyRoutes.size();
        mFlyTimer = 0.0f;
        mFlyProgress = 0.0f;

        mCurrentFocusPoint = currentRoute.FocusCoordinate;
        mTargetFocusPoint = mFlyRoutes[mCurrentRouteIndex].FocusCoordinate;
        return;
    }

    float easedProgress = EaseInEaseOut(mFlyProgress);

    XMFLOAT3 currentPos;
    currentPos.x = currentRoute.StartPos.x + (currentRoute.EndPos.x - currentRoute.StartPos.x) * easedProgress;
    currentPos.y = currentRoute.StartPos.y + (currentRoute.EndPos.y - currentRoute.StartPos.y) * easedProgress;
    currentPos.z = currentRoute.StartPos.z + (currentRoute.EndPos.z - currentRoute.StartPos.z) * easedProgress;

    mRenderingSystem->mCamera.SetPosition(currentPos);

    if (currentRoute.FocusOnPoint)
    {
        if (currentRoute.SmoothTransitFocusPoint)
        {
            float smoothProgress = easedProgress;
            XMFLOAT3 currentFocus;
            currentFocus.x = mCurrentFocusPoint.x + (mTargetFocusPoint.x - mCurrentFocusPoint.x) * smoothProgress;
            currentFocus.y = mCurrentFocusPoint.y + (mTargetFocusPoint.y - mCurrentFocusPoint.y) * smoothProgress;
            currentFocus.z = mCurrentFocusPoint.z + (mTargetFocusPoint.z - mCurrentFocusPoint.z) * smoothProgress;

            mRenderingSystem->mCamera.LookAt(currentFocus);
        }
        else mRenderingSystem->mCamera.LookAt(currentRoute.FocusCoordinate);
    }
    else mRenderingSystem->mCamera.UpdateViewMatrix();
}

bool StencilApp::Initialize()
{
    if(!D3DApp::Initialize()) return false;

    LoadShaders();
    LoadMeshes();
    MakeLights();
    LoadTextures();
    MakeMaterials();
    MakeDrawableObjects();
    MakeParticleSystems();

    InitializeFlyRoutes();
    mIsFlying = true;

    mRenderingSystem->mCamera.SetPosition(-1.0f, 3.0f, 5.0f);
    mRenderingSystem->mCamera.RotateY(DirectX::XM_PI - 0.2f);
    mRenderingSystem->mCamera.Pitch(DirectX::XM_PI / 12.f);
    mRenderingSystem->mCamera.UpdateViewMatrix();

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
    if (mIsFlying) UpdateFlyCamera(gt);
    else OnKeyboardInput(gt);

    //Set NeedsUpdate for every object that changes its values at runtime

    mAllDrawableObjects["Head"]->WorldRotation.y = gt.TotalTime() * 0.5;
    DrawableObjectUpdateList.push_back(mAllDrawableObjects["Head"]);

    LightObjectUpdateList.push_back(mAllLightObjects["Direct1"]);

    mAllLightObjects["Point1"]->WorldLocation = { -4.f + sin(gt.TotalTime()) * 3, 4.f, 2.f + cos(gt.TotalTime()) * 3 };
    mAllLightObjects["Point1"]->Color = { 0.5f + 0.5f * cos(gt.TotalTime()) , 0.5f + 0.5f * cos(gt.TotalTime() + 1) , 0.5f + 0.5f * cos(gt.TotalTime() + 4) };
    LightObjectUpdateList.push_back(mAllLightObjects["Point1"]);

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
    if (GetCapture() == mhMainWnd) ReleaseCapture();
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

    float speed = mRenderingSystem->mCamera.GetMoveSpeed();
    if (wheelDelta > 0) speed = std::min(speed + 4.0f, mRenderingSystem->mCamera.GetMaxMoveSpeed());
    else if (wheelDelta < 0) speed = (std::max)(speed - 4.0f, mRenderingSystem->mCamera.GetMinMoveSpeed());

    mRenderingSystem->mCamera.SetMoveSpeed(speed);
}
 
void StencilApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();
    const float speed = mRenderingSystem->mCamera.GetMoveSpeed();

    if (GetAsyncKeyState('W') & 0x8000)
        mRenderingSystem->mCamera.Walk(speed * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        mRenderingSystem->mCamera.Walk(-speed * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        mRenderingSystem->mCamera.Strafe(-speed * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        mRenderingSystem->mCamera.Strafe(speed * dt);

    if (GetAsyncKeyState('Q') & 0x8000)
        mRenderingSystem->mCamera.VerticalMove(-speed * dt * 0.5);

    if (GetAsyncKeyState('E') & 0x8000)
        mRenderingSystem->mCamera.VerticalMove(speed * dt * 0.5);

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
        ShaderDesc("RotatingTilesPS", SHADERS_ENGINE_DIR "DeferredGeometryPass.hlsl", "PS", defines, "ps"),
        ShaderDesc("EmitCS", SHADERS_ENGINE_DIR "ParticleCS.hlsl", "EmitCS", nullptr, "cs"),
        ShaderDesc("SimulateCS", SHADERS_ENGINE_DIR "ParticleCS.hlsl", "SimulateCS", nullptr, "cs"),
        ShaderDesc("SimulateCS2", SHADERS_ENGINE_DIR "ParticleCS.hlsl", "SimulateCS2", nullptr, "cs"),
        ShaderDesc("EmitSmokeCS", SHADERS_ENGINE_DIR "ParticleCS.hlsl", "EmitSmokeCS", nullptr, "cs"),
        ShaderDesc("SimulateSmokeCS", SHADERS_ENGINE_DIR "ParticleCS.hlsl", "SimulateSmokeCS", nullptr, "cs"),
        
        ShaderDesc("TerrainVS", SHADERS_ENGINE_DIR "Terrain.hlsl", "VS", nullptr, "vs"),
        ShaderDesc("TerrainPS", SHADERS_ENGINE_DIR "Terrain.hlsl", "PS", nullptr, "ps"),
        ShaderDesc("TerrainGS", SHADERS_ENGINE_DIR "Terrain.hlsl", "GS", nullptr, "gs"),
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

    MeshParsingResults["Head"] = mRenderingSystem->LoadMesh(MeshDesc("Head", "assets/models/african_head.obj", MeshDesc::ImportType::SingleMesh), false);
    MeshParsingResults["PatrickStar"] = mRenderingSystem->LoadMesh(MeshDesc("PatrickStar", "assets/models/patrickstarW5LODs.fbx", MeshDesc::ImportType::LODed), false);
    MeshParsingResults["Svidetel"] = mRenderingSystem->LoadMesh(MeshDesc("Svidetel", "assets/models/Svidetel.fbx", MeshDesc::ImportType::SingleMesh), true);
    MeshParsingResults["Erato"] = mRenderingSystem->LoadMesh(MeshDesc("Erato", "assets/models/Erato.fbx", MeshDesc::ImportType::SingleMesh), true);

    for (int i = 1; i < 22; i++)
    {
        std::string name = std::to_string(i);
        std::string path = "assets/models/Room/" + name + ".fbx";
        MeshParsingResults[name] =
            mRenderingSystem->LoadMesh(MeshDesc(name, path, MeshDesc::ImportType::SingleMesh), true);
    }
}

void StencilApp::LoadTextures()
{
    // has texture named INVALID (full black color) that is used whenever a texture is inaccessible

    // DDS textures only
    std::vector<TextureDesc> TexDescs = 
    {
        TextureDesc("bricksTex", L"assets/textures/bricks3.dds", TextureDesc::Texture2D, true),
        TextureDesc("checkboardTex", L"assets/textures/checkboard.dds", TextureDesc::Texture2D, true),
        TextureDesc("iceTex", L"assets/textures/ice.dds", TextureDesc::Texture2D, true),
        TextureDesc("white1x1Tex", L"assets/textures/white1x1.dds", TextureDesc::Texture2D, true),
        TextureDesc("yellow1x1Tex", L"assets/textures/yellow1x1.dds", TextureDesc::Texture2D, true),
        TextureDesc("AH_Diffuse", L"assets/textures/african_head_diffuse.dds", TextureDesc::Texture2D, true),
        TextureDesc("redTex", L"assets/textures/rsq.dds", TextureDesc::Texture2D, true),
        TextureDesc("PatrickTex", L"assets/textures/patrickstar.dds", TextureDesc::Texture2D, true),
        TextureDesc("SkyPref", L"assets/textures/skyPrefilter.dds", TextureDesc::CubeMap, true),
        TextureDesc("SkyBRDF", L"assets/textures/skyBrdf.dds", TextureDesc::Texture2D, false),
        TextureDesc("SkyIrradiance", L"assets/textures/skyIrradiance.dds", TextureDesc::CubeMap, false),

        TextureDesc("Picture", L"assets/textures/picture.dds", TextureDesc::Texture2D, true),
        TextureDesc("OrangeTex", L"assets/textures/orange1x1.dds", TextureDesc::Texture2D, true),

        //TextureDesc("SkyPref", L"assets/textures/roomPrefilter.dds", TextureDesc::CubeMap, true),
        //TextureDesc("SkyBRDF", L"assets/textures/roomBrdf.dds", TextureDesc::Texture2D, false),
        //TextureDesc("SkyIrradiance", L"assets/textures/roomIrradiance.dds", TextureDesc::CubeMap, false),
    };

    mRenderingSystem->LoadTextures(TexDescs);
}

void StencilApp::MakeMaterials()
{
    //You can modify Mesh Parsing Result materials here, before they are fully initialized
    std::vector<MaterialDesc> MaterialDescs =
    {
        MaterialDesc("bricks", "standardVS", "standardPS",  "", "", "bricksTex", "", "", 0.25f, 0.f, false),
        MaterialDesc("Bricks_DecalTesting", "standardVS", "standardPS", "", "", "bricksTex", "ShinyStones_NormalMap", "ShinyStones_HeightMap", 0.3f, 0.f, false),
        MaterialDesc("AH", "standardVS", "standardPS", "", "", "AH_Diffuse", "", "", 0.99f, 0.f, false),
        MaterialDesc("PatrickMat", "standardVS", "standardPS", "", "", "PatrickTex", "", "", 0.3f, 0.f, false),
        MaterialDesc("SkyBox", "SkyBoxVS", "SkyBoxPS",  "", "", "SkyPref", "", "", 1.f, 0.f, false),
        MaterialDesc("MetallicYellow", "standardVS", "standardPS", "", "", "yellow1x1Tex", "", "", 0.3f, 0.8f, false),
        MaterialDesc("Picture", "standardVS", "standardPS", "", "", "Picture", "", "", 0.3f, 0.0f, false),
        MaterialDesc("Black", "standardVS", "standardPS", "", "", "", "", "", 0.3f, 0.0f, false),
        MaterialDesc("Orange", "standardVS", "standardPS", "", "", "OrangeTex", "", "", 0.3f, 0.3f, false),
    };

    MeshParsingResults["Svidetel"][0].GeneratedMaterial.Roughness = 0.99f;
    MeshParsingResults["Erato"][0].GeneratedMaterial.Metallic = 0.01f;
    MeshParsingResults["Erato"][0].GeneratedMaterial.Roughness = 0.99f;

    for (auto& i : MeshParsingResults)
    {
        for (auto& j : i.second)
        {
            if (j.GenerateMaterial) MaterialDescs.push_back(j.GeneratedMaterial);
        }
    }

    for (int row = 0; row < 11; ++row) {
        for (int col = 0; col < 11; ++col) {
            float metallic = row / 10.0f;
            float roughness = col / 10.0f;
            std::string matName = "sphere_mat_" + std::to_string(row) + "_" + std::to_string(col);

            MaterialDesc sphereMat(
                matName,
                "standardVS",
                "standardPS",
                "", "",
                "white1x1Tex",
                "", "",
                roughness,
                metallic,
                false
            );

            MaterialDescs.push_back(sphereMat);
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
    Svidetel->WorldLocation = XMFLOAT3(-11.f, 4.9f, -2.f);
    Svidetel->Scale = XMFLOAT3(3.f, 3.f, 3.f);
    Svidetel->WorldRotation = { 0, DirectX::XM_PI / 2, 0 };

    mAllDrawableObjects[Svidetel->Name] = Svidetel;

    DrawableObject* SkyBoxSphere = new DrawableObject();
    SkyBoxSphere->Name = "SkyBoxSphere";
    SkyBoxSphere->GeometryName = "Sphere";
    SkyBoxSphere->MaterialName = "SkyBox";
    SkyBoxSphere->renderLayer = RenderLayer::Sky;
    SkyBoxSphere->Scale = XMFLOAT3(5000.0f, 5000.0f, 5000.0f);

    mAllDrawableObjects[SkyBoxSphere->Name] = SkyBoxSphere;

    DrawableObject* Head = new DrawableObject();
    Head->Name = "Head";
    Head->GeometryName = "Head";
    Head->MaterialName = "AH";
    Head->renderLayer = RenderLayer::Opaque;
    Head->WorldLocation = XMFLOAT3(-5.f, 2.f, 2.f);

    mAllDrawableObjects[Head->Name] = Head;

    DrawableObject* Patrick1 = new DrawableObject();
    Patrick1->Name = "Patrick";
    Patrick1->GeometryName = MeshParsingResults["PatrickStar"][0].GeometryName;
    Patrick1->MaterialName = "PatrickMat";
    Patrick1->renderLayer = RenderLayer::Opaque;
    Patrick1->WorldLocation = XMFLOAT3(-0.0f, 7.f, -2.0f);
    Patrick1->WorldRotation = { 0, -DirectX::XM_PI / 2, 0 };
    Patrick1->Scale = { 2.f, 2.f, 2.f };

    mAllDrawableObjects[Patrick1->Name] = Patrick1;

    DrawableObject* Erato = new DrawableObject();
    Erato->Name = "Erato";
    Erato->GeometryName = MeshParsingResults["Erato"][0].GeometryName;
    Erato->MaterialName = MeshParsingResults["Erato"][0].GeneratedMaterial.Name;
    Erato->renderLayer = RenderLayer::Opaque;
    Erato->WorldLocation = XMFLOAT3(22.0f, 1.f, 15.0f);
    Erato->WorldRotation = { 0, -DirectX::XM_PI / 2, 0 };
    Erato->Scale = { 0.5f, 0.5f, 0.5f };

    mAllDrawableObjects[Erato->Name] = Erato;

    const float spacing = 2.0f;
    XMFLOAT3 StartPosition = XMFLOAT3(30.0f, 15.0f, 1.0f);
    for (int row = 0; row < 11; ++row)
    {
        for (int col = 0; col < 11; ++col)
        {
            auto sphere = new DrawableObject();
            sphere->Name = "Sphere" + std::to_string(row) + "_" + std::to_string(col);
            sphere->GeometryName = "Sphere";
            sphere->MaterialName = "sphere_mat_" + std::to_string(row) + "_" + std::to_string(col);
            sphere->renderLayer = RenderLayer::Opaque;
            sphere->Scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
            sphere->WorldLocation = XMFLOAT3(
                StartPosition.x,
                StartPosition.y + (row - 5) * spacing,
                StartPosition.z + (col - 5) * spacing);

            mAllDrawableObjects[sphere->Name] = sphere;
        }
    }

    for (int i = 1; i < 22; i++)
    {
        std::string name = std::to_string(i);
        auto RoomObject = new DrawableObject();
        RoomObject->Name = "Room_" + name;
        RoomObject->GeometryName = MeshParsingResults[name][0].GeometryName;
        RoomObject->renderLayer = RenderLayer::Opaque;
        RoomObject->Scale = { 3.0f, 3.0f, 3.0f };
        RoomObject->WorldLocation = { 0.f, 5.f, 0.f };
        RoomObject->WorldRotation = { 0.f, DirectX::XM_PI / 2, 0.f };
        RoomObject->MaterialName = "sphere_mat_0_0";

        if (i == 21 || i == 10 || i == 15 || i == 4 || i == 17) RoomObject->MaterialName = MeshParsingResults[name][0].GeneratedMaterial.Name;
        if (i == 12) RoomObject->MaterialName = "Orange";
        if (i == 21) RoomObject->WorldLocation.x = -0.5f;

        mAllDrawableObjects[RoomObject->Name] = RoomObject;
    }

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
    Point1->Strength = 2.f;
    Point1->Color = { 1.f, 0.f, 0.92f };
    Point1->FalloffStart = 1.f;
    Point1->FalloffEnd = 9.f;

    mAllLightObjects[Point1->Name] = Point1;

    auto Point2 = new LightObject;
    Point2->Name = "Point2";
    Point2->LightType = LightType::Pointlight;
    Point2->WorldLocation = { -4.5f, 14.f, -3.f };
    Point2->Strength = 2.f;
    Point2->Color = { 1.f, 1.f, 1.f };
    Point2->FalloffStart = 1.f;
    Point2->FalloffEnd = 9.f;

    mAllLightObjects[Point2->Name] = Point2;

    auto Point3 = new LightObject;
    Point3->Name = "Point3";
    Point3->LightType = LightType::Pointlight;
    Point3->WorldLocation = { -4.5f, 14.f, 8.f };
    Point3->Strength = 2.f;
    Point3->Color = { 1.f, 1.f, 1.f };
    Point3->FalloffStart = 1.f;
    Point3->FalloffEnd = 9.f;

    mAllLightObjects[Point3->Name] = Point3;

    mRenderingSystem->BuildLightItems(mAllLightObjects);
}

void StencilApp::MakeParticleSystems()
{
    ParticleSystemDescriptor fireworkParticleSystemDesc;
    
    fireworkParticleSystemDesc.name = "fireworkParticleSystem";
    fireworkParticleSystemDesc.emitterPosition = { -7.0f, 9.5f, 0.5f };
    fireworkParticleSystemDesc.numParticlesToEmit = 10;
    fireworkParticleSystemDesc.maxParticles = 256;
    fireworkParticleSystemDesc.particleSize = 0.1f;
    fireworkParticleSystemDesc.emitComputeShaderName = "EmitCS";
    fireworkParticleSystemDesc.simulateComputeShaderName = "SimulateCS2";
    fireworkParticleSystemDesc.particleGeometryName = "2DCircle";
    fireworkParticleSystemDesc.IsBillboard = true;

    mParticleSystemDescriptors[fireworkParticleSystemDesc.name] = fireworkParticleSystemDesc;

    mRenderingSystem->BuildParticleSystems(mParticleSystemDescriptors);
}
