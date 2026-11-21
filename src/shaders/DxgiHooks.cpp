#include "PresentationHooks.h"
#ifdef _WIN32
#include "Runtime.h"
#include "D3d11Pipeline.h"
#include "D3d12Pipeline.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

namespace BsiShaders {
namespace {
using Microsoft::WRL::ComPtr;
struct Swapchain {
    std::mutex mutex;
    ComPtr<ID3D12CommandQueue> queue;
    std::unique_ptr<D3d11Pipeline> d11;
    std::unique_ptr<D3d12Pipeline> d12;
    DXGI_COLOR_SPACE_TYPE color=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
};
struct State {
    std::mutex mutex;
    std::map<IDXGISwapChain*,std::shared_ptr<Swapchain>> chains;
    std::atomic<bool> stopping{false};
    std::atomic<InstallPresentationHook> install{nullptr};
    bool factory=false,probe=false;
};
State &state(){static auto *s=new State;return *s;}
thread_local bool inside=false;
struct Scope {bool previous=inside;Scope(){inside=true;}~Scope(){inside=previous;}};
template<typename Fn,typename T>Fn original(T *object,unsigned index){
    void *target=(*reinterpret_cast<void***>(object))[index];
    auto *trampoline=originalPresentationFunction(target);
    return reinterpret_cast<Fn>(trampoline?trampoline:target);
}
std::shared_ptr<Swapchain> find(IDXGISwapChain *chain,bool create){
    auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);
    auto it=s.chains.find(chain);if(it!=s.chains.end())return it->second;
    if(!create||s.chains.size()>=32)return {};
    auto value=std::make_shared<Swapchain>();s.chains.emplace(chain,value);return value;
}
bool process(IDXGISwapChain *chain,UINT flags){
    if(state().stopping.load())return false;
    DXGI_SWAP_CHAIN_DESC desc{};
    if(FAILED(chain->GetDesc(&desc))||!Runtime::instance().eligible(reinterpret_cast<std::uintptr_t>(desc.OutputWindow)))return false;
    ComPtr<ID3D11Device> d11;ComPtr<ID3D12Device> d12;
    Renderer api=Renderer::Unknown;
    if(SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&d11))))api=Renderer::Direct3D11;
    else if(SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&d12))))api=Renderer::Direct3D12;
    if(flags&DXGI_PRESENT_TEST)return false;
    if(!Runtime::instance().accepts(api)
       ||!Runtime::instance().claimPresentation(api,reinterpret_cast<std::uintptr_t>(chain),reinterpret_cast<std::uintptr_t>(desc.OutputWindow)))return false;
    const auto parameters=Runtime::instance().settings();
    if(parameters.neutral()){Runtime::instance().report(true,true,false,"Neutral bypass",false,api);return false;}
    if(flags&DXGI_PRESENT_DO_NOT_WAIT){Runtime::instance().report(true,true,false,"Nonblocking DXGI presentation is safely bypassed to avoid grading an unpresented frame twice.",false,api);return false;}
    auto saved=find(chain,true);if(!saved)return false;
    std::lock_guard<std::mutex> lock(saved->mutex);
    if(saved->color!=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709){Runtime::instance().report(true,true,false,"HDR/non-SDR DXGI color spaces require a separate color-managed pass; original game frame retained.",false,api);return false;}
    std::string error;bool rendered=false;
    if(api==Renderer::Direct3D11){
        if(!saved->d11)saved->d11=std::make_unique<D3d11Pipeline>();
        ComPtr<ID3D11Texture2D> frame;
        if(SUCCEEDED(chain->GetBuffer(0,IID_PPV_ARGS(&frame))))rendered=saved->d11->initialize(d11.Get(),&error)&&saved->d11->render(frame.Get(),parameters,&error);
        else error="The DirectX 11 game backbuffer is unavailable.";
    }else{
        ComPtr<IDXGISwapChain3> modern;ComPtr<ID3D12Resource> frame;
        if(!saved->queue)error="The DirectX 12 presentation queue was created before shader capture. Restart the player; guessing another queue is unsafe.";
        else if(FAILED(chain->QueryInterface(IID_PPV_ARGS(&modern)))||FAILED(chain->GetBuffer(modern->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&frame))))error="The DirectX 12 current game backbuffer is unavailable.";
        else {
            if(!saved->d12)saved->d12=std::make_unique<D3d12Pipeline>();
            rendered=saved->d12->initialize(d12.Get(),saved->queue.Get(),&error)&&saved->d12->render(frame.Get(),parameters,&error);
        }
    }
    Runtime::instance().report(true,true,rendered,rendered?"Shaders active on the identified native DirectX game surface.":error,rendered,api,rendered);
    return rendered;
}
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
HRESULT STDMETHODCALLTYPE present(IDXGISwapChain *chain,UINT sync,UINT flags){
    const auto next=original<Present>(chain,8);if(inside)return next(chain,sync,flags);
    Scope scope;try {process(chain,flags);}catch(...){Runtime::instance().report(true,false,false,"DirectX shader processing failed; native presentation continues.");}
    return next(chain,sync,flags);
}
using Present1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE present1(IDXGISwapChain1 *chain,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS *parameters){
    const auto next=original<Present1>(chain,22);if(inside)return next(chain,sync,flags,parameters);
    Scope scope;bool rendered=false;try {rendered=process(chain,flags);}catch(...){Runtime::instance().report(true,false,false,"DirectX shader processing failed; native presentation continues.");}
    // A full-image postprocess invalidates the application's partial-damage
    // rectangles/scroll region. Request full damage WITHOUT changing vsync,
    // present flags or the game's swap effect/presentation policy.
    const DXGI_PRESENT_PARAMETERS full{};
    return next(chain,sync,flags,rendered?&full:parameters);
}
void reset(IDXGISwapChain *chain){auto saved=find(chain,false);if(saved){std::lock_guard<std::mutex> lock(saved->mutex);saved->d11.reset();saved->d12.reset();}}
using Resize=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
HRESULT STDMETHODCALLTYPE resize(IDXGISwapChain *chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags){
    const auto next=original<Resize>(chain,13);reset(chain);return next(chain,count,width,height,format,flags);
}
using Resize1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown*const*);
HRESULT STDMETHODCALLTYPE resize1(IDXGISwapChain3 *chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags,const UINT *nodes,IUnknown *const *queues){
    const auto next=original<Resize1>(chain,39);reset(chain);
    const auto hr=next(chain,count,width,height,format,flags,nodes,queues);
    if(SUCCEEDED(hr)&&queues&&count){
        ComPtr<ID3D12CommandQueue> queue;bool same=queues[0]&&SUCCEEDED(queues[0]->QueryInterface(IID_PPV_ARGS(&queue)));
        for(UINT i=1;i<count&&same;++i){ComPtr<ID3D12CommandQueue> candidate;same=queues[i]&&SUCCEEDED(queues[i]->QueryInterface(IID_PPV_ARGS(&candidate)))&&candidate.Get()==queue.Get();}
        auto saved=find(chain,true);if(saved){std::lock_guard<std::mutex> lock(saved->mutex);saved->queue=same?queue:ComPtr<ID3D12CommandQueue>{};}
    }
    return hr;
}
using Release=ULONG(STDMETHODCALLTYPE*)(IDXGISwapChain*);
ULONG STDMETHODCALLTYPE release(IDXGISwapChain *chain){
    const auto next=original<Release>(chain,2);const auto count=next(chain);
    std::shared_ptr<Swapchain> removed;
    if(!count){
        auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);auto it=s.chains.find(chain);
        if(it!=s.chains.end()){removed=std::move(it->second);s.chains.erase(it);}
    }
    if(!count){Runtime::instance().releasePresentation(Renderer::Direct3D11,reinterpret_cast<std::uintptr_t>(chain));
        Runtime::instance().releasePresentation(Renderer::Direct3D12,reinterpret_cast<std::uintptr_t>(chain));}
    // Release COM objects after leaving the registry lock: their own native
    // Release methods may in turn pass through a DXGI detour.
    return count;
}
using Color=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,DXGI_COLOR_SPACE_TYPE);
HRESULT STDMETHODCALLTYPE color(IDXGISwapChain3 *chain,DXGI_COLOR_SPACE_TYPE space){
    const auto next=original<Color>(chain,38);const auto hr=next(chain,space);
    if(SUCCEEDED(hr)){auto saved=find(chain,true);if(saved){std::lock_guard<std::mutex> lock(saved->mutex);saved->color=space;}}return hr;
}
bool method(IUnknown *object,unsigned index,void *replacement){
    auto install=state().install.load();if(!install)return false;
    return install((*reinterpret_cast<void***>(object))[index],replacement,nullptr);
}
void track(IDXGISwapChain *chain,IUnknown *device){
    if(!chain||state().stopping.load())return;
    method(chain,8,reinterpret_cast<void*>(&present));method(chain,13,reinterpret_cast<void*>(&resize));method(chain,2,reinterpret_cast<void*>(&release));
    ComPtr<IDXGISwapChain1> one;if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&one))))method(one.Get(),22,reinterpret_cast<void*>(&present1));
    ComPtr<IDXGISwapChain3> three;if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&three)))){method(three.Get(),38,reinterpret_cast<void*>(&color));method(three.Get(),39,reinterpret_cast<void*>(&resize1));}
    if(device){ComPtr<ID3D12CommandQueue> queue;if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue)))){auto saved=find(chain,true);if(saved){std::lock_guard<std::mutex> lock(saved->mutex);saved->queue=queue;}}}
}
using Create=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
HRESULT STDMETHODCALLTYPE create(IDXGIFactory *factory,IUnknown *device,DXGI_SWAP_CHAIN_DESC *desc,IDXGISwapChain **out){
    const auto next=original<Create>(factory,10);const auto hr=next(factory,device,desc,out);if(SUCCEEDED(hr)&&out)try {track(*out,device);}catch(...){}return hr;
}
using CreateHwnd=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
HRESULT STDMETHODCALLTYPE createHwnd(IDXGIFactory2 *factory,IUnknown *device,HWND window,const DXGI_SWAP_CHAIN_DESC1 *desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,IDXGIOutput *output,IDXGISwapChain1 **out){
    const auto next=original<CreateHwnd>(factory,15);const auto hr=next(factory,device,window,desc,fullscreen,output,out);if(SUCCEEDED(hr)&&out)try {track(*out,device);}catch(...){}return hr;
}
}
bool scanDxgiHooks(InstallPresentationHook install){
    auto &s=state();if(s.stopping.load())return false;
    const auto request=Runtime::instance().metrics().requested;
    if(request!=RendererFamily::DirectX&&request!=RendererFamily::Automatic)return false;
    if(!GetModuleHandleW(L"dxgi.dll"))return false;
    s.install=install;
    if(!s.factory){
        const auto createFactory=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"dxgi.dll"),"CreateDXGIFactory1")));
        ComPtr<IDXGIFactory> factory;
        if(createFactory&&SUCCEEDED(createFactory(IID_PPV_ARGS(&factory)))){
            s.factory=method(factory.Get(),10,reinterpret_cast<void*>(&create));
            ComPtr<IDXGIFactory2> modern;if(SUCCEEDED(factory.As(&modern)))s.factory=method(modern.Get(),15,reinterpret_cast<void*>(&createHwnd))&&s.factory;
        }
    }
    // Obtain the actual OS Present method for a pre-existing D3D11 swapchain.
    // This hidden, test-owned window never satisfies Runtime::eligible(). No
    // real game renderer, adapter setting, fps limit or swapchain is changed.
    if(s.factory&&!s.probe&&GetModuleHandleW(L"d3d11.dll")){
        s.probe=true;Scope guard;
        using CreateDevice=HRESULT(WINAPI*)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,const DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
        const auto createDevice=reinterpret_cast<CreateDevice>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"d3d11.dll"),"D3D11CreateDeviceAndSwapChain")));
        HWND window=CreateWindowExW(0,L"STATIC",L"BSI shader method probe",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(createDevice&&window){
            DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=desc.BufferDesc.Height=1;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;desc.Windowed=TRUE;
            ComPtr<IDXGISwapChain> chain;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
            if(SUCCEEDED(createDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)))track(chain.Get(),nullptr);
        }
        if(window)DestroyWindow(window);
    }
    return s.factory;
}
void stopDxgiHooks(){state().stopping.store(true);}
} // namespace BsiShaders
#endif
