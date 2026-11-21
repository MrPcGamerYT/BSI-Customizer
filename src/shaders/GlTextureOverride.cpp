#include "GlTextureOverride.h"
#include "GlTextureQuality.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#define BSI_GL_CALL __stdcall
#else
#define BSI_GL_CALL
#endif

namespace BsiShaders {
namespace {
using U=unsigned;using I=int;using F=float;using B=unsigned char;
constexpr U texture2d=0x0de1,texture0=0x84c0;
struct Sampler {
    I min=0,mag=0,s=0,t=0,r=0,compare=0,function=0,decode=0,reduction=0;
    F lo=0,hi=0,bias=0,anisotropy=1;
    std::array<F,4> border{};
    std::array<F,17> key()const{return {F(min),F(mag),F(s),F(t),F(r),F(compare),F(function),F(decode),F(reduction),lo,hi,bias,anisotropy,border[0],border[1],border[2],border[3]};}
};
bool colorFormat(I f){
    // Normalized RGB/RGBA only (including common game compression formats).
    // Never depth/stencil/integer/HDR/data-only single-channel resources.
    return f==0x1907||f==0x1908||f==0x8051||f==0x8058||f==0x8c41||f==0x8c43||f==0x8056||f==0x8d62
        ||(f>=0x83f0&&f<=0x83f3)||(f>=0x8c4c&&f<=0x8c4f)||f==0x8d64
        ||(f>=0x9274&&f<=0x9279)||(f>=0x93b0&&f<=0x93bd)||(f>=0x93d0&&f<=0x93dd)||f==0x8e8c||f==0x8e8d;
}
}
struct GlTextureOverride::Impl {
#define GL_FUNCTION(result,name,args) using name##Type=result(BSI_GL_CALL*)args;name##Type name=nullptr;
    GL_FUNCTION(const unsigned char*,GetString,(U))
    GL_FUNCTION(const unsigned char*,GetStringi,(U,U))
    GL_FUNCTION(void,GetIntegerv,(U,I*))
    GL_FUNCTION(void,GetBooleanv,(U,B*))
    GL_FUNCTION(B,IsEnabled,(U))
    GL_FUNCTION(void,ActiveTexture,(U))
    GL_FUNCTION(void,GetProgramiv,(U,U,I*))
    GL_FUNCTION(void,GetActiveUniform,(U,U,I,I*,I*,U*,char*))
    GL_FUNCTION(I,GetUniformLocation,(U,const char*))
    GL_FUNCTION(void,GetUniformiv,(U,I,I*))
    GL_FUNCTION(void,GetTexLevelParameteriv,(U,I,U,I*))
    GL_FUNCTION(void,GetTexParameteriv,(U,U,I*))
    GL_FUNCTION(void,GetTexParameterfv,(U,U,F*))
    GL_FUNCTION(void,GetSamplerParameteriv,(U,U,I*))
    GL_FUNCTION(void,GetSamplerParameterfv,(U,U,F*))
    GL_FUNCTION(void,GetFramebufferAttachmentParameteriv,(U,U,U,I*))
    GL_FUNCTION(void,GenSamplers,(I,U*))
    GL_FUNCTION(void,DeleteSamplers,(I,const U*))
    GL_FUNCTION(void,SamplerParameteri,(U,U,I))
    GL_FUNCTION(void,SamplerParameterf,(U,U,F))
    GL_FUNCTION(void,SamplerParameterfv,(U,U,const F*))
    GL_FUNCTION(void,BindSampler,(U,U))
#undef GL_FUNCTION
    bool ready=false,es=false,anisotropic=false,srgbDecode=false,minmax=false,protectedTextures=false;
    I units=0,attachments=0,extensionCount=0;
    std::map<std::array<F,17>,U> cache;
    struct Binding {U unit,original;};
    std::vector<Binding> changed;
    bool extension(const char *needle){
        for(I i=0;i<extensionCount;++i){const auto *text=GetStringi(0x1f03,U(i));if(text&&std::strcmp(reinterpret_cast<const char*>(text),needle)==0)return true;}
        return false;
    }
    struct Inspection {
        Impl &g; I active=0; bool keepBindings=false;
        explicit Inspection(Impl &api):g(api){g.GetIntegerv(0x84e0,&active);}
        ~Inspection(){
            // Allocations (uniform names/cache/detail) may fail after selecting
            // a unit or even binding one replacement. Fail BEFORE the game's
            // original draw, not merely in the hook's after-draw destructor.
            if(!keepBindings)g.restoreBindings();
            g.ActiveTexture(U(active));
        }
    };
    void restoreBindings()noexcept{
        for(const auto &binding:changed)BindSampler(binding.unit,binding.original);
        changed.clear();
    }
    void integer(U sampler,U key,I *out){if(sampler)GetSamplerParameteriv(sampler,key,out);else GetTexParameteriv(texture2d,key,out);}
    void floating(U sampler,U key,F *out){if(sampler)GetSamplerParameterfv(sampler,key,out);else GetTexParameterfv(texture2d,key,out);}
    Sampler readSampler(U sampler){
        Sampler v;
        integer(sampler,0x2801,&v.min);integer(sampler,0x2800,&v.mag);
        integer(sampler,0x2802,&v.s);integer(sampler,0x2803,&v.t);integer(sampler,0x8072,&v.r);
        integer(sampler,0x884c,&v.compare);integer(sampler,0x884d,&v.function);
        floating(sampler,0x813a,&v.lo);floating(sampler,0x813b,&v.hi);
        if(!es){floating(sampler,0x8501,&v.bias);floating(sampler,0x1004,v.border.data());}
        if(anisotropic)floating(sampler,0x84fe,&v.anisotropy);
        if(srgbDecode)integer(sampler,0x8a48,&v.decode);
        if(minmax)integer(sampler,0x9366,&v.reduction);
        return v;
    }
    U privateSampler(const Sampler &v){
        const auto key=v.key();
        for(const auto value:key)if(!std::isfinite(value))return 0;
        const auto found=cache.find(key);if(found!=cache.end())return found->second;
        if(cache.size()>=128)return 0;
        U sampler=0;GenSamplers(1,&sampler);if(!sampler)return 0;
        struct PendingSampler {
            Impl &g;U name;
            ~PendingSampler(){if(name)g.DeleteSamplers(1,&name);}
        } pending{*this,sampler};
        SamplerParameteri(sampler,0x2801,v.min);SamplerParameteri(sampler,0x2800,v.mag);
        SamplerParameteri(sampler,0x2802,v.s);SamplerParameteri(sampler,0x2803,v.t);SamplerParameteri(sampler,0x8072,v.r);
        SamplerParameteri(sampler,0x884c,v.compare);SamplerParameteri(sampler,0x884d,v.function);
        SamplerParameterf(sampler,0x813a,v.lo);SamplerParameterf(sampler,0x813b,v.hi);
        if(!es){SamplerParameterf(sampler,0x8501,v.bias);SamplerParameterfv(sampler,0x1004,v.border.data());}
        if(anisotropic)SamplerParameterf(sampler,0x84fe,v.anisotropy);
        if(srgbDecode)SamplerParameteri(sampler,0x8a48,v.decode);
        if(minmax)SamplerParameteri(sampler,0x9366,v.reduction);
        // Never count/bind an unverified replacement (and never consume the
        // application's GL error queue to detect our own failures).
        const auto actual=readSampler(sampler);
        if(actual.key()!=key)return 0;
        cache.emplace(key,sampler);pending.name=0;return sampler;
    }
    bool attached(U texture){
        I framebuffer=0;GetIntegerv(0x8ca6,&framebuffer); // DRAW_FRAMEBUFFER_BINDING
        if(!framebuffer)return false;
        const auto match=[&](U attachment){
            I type=0,name=0;GetFramebufferAttachmentParameteriv(0x8ca9,attachment,0x8cd0,&type);
            if(type==0x1702)GetFramebufferAttachmentParameteriv(0x8ca9,attachment,0x8cd1,&name);
            return type==0x1702&&U(name)==texture;
        };
        for(I i=0;i<attachments;++i)if(match(0x8ce0+U(i)))return true;
        return match(0x8d00)||match(0x8d20);
    }
    bool sceneDepth(){
        B write=0;GetBooleanv(0x0b72,&write);
        I function=0;GetIntegerv(0x0b74,&function);
        if(!IsEnabled(0x0b71)||!write||function==0x0200||function==0x0207)return false;
        I framebuffer=0,type=0,bits=0;GetIntegerv(0x8ca6,&framebuffer);
        const U attachment=framebuffer?0x8d00:0x1801; // FBO depth vs default GL_DEPTH
        GetFramebufferAttachmentParameteriv(0x8ca9,attachment,0x8cd0,&type);
        if(!type)return false;
        // GL_DEPTH_BITS was removed from desktop core: use the legal GL3/ES3
        // attachment query instead, including for the default framebuffer.
        GetFramebufferAttachmentParameteriv(0x8ca9,attachment,0x8216,&bits);
        return bits>=16;
    }
    I mipLevels(){
        if(protectedTextures){I protectedContent=1;GetTexParameteriv(texture2d,0x8bfa,&protectedContent);if(protectedContent)return 0;}
        I base=0,last=0;GetTexParameteriv(texture2d,0x813c,&base);GetTexParameteriv(texture2d,0x813d,&last);
        if(base<0||base>30||last<=base||last>1000)return 0;
        I width=0,height=0,format=0;
        GetTexLevelParameteriv(texture2d,base,0x1000,&width);GetTexLevelParameteriv(texture2d,base,0x1001,&height);
        GetTexLevelParameteriv(texture2d,base,0x1003,&format);
        if(width<64||height<64||width>16384||height>16384||!colorFormat(format))return 0;
        I levels=1;
        while((width>1||height>1)&&base+levels<=last&&levels<16){
            width=(std::max)(1,width/2);height=(std::max)(1,height/2);
            I w=0,h=0,f=0;GetTexLevelParameteriv(texture2d,base+levels,0x1000,&w);
            GetTexLevelParameteriv(texture2d,base+levels,0x1001,&h);GetTexLevelParameteriv(texture2d,base+levels,0x1003,&f);
            if(w!=width||h!=height||f!=format)return 0;
            ++levels;
        }
        return levels;
    }
};
GlTextureOverride::GlTextureOverride():impl(std::make_unique<Impl>()){}
GlTextureOverride::~GlTextureOverride()=default;
bool GlTextureOverride::initialize(ResolveGl resolve,void *user,std::string *error,bool externalProtectionPossible){
    auto &g=*impl;
    if(g.ready)return true;
    const auto fail=[&](const char *message){if(error)*error=message;return false;};
    if(!resolve)return fail("Potato needs a current, owned game GL context.");
#define LOAD(name) g.name=reinterpret_cast<Impl::name##Type>(resolve("gl" #name,user));if(!g.name)return fail("Potato sampler capture is missing gl" #name ". Original textures retained.")
    LOAD(GetString);LOAD(GetStringi);LOAD(GetIntegerv);LOAD(GetBooleanv);LOAD(IsEnabled);
    LOAD(ActiveTexture);LOAD(GetProgramiv);LOAD(GetActiveUniform);LOAD(GetUniformLocation);LOAD(GetUniformiv);
    LOAD(GetTexLevelParameteriv);LOAD(GetTexParameteriv);LOAD(GetTexParameterfv);
    LOAD(GetSamplerParameteriv);LOAD(GetSamplerParameterfv);LOAD(GetFramebufferAttachmentParameteriv);
    LOAD(GenSamplers);LOAD(DeleteSamplers);LOAD(SamplerParameteri);LOAD(SamplerParameterf);LOAD(SamplerParameterfv);LOAD(BindSampler);
#undef LOAD
    const auto *raw=g.GetString(0x1f02);if(!raw)return fail("GL version could not be identified for Potato.");
    const std::string version(reinterpret_cast<const char*>(raw));g.es=version.find("OpenGL ES")!=std::string::npos;
    int major=0,minor=0;const auto first=version.find_first_of("0123456789");
    if(first==std::string::npos||std::sscanf(version.c_str()+first,"%d.%d",&major,&minor)!=2
       ||major<3||(major==3&&minor<(g.es?1:3)))
        return fail("Potato requires GL 3.3 / GLES 3.1 with queryable texture levels and private sampler objects. This path is left untouched.");
    g.GetIntegerv(0x821d,&g.extensionCount);
    if(g.extensionCount<0||g.extensionCount>4096)return fail("GL extension/protection state could not be qualified; original textures retained.");
    for(I i=0;i<g.extensionCount;++i)if(!g.GetStringi(0x1f03,U(i)))return fail("GL extension/protection state is incomplete; original textures retained.");
    g.protectedTextures=g.extension("GL_EXT_protected_textures");
    if(externalProtectionPossible&&!g.protectedTextures)return fail("Protected EGL resources are possible but per-texture protection inspection is unavailable; native sampling retained.");
    if(g.protectedTextures){I flags=0;g.GetIntegerv(0x821e,&flags);if(flags&0x10)return fail("Protected GL contexts are never modified by Potato.");}
    g.GetIntegerv(0x8b4d,&g.units);g.GetIntegerv(0x8cdf,&g.attachments);
    if(g.units<1||g.units>256||g.attachments<1||g.attachments>32)return fail("Unqualified GL sampler/attachment limits; original textures retained.");
    g.anisotropic=g.extension("GL_EXT_texture_filter_anisotropic")||g.extension("GL_ARB_texture_filter_anisotropic");
    g.srgbDecode=g.extension("GL_EXT_texture_sRGB_decode");g.minmax=g.extension("GL_EXT_texture_filter_minmax");
    g.changed.reserve(32);g.ready=true;return true;
}
unsigned GlTextureOverride::begin(TextureQuality mode,bool reviewed,U primitive,std::string *detail){
    auto &g=*impl;end();
    const auto fail=[&](const char *message)->U{if(detail)*detail=message;return 0;};
    if(!g.ready||mode==TextureQuality::Native)return 0;
    if(!reviewed)return fail("Potato is not applied: review existing driver LOD/AA overrides first; no profile is reset.");
    if(primitive!=0x0004&&primitive!=0x0005&&primitive!=0x0006)return 0;
    if(!g.sceneDepth())
        return fail("Potato bypass: this is not a qualified depth-tested, depth-writing game scene draw (UI/always-pass/no-depth targets stay native).");
    // Blended UI/transparent passes remain native, even in the game context.
    if(g.IsEnabled(0x0be2))return fail("Potato bypass: blended/UI draw retains native sampling.");
    I program=0,count=0,maxName=0;g.GetIntegerv(0x8b8d,&program);if(!program)return 0;
    g.GetProgramiv(U(program),0x8b86,&count);g.GetProgramiv(U(program),0x8b87,&maxName);
    if(count<0||count>256||maxName<0||maxName>512)return fail("Potato bypass: shader uniform layout exceeds safe inspection bounds.");
    Impl::Inspection inspection(g);
    std::set<U> used;
    for(I i=0;i<count;++i){
        char name[512]{};I length=0,size=0;U type=0;
        g.GetActiveUniform(U(program),U(i),512,&length,&size,&type,name);
        if(type!=0x8b5e||size<1||size>32||length<1||length>=511)continue; // sampler2D ONLY
        std::string stem(name);
        if(size>1&&stem.size()>3&&stem.substr(stem.size()-3)=="[0]")stem.resize(stem.size()-3);
        for(I element=0;element<size;++element){
            const std::string uniform=size==1?std::string(name):stem+"["+std::to_string(element)+"]";
            const I location=g.GetUniformLocation(U(program),uniform.c_str());if(location<0)continue;
            I unit=-1;g.GetUniformiv(U(program),location,&unit);
            if(unit<0||unit>=g.units||unit>=32||!used.insert(U(unit)).second)continue;
            g.ActiveTexture(texture0+U(unit));I texture=0,original=0;
            g.GetIntegerv(0x8069,&texture);g.GetIntegerv(0x8919,&original);
            if(!texture||g.attached(U(texture)))continue;
            const I levels=g.mipLevels();if(levels<2)continue;
            Sampler sampler=g.readSampler(U(original));
            // GLES extension-only border/mirror-clamp state is not part of our
            // descriptor clone. Never silently replace it with default borders.
            const auto basicWrap=[](I wrap){return wrap==0x2901||wrap==0x8370||wrap==0x812f;};
            if(g.es&&(!basicWrap(sampler.s)||!basicWrap(sampler.t)||!basicWrap(sampler.r)))continue;
            if(g.minmax&&sampler.reduction!=0x9367)continue;
            TextureQualityProof proof;proof.renderer=g.es?Renderer::OpenGLES:Renderer::OpenGL;
            proof.use=TextureUse::SampledColor;proof.gameSurfaceIdentified=proof.resourceOwnershipProven=true;
            proof.privateReplacementSupported=true;proof.externalOverridesReviewed=reviewed;proof.visibleMipLevels=U(levels);
            GlQualitySampler source;source.minimumFilter=sampler.min;source.comparisonMode=sampler.compare;
            source.minimumLod=sampler.lo;source.maximumLod=sampler.hi;source.lodBias=sampler.bias;source.maximumAnisotropy=sampler.anisotropy;
            const auto plan=planGlTextureQuality(mode,proof,source);if(!plan.changed())continue;
            sampler.lo=plan.descriptor.minimumLod;const U replacement=g.privateSampler(sampler);if(!replacement)continue;
            g.changed.push_back({U(unit),U(original)});g.BindSampler(U(unit),replacement);
        }
    }
    if(detail)*detail=g.changed.empty()?"No qualified scene-color mip sampler in this game draw; native sampling retained.":"Potato prepared private coarser-mip samplers for an owned game scene draw; original bindings restore immediately afterward. This is not a measurement of game output or FPS.";
    inspection.keepBindings=true;
    return U(g.changed.size());
}
void GlTextureOverride::end()noexcept{
    // begin() restores ACTIVE_TEXTURE, including on an allocation failure.
    // This changes sampler bindings only, never application texture/program/FBO.
    impl->restoreBindings();
}
void GlTextureOverride::release(){
    auto &g=*impl;end();for(const auto &entry:g.cache)g.DeleteSamplers(1,&entry.second);g.cache.clear();g.ready=false;
}
} // namespace BsiShaders
