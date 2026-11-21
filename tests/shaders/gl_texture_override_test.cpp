// Deterministic public-GL doubles around the ACTUAL production sampler code.
// Tests legal protection queries and every C++ allocation failure, not pixels,
// a real protected driver/context, Windows detours or game-resource semantics.
#include "GlTextureOverride.h"
#include "GlPipeline.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#ifdef _WIN32
#define GL_CALL __stdcall
#else
#define GL_CALL
#endif
namespace {
int failAfter=-1;
bool activeChanged=false,privateBound=false,failedAfterUnitChange=false,failedAfterBinding=false;
void *allocate(std::size_t size){
    if(failAfter==0){
        failAfter=-1;failedAfterUnitChange=activeChanged;failedAfterBinding=privateBound;
        throw std::bad_alloc();
    }
    if(failAfter>0)--failAfter;
    if(void *value=std::malloc(size?size:1))return value;
    throw std::bad_alloc();
}
void require(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
using U=unsigned;using I=int;using F=float;using B=unsigned char;
struct Sampler {
    I min=0x2703,mag=0x2601,s=0x812f,t=0x812f,r=0x812f,compare=0,function=0x0203;
    F low=-1000,high=1000;
};
struct Fake {
    bool protectionExtension=false,protectedContext=false,protectedTexture=false,incompleteExtensions=false;
    I extensionCount=0,illegalQueries=0,flagsQueries=0,textureProtectionQueries=0;
    I active=3,program=1,depthWrite=1,depthFunction=0x0203,depthBits=24;
    bool depth=true,blend=false;
    std::array<Sampler,256> samplers{};
    std::array<U,4> bindings{{1,2,0,0}},textures{{1,2,0,0}};
    std::array<bool,256> live{};
    U next=16;
    I creations=0,deletions=0;
    void reset(){*this=Fake{};activeChanged=privateBound=false;}
    void refresh(){activeChanged=active!=3;privateBound=bindings[0]>=16||bindings[1]>=16;}
} g;
const unsigned char *GL_CALL getString(U name){
    return reinterpret_cast<const unsigned char*>(name==0x1f02?"OpenGL ES 3.1 BSI protection fixture":"");
}
const unsigned char *GL_CALL getStringi(U,U){
    return g.incompleteExtensions?nullptr:reinterpret_cast<const unsigned char*>("GL_EXT_protected_textures");
}
void GL_CALL getInteger(U name,I *out){
    switch(name){
    case 0x821d:*out=g.extensionCount;break;
    case 0x821e:++g.flagsQueries;if(!g.protectionExtension)++g.illegalQueries;*out=g.protectedContext?0x10:0;break;
    case 0x8b4d:case 0x8cdf:*out=4;break;
    case 0x8b8d:*out=g.program;break;
    case 0x0b74:*out=g.depthFunction;break;
    case 0x84e0:*out=0x84c0+g.active;break;
    case 0x8069:*out=I(g.textures.at(std::size_t(g.active)));break;
    case 0x8919:*out=I(g.bindings.at(std::size_t(g.active)));break;
    case 0x8ca6:*out=0;break;
    default:require(false,"unexpected integer query in sampler fixture");
    }
}
void GL_CALL getBoolean(U name,B *out){require(name==0x0b72,"only inspect depth write mask");*out=B(g.depthWrite);}
B GL_CALL enabled(U name){return B(name==0x0b71?g.depth:name==0x0be2?g.blend:false);}
void GL_CALL active(U unit){g.active=I(unit-0x84c0);g.refresh();}
void GL_CALL program(U,U name,I *out){*out=name==0x8b86?2:128;}
const char *uniforms[]{"first_test_owned_color_texture","second_test_owned_color_texture"};
void GL_CALL uniform(U,U index,I,I *length,I *size,U *type,char *name){
    *length=I(std::strlen(uniforms[index]));*size=1;*type=0x8b5e;std::strcpy(name,uniforms[index]);
}
I GL_CALL location(U,const char *name){return std::strcmp(name,uniforms[0])==0?0:std::strcmp(name,uniforms[1])==0?1:-1;}
void GL_CALL uniformValue(U,I location,I *out){*out=location;}
void GL_CALL level(U,I level,U name,I *out){*out=name==0x1003?0x8058:level<=6?(64>>level):0;}
void samplerInteger(const Sampler &s,U name,I *out){
    switch(name){
    case 0x2801:*out=s.min;break;case 0x2800:*out=s.mag;break;
    case 0x2802:*out=s.s;break;case 0x2803:*out=s.t;break;case 0x8072:*out=s.r;break;
    case 0x884c:*out=s.compare;break;case 0x884d:*out=s.function;break;
    default:require(false,"unexpected sampler integer state");
    }
}
void GL_CALL textureInteger(U,U name,I *out){
    if(name==0x8bfa){
        ++g.textureProtectionQueries;if(!g.protectionExtension)++g.illegalQueries;
        *out=g.protectedTexture&&g.textures.at(std::size_t(g.active))==1;return;
    }
    if(name==0x813c){*out=0;return;}if(name==0x813d){*out=1000;return;}
    samplerInteger(Sampler{},name,out);
}
void GL_CALL samplerInt(U sampler,U name,I *out){samplerInteger(g.samplers.at(sampler),name,out);}
void GL_CALL samplerFloat(U sampler,U name,F *out){
    const auto &s=g.samplers.at(sampler);*out=name==0x813a?s.low:name==0x813b?s.high:1;
}
void GL_CALL textureFloat(U,U name,F *out){samplerFloat(0,name,out);}
void GL_CALL attachment(U target,U attachment,U name,I *out){
    require(target==0x8ca9&&attachment==0x1801,"inspect default depth attachment, not nonexistent color texture attachments");
    require(name==0x8cd0||name==0x8216,"only public default depth type/bit-size queries");
    *out=name==0x8cd0?(g.depthBits?0x8218:0):g.depthBits;
}
void GL_CALL gen(I count,U *out){
    for(I i=0;i<count;++i){require(g.next<g.samplers.size(),"private sampler fixture capacity");out[i]=g.next++;g.live[out[i]]=true;++g.creations;}
}
void GL_CALL remove(I count,const U *names){for(I i=0;i<count;++i){require(g.live.at(names[i]),"delete each private sampler exactly once");g.live[names[i]]=false;++g.deletions;}}
void GL_CALL parameterInt(U sampler,U name,I value){
    auto &s=g.samplers.at(sampler);
    switch(name){case 0x2801:s.min=value;break;case 0x2800:s.mag=value;break;case 0x2802:s.s=value;break;
    case 0x2803:s.t=value;break;case 0x8072:s.r=value;break;case 0x884c:s.compare=value;break;case 0x884d:s.function=value;break;
    default:require(false,"unexpected replacement sampler integer parameter");}
}
void GL_CALL parameterFloat(U sampler,U name,F value){
    if(name==0x813a)g.samplers.at(sampler).low=value;else if(name==0x813b)g.samplers.at(sampler).high=value;
    else require(false,"unexpected replacement sampler float parameter");
}
void GL_CALL parameterVector(U,U,const F*){require(false,"GLES border-clamp state must be bypassed, not cloned incorrectly");}
void GL_CALL bind(U unit,U sampler){g.bindings.at(unit)=sampler;g.refresh();}
void GL_CALL unexpected(){require(false,"protected GL pipeline must stop before allocating/compiling/drawing");}
void *resolve(const char *name,void*){
#define ENTRY(gl,fn) if(std::strcmp(name,"gl" #gl)==0)return reinterpret_cast<void*>(&fn)
    ENTRY(GetString,getString);ENTRY(GetStringi,getStringi);ENTRY(GetIntegerv,getInteger);ENTRY(GetBooleanv,getBoolean);
    ENTRY(IsEnabled,enabled);ENTRY(ActiveTexture,active);ENTRY(GetProgramiv,program);ENTRY(GetActiveUniform,uniform);
    ENTRY(GetUniformLocation,location);ENTRY(GetUniformiv,uniformValue);ENTRY(GetTexLevelParameteriv,level);
    ENTRY(GetTexParameteriv,textureInteger);ENTRY(GetTexParameterfv,textureFloat);ENTRY(GetSamplerParameteriv,samplerInt);
    ENTRY(GetSamplerParameterfv,samplerFloat);ENTRY(GetFramebufferAttachmentParameteriv,attachment);ENTRY(GenSamplers,gen);
    ENTRY(DeleteSamplers,remove);ENTRY(SamplerParameteri,parameterInt);ENTRY(SamplerParameterf,parameterFloat);
    ENTRY(SamplerParameterfv,parameterVector);ENTRY(BindSampler,bind);
#undef ENTRY
    // GlPipeline loads its complete API table before inspecting protection.
    // None of those other entry points may actually execute in rejection tests.
    return reinterpret_cast<void*>(&unexpected);
}
void unchanged(){require(g.active==3&&g.bindings==std::array<U,4>{{1,2,0,0}},"restore ACTIVE_TEXTURE and every original sampler BEFORE returning/throwing");}
}
void *operator new(std::size_t size){return allocate(size);}
void *operator new[](std::size_t size){return allocate(size);}
void operator delete(void *ptr)noexcept{std::free(ptr);}
void operator delete[](void *ptr)noexcept{std::free(ptr);}
#if defined(__cpp_sized_deallocation)
void operator delete(void *ptr,std::size_t)noexcept{std::free(ptr);}
void operator delete[](void *ptr,std::size_t)noexcept{std::free(ptr);}
#endif
int main(){
    using namespace BsiShaders;
    {
        g.reset();GlTextureOverride quality;std::string error;
        require(quality.initialize(resolve,nullptr,&error),"unprotected GLES with no protected-texture extension is supported without illegal enum queries");
        TextureDrawReason reason=TextureDrawReason::NotInspected;
        require(quality.begin(TextureQuality::Strong,true,4,&error,&reason)==2&&reason==TextureDrawReason::Prepared,"two ordinary test-owned mipmapped colors qualify with an explicit prepared outcome");
        require(g.active==3&&g.bindings[0]>=16&&g.bindings[1]>=16,"private bindings persist only until end, active unit restores before draw");
        require(g.flagsQueries==0&&g.textureProtectionQueries==0&&g.illegalQueries==0,"never query extension-only enums when extension is absent");
        quality.end();unchanged();quality.release();require(g.creations==g.deletions,"release all owned sampler names");
    }
    {
        g.reset();GlTextureOverride quality;std::string error;
        require(!quality.initialize(resolve,nullptr,&error,true),
                "EGL protected resources possible without a GL per-texture query is UNKNOWN, never permission to modify them");
        unchanged();require(g.creations==0&&g.illegalQueries==0,"unknown protection fails closed without illegal extension queries");
    }
    {
        g.reset();g.protectionExtension=true;g.extensionCount=1;g.protectedTexture=true;
        GlTextureOverride quality;std::string error;require(quality.initialize(resolve,nullptr,&error),"regular protected-capable context can contain mixed protected/nonprotected resources");
        require(quality.begin(TextureQuality::Strong,true,4,&error)==1&&g.bindings[0]==1&&g.bindings[1]>=16,
                "a protected texture stays native EVEN IN AN UNPROTECTED context; only the other resource qualifies");
        require(g.flagsQueries==1&&g.textureProtectionQueries==2&&g.illegalQueries==0,"extension-aware context AND per-texture protection inspection");
        quality.end();unchanged();quality.release();
    }
    for(bool incomplete:{false,true}){
        g.reset();g.protectionExtension=true;g.extensionCount=1;g.protectedContext=!incomplete;g.incompleteExtensions=incomplete;
        GlTextureOverride quality;GlPipeline pipeline;std::string error;
        require(!quality.initialize(resolve,nullptr,&error)&&!pipeline.initialize(resolve,nullptr,&error),
                "protected contexts and incomplete protection inspection reject BOTH scene sampling and presentation grading");
        unchanged();require(g.creations==0&&g.illegalQueries==0,"protected refusal makes no resources and no illegal queries");
    }
    {
        g.reset();g.samplers[1].s=0x812d;GlTextureOverride quality;std::string error;
        require(quality.initialize(resolve,nullptr,&error)&&quality.begin(TextureQuality::Strong,true,4,&error)==1&&g.bindings[0]==1,
                "extension-only GLES border state is preserved by bypass, not silently replaced with default borders");
        quality.end();quality.release();unchanged();
    }
    for(bool noDepth:{false,true}){
        g.reset();if(noDepth)g.depthBits=0;else g.depthFunction=0x0207;
        GlTextureOverride quality;std::string error;
        TextureDrawReason reason=TextureDrawReason::NotInspected;
        require(quality.initialize(resolve,nullptr,&error)&&quality.begin(TextureQuality::Strong,true,4,&error,&reason)==0&&reason==TextureDrawReason::Depth,
                "an enabled depth-test bit alone cannot qualify an always-pass UI/no-depth-buffer draw; diagnose the depth bypass");
        unchanged();quality.release();
    }
    {
        g.reset();GlTextureOverride quality;std::string error;TextureDrawReason reason;
        require(quality.initialize(resolve,nullptr,&error),"initialize explicit inspection outcomes");
        require(!quality.begin(TextureQuality::Native,true,4,&error,&reason)&&reason==TextureDrawReason::NotInspected,"native/off does not pretend to inspect a game call");
        require(!quality.begin(TextureQuality::Strong,false,4,&error,&reason)&&reason==TextureDrawReason::Unreviewed,"unreviewed driver state remains a refusal");
        require(!quality.begin(TextureQuality::Strong,true,1,&error,&reason)&&reason==TextureDrawReason::Primitive,"non-triangle outcome is not stale prior scene evidence");
        g.blend=true;require(!quality.begin(TextureQuality::Strong,true,4,&error,&reason)&&reason==TextureDrawReason::Blended,"blend bypass is separate from depth/UI qualification");
        g.blend=false;g.program=0;
        require(!quality.begin(TextureQuality::Strong,true,4,&error,&reason)&&reason==TextureDrawReason::NoSampler&&!error.empty(),"a scene without an inspectable program cannot inherit a previous preparation detail");
        unchanged();quality.release();
    }
    unsigned failures=0;bool unitFailure=false,bindingFailure=false,completed=false;
    for(int allocation=0;allocation<100&&!completed;++allocation){
        g.reset();GlTextureOverride quality;std::string detail;
        require(quality.initialize(resolve,nullptr,&detail),"initialize before targeted begin-only allocation failures");
        failedAfterUnitChange=failedAfterBinding=false;failAfter=allocation;
        try {
            require(quality.begin(TextureQuality::Strong,true,4,&detail)==2,"uninterrupted inspection prepares two replacements");
            failAfter=-1;quality.end();completed=true;
        }catch(const std::bad_alloc&){
            failAfter=-1;++failures;unitFailure=unitFailure||failedAfterUnitChange;bindingFailure=bindingFailure||failedAfterBinding;
            unchanged(); // This is BEFORE end(): a failed draw must already be native.
        }
        failAfter=-1;quality.end();unchanged();quality.release();
        require(g.creations==g.deletions,"allocation failure cannot leak a newly generated sampler while inserting its cache entry");
    }
    require(completed&&failures>=4&&unitFailure&&bindingFailure,"inject every inspection allocation, including after unit change AND after a prior sampler replacement");
    std::printf("PASS: production GL sampler protection/extension/border guards and %u injected allocation failures; active-unit/partial-binding rollback before the game draw and no sampler-name leaks\n",failures);
    std::puts("LIMIT: public-API doubles, not a protected driver, game-resource semantic proof, Windows interception or GPU certification");
}
