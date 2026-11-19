#include "Runtime.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace BsiShaders {
Runtime &Runtime::instance(){static auto *runtime=new Runtime;return *runtime;}
void Runtime::start(std::uintptr_t window){
    {std::lock_guard<std::mutex> lock(mutex);host=window;}
    startPresentationHooks();
}
void Runtime::stop(){stopPresentationHooks();}
void Runtime::set(const Parameters &settings){
    if(!settings.valid())return;
    std::lock_guard<std::mutex> lock(mutex);current=settings;
    if(settings.neutral()){
        state.active=false;
        if(canRoute(state.renderer,state.observed,true,state.requested))state.status="Neutral bypass — game pixels are untouched.";
    }
}
void Runtime::selectRenderer(RendererFamily requested){
    std::lock_guard<std::mutex> lock(mutex);
    state.requested=requested;state.renderer=Renderer::Unknown;state.observed=false;state.active=false;
    state.status=(requested==RendererFamily::DirectX||requested==RendererFamily::Vulkan)
        ?"The selected renderer's shader backend is not implemented yet; game frames are untouched."
        :"Waiting for the selected game's presentation API.";
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
void Runtime::report(bool hooks,bool observed,bool active,const std::string &status,bool processed,Renderer renderer){
    std::lock_guard<std::mutex> lock(mutex);
    state.hooks=state.hooks||hooks;
    const auto selected=(state.requested==RendererFamily::Automatic&&state.observed)
        ?family(state.renderer):state.requested;
    if(observed && renderer!=Renderer::Unknown){
        if(!matches(selected,renderer))return;
        state.observed=true;state.renderer=renderer;
    }
    const bool routed=canRoute(state.renderer,state.observed,true,selected);
    state.active=active&&routed&&!current.neutral();
    state.status=(current.neutral()&&routed)?"Neutral bypass — game pixels are untouched.":status;
    if(active&&!routed)state.status="The observed game renderer has no implemented shader backend; no effects were applied.";
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
