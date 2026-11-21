#include "Parameters.h"
#include "RendererCapabilities.h"
#include "Runtime.h"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
    require(!runtime.accepts(Renderer::OpenGL),"explicit Vulkan-only test policy rejects GL; this is NOT an emulator-config hint");
    runtime.set(Parameters{});
    require(runtime.metrics().status.find("not validated")!=std::string::npos,"neutral state must not hide an unvalidated-device diagnostic");
    runtime.selectRenderer(RendererFamily::DirectX);runtime.set(p);
    runtime.report(true,true,true,"must not be reported active",true,Renderer::Direct3D11);
    require(!runtime.metrics().active&&runtime.metrics().processed==0,"an unvalidated DX11 device must not count a frame");
    require(!runtime.accepts(Renderer::OpenGLES),"explicit DirectX-only test policy rejects GL/GLES, separate from automatic config hints");
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
#ifdef _WIN32
    const auto root=CreateWindowExW(0,L"STATIC",L"routing unit root",WS_OVERLAPPEDWINDOW,0,0,900,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const auto child=CreateWindowExW(0,L"STATIC",L"routing unit game",WS_CHILD,0,0,640,480,root,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(root&&child,"create live routing-unit HWND identities");
    runtime.setWindow(reinterpret_cast<std::uintptr_t>(root));
    const auto window=reinterpret_cast<std::uintptr_t>(child);
    require(runtime.eligible(window)&&!runtime.eligible(reinterpret_cast<std::uintptr_t>(root)),"only the live game child passes native ownership/size gates, never the root UI");
#else
    const std::uintptr_t window=303;
#endif
    for(const auto hint:{RendererFamily::OpenGL,RendererFamily::DirectX,RendererFamily::Vulkan,RendererFamily::Automatic}){
        runtime.configureRenderer(hint);runtime.set(p);
        const auto before=runtime.metrics().processed;
        runtime.vulkanEvent(VulkanEvent::DeviceTracked);
        require(runtime.accepts(Renderer::OpenGLES)&&runtime.accepts(Renderer::Direct3D11)&&runtime.accepts(Renderer::Vulkan)
                &&!runtime.metrics().observed,"every configured/unknown hint keeps all actual presentation APIs eligible; a Vulkan device alone cannot lock routing");
        require(!runtime.claimPresentation(Renderer::Unknown,11,window)&&!runtime.claimPresentation(Renderer::OpenGL,0,window),"invalid presentation identities cannot claim a route");
        require(runtime.claimPresentation(Renderer::OpenGLES,11,window),"actual owned GLES presentation can claim even under a Vulkan hint");
        require(!runtime.claimPresentation(Renderer::Direct3D11,33,window)&&!runtime.claimPresentation(Renderer::OpenGL,44,window)
                &&!runtime.claimPresentation(Renderer::OpenGLES,55,window),"arbitration precedes processing, rejecting nested cross-family AND same-family translation providers");
        runtime.report(true,true,true,"actual GLES pass",true,Renderer::OpenGLES,true);
        require(runtime.metrics().requested==RendererFamily::Automatic&&runtime.metrics().configured==hint
                &&runtime.metrics().renderer==Renderer::OpenGLES&&runtime.metrics().processed==before+1,"hint and actual game API remain distinct; only the actual pass increments counters");
        runtime.releasePresentation(Renderer::OpenGL,11);
        require(!runtime.accepts(Renderer::Vulkan),"the wrong renderer cannot revoke another source's arbitration token");
        runtime.releasePresentation(Renderer::OpenGLES,11);
        require(runtime.accepts(Renderer::Vulkan)&&!runtime.metrics().observed,"destroying the selected game source re-arms automatic detection");
        require(runtime.claimPresentation(Renderer::Vulkan,66,window),"a replacement native Vulkan source can be adopted after old GLES teardown");
        runtime.releasePresentation(Renderer::Vulkan,66);
    }
    runtime.configureRenderer(RendererFamily::Vulkan);
    std::atomic<unsigned> claims{0};
    std::thread a([&]{if(runtime.claimPresentation(Renderer::OpenGLES,101,window))++claims;});
    std::thread b([&]{if(runtime.claimPresentation(Renderer::Direct3D11,202,window))++claims;});
    a.join();b.join();require(claims==1,"concurrent presentation callbacks must claim exactly one source BEFORE either backend can process pixels");
    runtime.configureRenderer(RendererFamily::Automatic);runtime.setTexture({true,true});
    require(!runtime.isPresentation(Renderer::OpenGLES,901,window)&&!runtime.metrics().observed,
            "a read-only draw authorization check cannot claim an unobserved route");
    runtime.textureReport(3,"unobserved must not count",true,TextureDrawReason::Prepared);
    require(!runtime.metrics().textureDraws&&!runtime.metrics().textureInspected,"no presentation means no texture evidence, even if a stale callback tries to report bindings");
    require(runtime.claimPresentation(Renderer::OpenGLES,901,window)&&runtime.isPresentation(Renderer::OpenGLES,901,window)
            &&!runtime.isPresentation(Renderer::OpenGLES,902,window)&&!runtime.isPresentation(Renderer::OpenGL,901,window),
            "draw authorization requires the already-selected renderer, presentation identity and exact game window");
    runtime.textureReport(0,"armed only",true);
    require(!runtime.metrics().textureInspected&&!runtime.metrics().textureDraws,"hook availability is not an inspected or prepared scene call");
    runtime.textureReport(0,"compositor depth bypass",true,TextureDrawReason::Depth);
    runtime.textureReport(0,"scene without mip samplers",true,TextureDrawReason::NoSampler);
    runtime.textureReport(2,"private scene preparation",true,TextureDrawReason::Prepared);
    runtime.textureReport(0,"later UI bypass",true,TextureDrawReason::Blended);
    m=runtime.metrics();
    require(m.textureInspected==4&&m.textureSceneDraws==2&&m.textureDraws==1&&m.textureBindings==2
            &&m.textureDepthBypasses==1&&m.textureBlendBypasses==1&&m.textureSamplerBypasses==1
            &&m.textureStatus=="private scene preparation"&&m.textureLastBypass=="later UI bypass",
            "aggregate scene/bypass evidence is truthful and a final HUD call does not erase earlier real sampler preparation");
    runtime.setTexture({false,true});runtime.textureReport(2,"off must not count",true,TextureDrawReason::Prepared);
    require(!runtime.metrics().textureDraws&&!runtime.metrics().textureInspected&&!runtime.metrics().textureSupported,
            "off clears old evidence instead of reusing lifetime sampler counts");
    runtime.setTexture({true,true});runtime.textureReport(1,"new enabled preparation",true,TextureDrawReason::Prepared);
    require(runtime.metrics().textureDraws==1,"on starts a new evidence interval");
    runtime.releasePresentation(Renderer::OpenGLES,901);
    require(!runtime.isPresentation(Renderer::OpenGLES,901,window)&&!runtime.metrics().textureDraws&&!runtime.metrics().textureBindings
            &&runtime.metrics().textureLastBypass.empty(),"source destruction revokes scene authorization and texture counters/last bypass");
    require(runtime.claimPresentation(Renderer::Direct3D11,903,window),"another actual renderer can replace the destroyed GL source");
    runtime.textureReport(4,"a presentation shader is not a D3D texture implementation",true,TextureDrawReason::Prepared);
    require(!runtime.metrics().textureDraws&&!runtime.metrics().textureSupported,"missing D3D/Vulkan texture binding implementation cannot be reported as successful");
    runtime.releasePresentation(Renderer::Direct3D11,903);runtime.setTexture({});
    runtime.configureRenderer(RendererFamily::Automatic);
#ifdef _WIN32
    DestroyWindow(child);DestroyWindow(root);
#endif
    for(std::size_t i=0;i<controls.size();++i){
        Parameters v;v.enabled=true;v.values[i]=controls[i].minimum;
        require(v.valid(),"minimum value validation");
        v.values[i]=controls[i].maximum;require(v.valid(),"maximum value validation");
        v.values[i]=controls[i].maximum+1;require(!v.valid(),"out of range validation");
    }
    std::puts("PASS: automatic actual-presentation routing across all config hints, concurrent/nested/same-family arbitration and source-teardown re-detection, explicit backend qualification, actual-surface and device-feature gating, no ANGLE double-processing, no unvalidated success counters, strict neutral bypass and value bounds");
}
