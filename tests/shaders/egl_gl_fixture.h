#pragma once
// Authored public-GL dispatch/state double for REAL Windows draw detours.
// No GL driver, pixels, command-buffer memory or protected GPU memory executes.
#include "GlDrawCalls.h"
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace EglDrawFixture {
using U=unsigned;using I=int;using F=float;using B=unsigned char;
struct Sampler {
    I min=0x2703,mag=0x2601,s=0x812f,t=0x812f,r=0x812f,compare=0,function=0x0203;
    F low=-1000,high=1000;
};
struct State {
    std::array<Sampler,128> samplers{};
    std::array<U,4> bindings{{1,2,0,0}},textures{{1,2,0,0}};
    I active=3,depthFunction=0x0203,depthBits=24;
    bool depth=true,depthWrite=true,blend=false,mips=true,protectedTexture=false;
    U next=16;
};
State &state();
bool protectedContext();
inline volatile LONG draws=0,privateDraws=0,invalid=0;
inline std::uint64_t expectedHash=0,lastHash=0;
inline U lastPrivateBindings=0;
inline void bad(){InterlockedIncrement(&invalid);}
inline const unsigned char *WINAPI getString(U name){return reinterpret_cast<const unsigned char*>(name==0x1f02?"OpenGL ES 3.1 BSI dispatch double":"");}
inline const unsigned char *WINAPI getStringi(U,U){return reinterpret_cast<const unsigned char*>("GL_EXT_protected_textures");}
inline void WINAPI getInteger(U name,I *out){
    auto &g=state();
    switch(name){
    case 0x821d:*out=1;break;case 0x821e:*out=protectedContext()?0x10:0;break;
    case 0x8b4d:case 0x8cdf:*out=4;break;case 0x8b8d:*out=1;break;
    case 0x0b74:*out=g.depthFunction;break;case 0x84e0:*out=0x84c0+g.active;break;
    case 0x8069:*out=I(g.textures.at(std::size_t(g.active)));break;
    case 0x8919:*out=I(g.bindings.at(std::size_t(g.active)));break;
    case 0x8ca6:*out=0;break;default:*out=0;bad();break;
    }
}
inline void WINAPI getBoolean(U name,B *out){if(name!=0x0b72)bad();*out=B(state().depthWrite);}
inline B WINAPI enabled(U name){return B(name==0x0b71?state().depth:name==0x0be2?state().blend:false);}
inline void WINAPI active(U unit){state().active=I(unit-0x84c0);}
inline void WINAPI program(U,U name,I *out){*out=name==0x8b86?2:128;}
inline const char *const uniforms[]{"test_owned_first_color","test_owned_second_color"};
inline void WINAPI uniform(U,U index,I,I *length,I *size,U *type,char *name){*length=I(std::strlen(uniforms[index]));*size=1;*type=0x8b5e;std::strcpy(name,uniforms[index]);}
inline I WINAPI location(U,const char *name){return std::strcmp(name,uniforms[0])==0?0:std::strcmp(name,uniforms[1])==0?1:-1;}
inline void WINAPI uniformValue(U,I slot,I *out){*out=slot;}
inline void WINAPI level(U,I mip,U name,I *out){*out=name==0x1003?0x8058:(!state().mips&&mip>0)?0:mip<=6?(64>>mip):0;}
inline void samplerInteger(const Sampler &s,U name,I *out){
    switch(name){
    case 0x2801:*out=s.min;break;case 0x2800:*out=s.mag;break;case 0x2802:*out=s.s;break;
    case 0x2803:*out=s.t;break;case 0x8072:*out=s.r;break;case 0x884c:*out=s.compare;break;
    case 0x884d:*out=s.function;break;default:*out=0;bad();break;
    }
}
inline void WINAPI textureInteger(U,U name,I *out){
    if(name==0x8bfa){*out=state().protectedTexture;return;}
    if(name==0x813c){*out=0;return;}if(name==0x813d){*out=1000;return;}
    samplerInteger(Sampler{},name,out);
}
inline void WINAPI samplerInt(U sampler,U name,I *out){samplerInteger(state().samplers.at(sampler),name,out);}
inline void WINAPI samplerFloat(U sampler,U name,F *out){const auto &s=state().samplers.at(sampler);*out=name==0x813a?s.low:name==0x813b?s.high:1;}
inline void WINAPI textureFloat(U,U name,F *out){samplerFloat(0,name,out);}
inline void WINAPI attachment(U target,U slot,U name,I *out){
    if(target!=0x8ca9||slot!=0x1801||(name!=0x8cd0&&name!=0x8216))bad();
    *out=name==0x8cd0?(state().depthBits?0x8218:0):state().depthBits;
}
inline void WINAPI gen(I count,U *out){for(I i=0;i<count;++i){auto &g=state();if(g.next>=g.samplers.size()){bad();out[i]=0;}else out[i]=g.next++;}}
inline void WINAPI remove(I,const U*){}
inline void WINAPI parameterInt(U sampler,U name,I value){
    auto &s=state().samplers.at(sampler);
    switch(name){case 0x2801:s.min=value;break;case 0x2800:s.mag=value;break;case 0x2802:s.s=value;break;
    case 0x2803:s.t=value;break;case 0x8072:s.r=value;break;case 0x884c:s.compare=value;break;case 0x884d:s.function=value;break;default:bad();break;}
}
inline void WINAPI parameterFloat(U sampler,U name,F value){if(name==0x813a)state().samplers.at(sampler).low=value;else if(name==0x813b)state().samplers.at(sampler).high=value;else bad();}
inline void WINAPI parameterVector(U,U,const F*){bad();}
inline void WINAPI bind(U unit,U sampler){state().bindings.at(unit)=sampler;}

template<typename T>std::uint64_t valueHash(T value){
    if constexpr(std::is_pointer_v<T>)return std::uint64_t(reinterpret_cast<std::uintptr_t>(value));
    else return std::uint64_t(value);
}
template<typename... Args>std::uint64_t hash(U kind,U alias,Args... args){
    std::uint64_t result=kind*17u+alias;
    ((result=(result^valueHash(args))*1099511628211ull),...);return result;
}
template<typename... Args>void record(U kind,U alias,Args... args){
    InterlockedIncrement(&draws);lastHash=hash(kind,alias,args...);lastPrivateBindings=0;
    const auto &g=state();if(g.active!=3)bad();
    for(U unit=0;unit<2;++unit)if(g.bindings[unit]>=16){
        ++lastPrivateBindings;if(g.samplers.at(g.bindings[unit]).low!=3)bad();
    }
    if(lastPrivateBindings)InterlockedIncrement(&privateDraws);
}
#define BSI_FIXTURE_DRAW(id,params,args,...) \
    template<U Alias> void WINAPI draw##id params { \
        const auto native=[&](auto... values){record(static_cast<U>(BsiShaders::GlDrawKind::id),Alias,values...);}; \
        native args; \
    } \
    template<std::size_t... Index> std::array<void*,sizeof...(Index)> callbacks##id(std::index_sequence<Index...>){return {{reinterpret_cast<void*>(&draw##id<Index>)...}};}
BSI_GL_DRAW_CALLS(BSI_FIXTURE_DRAW)
#undef BSI_FIXTURE_DRAW
inline const std::array<std::array<void*,BsiShaders::glDrawAliases>,BsiShaders::glDrawKinds> callbacks{{
#define BSI_FIXTURE_CALLBACKS(id,params,args,...) callbacks##id(std::make_index_sequence<BsiShaders::glDrawAliases>{}),
    BSI_GL_DRAW_CALLS(BSI_FIXTURE_CALLBACKS)
#undef BSI_FIXTURE_CALLBACKS
}};
inline void *resolve(const char *name){
#define ENTRY(gl,fn) if(std::strcmp(name,"gl" #gl)==0)return reinterpret_cast<void*>(&fn)
    ENTRY(GetString,getString);ENTRY(GetStringi,getStringi);ENTRY(GetIntegerv,getInteger);ENTRY(GetBooleanv,getBoolean);
    ENTRY(IsEnabled,enabled);ENTRY(ActiveTexture,active);ENTRY(GetProgramiv,program);ENTRY(GetActiveUniform,uniform);
    ENTRY(GetUniformLocation,location);ENTRY(GetUniformiv,uniformValue);ENTRY(GetTexLevelParameteriv,level);
    ENTRY(GetTexParameteriv,textureInteger);ENTRY(GetTexParameterfv,textureFloat);ENTRY(GetSamplerParameteriv,samplerInt);
    ENTRY(GetSamplerParameterfv,samplerFloat);ENTRY(GetFramebufferAttachmentParameteriv,attachment);ENTRY(GenSamplers,gen);
    ENTRY(DeleteSamplers,remove);ENTRY(SamplerParameteri,parameterInt);ENTRY(SamplerParameterf,parameterFloat);
    ENTRY(SamplerParameterfv,parameterVector);ENTRY(BindSampler,bind);
#undef ENTRY
    for(U kind=0;kind<BsiShaders::glDrawKinds;++kind)for(U alias=0;alias<BsiShaders::glDrawAliases;++alias)
        if(BsiShaders::glDrawNames[kind][alias]&&std::strcmp(name,BsiShaders::glDrawNames[kind][alias])==0)return callbacks[kind][alias];
    return nullptr;
}
inline bool restored(){const auto &g=state();return g.active==3&&g.bindings==std::array<U,4>{{1,2,0,0}}&&g.samplers[1].low==-1000&&g.samplers[2].low==-1000;}
inline void invoke(U kind,U alias,U mode){
    // Deliberately distinctive offsets/counts/signed base vertices and a >32-bit
    // indirect-count offset. None of these sentinel addresses may be dereferenced.
    const int first=7,count=13,instances=3,baseVertex=-11,drawCount=2,maxDrawCount=5,stride=28;
    const U type=0x1403,start=17,end=71,baseInstance=29;
    const std::intptr_t countOffset=std::intptr_t(0x123456789ull);
    const void *indices=reinterpret_cast<void*>(0x123400),*indirect=reinterpret_cast<void*>(0x234500);
    const int firsts[]{3,7},counts[]{5,11},baseVertices[]{-3,13};
    const void *const offsets[]{reinterpret_cast<void*>(0x345600),reinterpret_cast<void*>(0x456700)};
    const auto original=[&](auto... values){
        expectedHash=hash(kind,alias,values...);
        using Function=void(WINAPI*)(decltype(values)...);
        reinterpret_cast<Function>(callbacks[kind][alias])(values...);
    };
    switch(static_cast<BsiShaders::GlDrawKind>(kind)){
#define BSI_FIXTURE_INVOKE(id,params,args,...) case BsiShaders::GlDrawKind::id:original args;break;
        BSI_GL_DRAW_CALLS(BSI_FIXTURE_INVOKE)
#undef BSI_FIXTURE_INVOKE
    default:bad();break;
    }
}
#undef BSI_GL_DRAW_CALLS
} // namespace EglDrawFixture
