// Real Win32 child-window + WARP DXGI presentation. This fixture exercises the
// production detours/lifecycle, not an emulator or physical vendor GPU.
#include "Runtime.h"
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace BsiShaders;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool v,const char *text){if(!v){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
void okay(HRESULT hr,const char *text){if(FAILED(hr)){std::cerr<<"FAIL: "<<text<<" HRESULT="<<std::hex<<hr<<'\n';std::exit(1);}}
LRESULT CALLBACK windowProc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcW(w,m,a,b);}
}
int main(){
    HWND root=CreateWindowExW(0,L"STATIC",L"BSI test-owned root",WS_OVERLAPPEDWINDOW,0,0,900,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND game=CreateWindowExW(0,L"STATIC",L"BSI test-owned game",WS_CHILD,0,0,640,480,root,nullptr,GetModuleHandleW(nullptr),nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"QtShaderTestIgnoredWindow";require(RegisterClassW(&cls)!=0,"register explicitly Qt-classified UI window");
    HWND ui=CreateWindowExW(0,cls.lpszClassName,L"not a game",WS_CHILD,0,0,640,480,root,nullptr,cls.hInstance,nullptr);
    require(root&&game&&ui,"create test-owned native windows");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;okay(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"create native WARP");
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory2> factory;okay(device.As(&dxgi),"get DXGI device");okay(dxgi->GetAdapter(&adapter),"get adapter");okay(adapter->GetParent(IID_PPV_ARGS(&factory)),"get actual DXGI factory");
    auto &runtime=Runtime::instance();runtime.configureRenderer(RendererFamily::Vulkan);runtime.start(reinterpret_cast<std::uintptr_t>(root));
    for(unsigned i=0;i<100&&!runtime.metrics().hooks;++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(runtime.metrics().hooks,"production presentation hooks must actually attach");
    const auto create=[&](HWND window){
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=640;desc.Height=480;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;okay(factory->CreateSwapChainForHwnd(device.Get(),window,&desc,nullptr,nullptr,&chain),"create genuine child-window swapchain through hooked factory");return chain;
    };
    auto chain=create(game);auto uiChain=create(ui);
    const auto frame=[&](IDXGISwapChain1 *swap,bool test){
        {ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11RenderTargetView> view;okay(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"get game backbuffer");okay(device->CreateRenderTargetView(buffer.Get(),nullptr,&view),"create native RTV");const float color[]{.8f,.4f,.2f,.7f};context->ClearRenderTargetView(view.Get(),color);}
        DXGI_PRESENT_PARAMETERS parameters{};okay(swap->Present1(0,test?DXGI_PRESENT_TEST:0,&parameters),"actual DXGI Present1");
    };
    Parameters parameters;parameters.enabled=true;parameters.values[0]=55;runtime.set(parameters);
    frame(chain.Get(),true);require(runtime.metrics().processed==0,"DXGI_PRESENT_TEST must not grade or count a game frame");
    frame(uiChain.Get(),false);require(runtime.metrics().processed==0,"Qt UI swapchain must not be mistaken for the game");
    frame(chain.Get(),false);auto m=runtime.metrics();
    require(m.active&&m.deviceReady&&m.renderer==Renderer::Direct3D11&&m.processed==1,"real native Present1 did not route exactly one actual D3D11 game shader pass");
    runtime.set(Parameters{});frame(chain.Get(),false);require(!runtime.metrics().active&&runtime.metrics().processed==1,"disabled presentation must be an exact no-pass path");
    okay(chain->ResizeBuffers(2,800,450,DXGI_FORMAT_R8G8B8A8_UNORM,0),"ResizeBuffers after a shader pass must have no retained backbuffer references");
    runtime.set(parameters);frame(chain.Get(),false);require(runtime.metrics().active&&runtime.metrics().processed==2,"resized swapchain did not recreate its shader resources");
    runtime.configureRenderer(RendererFamily::OpenGL);frame(chain.Get(),false);
    require(runtime.metrics().active&&runtime.metrics().processed==3&&runtime.metrics().renderer==Renderer::Direct3D11
            &&runtime.metrics().configured==RendererFamily::OpenGL&&runtime.metrics().requested==RendererFamily::Automatic,
            "actual native D3D11 presentation routes under both Vulkan and OpenGL emulator hints, without relabeling the API");
    require(!runtime.claimPresentation(Renderer::OpenGLES,987,reinterpret_cast<std::uintptr_t>(game)),"an underlying/second translation provider cannot grade the selected presentation twice");
    chain.Reset();require(!runtime.metrics().observed,"final game swapchain Release re-arms automatic detection");
    uiChain.Reset();runtime.stop();DestroyWindow(ui);DestroyWindow(game);DestroyWindow(root);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    std::cout<<"PASS: production normal-DLL DXGI factory/Present1 hooks, identified native game child, Qt UI exclusion, test/neutral bypass, exactly-once counters, resize/release re-detection and actual D3D11 routing under Vulkan/OpenGL configuration hints\n";
    std::cout<<"LIMIT: native WARP fixture, not HD-Player integration or physical Intel/AMD/NVIDIA hardware validation\n";
}
