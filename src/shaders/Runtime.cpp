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
void Runtime::start(std::uintptr_t window){
    setWindow(window);
    startPresentationHooks();
}
void Runtime::setWindow(std::uintptr_t window){
    std::lock_guard<std::mutex> lock(mutex);
    if(host==window)return;
    host=window;state.observed=false;state.active=false;state.deviceReady=false;state.renderer=Renderer::Unknown;
    state.status=window?"Waiting for the identified player's game presentation surface.":"The native player surface is not available; no effects are active.";
}
void Runtime::stop(){stopPresentationHooks();}
void Runtime::set(const Parameters &settings){
    if(!settings.valid())return;
    std::lock_guard<std::mutex> lock(mutex);current=settings;
    if(settings.neutral()){
        state.active=false;
        if(canRoute(state.renderer,state.observed,state.deviceReady,state.requested))state.status="Neutral bypass — game pixels are untouched.";
    }
}
void Runtime::selectRenderer(RendererFamily requested){
    std::lock_guard<std::mutex> lock(mutex);
    state.requested=requested;state.renderer=Renderer::Unknown;state.observed=false;state.active=false;state.deviceReady=false;
    state.status="Waiting for an identified game surface matching the player's renderer setting. No effects have been confirmed.";
}
bool Runtime::accepts(Renderer observed){
    std::lock_guard<std::mutex> lock(mutex);
    // EGL may itself present through DXGI. Honor the player's graphics setting
    // and, in auto mode, bind to the first identified game API family so a
    // translation layer cannot grade the same image a second time.
    const auto selected=(state.requested==RendererFamily::Automatic&&state.observed)
        ?family(state.renderer):state.requested;
    return canRoute(observed,true,true,selected);
}
Parameters Runtime::settings(){std::lock_guard<std::mutex> lock(mutex);return current;}
Metrics Runtime::metrics(){std::lock_guard<std::mutex> lock(mutex);return state;}
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
    state.status=(current.neutral()&&routed)?"Neutral bypass — game pixels are untouched.":status;
    if(active&&!routed)state.status="The observed game renderer's backend/device requirements are not validated; no effects were confirmed.";
    if(processed&&state.active)++state.processed;
}
bool Runtime::eligible(std::uintptr_t surface){
#ifdef _WIN32
    std::uintptr_t root;
    {std::lock_guard<std::mutex> lock(mutex);root=host;}
    HWND window=reinterpret_cast<HWND>(surface),player=reinterpret_cast<HWND>(root);
    if(!window||!player||!IsWindow(window)||window==player||!IsChild(player,window))return false;
    DWORD owner=0;GetWindowThreadProcessId(window,&owner);
    if(owner!=GetCurrentProcessId())return false;
    wchar_t type[160]{};
    if(!GetClassNameW(window,type,160)||wcsncmp(type,L"Qt",2)==0||wcsncmp(type,L"Chrome",6)==0)return false;
    RECT rect{};return GetClientRect(window,&rect)&&rect.right>=320&&rect.bottom>=200;
#else
    (void)surface;return false;
#endif
}
#ifndef _WIN32
void startPresentationHooks(){Runtime::instance().report(false,false,false,"Windows presentation hooks are unavailable on this platform.");}
void stopPresentationHooks(){}
#endif
} // namespace BsiShaders
