// Real MinHook/Win32 ownership + two EGL-shaped lifecycle DLL doubles.
// No GL pixels, protected GPU memory, emulator or physical driver executes.
#include "Runtime.h"
#include "PresentationHooks.h"
#include "GlDrawCalls.h"
#undef BSI_GL_DRAW_CALLS
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
    void scene(unsigned flags){function<void(*)(unsigned)>("bsiFixtureSceneState")(flags);}
    void draw(GlDrawKind kind=GlDrawKind::ElementsBaseVertex,unsigned alias=0,LONG bindings=2,unsigned mode=4){
        const auto before=count("bsiFixtureDrawCalls"),privateBefore=count("bsiFixturePrivateDraws");
        function<void(*)(unsigned,unsigned,unsigned)>("bsiFixtureDraw")(static_cast<unsigned>(kind),alias,mode);
        require(count("bsiFixtureDrawCalls")==before+1,"each real detour invokes its original exactly once, including bypasses/multi/indirect calls");
        require(count("bsiFixtureArgumentsIntact")&&count("bsiFixtureBindingsRestored")&&!count("bsiFixtureInvalidGl"),
                "all counts, offsets, pointer arrays, signed base vertices and 64-bit count-buffer offsets forward intact; restore exact native samplers/active unit");
        require(count("bsiFixtureLastPrivateBindings")==bindings&&count("bsiFixturePrivateDraws")==privateBefore+(bindings?1:0),
                "private coarser-LOD samplers are visible INSIDE the original draw, never merely counted afterward");
    }
    void nestedDraw(unsigned mode,LONG outerBindings,LONG innerBindings){
        const auto before=count("bsiFixtureDrawCalls"),privateBefore=count("bsiFixturePrivateDraws");
        function<void(*)(unsigned)>("bsiFixtureNestedDraw")(mode);
        require(count("bsiFixtureDrawCalls")==before+2,"outer and inner originals each execute exactly once through real detours");
        require(count("bsiFixtureArgumentsIntact")&&count("bsiFixtureBindingsRestored")&&!count("bsiFixtureInvalidGl"),
                "nested originals retain exact independent arguments and restore the original samplers/active unit");
        require(count("bsiFixtureLastPrivateBindings")==outerBindings&&count("bsiFixtureNestedPrivateBindings")==innerBindings
                &&count("bsiFixturePrivateDraws")==privateBefore+(outerBindings?1:0)+(innerBindings?1:0),
                "check private samplers INSIDE both outer and nested originals, not just report counters");
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
    // Reproduce the missed-scene pattern: plain DrawArrays is only the final
    // depth-disabled compositor; geometry uses base-vertex/multi/indirect APIs
    // and a second context which NEVER swaps on its own.
    runtime.configureRenderer(RendererFamily::Vulkan);runtime.setTexture({true,true});
    const auto presenter=a.context(),scene=a.context(),protectedScene=a.context(true);
    const auto target=a.surface(game),sameWindowOtherDrawable=a.surface(game),uiTarget=a.surface(ui),secret=a.surface(game,0,true);
    require(presenter&&scene&&protectedScene&&target&&sameWindowOtherDrawable&&uiTarget&&secret,"texture dispatch fixtures");
    a.current(target,scene);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(!runtime.metrics().observed&&!runtime.metrics().textureInspected,"a draw alone must not claim presentation or invent game evidence");
    a.current(target,presenter);a.swap(target);
    require(runtime.metrics().observed&&runtime.metrics().textureSupported&&!runtime.metrics().textureDraws,"only the owned swap arms inspection, not successful texture draws");
    a.scene(1);a.draw(GlDrawKind::Arrays,0,0);
    m=runtime.metrics();require(m.textureInspected==1&&m.textureDepthBypasses==1&&!m.textureSceneDraws&&!m.textureDraws,
            "compositor-only draw is inspected but never relabeled as a successful scene preparation");
    a.scene(0);unsigned variants=0;
    for(unsigned kind=0;kind<glDrawKinds;++kind)for(unsigned alias=0;alias<glDrawAliases;++alias)if(glDrawNames[kind][alias]){
        a.draw(static_cast<GlDrawKind>(kind),alias);++variants;
    }
    m=runtime.metrics();require(m.textureDraws==variants&&m.textureBindings==variants*2&&m.textureSceneDraws==variants&&m.textureInspected==variants+1,
            "all scene draw signatures/core/extension aliases, not only the five compositor-era hooks, prepare and count real temporary bindings");
    const auto preparedStatus=m.textureStatus;
    a.scene(1);a.draw(GlDrawKind::Arrays,0,0);
    require(runtime.metrics().textureStatus==preparedStatus&&!runtime.metrics().textureLastBypass.empty(),"last HUD bypass cannot overwrite earlier scene preparation evidence");
    a.scene(0);a.current(target,scene);a.draw();
    require(runtime.metrics().textureDraws==variants+1&&runtime.metrics().renderer==Renderer::OpenGLES&&runtime.metrics().processed==0,
            "a second scene context on the EXACT selected drawable qualifies without stealing presentation or counting visual-effect frames");

    auto inspected=runtime.metrics().textureInspected;
    a.current(target,protectedScene);const auto glBefore=a.count("bsiFixtureGlQueries");a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(a.count("bsiFixtureGlQueries")==glBefore&&runtime.metrics().textureInspected==inspected,"protected alternate context is rejected before ANY GL initialization");
    a.current(sameWindowOtherDrawable,scene);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    a.current(uiTarget,scene);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    a.current(secret,scene);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    a.current(nullptr,scene);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(runtime.metrics().textureInspected==inspected,"same HWND is insufficient for another drawable; UI, protected and pbuffer/unmapped drawables stay native even with a known scene context");
    const auto otherGame=CreateWindowExW(0,L"STATIC",L"other owned child",WS_CHILD,0,0,640,360,root,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(otherGame!=nullptr,"separate owned child fixture");
    const auto otherTarget=a.surface(otherGame);a.current(otherTarget,scene);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(runtime.metrics().textureInspected==inspected,"another eligible owned child is NOT the selected game drawable");
    a.removeSurface(otherTarget);DestroyWindow(otherGame);

    a.current(target,scene);
    for(unsigned flags:{1u,2u,4u,8u,16u,32u,64u}){a.scene(flags);a.draw(GlDrawKind::ElementsBaseVertex,0,0);}
    a.scene(0);a.draw(GlDrawKind::ElementsBaseVertex,0,0,1); // lines, not scene triangles
    m=runtime.metrics();require(m.textureInspected==inspected+8&&m.textureDepthBypasses==6&&m.textureBlendBypasses==1
            &&m.textureSamplerBypasses==2&&m.textureOtherBypasses==1,"depth/write/always-pass/no-depth, blended, missing/protected mips and primitive bypasses remain distinct and never prepared");
    require(m.textureDepthReasons[static_cast<std::size_t>(TextureDepthFailure::TestDisabled)]==3
            &&m.textureDepthReasons[static_cast<std::size_t>(TextureDepthFailure::WritesDisabled)]==1
            &&m.textureDepthReasons[static_cast<std::size_t>(TextureDepthFailure::Always)]==1
            &&m.textureDepthReasons[static_cast<std::size_t>(TextureDepthFailure::MissingAttachment)]==1,
            "the aggregate depth bypass records the actual first blocker, not a guessed missing mip chain");

    // Reproduce the guard-only failure independently of any claim about the
    // user's driver: an outer no-depth wrapper sets real depth inside its
    // original, then issues a different hooked draw on this EXACT owned route.
    runtime.setTexture({false,true});runtime.setTexture({true,true});
    a.scene(1);a.nestedDraw(1,0,2);
    m=runtime.metrics();
    require(m.textureInspected==2&&m.textureDepthBypasses==1&&m.textureSceneDraws==1&&m.textureDraws==1&&m.textureBindings==2
            &&m.textureNestedInspected==1&&m.textureNestedDraws==1,
            "a zero-binding wrapper cannot keep its recursion guard/render lock around the later qualifying inner original");
    a.scene(1);a.nestedDraw(3,0,0); // both calls genuinely lack scene depth
    m=runtime.metrics();
    require(m.textureInspected==4&&m.textureDepthBypasses==3&&m.textureDraws==1&&m.textureNestedInspected==2&&m.textureNestedDraws==1,
            "nested inspection is not permission to force depth-disabled UI/composition through the policy");
    a.scene(0);a.nestedDraw(2,2,2);
    m=runtime.metrics();
    require(m.textureInspected==5&&m.textureDraws==2&&m.textureBindings==4&&m.textureNestedInspected==2&&m.textureNestedDraws==1,
            "a prepared outer draw retains its lease until its original returns; inner callbacks cannot double-override/count borrowed samplers");
    inspected=m.textureInspected;
    const auto beforeProtectedNested=a.count("bsiFixtureGlQueries");
    a.current(target,protectedScene);a.scene(1);a.nestedDraw(1,0,0);
    require(a.count("bsiFixtureGlQueries")==beforeProtectedNested,"nested protected-context calls still perform no optional GL initialization");
    for(auto *unowned:{sameWindowOtherDrawable,uiTarget,secret,static_cast<void*>(nullptr)}){
        a.current(unowned,scene);a.scene(1);a.nestedDraw(1,0,0);
    }
    require(runtime.metrics().textureInspected==inspected,"nested draws cannot infer another drawable, UI, protected or unmapped producer's ownership");
    a.current(target,scene);a.scene(0);
    runtime.setTexture({false,true});a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(!runtime.metrics().textureDraws&&!runtime.metrics().textureBindings&&!runtime.metrics().textureInspected,"off clears prior texture evidence and cannot reuse lifetime success as current activity");
    runtime.setTexture({true,true});a.draw();require(runtime.metrics().textureDraws==1,"on can arm an already tracked scene without a restart");
    a.current(sameWindowOtherDrawable,presenter);a.swap(sameWindowOtherDrawable);
    require(runtime.metrics().observed&&!runtime.metrics().textureDraws&&!runtime.metrics().textureInspected,
            "even the SAME presenter context/HWND must reset evidence when its actually presented drawable changes");
    a.current(target,scene);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(!runtime.metrics().textureInspected,"the former drawable loses alternate scene authorization after a new drawable is presented");
    a.current(target,presenter);a.swap(target);a.current(target,scene);a.draw();
    require(runtime.metrics().textureDraws==1,"an exact original drawable can be re-adopted only by a fresh owned swap");
    a.removeContext(presenter);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(!runtime.metrics().observed&&!runtime.metrics().textureDraws&&!runtime.metrics().textureInspected,"destroying the presenter revokes alternate scene authorization and its evidence");
    a.swap(target);a.draw();require(runtime.metrics().observed&&runtime.metrics().textureDraws==1,"the former scene can be adopted ONLY after its own qualified swap");
    a.removeSurface(target);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(!runtime.metrics().observed&&!runtime.metrics().textureDraws,"surface destruction revokes all associated scene contexts");
    a.removeContext(scene);a.removeContext(protectedScene);a.removeSurface(sameWindowOtherDrawable);a.removeSurface(secret);

    // Leave provider A current on excluded UI while provider B draws the game.
    const auto uiContext=a.context();a.current(uiTarget,uiContext);
    const auto second=b.context(),secondTarget=b.surface(game,2);b.current(secondTarget,second);b.swap(secondTarget);b.draw();
    require(runtime.metrics().observed&&runtime.metrics().textureDraws==1&&runtime.metrics().textureInspected==1,
            "independent late EGL provider draws are not stolen by an earlier UI provider or old game-source counters");
    // The actual user-enabled compatibility policy must reach the ORIGINALS
    // with private samplers despite depth test/write/attachment all absent.
    // Keep legacy cases above: this is an explicit strategy, not a weakened
    // ownership/protection route or a fabricated successful depth predicate.
    runtime.setTexture({true,true,TextureDrawPolicy::OwnedMipCompatibility});
    b.scene(1|2|8);unsigned compatibleVariants=0;
    for(unsigned kind=0;kind<glDrawKinds;++kind)for(unsigned alias=0;alias<glDrawAliases;++alias)if(glDrawNames[kind][alias]){
        b.draw(static_cast<GlDrawKind>(kind),alias);++compatibleVariants;
    }
    m=runtime.metrics();
    require(compatibleVariants==variants&&m.texturePolicy==TextureDrawPolicy::OwnedMipCompatibility
            &&m.textureInspected==variants&&m.textureSceneDraws==variants&&m.textureDraws==variants
            &&m.textureBindings==variants*2&&!m.textureDepthBypasses&&m.textureDepthReasons==decltype(m.textureDepthReasons){},
            "all 46 real detours apply depth-independent private mip sampling without inventing scene depth or extra frames");
    for(unsigned flags:{1u|2u|8u|16u,1u|2u|8u|32u,1u|2u|8u|64u}){b.scene(flags);b.draw(GlDrawKind::ElementsBaseVertex,0,0);}
    require(runtime.metrics().textureDraws==variants,"compatibility still refuses blended, incomplete-mip and protected resources");
    inspected=runtime.metrics().textureInspected;
    b.function<void(*)(int)>("bsiFixtureExtensions")(1);
    const auto incompatibleDrawable=b.surface(game),compatSecret=b.surface(game,0,true),compatProtected=b.context(true);
    for(auto *unowned:{incompatibleDrawable,compatSecret,static_cast<void*>(nullptr)}){
        b.current(unowned,second);b.scene(1|2|8);b.draw(GlDrawKind::ElementsBaseVertex,0,0);
    }
    b.current(secondTarget,compatProtected);b.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(runtime.metrics().textureInspected==inspected,"compatibility does not adopt another drawable, unmapped/pbuffer or protected route");
    a.scene(1|2|8);a.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(runtime.metrics().textureInspected==inspected,"compatibility still excludes the earlier Qt/UI provider");
    b.current(secondTarget,second);b.scene(1|2|8);b.nestedDraw(3,2,2);
    require(runtime.metrics().textureInspected==inspected+1&&runtime.metrics().textureDraws==variants+1,
            "live compatibility bindings keep their recursion lease; a nested alias borrows them without double counting/preparation");
    runtime.setTexture({false,true,TextureDrawPolicy::OwnedMipCompatibility});b.draw(GlDrawKind::ElementsBaseVertex,0,0);
    require(!runtime.metrics().textureDraws&&!runtime.metrics().textureInspected,"compatibility Off restores native originals and clears evidence");
    b.removeContext(compatProtected);b.removeSurface(incompatibleDrawable);b.removeSurface(compatSecret);
    b.removeContext(second);b.removeSurface(secondTarget);a.removeContext(uiContext);a.removeSurface(uiTarget);
    require(!runtime.metrics().observed&&!runtime.metrics().textureDraws&&!a.count("bsiFixtureInvalidGl")&&!b.count("bsiFixtureInvalidGl"),"teardown and dispatch fixtures leave no stale presentation/texture success or invalid GL state");
    runtime.setTexture({});runtime.stop();DestroyWindow(ui);DestroyWindow(game);DestroyWindow(root);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    std::printf("PASS: production EGL/MinHook lifecycle plus %u draw signature/alias detours; exact arguments/once-only native draws, private samplers DURING base-vertex/multi/indirect/instanced draws, zero-binding wrapper -> nested scene versus live-override guards, precise first depth blockers, alternate context on the exact owned drawable, UI/protection/pbuffer/foreign-drawable refusal, reset and teardown\n",variants);
    std::puts("LIMIT: authored EGL/GL state doubles only, no GL driver/game pixels, protected GPU memory or physical GPU/player certification");
}
