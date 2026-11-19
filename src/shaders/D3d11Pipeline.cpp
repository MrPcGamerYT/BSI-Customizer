#include "D3d11Pipeline.h"
#ifdef _WIN32
#include "HlslSources.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstdio>

namespace BsiShaders {
using Microsoft::WRL::ComPtr;
namespace {
bool result(HRESULT hr,const char *operation,std::string *error){
    if(SUCCEEDED(hr))return true;
    if(error){char hex[16]{};std::snprintf(hex,sizeof(hex),"0x%08lx",static_cast<unsigned long>(hr));*error=std::string(operation)+" failed ("+hex+"); original presentation continues.";}
    return false;
}
DXGI_FORMAT typed(DXGI_FORMAT format){
    if(format==DXGI_FORMAT_R8G8B8A8_TYPELESS)return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(format==DXGI_FORMAT_B8G8R8A8_TYPELESS)return DXGI_FORMAT_B8G8R8A8_UNORM;
    return format;
}
struct ContextScope {
    ID3D11DeviceContext1 *context;
    ComPtr<ID3DDeviceContextState> previous;
    ContextScope(ID3D11DeviceContext1 *ctx,ID3DDeviceContextState *ours):context(ctx){context->SwapDeviceContextState(ours,&previous);}
    ~ContextScope(){
        // Do not keep a backbuffer RTV in our saved state: that would break the
        // player's next ResizeBuffers. Never ClearState() the player's state.
        context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);
    }
};
}
struct D3d11Pipeline::Impl {
    ComPtr<ID3D11Device1> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> fragment;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Texture2D> copy;
    ComPtr<ID3D11ShaderResourceView> source;
    UINT width=0,height=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool ready=false;
};
D3d11Pipeline::D3d11Pipeline():impl(new Impl){}
D3d11Pipeline::~D3d11Pipeline()=default;
void D3d11Pipeline::release(){impl.reset(new Impl);}
bool D3d11Pipeline::supports(DXGI_FORMAT format){
    format=typed(format);
    return format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        ||format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}
bool D3d11Pipeline::initialize(ID3D11Device *device,std::string *error){
    if(!device||!error)return false;
    if(impl->ready){if(impl->device.Get()==device)return true;release();}
    else release();
    auto &p=*impl;
    if(!result(device->QueryInterface(IID_PPV_ARGS(&p.device)),"D3D11.1 state isolation",error))return false;
    const auto level=device->GetFeatureLevel();
    if(level<D3D_FEATURE_LEVEL_10_0){*error="DirectX shader model 4.0 / feature level 10.0 is required; no game pixels changed.";return false;}
    p.device->GetImmediateContext1(&p.context);
    if(!p.context){*error="No immediate DirectX context; shaders bypassed.";return false;}
    D3D_FEATURE_LEVEL chosen{};
    if(!result(p.device->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&p.state),"CreateDeviceContextState",error))return false;
    // Load the OS compiler only when DirectX is actually used. An OpenGL or
    // Vulkan player does not acquire an unnecessary d3dcompiler dependency.
    // Compiler blobs have DLL-owned COM vtables. Keep the library loaded until
    // AFTER every blob is released, not just until D3DCompile returns.
    struct Library {HMODULE handle;~Library(){if(handle)FreeLibrary(handle);}};
    Library compiler{LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if(!compiler.handle){*error="Windows shader compiler d3dcompiler_47.dll is unavailable; DirectX effects bypassed.";return false;}
    const auto compile=reinterpret_cast<pD3DCompile>(reinterpret_cast<void*>(GetProcAddress(compiler.handle,"D3DCompile")));
    if(!compile){*error="Windows shader compiler entry point is unavailable.";return false;}
    const auto build=[&](const std::string &text,const char *profile,ComPtr<ID3DBlob> *code){
        ComPtr<ID3DBlob> log;
        const auto hr=compile(text.data(),text.size(),"BSI original shared effect",nullptr,nullptr,"main",profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code->GetAddressOf(),&log);
        if(FAILED(hr)){*error="DirectX shader compilation failed: ";if(log)*error+=std::string(static_cast<const char*>(log->GetBufferPointer()),log->GetBufferSize());return false;}return true;
    };
    ComPtr<ID3DBlob> vertex,fragment;
    const bool compiled=build(hlslVertexSource(),"vs_4_0",&vertex)&&build(hlslFragmentSource(),"ps_4_0",&fragment);
    if(!compiled)return false;
    if(!result(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&p.vertex),"CreateVertexShader",error)
       ||!result(device->CreatePixelShader(fragment->GetBufferPointer(),fragment->GetBufferSize(),nullptr,&p.fragment),"CreatePixelShader",error))return false;
    D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampling.MaxLOD=D3D11_FLOAT32_MAX;
    sampling.MaxAnisotropy=1;sampling.ComparisonFunc=D3D11_COMPARISON_NEVER;
    if(!result(device->CreateSamplerState(&sampling,&p.sampler),"CreateSamplerState",error))return false;
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    if(!result(device->CreateRasterizerState(&raster,&p.raster),"CreateRasterizerState",error))return false;
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    depth.FrontFace.StencilFailOp=depth.FrontFace.StencilDepthFailOp=depth.FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
    depth.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;depth.BackFace=depth.FrontFace;
    if(!result(device->CreateDepthStencilState(&depth,&p.depth),"CreateDepthStencilState",error))return false;
    D3D11_BUFFER_DESC constants{};constants.ByteWidth=48;constants.Usage=D3D11_USAGE_DEFAULT;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(!result(device->CreateBuffer(&constants,nullptr,&p.constants),"CreateBuffer(constants)",error))return false;
    p.ready=true;return true;
}
bool D3d11Pipeline::render(ID3D11Texture2D *target,const Parameters &settings,std::string *error){
    if(!error)return false;
    if(!settings.valid()){*error="Shader parameters are out of range.";return false;}
    if(settings.neutral())return true;
    auto &p=*impl;
    if(!p.ready||!target){*error="DirectX shader device/frame is not ready.";return false;}
    ComPtr<ID3D11Device> owner;target->GetDevice(&owner);
    ComPtr<ID3D11Device1> owner1;
    if(FAILED(owner.As(&owner1))||owner1.Get()!=p.device.Get()){*error="DirectX game frame belongs to a different device.";return false;}
    D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);const auto format=typed(desc.Format);
    if(!supports(format)||!desc.Width||!desc.Height||desc.ArraySize!=1||desc.MipLevels!=1
       ||std::uint64_t(desc.Width)*desc.Height>33554432||!(desc.BindFlags&D3D11_BIND_RENDER_TARGET)
       ||(desc.MiscFlags&(D3D11_RESOURCE_MISC_RESTRICTED_CONTENT|D3D11_RESOURCE_MISC_HW_PROTECTED))){
        *error="Unsupported/protected DirectX SDR image format, dimensions or bind flags; original game frame retained.";return false;
    }
    UINT support=0;
    if(FAILED(p.device->CheckFormatSupport(format,&support))
       ||(support&(D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE|D3D11_FORMAT_SUPPORT_RENDER_TARGET))
          !=(D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE|D3D11_FORMAT_SUPPORT_RENDER_TARGET)){
        *error="The DirectX device lacks the required image sampling/render-target features.";return false;
    }
    if(!p.copy||p.width!=desc.Width||p.height!=desc.Height||p.format!=format){
        p.source.Reset();p.copy.Reset();
        D3D11_TEXTURE2D_DESC copy{};copy.Width=desc.Width;copy.Height=desc.Height;copy.MipLevels=copy.ArraySize=1;
        copy.Format=format;copy.SampleDesc.Count=1;copy.Usage=D3D11_USAGE_DEFAULT;copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(!result(p.device->CreateTexture2D(&copy,nullptr,&p.copy),"CreateTexture2D(copy)",error)
           ||!result(p.device->CreateShaderResourceView(p.copy.Get(),nullptr,&p.source),"CreateShaderResourceView",error))return false;
        p.width=desc.Width;p.height=desc.Height;p.format=format;
    }
    ComPtr<ID3D11RenderTargetView> output;
    D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=format;view.ViewDimension=desc.SampleDesc.Count>1?D3D11_RTV_DIMENSION_TEXTURE2DMS:D3D11_RTV_DIMENSION_TEXTURE2D;
    if(!result(p.device->CreateRenderTargetView(target,&view,&output),"CreateRenderTargetView",error))return false;
    if(desc.SampleDesc.Count>1&&!(support&D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE)){*error="This DirectX format cannot resolve multisampled game frames.";return false;}
    {
        ContextScope scope(p.context.Get(),p.state.Get());auto *ctx=p.context.Get();
        if(desc.SampleDesc.Count>1)ctx->ResolveSubresource(p.copy.Get(),0,target,0,format);else ctx->CopyResource(p.copy.Get(),target);
        std::array<float,12> constants{};for(std::size_t i=0;i<settings.values.size();++i)constants[i]=settings.values[i]/100.f;
        constants[10]=1.f/desc.Width;constants[11]=1.f/desc.Height;ctx->UpdateSubresource(p.constants.Get(),0,nullptr,constants.data(),0,0);
        auto *rt=output.Get();auto *cb=p.constants.Get();auto *source=p.source.Get();auto *sampler=p.sampler.Get();
        ctx->OMSetRenderTargets(1,&rt,nullptr);ctx->OMSetDepthStencilState(p.depth.Get(),0);ctx->OMSetBlendState(nullptr,nullptr,0xffffffffu);
        ctx->RSSetState(p.raster.Get());const D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};ctx->RSSetViewports(1,&viewport);
        ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(p.vertex.Get(),nullptr,0);ctx->PSSetShader(p.fragment.Get(),nullptr,0);
        ctx->PSSetConstantBuffers(0,1,&cb);ctx->PSSetShaderResources(0,1,&source);ctx->PSSetSamplers(0,1,&sampler);ctx->Draw(3,0);
    }
    return result(p.device->GetDeviceRemovedReason(),"DirectX shader submission",error);
}
} // namespace BsiShaders
#endif
