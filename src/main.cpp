#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include "Halton.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

static const wchar_t* kShaderPath = L"shaders/Clouds.hlsl";

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

struct CameraCB {
    XMMATRIX view;
    XMMATRIX proj;
    XMMATRIX prevViewProj;
    XMFLOAT3 camPos;
    float    time;
    XMFLOAT2 jitter;
    float    taaEnabled;
    float    pad0;
};

struct WeatherCB {
    XMFLOAT3 sunDir; float g;
    float coverage; float density; float baseHeight; float thickness;
    XMFLOAT2 wind; float stepMul; float jitterEnabled;
};

struct FrameContext {
    HWND hwnd{};
    UINT width{1280}, height{720};
    bool running{true};
    float moveSpeed = 8.f;
    float yaw = 0.0f, pitch = 0.0f;
    XMFLOAT3 camPos{0, 1000, -1500};
    float sunAz = 45.f * XM_PI/180.f;
    float sunEl = 20.f * XM_PI/180.f;
    float g = 0.65f;
    float coverage = 0.45f;
    float density = 0.8f;
    float baseH = 1500.f;
    float thick = 2000.f;
    XMFLOAT2 wind{15.f, 5.f};
    float stepMul = 1.0f;
    bool taa = true;
    bool jitter = true;
};

bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

HRESULT CompileShader(const wchar_t* path, const char* entry, const char* target, ID3DBlob** blob) {
    DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, flags, 0, blob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
    }
    return hr;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    FrameContext fc;

    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"CloudWnd";
    RegisterClassEx(&wc);

    RECT r{0,0,(LONG)fc.width,(LONG)fc.height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"Volumetric Clouds (D3D11)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, 0, 0, hInst, 0);
    fc.hwnd = hwnd;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = fc.width;
    sd.BufferDesc.Height = fc.height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* device{}; ID3D11DeviceContext* ctx{}; IDXGISwapChain* swap{};
    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL req[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, req, 1,
        D3D11_SDK_VERSION, &sd, &swap, &device, &fl, &ctx);
    if (FAILED(hr)) return 0;

    auto CreateRT = [&](UINT w, UINT h, ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1;
        td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        device->CreateTexture2D(&td, nullptr, tex);
        device->CreateRenderTargetView(*tex, nullptr, rtv);
        device->CreateShaderResourceView(*tex, nullptr, srv);
    };

    ID3D11Texture2D* bbTex{}; ID3D11RenderTargetView* bbRTV{}; ID3D11ShaderResourceView* bbSRV{};
    CreateRT(fc.width, fc.height, &bbTex, &bbRTV, &bbSRV);

    ID3D11Texture2D* histTex{}; ID3D11RenderTargetView* histRTV{}; ID3D11ShaderResourceView* histSRV{};
    CreateRT(fc.width, fc.height, &histTex, &histRTV, &histSRV);

    ID3D11Texture2D* scBB{}; swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&scBB);
    ID3D11RenderTargetView* scRTV{}; device->CreateRenderTargetView(scBB, nullptr, &scRTV);
    scBB->Release();

    ID3DBlob* vsBlob{}; ID3DBlob* psBlob{}; ID3DBlob* blitBlob{};
    CompileShader(L"shaders/Clouds.hlsl", "VSFull", "vs_5_0", &vsBlob);
    CompileShader(L"shaders/Clouds.hlsl", "PSClouds", "ps_5_0", &psBlob);
    CompileShader(L"shaders/Clouds.hlsl", "PSBlit", "ps_5_0", &blitBlob);

    ID3D11VertexShader* vs{}; device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
    ID3D11PixelShader* ps{}; device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
    ID3D11PixelShader* psBlit{}; device->CreatePixelShader(blitBlob->GetBufferPointer(), blitBlob->GetBufferSize(), nullptr, &psBlit);
    ID3D11InputLayout* il{};

    ID3D11SamplerState* samp{};
    D3D11_SAMPLER_DESC sdsc{}; sdsc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sdsc.AddressU=sdsc.AddressV=sdsc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sdsc, &samp);

    ID3D11Buffer* cbCamera{}; ID3D11Buffer* cbWeather{};
    auto MakeCB=[&](UINT size, ID3D11Buffer** out){
        D3D11_BUFFER_DESC bd{}; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; bd.ByteWidth=((size+15)/16)*16; bd.Usage=D3D11_USAGE_DYNAMIC; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&bd,nullptr,out);
    };
    MakeCB(sizeof(CameraCB), &cbCamera);
    MakeCB(sizeof(WeatherCB), &cbWeather);

    XMMATRIX prevVP = XMMatrixIdentity();
    uint32_t frameIndex = 1;
    auto t0 = std::chrono::high_resolution_clock::now();

    while (fc.running) {
        MSG msg; while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message==WM_QUIT) fc.running=false;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        if (!fc.running) break;

        float dt = 1.0f/60.0f;
        XMVECTOR forward = XMVector3Normalize(XMVectorSet(sinf(fc.yaw), 0, cosf(fc.yaw), 0));
        XMVECTOR right = XMVector3Cross(forward, XMVectorSet(0,1,0,0));
        XMVECTOR pos = XMLoadFloat3(&fc.camPos);
        if (KeyDown('W')) pos = XMVectorAdd(pos, XMVectorScale(forward, fc.moveSpeed));
        if (KeyDown('S')) pos = XMVectorAdd(pos, XMVectorScale(forward, -fc.moveSpeed));
        if (KeyDown('A')) pos = XMVectorAdd(pos, XMVectorScale(right, -fc.moveSpeed));
        if (KeyDown('D')) pos = XMVectorAdd(pos, XMVectorScale(right, fc.moveSpeed));
        if (KeyDown('Q')) pos = XMVectorAdd(pos, XMVectorSet(0,-fc.moveSpeed,0,0));
        if (KeyDown('E')) pos = XMVectorAdd(pos, XMVectorSet(0,fc.moveSpeed,0,0));
        XMStoreFloat3(&fc.camPos, pos);

        if (KeyDown(VK_LEFT))  fc.wind.x -= 0.1f;
        if (KeyDown(VK_RIGHT)) fc.wind.x += 0.1f;
        if (KeyDown(VK_UP))    fc.wind.y += 0.1f;
        if (KeyDown(VK_DOWN))  fc.wind.y -= 0.1f;
        if (KeyDown('Z')) fc.coverage = max(0.f, fc.coverage - 0.002f);
        if (KeyDown('X')) fc.coverage = min(1.f, fc.coverage + 0.002f);
        if (KeyDown('C')) fc.density = max(0.f, fc.density - 0.002f);
        if (KeyDown('V')) fc.density = min(2.f, fc.density + 0.002f);
        if (KeyDown('O')) fc.sunAz -= 0.002f;
        if (KeyDown('P')) fc.sunAz += 0.002f;
        if (KeyDown('K')) fc.sunEl = max(-XM_PIDIV2*0.9f, fc.sunEl - 0.002f);
        if (KeyDown('L')) fc.sunEl = min(XM_PIDIV2*0.9f,  fc.sunEl + 0.002f);
        if (GetAsyncKeyState('T') & 1) fc.taa = !fc.taa;
        if (GetAsyncKeyState('F') & 1) fc.jitter = !fc.jitter;
        if (GetAsyncKeyState('R') & 1) { fc.coverage=0.45f; fc.density=0.8f; fc.wind={15,5}; }

        float aspect = (float)fc.width / (float)fc.height;
        XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), aspect, 0.1f, 50000.f);
        XMVECTOR p = XMLoadFloat3(&fc.camPos);
        XMVECTOR f = XMVectorSet(cosf(fc.pitch)*sinf(fc.yaw), sinf(fc.pitch), cosf(fc.pitch)*cosf(fc.yaw), 0);
        XMVECTOR up = XMVectorSet(0,1,0,0);
        XMMATRIX view = XMMatrixLookToLH(p, f, up);
        XMMATRIX vp = XMMatrixMultiply(view, proj);

        float jx = 0.f, jy = 0.f;
        if (fc.jitter) { jx = (Halton(frameIndex,2) - 0.5f) / fc.width; jy = (Halton(frameIndex,3) - 0.5f) / fc.height; }

        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(cbCamera, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            auto* c = (CameraCB*)map.pData;
            c->view = XMMatrixTranspose(view);
            c->proj = XMMatrixTranspose(proj);
            c->prevViewProj = XMMatrixTranspose(prevVP);
            c->camPos = fc.camPos;
            auto now = std::chrono::high_resolution_clock::now();
            c->time = std::chrono::duration<float>(now.time_since_epoch()).count()*0.001f;
            c->jitter = XMFLOAT2(jx, jy);
            c->taaEnabled = fc.taa ? 1.f : 0.f;
            ctx->Unmap(cbCamera, 0);
        }
        if (SUCCEEDED(ctx->Map(cbWeather, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            auto* w = (WeatherCB*)map.pData;
            XMFLOAT3 s{ cosf(fc.sunEl)*cosf(fc.sunAz), sinf(fc.sunEl), cosf(fc.sunEl)*sinf(fc.sunAz) };
            w->sunDir = s; w->g = fc.g;
            w->coverage = fc.coverage; w->density = fc.density; w->baseHeight = fc.baseH; w->thickness = fc.thick;
            w->wind = fc.wind; w->stepMul = fc.stepMul; w->jitterEnabled = fc.jitter ? 1.f : 0.f;
            ctx->Unmap(cbWeather, 0);
        }

        // Render clouds into bbRTV
        ID3D11RenderTargetView* rt[] = { bbRTV };
        ctx->OMSetRenderTargets(1, rt, nullptr);
        float clearC[4]{0.6f,0.75f,0.95f,1};
        ctx->ClearRenderTargetView(bbRTV, clearC);
        D3D11_VIEWPORT vpD{}; vpD.Width=(float)fc.width; vpD.Height=(float)fc.height; vpD.MinDepth=0; vpD.MaxDepth=1;
        ctx->RSSetViewports(1, &vpD);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetSamplers(0, 1, &samp);
        ctx->PSSetConstantBuffers(0, 1, &cbCamera);
        ctx->PSSetConstantBuffers(1, 1, &cbWeather);
        ctx->PSSetShaderResources(0, 1, &histSRV);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->IASetInputLayout(il);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(3,0);

        // accumulate into history
        ctx->CopyResource(histTex, bbTex);

        // blit to swapchain
        ID3D11Texture2D* scBB{}; swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&scBB);
        ID3D11RenderTargetView* scRTV{}; device->CreateRenderTargetView(scBB, nullptr, &scRTV);
        ctx->OMSetRenderTargets(1, &scRTV, nullptr);
        ID3D11ShaderResourceView* src = histSRV;
        ctx->PSSetShaderResources(0, 1, &src);
        ctx->PSSetShader(psBlit, nullptr, 0);
        ctx->Draw(3,0);
        scRTV->Release(); scBB->Release();

        swap->Present(1,0);
        prevVP = vp;
        frameIndex++;
        ID3D11ShaderResourceView* nullSRV{}; ctx->PSSetShaderResources(0,1,&nullSRV);
        if (GetAsyncKeyState(VK_ESCAPE)&1) fc.running=false;
    }

    return 0;
}
