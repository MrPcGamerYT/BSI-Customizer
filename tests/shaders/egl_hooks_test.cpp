// Real MinHook/Win32 ownership + two EGL-shaped lifecycle DLL doubles.
// No GL pixels, protected GPU memory, emulator or physical driver executes.
#include "Runtime.h"
#include "PresentationHooks.h"
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>

using namespace BsiShaders;
namespace {
void require(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n%s\n",message,Runtime::instance().metrics().status.c_str());std::exit(1);}}
LRESULT CALLBACK windowProc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcW(w,m,a,b);}
struct Fixture {
    HMODULE module=nullptr;void *display=nullptr;
    template<typename Function>Function function(const char *name){
        const auto entry=GetProcAddress(module,name);require(entry!=nullptr,"required public EGL fixture entry point");
        return reinterpret_cast<Function>(reinterpret_cast<void*>(entry));
    }
    void open(const char *path){
        module=LoadLibraryA(path);require(module!=nullptr,"load independently named EGL provider");
        display=function<void*(*)()>("bsiFixtureDisplay")();
        const char *names[]{"eglSwapBuffers","eglCreateWindowSurface","eglCreatePlatformWindowSurface","eglCreatePlatformWindowSurfaceEXT","eglDestroySurface","eglDestroyContext"};
        std::set<FARPROC> entries;
        for(const auto *name:names)require(entries.insert(GetProcAddress(module,name)).second,"detourable distinct fixture exports, not ICF-folded aliases");
    }
    void ready(){
        for(unsigned i=0;i<100&&!originalPresentationFunction(reinterpret_cast<void*>(GetProcAddress(module,"eglSwapBuffers")));++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(originalPresentationFunction(reinterpret_cast<void*>(GetProcAddress(module,"eglSwapBuffers")))!=nullptr,"the actual EGL provider was hooked, not just a loaded GL/DX/Vulkan DLL");
    }
    void *context(bool protectedContent=false){return function<void*(*)(int)>("bsiFixtureContext")(protectedContent);}
    void *surface(HWND window,int variant=0,bool protectedContent=false){
        const int attrs[]{0x32c0,protectedContent,0x3038};
        if(variant==1){const std::intptr_t wide[]{0x32c0,protectedContent,0x3038};return function<void*(WINAPI*)(void*,void*,void*,const std::intptr_t*)>("eglCreatePlatformWindowSurface")(display,nullptr,window,wide);}
        return function<void*(WINAPI*)(void*,void*,void*,const int*)>(variant==2?"eglCreatePlatformWindowSurfaceEXT":"eglCreateWindowSurface")(display,nullptr,window,attrs);
    }
    void current(void *surface,void *context){require(function<unsigned(WINAPI*)(void*,void*,void*,void*)>("eglMakeCurrent")(display,surface,surface,context)!=0,"bind exact fixture context and drawable");}
    LONG count(const char *name="bsiFixtureSwapCalls"){return function<LONG(*)()>(name)();}
    void swap(void *surface){
        const auto before=count();require(function<unsigned(WINAPI*)(void*,void*)>("eglSwapBuffers")(display,surface)!=0,"original EGL presentation succeeds");
        require(count()==before+1,"every original EGL swap executes exactly once, including every safety bypass");
    }
    void removeSurface(void *value){require(function<unsigned(WINAPI*)(void*,void*)>("eglDestroySurface")(display,value)!=0,"native surface teardown");}
    void removeContext(void *value){require(function<unsigned(WINAPI*)(void*,void*)>("eglDestroyContext")(display,value)!=0,"native context teardown");}
};
}
int main(int argc,char **argv){
    require(argc==3,"pass two authored EGL provider DLL paths");
    HWND root=CreateWindowExW(0,L"STATIC",L"EGL test-owned player",WS_OVERLAPPEDWINDOW,0,0,900,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND game=CreateWindowExW(0,L"STATIC",L"EGL test-owned game",WS_CHILD,0,0,640,360,root,nullptr,GetModuleHandleW(nullptr),nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"QtEglTestExcluded";
    require(RegisterClassW(&cls)!=0,"register actual Qt-classified excluded HWND");
    HWND ui=CreateWindowExW(0,cls.lpszClassName,L"EGL UI only",WS_CHILD,0,0,640,360,root,nullptr,cls.hInstance,nullptr);
    require(root&&game&&ui,"create test-owned native windows");
    Fixture a,b;a.open(argv[1]);
    const auto ca=a.context(),lateUnknown=a.surface(game);require(ca&&lateUnknown,"create intentionally pre-existing surface before hooks");
    auto &runtime=Runtime::instance();runtime.configureRenderer(RendererFamily::Vulkan);runtime.start(reinterpret_cast<std::uintptr_t>(root));a.ready();
    const auto uiA=a.surface(ui);a.current(uiA,ca);a.swap(uiA);
    require(!runtime.metrics().observed,"Qt/UI-first EGL provider does not identify the game renderer");
    a.current(lateUnknown,ca);a.swap(lateUnknown);
    require(!runtime.metrics().observed,"unknown pre-existing EGL surfaces are never guessed from config, context or device");a.removeSurface(lateUnknown);
    const auto protectedSurface=a.surface(game,0,true);a.current(protectedSurface,ca);a.swap(protectedSurface);
    require(!runtime.metrics().observed&&a.count("bsiFixtureGlQueries")==0,"protected surface is excluded before ANY optional GL work");a.removeSurface(protectedSurface);
    const auto normal=a.surface(game,1),protectedContext=a.context(true);
    require(normal&&protectedContext,"platform-window surface and protected context fixture");
    a.current(normal,protectedContext);a.swap(normal);
    require(!runtime.metrics().observed&&a.count("bsiFixtureProtectionQueries")>0&&a.count("bsiFixtureGlQueries")==0,
            "regular surface with protected context is rejected by legal EGL context queries");a.removeContext(protectedContext);
    const auto other=a.surface(game);a.current(normal,ca);a.swap(other);
    require(!runtime.metrics().observed,"a swapped surface different from the current EGL_DRAW drawable cannot adopt the current context");
    a.swap(normal);auto m=runtime.metrics();
    require(m.observed&&m.renderer==Renderer::OpenGLES&&m.configured==RendererFamily::Vulkan&&m.requested==RendererFamily::Automatic&&!m.active&&m.processed==0,
            "actual owned GLES presentation routes under a Vulkan hint without fake shader-frame counts or API relabeling");
    require(!runtime.claimPresentation(Renderer::Direct3D11,123,reinterpret_cast<std::uintptr_t>(game)),"nested translation-layer presentation cannot take the selected source");
    const auto unrelated=a.surface(game);a.removeSurface(unrelated);
    require(runtime.metrics().observed,"destroying another drawable on the same HWND must not release the selected surface's claim");
    a.removeSurface(normal);require(!runtime.metrics().observed,"surface teardown releases routing even while its context remains alive");
    a.current(other,ca);a.swap(other);require(runtime.metrics().observed,"recreated drawable with the same native context is re-adopted");
    a.removeContext(ca);require(!runtime.metrics().observed,"context teardown releases its presentation token");a.removeSurface(other);a.removeSurface(uiA);
    b.open(argv[2]);b.ready();
    const auto cb=b.context(),gameB=b.surface(game,2);require(cb&&gameB,"second late-loaded EXT-platform provider has its own context/drawable");
    b.current(gameB,cb);b.swap(gameB);
    require(runtime.metrics().observed&&runtime.metrics().renderer==Renderer::OpenGLES&&runtime.metrics().processed==0,
            "late second EGL provider is not stolen by the first UI provider or a stale context");
    b.function<void(*)(int)>("bsiFixtureExtensions")(0);runtime.configureRenderer(RendererFamily::DirectX);b.swap(gameB);
    require(runtime.metrics().observed&&b.count("bsiFixtureProtectionQueries")==0&&b.count("bsiFixtureIllegalQueries")==0,
            "extension-only protection enums are never queried when absent; actual GLES still ignores a DirectX config hint");
    b.removeContext(cb);b.removeSurface(gameB);
    require(!runtime.metrics().observed&&a.count("bsiFixtureGlQueries")==0&&b.count("bsiFixtureGlQueries")==0,
            "neutral/lifecycle tests execute no GL inspection or effect passes and leave no routing claim");
    runtime.stop();DestroyWindow(ui);DestroyWindow(game);DestroyWindow(root);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    std::puts("PASS: production EGL detours, UI-first/late independent providers, core/EXT platform-window creation, exact current drawable, protection gates, config-independent actual GLES and surface/context teardown re-detection; exactly-once native swaps");
    std::puts("LIMIT: authored EGL lifecycle doubles only, no GL driver/game pixels, protected GPU memory or physical GPU/player certification");
}
