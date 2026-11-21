#include "Runtime.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace BsiShaders {
Runtime &Runtime::instance(){static auto *runtime=new Runtime;return *runtime;}
void Runtime::resetTextureEvidence(){
    state.textureSupported=false;
    state.textureDraws=state.textureBindings=state.textureInspected=state.textureSceneDraws=0;
    state.textureDepthBypasses=state.textureBlendBypasses=state.textureSamplerBypasses=state.textureOtherBypasses=0;
    state.textureLastBypass.clear();
}
void Runtime::start(std::uintptr_t window){
    setWindow(window);
    startPresentationHooks();
}
void Runtime::setWindow(std::uintptr_t window){
    std::lock_guard<std::mutex> lock(mutex);
    if(host==window)return;
    host=window;presentationObject=0;presentationWindow=0;
    state.observed=false;state.active=false;state.deviceReady=false;state.renderer=Renderer::Unknown;
    resetTextureEvidence();
    if(currentTexture.enabled)state.textureStatus="The native player surface changed; waiting for a newly qualified game scene draw.";
    state.status=window?"Waiting for the identified player's game presentation surface.":"The native player surface is not available; no effects are active.";
}
void Runtime::stop(){stopPresentationHooks();}
void Runtime::set(const Parameters &settings){
    if(!settings.valid())return;
    std::lock_guard<std::mutex> lock(mutex);current=settings;
    if(settings.neutral()){
        state.active=false;
        if(canRoute(state.renderer,state.observed,state.deviceReady,state.requested))state.status="Neutral bypass - game pixels are untouched.";
    }
}
void Runtime::selectRenderer(RendererFamily requested){
    std::lock_guard<std::mutex> lock(mutex);
    state.requested=requested;presentationObject=0;presentationWindow=0;
    state.renderer=Renderer::Unknown;state.observed=false;state.active=false;state.deviceReady=false;
    resetTextureEvidence();
    if(currentTexture.enabled)state.textureStatus="Waiting for a newly qualified game presentation; no texture override is confirmed.";
    state.status="Waiting for an identified game surface matching the player's renderer setting. No effects have been confirmed.";
}
void Runtime::configureRenderer(RendererFamily configured){
    std::lock_guard<std::mutex> lock(mutex);
    state.configured=configured;state.requested=RendererFamily::Automatic;presentationObject=0;presentationWindow=0;
    state.renderer=Renderer::Unknown;state.observed=false;state.active=false;state.deviceReady=false;
    resetTextureEvidence();
    if(currentTexture.enabled)state.textureStatus="Automatically waiting for a qualified game scene draw; a graphics setting is not proof of texture reduction.";
    state.status="Automatically detecting the owned game's actual host presentation API. The emulator graphics setting is only a hint.";
}
bool Runtime::accepts(Renderer observed){
    std::lock_guard<std::mutex> lock(mutex);
    // EGL may itself present through DXGI. Bind automatic routing to the first
    // identified GAME presentation family, not a loaded DLL/device/config hint,
    // so a translation layer cannot grade the same image a second time.
    const auto selected=(state.requested==RendererFamily::Automatic&&state.observed)
        ?family(state.renderer):state.requested;
    return canRoute(observed,true,true,selected);
}
bool Runtime::claimPresentation(Renderer renderer,std::uintptr_t object,std::uintptr_t window){
    if(!object||!window||renderer==Renderer::Unknown)return false;
    std::lock_guard<std::mutex> lock(mutex);
#ifdef _WIN32
    // A destroyed HWND can precede its context's eventual deletion. Do not
    // leave automatic routing pinned to that dead presentation source.
    if(presentationWindow&&!IsWindow(reinterpret_cast<HWND>(presentationWindow))){
        presentationObject=0;presentationWindow=0;state.renderer=Renderer::Unknown;
        state.observed=false;state.deviceReady=false;state.active=false;resetTextureEvidence();
    }
#endif
    if(!matches(state.requested,renderer))return false;
    if(presentationObject)
        return state.renderer==renderer&&presentationObject==object&&presentationWindow==window;
    if(state.observed&&!matches(family(state.renderer),renderer))return false;
    presentationObject=object;presentationWindow=window;state.renderer=renderer;
    state.observed=true;state.deviceReady=false;state.active=false;resetTextureEvidence();
    if(currentTexture.enabled)state.textureStatus=family(renderer)!=RendererFamily::OpenGL
        ?"Potato graphic is not implemented for the observed DirectX/Vulkan texture-binding path. Original textures are retained; presentation shaders remain independent."
        :"Owned GL/GLES presentation identified. Waiting for eligible scene samplers; a compositor frame alone does not prove texture reduction.";
    return true;
}
bool Runtime::isPresentation(Renderer renderer,std::uintptr_t object,std::uintptr_t window){
    std::lock_guard<std::mutex> lock(mutex);
    return object&&window&&state.observed&&state.renderer==renderer
        &&presentationObject==object&&presentationWindow==window;
}
void Runtime::releasePresentation(Renderer renderer,std::uintptr_t object){
    std::lock_guard<std::mutex> lock(mutex);
    if(!object||presentationObject!=object||state.renderer!=renderer)return;
    presentationObject=0;presentationWindow=0;state.observed=false;state.active=false;state.deviceReady=false;state.renderer=Renderer::Unknown;
    resetTextureEvidence();
    if(currentTexture.enabled)state.textureStatus="The previous game presentation source was destroyed; no texture override is confirmed on a replacement yet.";
    state.status="The previous game presentation source was destroyed; automatically detecting its replacement. No new effects have been confirmed.";
}
Parameters Runtime::settings(){std::lock_guard<std::mutex> lock(mutex);return current;}
void Runtime::setTexture(const TextureConfiguration &settings){
    std::lock_guard<std::mutex> lock(mutex);
    if(currentTexture.enabled==settings.enabled&&currentTexture.externalOverridesReviewed==settings.externalOverridesReviewed)return;
    currentTexture=settings;resetTextureEvidence();
    state.textureStatus=!settings.enabled?"Potato graphic is off; native texture sampling retained.":
        state.observed&&family(state.renderer)!=RendererFamily::OpenGL?
        "Potato graphic is not implemented for the observed DirectX/Vulkan texture-binding path. Original textures are retained; presentation shaders remain independent.":
        "Potato graphic saved. Waiting for a qualified owned GL/GLES scene draw; no texture reduction is confirmed yet.";
}
TextureConfiguration Runtime::texture(){std::lock_guard<std::mutex> lock(mutex);return currentTexture;}
void Runtime::textureReport(unsigned bindings,const std::string &detail,bool supported,TextureDrawReason reason){
    std::lock_guard<std::mutex> lock(mutex);
    if(!currentTexture.enabled||!state.observed||family(state.renderer)!=RendererFamily::OpenGL)return;
    state.textureSupported=supported;
    if(reason!=TextureDrawReason::NotInspected){
        ++state.textureInspected;
        switch(reason){
        case TextureDrawReason::Depth:++state.textureDepthBypasses;break;
        case TextureDrawReason::Blended:++state.textureBlendBypasses;break;
        case TextureDrawReason::NoSampler:++state.textureSceneDraws;++state.textureSamplerBypasses;break;
        case TextureDrawReason::Prepared:++state.textureSceneDraws;break;
        default:++state.textureOtherBypasses;break;
        }
        if(!bindings&&!detail.empty())state.textureLastBypass=detail;
    }
    // A subsequent HUD/compositor draw is not evidence that an earlier scene
    // preparation failed. Retain that evidence AND expose the latest bypass.
    if(!detail.empty()&&(!state.textureDraws||bindings||!supported))state.textureStatus=detail;
    if(bindings&&supported&&reason==TextureDrawReason::Prepared){++state.textureDraws;state.textureBindings+=bindings;}
}
Metrics Runtime::metrics(){std::lock_guard<std::mutex> lock(mutex);return state;}
void Runtime::waiting(bool hooks,const std::string &status){
    std::lock_guard<std::mutex> lock(mutex);
    state.hooks=state.hooks||hooks;
    // A worker's stale 'not observed' snapshot must not clear a render thread's
    // newer confirmed active result, or overwrite a precise Vulkan blocker.
    if(!state.observed){
        if(state.requested==RendererFamily::Vulkan)state.status=state.vulkan.waiting();
        else if(state.requested==RendererFamily::Automatic&&(state.configured==RendererFamily::Vulkan||state.vulkan.loaders))
            state.status="Automatic game-presentation detection is still scanning OpenGL/EGL, DirectX and Vulkan. "+state.vulkan.waiting();
        else state.status=status;
    }
}
void Runtime::vulkanEvent(VulkanEvent event,const std::string &detail){
    std::lock_guard<std::mutex> lock(mutex);state.vulkan.note(event,detail);
    if(!state.observed){
        if(state.requested==RendererFamily::Vulkan)state.status=state.vulkan.waiting();
        else if(state.requested==RendererFamily::Automatic)
            state.status="Automatic detection is not locked to the configured renderer; other owned game presentation APIs remain eligible. "+state.vulkan.waiting();
    }
}
std::string Runtime::gameWindowDiagnostic(std::uintptr_t surface){
#ifdef _WIN32
    std::uintptr_t root;{std::lock_guard<std::mutex> lock(mutex);root=host;}
    HWND window=reinterpret_cast<HWND>(surface),player=reinterpret_cast<HWND>(root);
    if(!player||!IsWindow(player))return "Vulkan surface captured before the native player HWND was attached; waiting for platform-surface creation.";
    if(!window||!IsWindow(window))return "Vulkan surface has no live, captured Win32 HWND. Unidentified frames remain untouched.";
    if(window==player)return "Vulkan surface belongs to the player/UI root, not an identified game child. No UI framebuffer is processed.";
    DWORD owner=0;GetWindowThreadProcessId(window,&owner);
    if(owner!=GetCurrentProcessId())return "Vulkan HWND belongs to another process; external game windows are not adopted.";
    if(!IsChild(player,window))return "Vulkan HWND is not a child of the attached player window; ownership is unproven. No unrelated window is processed.";
    wchar_t type[160]{};
    if(!GetClassNameW(window,type,160)||wcsncmp(type,L"Qt",2)==0||wcsncmp(type,L"Chrome",6)==0)
        return "Vulkan HWND is a Qt/Chrome or unidentified UI surface; it is excluded from game effects.";
    RECT rect{};
    if(!GetClientRect(window,&rect)||rect.right<320||rect.bottom<200)
        return "Captured Vulkan game child is not yet at least 320 by 200 pixels; waiting for native layout.";
    return {};
#else
    (void)surface;return "Win32 game-window ownership checks are unavailable on this platform.";
#endif
}
void Runtime::report(bool hooks,bool observed,bool active,const std::string &status,bool processed,Renderer renderer,bool deviceRequirementsMet){
    std::lock_guard<std::mutex> lock(mutex);
    state.hooks=state.hooks||hooks;
    const auto selected=(state.requested==RendererFamily::Automatic&&state.observed)
        ?family(state.renderer):state.requested;
    if(observed && renderer!=Renderer::Unknown){
        if(!matches(selected,renderer))return;
        state.observed=true;state.renderer=renderer;state.deviceReady=deviceRequirementsMet;
    }
    const bool routed=canRoute(state.renderer,state.observed,state.deviceReady,selected);
    state.active=active&&routed&&!current.neutral();
    state.status=(current.neutral()&&routed)?"Neutral bypass - game pixels are untouched.":status;
    if(active&&!routed)state.status="The observed game renderer's backend/device requirements are not validated; no effects were confirmed.";
    if(processed&&state.active)++state.processed;
}
bool Runtime::ownsGameWindow(std::uintptr_t surface){
#ifdef _WIN32
    std::uintptr_t root;
    {std::lock_guard<std::mutex> lock(mutex);root=host;}
    HWND window=reinterpret_cast<HWND>(surface),player=reinterpret_cast<HWND>(root);
    if(!window||!player||!IsWindow(window)||window==player||!IsChild(player,window))return false;
    DWORD owner=0;GetWindowThreadProcessId(window,&owner);
    if(owner!=GetCurrentProcessId())return false;
    wchar_t type[160]{};
    if(!GetClassNameW(window,type,160)||wcsncmp(type,L"Qt",2)==0||wcsncmp(type,L"Chrome",6)==0)return false;
    return true;
#else
    (void)surface;return false;
#endif
}
bool Runtime::eligible(std::uintptr_t surface){
#ifdef _WIN32
    if(!ownsGameWindow(surface))return false;
    RECT rect{};return GetClientRect(reinterpret_cast<HWND>(surface),&rect)&&rect.right>=320&&rect.bottom>=200;
#else
    (void)surface;return false;
#endif
}
#ifndef _WIN32
void startPresentationHooks(){Runtime::instance().report(false,false,false,"Windows presentation hooks are unavailable on this platform.");}
void stopPresentationHooks(){}
#endif
} // namespace BsiShaders
