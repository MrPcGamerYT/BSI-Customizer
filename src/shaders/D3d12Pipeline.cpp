#include "D3d12Pipeline.h"
#ifdef _WIN32
#include "D3d11Pipeline.h"
#include <d3d11on12.h>
#include <wrl/client.h>

namespace BsiShaders {
using Microsoft::WRL::ComPtr;
struct D3d12Pipeline::Impl {
    // First member is destroyed last, after the DLL's COM objects.
    struct Library {HMODULE handle=nullptr;~Library(){if(handle)FreeLibrary(handle);}} library;
    ComPtr<ID3D12Device> native;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11On12Device> on12;
    D3d11Pipeline shaders;
    bool ready=false;
};
D3d12Pipeline::D3d12Pipeline():impl(new Impl){}
D3d12Pipeline::~D3d12Pipeline()=default;
void D3d12Pipeline::release(){impl.reset(new Impl);}
bool D3d12Pipeline::initialize(ID3D12Device *device,ID3D12CommandQueue *queue,std::string *error){
    if(!device||!queue||!error)return false;
    if(impl->ready&&impl->native.Get()==device&&impl->queue.Get()==queue)return true;
    release();auto &p=*impl;
    ComPtr<ID3D12Device> owner;
    const auto desc=queue->GetDesc();
    if(desc.Type!=D3D12_COMMAND_LIST_TYPE_DIRECT||desc.NodeMask>1||FAILED(queue->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=device){
        *error="The captured DirectX 12 queue is not the game's compatible direct queue; shaders bypassed.";return false;
    }
    p.library.handle=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!p.library.handle){*error="Windows D3D11On12 is unavailable; the DirectX 12 frame was not changed.";return false;}
    const auto create=reinterpret_cast<PFN_D3D11ON12_CREATE_DEVICE>(reinterpret_cast<void*>(GetProcAddress(p.library.handle,"D3D11On12CreateDevice")));
    if(!create){*error="Windows D3D11On12 entry point is unavailable.";return false;}
    IUnknown *queues[]{queue};
    const auto hr=create(device,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,queues,1,0,&p.device,&p.context,nullptr);
    if(FAILED(hr)||FAILED(p.device.As(&p.on12))){*error="Could not create an isolated shader device on the game's DirectX 12 queue.";return false;}
    p.native=device;p.queue=queue;
    p.ready=p.shaders.initialize(p.device.Get(),error);return p.ready;
}
bool D3d12Pipeline::render(ID3D12Resource *frame,const Parameters &settings,std::string *error){
    if(!error)return false;
    if(!settings.valid()){*error="Shader parameters are out of range.";return false;}
    if(settings.neutral())return true;
    auto &p=*impl;
    if(!p.on12||!frame){*error="DirectX 12 shader queue/frame is not ready.";return false;}
    const auto desc=frame->GetDesc();
    ComPtr<ID3D12Device> owner;
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.DepthOrArraySize!=1||desc.MipLevels!=1
       ||!D3d11Pipeline::supports(desc.Format)||!(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
       ||FAILED(frame->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=p.native.Get()){
        *error="The DirectX 12 game image has an unsupported format, resource state contract or device.";return false;
    }
    D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> wrapped;
    // The game has already recorded PRESENT by the time DXGI Present is
    // called. Tell On12 that exact incoming state, not RENDER_TARGET. On12
    // inserts its own copy/render barriers and restores PRESENT on release.
    if(FAILED(p.on12->CreateWrappedResource(frame,&flags,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT,IID_PPV_ARGS(&wrapped)))){
        *error="D3D11On12 could not wrap the observed game backbuffer; no shader pass submitted.";return false;
    }
    ID3D11Resource *resources[]{wrapped.Get()};p.on12->AcquireWrappedResources(resources,1);
    bool rendered=false;
    try {rendered=p.shaders.render(wrapped.Get(),settings,error);}
    catch(...){*error="DirectX 12 shader processing failed; native presentation continues.";}
    p.on12->ReleaseWrappedResources(resources,1);p.context->Flush();
    return rendered&&SUCCEEDED(p.native->GetDeviceRemovedReason());
}
} // namespace BsiShaders
#endif
