// Authored EGL lifecycle double, NOT an EGL/GL driver. No GPU calls execute.
// Two differently named DLLs exercise the production provider/HWND detours.
#include <windows.h>
#include <cstdint>
#include "egl_gl_fixture.h"

#define API extern "C" __declspec(dllexport)
namespace {
struct Surface { HWND window=nullptr;bool protectedContent=false; } surfaces[32];
struct Context { bool protectedContent=false;EglDrawFixture::State gl; } contexts[16];
unsigned surfaceCount=0,contextCount=0;
int displayToken=0;
thread_local Surface *currentSurface=nullptr;
thread_local Context *currentContext=nullptr;
volatile LONG calls=0,glQueries=0,protectionQueries=0,illegalQueries=0,housekeeping=0;
bool extensions=true;
template<typename Attribute>void *create(void *window,const Attribute *attrs){
    if(!IsWindow(reinterpret_cast<HWND>(window))||surfaceCount==32)return nullptr;
    auto &surface=surfaces[surfaceCount++];surface.window=reinterpret_cast<HWND>(window);
    if(attrs)for(unsigned i=0;i<128&&attrs[i]!=0x3038;i+=2)if(attrs[i]==0x32c0)surface.protectedContent=attrs[i+1]!=0;
    return &surface;
}
}
namespace EglDrawFixture {
State &state(){static State unbound;return currentContext?currentContext->gl:unbound;}
bool protectedContext(){return currentContext&&currentContext->protectedContent;}
}
API void *WINAPI eglGetCurrentContext(){return currentContext;}
API void *WINAPI eglGetCurrentDisplay(){return currentContext?&displayToken:nullptr;}
API void *WINAPI eglGetCurrentSurface(int){return currentSurface;}
API unsigned WINAPI eglQueryAPI(){return 0x30a0;} // actual GLES, irrespective of configuration hint
API const char *WINAPI eglQueryString(void*,int){return extensions?"EGL_EXT_protected_content EGL_EXT_protected_surface":"";}
API void *WINAPI eglCreateWindowSurface(void*,void*,void *window,const int *attrs){InterlockedExchangeAdd(&housekeeping,1);return create(window,attrs);}
API void *WINAPI eglCreatePlatformWindowSurface(void*,void*,void *window,const std::intptr_t *attrs){InterlockedExchangeAdd(&housekeeping,2);return create(window,attrs);}
API void *WINAPI eglCreatePlatformWindowSurfaceEXT(void*,void*,void *window,const int *attrs){InterlockedExchangeAdd(&housekeeping,3);return create(window,attrs);}
API unsigned WINAPI eglDestroySurface(void*,void *value){
    InterlockedExchangeAdd(&housekeeping,4);if(currentSurface==value)currentSurface=nullptr;return value!=nullptr;
}
API unsigned WINAPI eglDestroyContext(void*,void *value){
    InterlockedExchangeAdd(&housekeeping,5);if(currentContext==value){currentContext=nullptr;currentSurface=nullptr;}return value!=nullptr;
}
API unsigned WINAPI eglQuerySurface(void*,void *value,int name,int *out){
    if(!value||!out)return 0;const auto &surface=*static_cast<Surface*>(value);
    if(name==0x32c0){InterlockedIncrement(&protectionQueries);if(!extensions)InterlockedIncrement(&illegalQueries);*out=surface.protectedContent;return 1;}
    if(name==0x3057){*out=640;return 1;}if(name==0x3056){*out=360;return 1;}return 0;
}
API unsigned WINAPI eglQueryContext(void*,void *value,int name,int *out){
    if(!value||!out||name!=0x32c0)return 0;InterlockedIncrement(&protectionQueries);
    if(!extensions)InterlockedIncrement(&illegalQueries);*out=static_cast<Context*>(value)->protectedContent;return 1;
}
API void *WINAPI eglGetProcAddress(const char *name){InterlockedIncrement(&glQueries);return EglDrawFixture::resolve(name);}
API unsigned WINAPI eglSwapBuffers(void*,void*){InterlockedIncrement(&calls);return 1;}
API unsigned WINAPI eglMakeCurrent(void*,void *draw,void*,void *context){
    currentSurface=static_cast<Surface*>(draw);currentContext=static_cast<Context*>(context);return 1;
}
API void *bsiFixtureDisplay(){return &displayToken;}
API void *bsiFixtureContext(int protectedContent){if(contextCount==16)return nullptr;auto &context=contexts[contextCount++];context.protectedContent=protectedContent!=0;return &context;}
API LONG bsiFixtureSwapCalls(){return calls;}
API LONG bsiFixtureGlQueries(){return glQueries;}
API LONG bsiFixtureProtectionQueries(){return protectionQueries;}
API LONG bsiFixtureIllegalQueries(){return illegalQueries;}
API void bsiFixtureExtensions(int available){extensions=available!=0;InterlockedExchange(&protectionQueries,0);}
API void bsiFixtureDraw(unsigned kind,unsigned alias,unsigned mode){EglDrawFixture::invoke(kind,alias,mode);}
API void bsiFixtureNestedDraw(unsigned mode){
    using namespace EglDrawFixture;
    nestedMode=mode;nestedPrivateBindings=0;nestedArgumentsIntact=true;
    invoke(static_cast<U>(BsiShaders::GlDrawKind::Arrays),0,4);nestedMode=0;
}
API LONG bsiFixtureNestedPrivateBindings(){return LONG(EglDrawFixture::nestedPrivateBindings);}
API LONG bsiFixtureDrawCalls(){return EglDrawFixture::draws;}
API LONG bsiFixturePrivateDraws(){return EglDrawFixture::privateDraws;}
API LONG bsiFixtureInvalidGl(){return EglDrawFixture::invalid;}
API LONG bsiFixtureLastPrivateBindings(){return LONG(EglDrawFixture::lastPrivateBindings);}
API LONG bsiFixtureArgumentsIntact(){return EglDrawFixture::nestedArgumentsIntact&&EglDrawFixture::lastHash==EglDrawFixture::expectedHash;}
API LONG bsiFixtureBindingsRestored(){return EglDrawFixture::restored();}
API void bsiFixtureSceneState(unsigned flags){
    auto &g=EglDrawFixture::state();g.depth=!(flags&1);g.depthWrite=!(flags&2);
    g.depthFunction=flags&4?0x0207:0x0203;g.depthBits=flags&8?0:24;
    g.blend=(flags&16)!=0;g.mips=!(flags&32);g.protectedTexture=(flags&64)!=0;
}
