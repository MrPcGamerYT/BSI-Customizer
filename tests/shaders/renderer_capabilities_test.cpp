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
    // The pipeline code now exists for all target families. This unit test is
    // NOT their certification: separate actual Vulkan and DirectX WARP pixel
    // tests must execute those production implementations successfully.
    require(capability(Renderer::Direct3D11).pipelineImplemented,"D3D11 pipeline routing is declared");
    require(capability(Renderer::Direct3D12).pipelineImplemented,"D3D12 pipeline routing is declared");
    require(capability(Renderer::Vulkan).pipelineImplemented,"Vulkan pipeline routing is declared");
    Parameters p;p.enabled=true;p.values[0]=20;
    auto &runtime=Runtime::instance();runtime.set(p);
    runtime.selectRenderer(RendererFamily::Vulkan);
    runtime.report(true,true,true,"must not be reported active",true,Renderer::Vulkan);
    auto m=runtime.metrics();
    require(!m.active&&!m.deviceReady&&m.processed==0,"an unvalidated Vulkan device cannot claim processed frames");
    require(m.renderer==Renderer::Vulkan,"observed API is not silently changed to OpenGL");
    require(!runtime.accepts(Renderer::OpenGL),"Vulkan setting blocks GL hooks even if OpenGL is loaded");
    runtime.set(Parameters{});
    require(runtime.metrics().status.find("not validated")!=std::string::npos,"neutral state must not hide an unvalidated-device diagnostic");
    runtime.selectRenderer(RendererFamily::DirectX);runtime.set(p);
    runtime.report(true,true,true,"must not be reported active",true,Renderer::Direct3D11);
    require(!runtime.metrics().active&&runtime.metrics().processed==0,"an unvalidated DX11 device must not count a frame");
    require(!runtime.accepts(Renderer::OpenGLES),"DirectX setting blocks the GL/GLES fallback");
    runtime.selectRenderer(RendererFamily::Automatic);
    runtime.report(true,false,true,"unidentified",true,Renderer::OpenGL,true);
    require(!runtime.metrics().active&&runtime.metrics().processed==0,"a surface must actually be identified");
    runtime.report(true,true,true,"identified OpenGL candidate with validated features",true,Renderer::OpenGLES,true);
    require(runtime.metrics().active&&runtime.metrics().processed==1,"identified implemented family can report a processed frame");
    runtime.report(true,true,true,"underlying ANGLE DXGI present",true,Renderer::Direct3D11,true);
    require(runtime.metrics().renderer==Renderer::OpenGLES&&runtime.metrics().processed==1,
            "auto selection prevents double-processing through ANGLE's underlying DXGI layer");
    runtime.waiting(true,"stale worker scan");
    require(runtime.metrics().active&&runtime.metrics().processed==1,"a stale waiting scan must not erase a newer confirmed game frame");
    runtime.vulkanEvent(VulkanEvent::LoaderHooked,"authored-test-provider.dll");
    require(runtime.metrics().renderer==Renderer::OpenGLES&&runtime.metrics().processed==1,"Vulkan lifecycle evidence is not an observed game API or another processed frame");
    runtime.set(Parameters{});
    require(!runtime.metrics().active,"disabled/neutral settings report no active effects");
    runtime.report(true,true,true,"neutral frame",true,Renderer::OpenGLES,true);
    require(runtime.metrics().processed==1,"neutral bypass cannot count as an effect pass");
    VulkanCapture capture;
    require(capture.waiting().find("no complete Vulkan loader")!=std::string::npos,"missing-loader diagnostic");
    capture.note(VulkanEvent::LoaderHooked,"first.dll");
    require(capture.waiting().find("no instance creation")!=std::string::npos,"late lifecycle is diagnosed before shader compilation");
    capture.note(VulkanEvent::InstanceCreated);
    require(capture.waiting().find("device/queue")!=std::string::npos,"instance alone does not prove a device");
    capture.note(VulkanEvent::DeviceRejected,"missing public device entry point");
    require(capture.waiting()=="missing public device entry point","retain an actionable device-capture blocker");
    capture.note(VulkanEvent::DeviceTracked);
    require(capture.waiting().find("no Win32")!=std::string::npos,"non-Win32 surfaces are not guessed");
    capture.note(VulkanEvent::SurfaceCreated);capture.note(VulkanEvent::SwapchainTracked);
    capture.note(VulkanEvent::Presented);capture.note(VulkanEvent::WindowRejected,"Qt UI excluded");
    require(capture.waiting()=="Qt UI excluded"&&capture.rejectedWindows==1,"ownership failures have a distinct diagnostic");
    runtime.selectRenderer(RendererFamily::Vulkan);
    runtime.vulkanEvent(VulkanEvent::QueueMissed,"device predates capture; restart required");
    runtime.waiting(true,"generic scan must not hide the Vulkan stage");
    require(!runtime.metrics().observed&&!runtime.metrics().active&&runtime.metrics().processed==1
            &&runtime.metrics().status.find("no instance creation")!=std::string::npos,
            "loader/stage diagnostics never fabricate observed game frames or weaken routing");
    for(std::size_t i=0;i<controls.size();++i){
        Parameters v;v.enabled=true;v.values[i]=controls[i].minimum;
        require(v.valid(),"minimum value validation");
        v.values[i]=controls[i].maximum;require(v.valid(),"maximum value validation");
        v.values[i]=controls[i].maximum+1;require(!v.valid(),"out of range validation");
    }
    std::puts("PASS: vendor-neutral renderer-family routing, native-setting isolation, actual-surface and device-feature gating, no ANGLE double-processing, no unvalidated success counters, strict neutral bypass and value bounds");
}
