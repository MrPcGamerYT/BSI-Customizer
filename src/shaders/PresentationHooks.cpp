#include "Runtime.h"
#ifdef _WIN32
#include "GlPipeline.h"
#include "PresentationHooks.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include "MinHook.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace BsiShaders {
namespace {
using WglSwap=BOOL(WINAPI*)(HDC);
using WglCurrent=HGLRC(WINAPI*)();
using WglProc=PROC(WINAPI*)(LPCSTR);
using WglDelete=BOOL(WINAPI*)(HGLRC);
using EglSwap=unsigned(WINAPI*)(void*,void*);
using EglCreate=void*(WINAPI*)(void*,void*,void*,const int*);
using EglDestroy=unsigned(WINAPI*)(void*,void*);
using EglCurrent=void*(WINAPI*)();
using EglQuery=unsigned(WINAPI*)(void*,void*,int,int*);
using EglProc=void*(WINAPI*)(const char*);
using EglQueryApi=unsigned(WINAPI*)();
struct Hooks {
    std::mutex mutex,renderMutex,surfaceMutex,hookMutex;
    std::atomic<bool> stopping{false};
    std::thread worker;
    bool started=false;
    HMODULE gl=nullptr,egl=nullptr;
    WglSwap swaps[2]{};void *swapTargets[2]{};
    WglCurrent currentWgl=nullptr;WglProc wglProc=nullptr;WglDelete deleteWgl=nullptr;
    EglSwap swapEgl=nullptr;EglCreate createEgl=nullptr;
    EglDestroy destroySurface=nullptr,destroyContext=nullptr;
    EglCurrent currentEgl=nullptr;EglQuery queryEgl=nullptr;EglProc eglProc=nullptr;EglQueryApi eglApi=nullptr;
    std::map<void*,HWND> surfaces;
    std::map<std::pair<bool,std::uintptr_t>,std::unique_ptr<GlPipeline>> pipelines;
    struct Detour {void *replacement;void *original;};
    std::map<void*,Detour> targets;
};
Hooks &hooks(){static auto *state=new Hooks;return *state;}
template<class T> T exportFunction(HMODULE module,const char *name){
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module,name)));
}
thread_local bool presenting=false;
struct Recursion { bool previous;Recursion():previous(presenting){presenting=true;}~Recursion(){presenting=previous;} };
void *resolve(const char *name,void *user){
    auto &h=hooks();const bool egl=user!=nullptr;
    void *address=nullptr;
    if(egl){if(h.eglProc)address=h.eglProc(name);if(!address&&h.egl)address=reinterpret_cast<void*>(GetProcAddress(h.egl,name));}
    else {
        if(h.wglProc)address=reinterpret_cast<void*>(h.wglProc(name));
        const auto value=reinterpret_cast<std::intptr_t>(address);
        if(!address||value==1||value==2||value==3||value==-1)address=h.gl?reinterpret_cast<void*>(GetProcAddress(h.gl,name)):nullptr;
    }
    return address;
}
void process(bool egl,std::uintptr_t context,int width,int height){
    auto &h=hooks();if(!context||presenting||h.stopping.load())return;
    const auto api=egl&&h.eglApi?h.eglApi():0x30a2u;
    if(egl&&api!=0x30a0u&&api!=0x30a2u)return;
    const auto renderer=api==0x30a0u?Renderer::OpenGLES:Renderer::OpenGL;
    if(!Runtime::instance().accepts(renderer))return;
    const auto p=Runtime::instance().settings();
    if(p.neutral()){Runtime::instance().report(true,true,false,"Neutral bypass",false,renderer);return;}
    std::unique_lock<std::mutex> lock(h.renderMutex,std::try_to_lock);if(!lock.owns_lock())return;
    Recursion guard;
    try {
        const auto key=std::make_pair(egl,context);
        auto it=h.pipelines.find(key);
        if(it==h.pipelines.end()){
            if(h.pipelines.size()>=16){Runtime::instance().report(true,true,false,"Too many game contexts; shaders safely bypassed.",false,renderer);return;}
            it=h.pipelines.emplace(key,std::make_unique<GlPipeline>()).first;
        }
        std::string error;
        const bool ready=it->second->initialize(resolve,egl?reinterpret_cast<void*>(1):nullptr,&error);
        const bool rendered=ready&&it->second->render(width,height,p,&error);
        Runtime::instance().report(true,true,rendered,rendered?"Shaders active on the native OpenGL game surface.":error,rendered,renderer,rendered);
    } catch(...) {Runtime::instance().report(true,true,false,"Shader processing failed; original presentation continues.",false,renderer);}
}
void processWgl(HDC dc){
    auto &h=hooks();if(presenting||h.stopping.load()||!h.currentWgl)return;
    HWND window=WindowFromDC(dc);
    if(!Runtime::instance().eligible(reinterpret_cast<std::uintptr_t>(window)))return;
    RECT rect{};if(!GetClientRect(window,&rect))return;
    process(false,reinterpret_cast<std::uintptr_t>(h.currentWgl()),rect.right,rect.bottom);
}
template<int Index> BOOL WINAPI swapWgl(HDC dc){
    processWgl(dc);Recursion guard;return hooks().swaps[Index](dc);
}
BOOL WINAPI removeWgl(HGLRC context){
    auto &h=hooks();const BOOL okay=h.deleteWgl(context);
    if(okay){std::lock_guard<std::mutex> lock(h.renderMutex);h.pipelines.erase({false,reinterpret_cast<std::uintptr_t>(context)});}
    return okay;
}
void *WINAPI createSurface(void *display,void *config,void *window,const int *attrs){
    auto &h=hooks();void *surface=h.createEgl(display,config,window,attrs);
    if(surface&&window&&IsWindow(reinterpret_cast<HWND>(window))){
        std::lock_guard<std::mutex> lock(h.surfaceMutex);
        if(h.surfaces.size()<128)h.surfaces[surface]=reinterpret_cast<HWND>(window);
    }
    return surface;
}
unsigned WINAPI removeSurface(void *display,void *surface){
    auto &h=hooks();const auto okay=h.destroySurface(display,surface);
    if(okay){std::lock_guard<std::mutex> lock(h.surfaceMutex);h.surfaces.erase(surface);}
    return okay;
}
unsigned WINAPI removeContext(void *display,void *context){
    auto &h=hooks();const auto okay=h.destroyContext(display,context);
    if(okay){std::lock_guard<std::mutex> lock(h.renderMutex);h.pipelines.erase({true,reinterpret_cast<std::uintptr_t>(context)});}
    return okay;
}
unsigned WINAPI swapEgl(void *display,void *surface){
    auto &h=hooks();HWND window=nullptr;
    if(!presenting&&!h.stopping.load()){
        {std::lock_guard<std::mutex> lock(h.surfaceMutex);const auto it=h.surfaces.find(surface);if(it!=h.surfaces.end())window=it->second;}
        if(Runtime::instance().eligible(reinterpret_cast<std::uintptr_t>(window))){
            int width=0,height=0;
            if(h.queryEgl(display,surface,0x3057,&width)&&h.queryEgl(display,surface,0x3056,&height))
                process(true,reinterpret_cast<std::uintptr_t>(h.currentEgl()),width,height);
        }
    }
    Recursion guard;return h.swapEgl(display,surface);
}
bool install(void *target,void *replacement,void **original){
    if(!target||!replacement)return false;
    auto &h=hooks();std::lock_guard<std::mutex> lock(h.hookMutex);
    if(h.stopping.load())return false;
    const auto found=h.targets.find(target);
    if(found!=h.targets.end()){
        if(found->second.replacement!=replacement)return false;
        if(original)*original=found->second.original;
        return true;
    }
    // A host may unload/reload its renderer. Keep an installed detour's code
    // module alive rather than leaving a trampoline into an unmapped DLL.
    HMODULE owner=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(target),&owner))return false;
    void *trampoline=nullptr;
    if(MH_CreateHook(target,replacement,&trampoline)!=MH_OK)return false;
    if(original)*original=trampoline;
    // Publish the trampoline before enabling the callback on a host thread.
    h.targets.emplace(target,Hooks::Detour{replacement,trampoline});
    if(MH_EnableHook(target)!=MH_OK){h.targets.erase(target);MH_RemoveHook(target);if(original)*original=nullptr;return false;}
    return true;
}
void scan(){
    auto &h=hooks();
    if(!h.gl){
        h.gl=GetModuleHandleW(L"opengl32.dll");
        if(h.gl){h.currentWgl=exportFunction<WglCurrent>(h.gl,"wglGetCurrentContext");h.wglProc=exportFunction<WglProc>(h.gl,"wglGetProcAddress");}
    }
    if(h.gl&&h.currentWgl&&h.wglProc){
        // Context identity is reusable after destruction. Do not cache driver
        // object names at all unless their destruction can be observed.
        if(!h.deleteWgl)install(reinterpret_cast<void*>(GetProcAddress(h.gl,"wglDeleteContext")),reinterpret_cast<void*>(&removeWgl),reinterpret_cast<void**>(&h.deleteWgl));
        void *candidates[]{reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"),"SwapBuffers")),reinterpret_cast<void*>(GetProcAddress(h.gl,"wglSwapBuffers"))};
        void *callbacks[]{reinterpret_cast<void*>(&swapWgl<0>),reinterpret_cast<void*>(&swapWgl<1>)};
        if(h.deleteWgl)for(int i=0;i<2;++i)if(!h.swaps[i]){
            if(install(candidates[i],callbacks[i],reinterpret_cast<void**>(&h.swaps[i])))h.swapTargets[i]=candidates[i];
        }
    }
    if(!h.egl){
        HMODULE modules[512]{};DWORD required=0;
        if(EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&required)){
            const unsigned count=std::min<unsigned>(required/sizeof(HMODULE),512);
            for(unsigned i=0;i<count;++i){
                auto module=modules[i];
                if(!GetProcAddress(module,"eglSwapBuffers")||!GetProcAddress(module,"eglCreateWindowSurface")||!GetProcAddress(module,"eglGetCurrentContext")||!GetProcAddress(module,"eglQuerySurface")||!GetProcAddress(module,"eglGetProcAddress")||!GetProcAddress(module,"eglDestroySurface")||!GetProcAddress(module,"eglDestroyContext")||!GetProcAddress(module,"eglQueryAPI"))continue;
                h.egl=module;h.currentEgl=exportFunction<EglCurrent>(module,"eglGetCurrentContext");
                h.queryEgl=exportFunction<EglQuery>(module,"eglQuerySurface");h.eglProc=exportFunction<EglProc>(module,"eglGetProcAddress");
                h.eglApi=exportFunction<EglQueryApi>(module,"eglQueryAPI");
                // Surface ownership tracking is mandatory. If it cannot be
                // installed, do not process an unidentified Qt/UI framebuffer.
                if(!install(reinterpret_cast<void*>(GetProcAddress(module,"eglCreateWindowSurface")),reinterpret_cast<void*>(&createSurface),reinterpret_cast<void**>(&h.createEgl))){h.egl=nullptr;continue;}
                const bool surfaces=install(reinterpret_cast<void*>(GetProcAddress(module,"eglDestroySurface")),reinterpret_cast<void*>(&removeSurface),reinterpret_cast<void**>(&h.destroySurface));
                const bool contexts=install(reinterpret_cast<void*>(GetProcAddress(module,"eglDestroyContext")),reinterpret_cast<void*>(&removeContext),reinterpret_cast<void**>(&h.destroyContext));
                if(surfaces&&contexts)install(reinterpret_cast<void*>(GetProcAddress(module,"eglSwapBuffers")),reinterpret_cast<void*>(&swapEgl),reinterpret_cast<void**>(&h.swapEgl));
                // Never retarget already-live callbacks to another EGL DLL's
                // entry points after a partial failure. Keep the pinned
                // provider stable and safely bypass incomplete lifecycle hooks.
                break;
            }
        }
    }
    const bool dxgi=scanDxgiHooks(install),vulkan=scanVulkanHooks(install);
    const bool attached=h.swaps[0]||h.swaps[1]||h.swapEgl||dxgi||vulkan;
    Runtime::instance().waiting(attached,attached?"Waiting for an identified game surface matching the player's OpenGL, DirectX or Vulkan setting.":"Waiting for the game's presentation API; no eligible backend has been observed.");
}
}
void *originalPresentationFunction(void *target){
    auto &h=hooks();std::lock_guard<std::mutex> lock(h.hookMutex);
    const auto it=h.targets.find(target);return it==h.targets.end()?nullptr:it->second.original;
}
void startPresentationHooks(){
    auto &h=hooks();std::lock_guard<std::mutex> lock(h.mutex);if(h.started)return;
    h.started=true;
    // Qt calls this after loading/registering the normal QML plugin, outside
    // DllMain/loader lock. Pin callback code for the process lifetime.
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&startPresentationHooks),&self)){
        Runtime::instance().report(false,false,false,"Could not pin shader callback code; hooks not installed.");return;
    }
    const auto status=MH_Initialize();
    if(status!=MH_OK){Runtime::instance().report(false,false,false,"Could not initialize presentation hooks.");return;}
    // Prime ONLY the OS API selected in the player's settings, synchronously
    // before returning from the root bootstrap. Polling alone can miss device /
    // swapchain creation between LoadLibrary and the next scan. No system DLL
    // is replaced and no other renderer is selected as a fallback.
    const auto requested=Runtime::instance().metrics().requested;
    // Honor the player's application/user DLL directories before System32.
    // Forcing only the OS Vulkan loader can hook a different provider from the
    // one the game uses. scanVulkanHooks separately captures each complete
    // already-loaded provider by exports, including a differently named DLL.
    // Never search the current working directory or replace a system DLL.
    if(requested==RendererFamily::Vulkan)LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    else if(requested==RendererFamily::DirectX){
        LoadLibraryExW(L"dxgi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    }else if(requested==RendererFamily::OpenGL)LoadLibraryExW(L"opengl32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    try {
        scan();
        h.worker=std::thread([]{
            while(!hooks().stopping.load()){
                try {scan();}catch(...){}
                for(int i=0;i<15&&!hooks().stopping.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }catch(...){Runtime::instance().report(false,false,false,"Presentation-hook initialization was incomplete; unsupported/unidentified surfaces remain untouched.");}
}
void stopPresentationHooks(){
    auto &h=hooks();std::lock_guard<std::mutex> lock(h.mutex);
    h.stopping.store(true);if(h.worker.joinable())h.worker.join();
    // Do not free trampolines while a host thread could still be returning
    // through one. The pinned module/driver reclaim them at process exit.
    stopDxgiHooks();stopVulkanHooks();
    std::lock_guard<std::mutex> hooksLock(h.hookMutex);
    for(const auto &entry:h.targets)MH_DisableHook(entry.first);
}
} // namespace BsiShaders
#endif
