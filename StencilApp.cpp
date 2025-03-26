//***************************************************************************************
// StencilApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

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
    StencilApp(HINSTANCE hInstance);
    StencilApp(const StencilApp& rhs) = delete;
    StencilApp& operator=(const StencilApp& rhs) = delete;
    ~StencilApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);

    void BuildRenderItems();

};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
//int main()
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        StencilApp theApp(hInstance);
		//StencilApp theApp(0);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

StencilApp::StencilApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

StencilApp::~StencilApp() {
	if (mRenderingSystem->getd3dDevice() != nullptr) {
		mRenderingSystem->FlushCommandQueue();
	}
}

bool StencilApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    return true;
}
 
void StencilApp::OnResize()
{
    D3DApp::OnResize();

}

void StencilApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);


	mRenderingSystem->Update();
}

void StencilApp::Draw(const GameTimer& gt)
{
	mRenderingSystem->Render(gt);
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
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mRenderingSystem->mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mRenderingSystem->mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
		mRenderingSystem->mTheta -= dx;
		mRenderingSystem->mPhi -= dy;

        // Restrict the angle mPhi.
		mRenderingSystem->mPhi = MathHelper::Clamp(mRenderingSystem->mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.2f*static_cast<float>(x - mRenderingSystem->mLastMousePos.x);
        float dy = 0.2f*static_cast<float>(y - mRenderingSystem->mLastMousePos.y);

        // Update the camera radius based on input.
		mRenderingSystem->mRadius += dx - dy;

        // Restrict the radius.
		mRenderingSystem->mRadius = MathHelper::Clamp(mRenderingSystem->mRadius, 5.0f, 150.0f);
    }

	mRenderingSystem->mLastMousePos.x = x;
	mRenderingSystem->mLastMousePos.y = y;
}
 
void StencilApp::OnKeyboardInput(const GameTimer& gt)
{

}
 
void StencilApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mRenderingSystem->mEyePos.x = mRenderingSystem->mRadius*sinf(mRenderingSystem->mPhi)*cosf(mRenderingSystem->mTheta);
	mRenderingSystem->mEyePos.z = mRenderingSystem->mRadius*sinf(mRenderingSystem->mPhi)*sinf(mRenderingSystem->mTheta);
	mRenderingSystem->mEyePos.y = mRenderingSystem->mRadius*cosf(mRenderingSystem->mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mRenderingSystem->mEyePos.x, mRenderingSystem->mEyePos.y, mRenderingSystem->mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mRenderingSystem->mView, view);
}

void StencilApp::BuildRenderItems()
{
    mRenderingSystem->BuildRenderItems();
}