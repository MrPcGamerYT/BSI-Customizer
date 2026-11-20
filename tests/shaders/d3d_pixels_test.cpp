// Native Direct3D WARP image tests. They test the production C++ shaders, not a
// look-alike CPU function, but do not certify hardware, DXGI hooks or HD-Player.
#include "D3d11Pipeline.h"
#include "D3d12Pipeline.h"
#include "D3dTextureQuality.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>
using namespace BsiShaders;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool c,const std::string &text){if(!c){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
void okay(HRESULT hr,const char *text){if(FAILED(hr)){std::cerr<<"FAIL: "<<text<<" HRESULT="<<std::hex<<hr<<'\n';std::exit(1);}}
TextureQualityProof qualityProof(Renderer api){
    // Explicit test-owned descriptor premises, never inferred game ownership.
    TextureQualityProof p;p.renderer=api;p.requested=RendererFamily::DirectX;p.use=TextureUse::SampledColor;p.visibleMipLevels=7;
    p.gameSurfaceIdentified=p.resourceOwnershipProven=p.privateReplacementSupported=p.externalOverridesReviewed=true;return p;
}
constexpr UINT width=64,height=64,bytes=width*height*4;
using Pixels=std::vector<unsigned char>;
using Run=std::function<Pixels(const Pixels&,const Parameters&)>;
void exercise(const Run &run){
    Pixels flat(bytes);for(UINT i=0;i<bytes;i+=4){flat[i]=204;flat[i+1]=102;flat[i+2]=51;flat[i+3]=173;}
    Parameters p;p.enabled=true;require(run(flat,p)==flat,"neutral frame must be byte-identical");p.values.fill(40);p.enabled=false;require(run(flat,p)==flat,"disabled frame must be byte-identical");
    for(std::size_t i=0;i<6;++i)for(int weight:{controls[i].minimum,controls[i].maximum}){
        p=Parameters{};p.enabled=true;p.values[i]=weight;const auto pixels=run(flat,p);const auto reference=grade({204.f/255,102.f/255,51.f/255},p);
        for(int c=0;c<3;++c)require(std::abs(int(pixels[c])-int(std::lround(reference[c]*255)))<=1,"GPU pixels differ from bounded reference color math");
        for(UINT c=3;c<bytes;c+=4)require(pixels[c]==173,"source alpha was modified");
    }
    Pixels pattern(bytes);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){
        float v=.15f+.65f/(1+std::exp(std::fmax(-50.f,std::fmin(50.f,(float(y)-x)*.8f))));if(x>27&&x<35&&y>27&&y<35)v=1;
        const auto i=(y*width+x)*4;pattern[i]=static_cast<unsigned char>(std::lround(v*255));pattern[i+1]=static_cast<unsigned char>(std::lround(v*.8f*255));pattern[i+2]=static_cast<unsigned char>(std::lround(v*.6f*255));pattern[i+3]=173;
    }
    for(std::size_t i=6;i<10;++i){p=Parameters{};p.enabled=true;p.values[i]=85;require(run(pattern,p)!=pattern,"a spatial shader did not change actual DirectX image pixels");}
}
void d3d11(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    okay(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"create real D3D11 WARP device");
    D3D11_SAMPLER_DESC nativeSampling{};nativeSampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    nativeSampling.AddressU=nativeSampling.AddressV=nativeSampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    nativeSampling.ComparisonFunc=D3D11_COMPARISON_ALWAYS;nativeSampling.MaxAnisotropy=1;nativeSampling.MaxLOD=6;
    ComPtr<ID3D11SamplerState> nativeSampler;okay(device->CreateSamplerState(&nativeSampling,&nativeSampler),"create original D3D11 test sampler");
    for(auto mode:{TextureQuality::Light,TextureQuality::Strong}){
        const auto planned=planD3dTextureQuality(mode,qualityProof(Renderer::Direct3D11),nativeSampling);require(planned.changed(),"qualified D3D11 quality descriptor");
        ComPtr<ID3D11SamplerState> replacement;okay(device->CreateSamplerState(&planned.descriptor,&replacement),"create private D3D11 quality sampler");
        D3D11_SAMPLER_DESC original{},actual{};nativeSampler->GetDesc(&original);replacement->GetDesc(&actual);
        require(original.MinLOD==0&&actual.MinLOD==(mode==TextureQuality::Light?1.5f:3.f),"real D3D11 sampler round-trip preserves original and accepts bounded mip floor");
    }
    std::cout<<"PASS: experimental D3D11 minimum-LOD sampler creation/GetDesc round-trip; not a quality pixel or game-binding test\n";
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> image,readback;okay(device->CreateTexture2D(&desc,nullptr,&image),"create D3D11 render image");
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;okay(device->CreateTexture2D(&desc,nullptr,&readback),"create D3D11 readback");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_WIREFRAME;raster.CullMode=D3D11_CULL_FRONT;raster.ScissorEnable=TRUE;
    ComPtr<ID3D11RasterizerState> nativeRaster;okay(device->CreateRasterizerState(&raster,&nativeRaster),"create host raster state");context->RSSetState(nativeRaster.Get());
    const D3D11_VIEWPORT nativeViewport{2,3,17,19,.2f,.8f};context->RSSetViewports(1,&nativeViewport);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    D3d11Pipeline pipeline;std::string error;require(pipeline.initialize(device.Get(),&error),error);
    const Run run=[&](const Pixels &input,const Parameters &p){
        context->UpdateSubresource(image.Get(),0,nullptr,input.data(),width*4,0);require(pipeline.render(image.Get(),p,&error),error);
        ComPtr<ID3D11RasterizerState> restored;context->RSGetState(&restored);require(restored.Get()==nativeRaster.Get(),"the player's rasterizer state was not restored");
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);require(topology==D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,"native topology changed");
        UINT count=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&count,&viewport);require(count==1&&std::memcmp(&viewport,&nativeViewport,sizeof(viewport))==0,"native viewport changed");
        context->CopyResource(readback.Get(),image.Get());D3D11_MAPPED_SUBRESOURCE mapped{};okay(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"read actual D3D11 pixels");
        Pixels pixels(bytes);for(UINT row=0;row<height;++row)std::memcpy(pixels.data()+row*width*4,static_cast<unsigned char*>(mapped.pData)+row*mapped.RowPitch,width*4);context->Unmap(readback.Get(),0);return pixels;
    };
    exercise(run);Parameters p;p.enabled=true;p.values[0]=101;require(!pipeline.render(image.Get(),p,&error),"out-of-range controls must not render");
    pipeline.release();require(pipeline.initialize(device.Get(),&error),error);p.values[0]=30;require(run(Pixels(bytes,127),p).size()==bytes,"DirectX resource recreation failed");
    std::cout<<"PASS: native D3D11 WARP production shaders, all 10 pixel effects, exact neutral, alpha, reference math, full context isolation checks, bounds and recreation\n";
}
void d3d12(){
    ComPtr<IDXGIFactory4> factory;okay(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"create DXGI factory");ComPtr<IDXGIAdapter> warp;okay(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"get WARP adapter");
    ComPtr<ID3D12Device> device;okay(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"create actual D3D12 WARP device");
    D3D12_SAMPLER_DESC nativeSampling{};nativeSampling.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    nativeSampling.AddressU=nativeSampling.AddressV=nativeSampling.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    nativeSampling.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;nativeSampling.MaxAnisotropy=1;nativeSampling.MaxLOD=6;
    const auto quality=qualityProof(Renderer::Direct3D12);
    const auto planned=planD3dTextureQuality(TextureQuality::Strong,quality,nativeSampling);require(planned.changed(),"qualified D3D12 sampler descriptor");
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapInfo{};samplerHeapInfo.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;samplerHeapInfo.NumDescriptors=2;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;okay(device->CreateDescriptorHeap(&samplerHeapInfo,IID_PPV_ARGS(&samplerHeap)),"create private D3D12 sampler heap");
    auto handle=samplerHeap->GetCPUDescriptorHandleForHeapStart();device->CreateSampler(&nativeSampling,handle);
    handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);device->CreateSampler(&planned.descriptor,handle);
    D3D12_STATIC_SAMPLER_DESC staticSource{};staticSource.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSource.AddressU=staticSource.AddressV=staticSource.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSource.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;staticSource.MaxLOD=6;staticSource.MaxAnisotropy=1;
    staticSource.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    const auto stat=planD3dTextureQuality(TextureQuality::Strong,quality,staticSource);require(stat.changed(),"qualified D3D12 static sampler descriptor");
    D3D12_ROOT_SIGNATURE_DESC root{};root.NumStaticSamplers=1;root.pStaticSamplers=&stat.descriptor;
    ComPtr<ID3DBlob> rootBytes,rootError;okay(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&rootError),"serialize private D3D12 static sampler");
    ComPtr<ID3D12RootSignature> rootSignature;okay(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&rootSignature)),"create private D3D12 root signature");
    require(nativeSampling.MinLOD==0&&staticSource.MinLOD==0,"original DirectX 12 descriptors untouched");
    std::cout<<"PASS: experimental D3D12 dynamic sampler heap and serialized static-sampler root signature; not a quality pixel or game-binding test\n";
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue> queue;okay(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"create D3D12 queue");
    ComPtr<ID3D12Fence> fence;okay(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"create D3D12 test fence");UINT64 serial=0;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"create test fence event");
    const auto idle=[&]{okay(queue->Signal(fence.Get(),++serial),"signal D3D12 fence");okay(fence->SetEventOnCompletion(serial,event),"arm D3D12 fence");require(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"D3D12 GPU test timed out");};
    D3D12_RESOURCE_DESC imageDesc{};imageDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;imageDesc.Width=width;imageDesc.Height=height;imageDesc.DepthOrArraySize=imageDesc.MipLevels=1;imageDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;imageDesc.SampleDesc.Count=1;imageDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;ComPtr<ID3D12Resource> image;
    okay(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&imageDesc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&image)),"create native D3D12 image");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;device->GetCopyableFootprints(&imageDesc,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload,readback;heap.Type=D3D12_HEAP_TYPE_UPLOAD;okay(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"create D3D12 upload");
    heap.Type=D3D12_HEAP_TYPE_READBACK;okay(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"create D3D12 readback");
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;okay(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"create test command allocator");okay(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"create test commands");okay(commands->Close(),"close initial commands");
    const auto transfer=[&](bool toImage){
        okay(allocator->Reset(),"reset allocator");okay(commands->Reset(allocator.Get(),nullptr),"reset commands");
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=image.Get();b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b.Transition.StateAfter=toImage?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COPY_SOURCE;commands->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION imageLocation{};imageLocation.pResource=image.Get();imageLocation.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION bufferLocation{};bufferLocation.pResource=toImage?upload.Get():readback.Get();bufferLocation.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;bufferLocation.PlacedFootprint=footprint;
        if(toImage)commands->CopyTextureRegion(&imageLocation,0,0,0,&bufferLocation,nullptr);else commands->CopyTextureRegion(&bufferLocation,0,0,0,&imageLocation,nullptr);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);commands->ResourceBarrier(1,&b);okay(commands->Close(),"close transfer");ID3D12CommandList *list[]{commands.Get()};queue->ExecuteCommandLists(1,list);idle();
    };
    D3d12Pipeline pipeline;std::string error;require(pipeline.initialize(device.Get(),queue.Get(),&error),error);
    const Run run=[&](const Pixels &input,const Parameters &p){
        void *data=nullptr;okay(upload->Map(0,nullptr,&data),"map D3D12 upload");for(UINT row=0;row<height;++row)std::memcpy(static_cast<unsigned char*>(data)+footprint.Offset+row*footprint.Footprint.RowPitch,input.data()+row*width*4,width*4);upload->Unmap(0,nullptr);transfer(true);
        require(pipeline.render(image.Get(),p,&error),error);idle();transfer(false);okay(readback->Map(0,nullptr,&data),"map actual D3D12 result");Pixels result(bytes);for(UINT row=0;row<height;++row)std::memcpy(result.data()+row*width*4,static_cast<unsigned char*>(data)+footprint.Offset+row*footprint.Footprint.RowPitch,width*4);readback->Unmap(0,nullptr);return result;
    };
    exercise(run);idle();pipeline.release();CloseHandle(event);
    std::cout<<"PASS: actual D3D12 WARP resources/queue with production On12 shader backend, all 10 effects, neutral byte identity, reference math, alpha and copy/presentation-state round trips\n";
}
}
int main(int argc,char **argv){if(argc>1&&std::strcmp(argv[1],"d3d12")==0)d3d12();else d3d11();std::cout<<"LIMIT: WARP API fixture, not hardware, DXGI hook, native player UI or Intel/AMD/NVIDIA certification\n";}
