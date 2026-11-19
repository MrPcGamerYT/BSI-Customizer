#include "Parameters.h"
#include "RendererCapabilities.h"
#include "Runtime.h"
#include <cstdio>
#include <cstdlib>

using namespace BsiShaders;
namespace {
void require(bool condition,const char *message){
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
}
int main(){
    require(!canRoute(Renderer::Unknown,true,true),"loaded modules alone do not identify a renderer");
    for(const auto &entry:rendererCapabilities){
        require(!canRoute(entry.renderer,false,true),"Qt UI or unidentified surfaces cannot be routed");
        require(!canRoute(entry.renderer,true,false),"missing device capabilities cannot be ignored");
    }
    require(canRoute(Renderer::OpenGL,true,true,RendererFamily::OpenGL),"OpenGL candidate routing");
    require(canRoute(Renderer::OpenGLES,true,true,RendererFamily::OpenGL),"ANGLE/GLES follows the OpenGL setting");
    require(!canRoute(Renderer::OpenGL,true,true,RendererFamily::DirectX),"OpenGL cannot substitute for selected DirectX");
    require(!canRoute(Renderer::OpenGL,true,true,RendererFamily::Vulkan),"OpenGL cannot substitute for selected Vulkan");
    require(!capability(Renderer::Direct3D11).pipelineImplemented,"DirectX 11 is not implemented yet");
    require(!capability(Renderer::Direct3D12).pipelineImplemented,"DirectX 12 is not implemented yet");
    require(!capability(Renderer::Vulkan).pipelineImplemented,"Vulkan is not implemented yet");
    Parameters p;p.enabled=true;p.values[0]=20;
    auto &runtime=Runtime::instance();runtime.set(p);
    runtime.selectRenderer(RendererFamily::Vulkan);
    runtime.report(true,true,true,"must not be reported active",true,Renderer::Vulkan);
    auto m=runtime.metrics();
    require(!m.active&&m.processed==0,"an absent Vulkan adapter cannot claim processed frames");
    require(m.renderer==Renderer::Vulkan,"observed API is not silently changed to OpenGL");
    require(!runtime.accepts(Renderer::OpenGL),"Vulkan setting blocks GL hooks even if OpenGL is loaded");
    runtime.set(Parameters{});
    require(runtime.metrics().status.find("no implemented")!=std::string::npos,"neutral state must not hide the missing backend diagnostic");
    runtime.selectRenderer(RendererFamily::DirectX);runtime.set(p);
    runtime.report(true,true,true,"must not be reported active",true,Renderer::Direct3D11);
    require(!runtime.metrics().active&&runtime.metrics().processed==0,"missing DX11 must not count a frame");
    require(!runtime.accepts(Renderer::OpenGLES),"DirectX setting blocks the GL/GLES fallback");
    runtime.selectRenderer(RendererFamily::Automatic);
    runtime.report(true,false,true,"unidentified",true,Renderer::OpenGL);
    require(!runtime.metrics().active&&runtime.metrics().processed==0,"a surface must actually be identified");
    runtime.report(true,true,true,"identified OpenGL candidate",true,Renderer::OpenGLES);
    require(runtime.metrics().active&&runtime.metrics().processed==1,"identified implemented family can report a processed frame");
    runtime.report(true,true,true,"underlying ANGLE DXGI present",true,Renderer::Direct3D11);
    require(runtime.metrics().renderer==Renderer::OpenGLES&&runtime.metrics().processed==1,
            "auto selection prevents double-processing through ANGLE's underlying DXGI layer");
    runtime.set(Parameters{});
    require(!runtime.metrics().active,"disabled/neutral settings report no active effects");
    runtime.report(true,true,true,"neutral frame",true,Renderer::OpenGLES);
    require(runtime.metrics().processed==1,"neutral bypass cannot count as an effect pass");
    for(std::size_t i=0;i<controls.size();++i){
        Parameters v;v.enabled=true;v.values[i]=controls[i].minimum;
        require(v.valid(),"minimum value validation");
        v.values[i]=controls[i].maximum;require(v.valid(),"maximum value validation");
        v.values[i]=controls[i].maximum+1;require(!v.valid(),"out of range validation");
    }
    std::puts("PASS: vendor-neutral renderer-family routing, native-setting isolation, actual-surface gating, no ANGLE double-processing, no fake DirectX/Vulkan support, strict neutral/frame counters and value bounds");
}
