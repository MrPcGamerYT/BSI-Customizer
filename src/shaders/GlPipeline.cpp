#include "GlPipeline.h"
#include "ShaderSources.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <utility>

#ifdef _WIN32
#define BSI_GL_CALL __stdcall
#else
#define BSI_GL_CALL
#endif
namespace BsiShaders {
namespace {
using Enum = unsigned; using UInt = unsigned; using Int = int; using Bool = unsigned char;
using Size = std::intptr_t;
constexpr Enum Texture0=0x84c0, Texture2d=0x0de1, ArrayBuffer=0x8892;
constexpr Enum Blend=0x0be2, Depth=0x0b71, Stencil=0x0b90, Cull=0x0b44, Scissor=0x0c11, Dither=0x0bd0;
constexpr Enum Srgb=0x8db9, Discard=0x8c89;
#define GL_FUNCTIONS(X) \
 X(const unsigned char*,GetString,(Enum)) X(void,GetIntegerv,(Enum,Int*)) \
 X(void,GetBooleanv,(Enum,Bool*)) X(Bool,IsEnabled,(Enum)) \
 X(void,Enable,(Enum)) X(void,Disable,(Enum)) X(void,Viewport,(Int,Int,Int,Int)) \
 X(void,ColorMask,(Bool,Bool,Bool,Bool)) X(void,ActiveTexture,(Enum)) \
 X(void,GenTextures,(Int,UInt*)) X(void,DeleteTextures,(Int,const UInt*)) \
 X(void,BindTexture,(Enum,UInt)) X(void,TexParameteri,(Enum,Enum,Int)) \
 X(void,TexImage2D,(Enum,Int,Int,Int,Int,Int,Enum,Enum,const void*)) \
 X(void,CopyTexSubImage2D,(Enum,Int,Int,Int,Int,Int,Int,Int)) \
 X(UInt,CreateShader,(Enum)) X(void,ShaderSource,(UInt,Int,const char*const*,const Int*)) \
 X(void,CompileShader,(UInt)) X(void,GetShaderiv,(UInt,Enum,Int*)) \
 X(void,GetShaderInfoLog,(UInt,Int,Int*,char*)) X(void,DeleteShader,(UInt)) \
 X(UInt,CreateProgram,()) X(void,AttachShader,(UInt,UInt)) X(void,BindAttribLocation,(UInt,UInt,const char*)) \
 X(void,LinkProgram,(UInt)) X(void,GetProgramiv,(UInt,Enum,Int*)) \
 X(void,GetProgramInfoLog,(UInt,Int,Int*,char*)) X(void,DeleteProgram,(UInt)) X(void,UseProgram,(UInt)) \
 X(Int,GetUniformLocation,(UInt,const char*)) X(void,Uniform1i,(Int,Int)) \
 X(void,Uniform1fv,(Int,Int,const float*)) X(void,Uniform2f,(Int,float,float)) \
 X(void,GenBuffers,(Int,UInt*)) X(void,DeleteBuffers,(Int,const UInt*)) \
 X(void,BindBuffer,(Enum,UInt)) X(void,BufferData,(Enum,Size,const void*,Enum)) \
 X(void,EnableVertexAttribArray,(UInt)) X(void,DisableVertexAttribArray,(UInt)) \
 X(void,VertexAttribPointer,(UInt,Int,Enum,Bool,Int,const void*)) \
 X(void,GetVertexAttribiv,(UInt,Enum,Int*)) X(void,GetVertexAttribPointerv,(UInt,Enum,void**)) \
 X(void,DrawArrays,(Enum,Int,Int))
struct Api {
#define DECLARE(ret,name,args) using name##Fn=ret(BSI_GL_CALL*)args; name##Fn name=nullptr;
    GL_FUNCTIONS(DECLARE)
    DECLARE(void,GenVertexArrays,(Int,UInt*))
    DECLARE(void,BindVertexArray,(UInt))
    DECLARE(void,DeleteVertexArrays,(Int,const UInt*))
    DECLARE(void,BindSampler,(UInt,UInt))
    DECLARE(void,PolygonMode,(Enum,Enum))
#undef DECLARE
    bool es=false, core=false, modern=false, samplers=false;
    bool load(ResolveGl resolve, void *user, std::string *error) {
#define LOAD(ret,name,args) name=reinterpret_cast<name##Fn>(resolve("gl" #name,user)); if(!name){*error="Missing OpenGL entry point: gl" #name;return false;}
        GL_FUNCTIONS(LOAD)
#undef LOAD
        const auto *bytes=GetString(0x1f02);
        if(!bytes){*error="No current OpenGL context";return false;}
        const std::string version(reinterpret_cast<const char*>(bytes));
        es=version.find("OpenGL ES")!=std::string::npos;
        const char *start=version.c_str();
        while(*start && (*start<'0'||*start>'9'))++start;
        const int major=std::atoi(start);
        const char *dot=std::strchr(start,'.');
        const int minor=dot?std::atoi(dot+1):0;
        if(major<2){*error="OpenGL 2.0 / OpenGL ES 2.0 or newer is required";return false;}
        modern=major>=3;
        // EXT_protected_textures exposes protected-context state publicly.
        // Query extensions without consuming the application's error queue.
        bool protectedExtension=false;
        if(modern){
            using Stringi=const unsigned char*(BSI_GL_CALL*)(Enum,UInt);
            const auto stringi=reinterpret_cast<Stringi>(resolve("glGetStringi",user));
            if(!stringi){*error="Modern GL extension/protected-context inspection is unavailable";return false;}
            Int count=0;GetIntegerv(0x821d,&count);
            if(count<0||count>4096){*error="GL extension list exceeds safe inspection bounds";return false;}
            for(Int i=0;i<count;++i){const auto *extension=stringi(0x1f03,UInt(i));
                if(!extension){*error="GL extension/protection inspection is incomplete";return false;}
                if(std::strcmp(reinterpret_cast<const char*>(extension),"GL_EXT_protected_textures")==0)protectedExtension=true;}
        }else{
            const auto *raw=GetString(0x1f03);
            const std::string extensions=raw?reinterpret_cast<const char*>(raw):"";
            protectedExtension=(" "+extensions+" ").find(" GL_EXT_protected_textures ")!=std::string::npos;
        }
        if(protectedExtension){Int flags=0;GetIntegerv(0x821e,&flags);if(flags&0x10){*error="Protected GL contexts are never processed";return false;}}
        if(!es && (major>3 || (major==3 && minor>=2))){Int profile=0;GetIntegerv(0x9126,&profile);core=(profile&1)!=0;}
        if(core){
            GenVertexArrays=reinterpret_cast<GenVertexArraysFn>(resolve("glGenVertexArrays",user));
            BindVertexArray=reinterpret_cast<BindVertexArrayFn>(resolve("glBindVertexArray",user));
            DeleteVertexArrays=reinterpret_cast<DeleteVertexArraysFn>(resolve("glDeleteVertexArrays",user));
            if(!GenVertexArrays||!BindVertexArray||!DeleteVertexArrays){*error="Core profile vertex arrays are unavailable";return false;}
        }
        samplers=(es && modern)||(!es && (major>3||(major==3 && minor>=3)));
        if(samplers){BindSampler=reinterpret_cast<BindSamplerFn>(resolve("glBindSampler",user));if(!BindSampler){*error="Sampler-state preservation unavailable";return false;}}
        if(!es){PolygonMode=reinterpret_cast<PolygonModeFn>(resolve("glPolygonMode",user));if(!PolygonMode){*error="Polygon-state preservation unavailable";return false;}}
        return true;
    }
};
struct State {
    Api &g; Int program=0,active=0,texture=0,buffer=0,vao=0,sampler=0,unpack=0;
    Int viewport[4]{},polygon[2]{},attrib[6]{}; void *pointer=nullptr; Bool mask[4]{};
    std::array<Enum,8> caps{{Blend,Depth,Stencil,Cull,Scissor,Dither,Srgb,Discard}};
    std::array<Bool,8> enabled{};
    explicit State(Api &api):g(api){
        g.GetIntegerv(0x8b8d,&program);g.GetIntegerv(0x84e0,&active);
        g.ActiveTexture(Texture0);g.GetIntegerv(0x8069,&texture);
        g.GetIntegerv(0x8894,&buffer);g.GetIntegerv(0x0ba2,viewport);g.GetBooleanv(0x0c23,mask);
        if(g.samplers){g.GetIntegerv(0x8919,&sampler);g.BindSampler(0,0);}
        if(g.modern){g.GetIntegerv(0x88ef,&unpack);g.BindBuffer(0x88ec,0);}
        if(g.core)g.GetIntegerv(0x85b5,&vao);
        else {
            const Enum names[]{0x8622,0x8623,0x8624,0x8625,0x886a,0x889f};
            for(unsigned i=0;i<6;++i)g.GetVertexAttribiv(0,names[i],&attrib[i]);
            g.GetVertexAttribPointerv(0,0x8645,&pointer);
        }
        if(!g.es){g.GetIntegerv(0x0b40,polygon);g.PolygonMode(0x0408,0x1b02);}
        for(std::size_t i=0;i<caps.size();++i){
            if((i==6 && (g.es||!g.modern))||(i==7&&!g.modern))continue;
            enabled[i]=g.IsEnabled(caps[i]);g.Disable(caps[i]);
        }
        g.ColorMask(1,1,1,1);
    }
    ~State(){
        if(g.core)g.BindVertexArray(UInt(vao));
        else {
            g.BindBuffer(ArrayBuffer,UInt(attrib[5]));
            g.VertexAttribPointer(0,attrib[1],Enum(attrib[3]),Bool(attrib[4]),attrib[2],pointer);
            if(attrib[0])g.EnableVertexAttribArray(0);else g.DisableVertexAttribArray(0);
        }
        g.BindBuffer(ArrayBuffer,UInt(buffer));
        if(g.modern)g.BindBuffer(0x88ec,UInt(unpack));
        if(g.samplers)g.BindSampler(0,UInt(sampler));
        g.BindTexture(Texture2d,UInt(texture));g.ActiveTexture(Enum(active));
        g.UseProgram(UInt(program));g.Viewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        g.ColorMask(mask[0],mask[1],mask[2],mask[3]);
        if(!g.es){
            if(g.core)g.PolygonMode(0x0408,Enum(polygon[0]));
            else {g.PolygonMode(0x0404,Enum(polygon[0]));g.PolygonMode(0x0405,Enum(polygon[1]));}
        }
        for(std::size_t i=0;i<caps.size();++i){
            if((i==6&&(g.es||!g.modern))||(i==7&&!g.modern))continue;
            if(enabled[i])g.Enable(caps[i]);else g.Disable(caps[i]);
        }
    }
};
}
struct GlPipeline::Impl {
    Api g; UInt program=0,texture=0,vbo=0,vao=0; Int frame=-1,texel=-1,weights=-1;
    int width=0,height=0,maximum=0; bool ready=false,bufferReady=false;
    UInt compile(Enum type,const std::string &source,std::string *error){
        UInt shader=g.CreateShader(type);if(!shader){*error="Could not allocate shader";return 0;}
        const char *s=source.c_str();g.ShaderSource(shader,1,&s,nullptr);g.CompileShader(shader);
        Int okay=0;g.GetShaderiv(shader,0x8b81,&okay);
        if(!okay){char log[1024]{};g.GetShaderInfoLog(shader,sizeof(log),nullptr,log);*error=std::string("Shader compilation failed: ")+log;g.DeleteShader(shader);return 0;}
        return shader;
    }
};
GlPipeline::GlPipeline():impl(new Impl){}
// Driver objects belong to a specific context. An arbitrary-thread destructor
// must not issue GL calls; the host releases them with the context current, or
// lets the driver reclaim them when that context is destroyed.
GlPipeline::~GlPipeline()=default;
bool GlPipeline::initialized()const{return impl->ready;}
bool GlPipeline::initialize(ResolveGl resolve,void *user,std::string *error){
    if(impl->ready)return true;
    auto &p=*impl;if(!resolve||!error)return false;
    if(!p.g.load(resolve,user,error))return false;
    auto &g=p.g;
    UInt vertex=p.compile(0x8b31,vertexSource(g.es,g.core),error);if(!vertex)return false;
    UInt fragment=p.compile(0x8b30,fragmentSource(g.es,g.core),error);
    if(!fragment){g.DeleteShader(vertex);return false;}
    p.program=g.CreateProgram();
    if(!p.program){g.DeleteShader(vertex);g.DeleteShader(fragment);*error="Could not allocate shader program";return false;}
    g.AttachShader(p.program,vertex);g.AttachShader(p.program,fragment);g.BindAttribLocation(p.program,0,"position");g.LinkProgram(p.program);
    g.DeleteShader(vertex);g.DeleteShader(fragment);
    Int okay=0;g.GetProgramiv(p.program,0x8b82,&okay);
    if(!okay){char log[1024]{};g.GetProgramInfoLog(p.program,sizeof(log),nullptr,log);*error=std::string("Shader link failed: ")+log;release();return false;}
    p.frame=g.GetUniformLocation(p.program,"frame");p.texel=g.GetUniformLocation(p.program,"texel");p.weights=g.GetUniformLocation(p.program,"weights[0]");
    if(p.frame<0||p.texel<0||p.weights<0){*error="Required shader uniforms are unavailable";release();return false;}
    g.GetIntegerv(0x0d33,&p.maximum);
    g.GenTextures(1,&p.texture);g.GenBuffers(1,&p.vbo);if(g.core)g.GenVertexArrays(1,&p.vao);
    if(!p.texture||!p.vbo||(g.core&&!p.vao)){*error="Could not allocate shader resources";release();return false;}
    p.ready=true;return true;
}
bool GlPipeline::render(int width,int height,const Parameters &settings,std::string *error){
    // Strong neutral bypass: no GL API calls, state mutations or texture copy.
    if(settings.neutral())return true;
    if(!error)return false;
    if(!settings.valid()){*error="Shader values are outside their supported ranges";return false;}
    auto &p=*impl;auto &g=p.g;
    if(!p.ready){*error="Shader context is not initialized";return false;}
    if(width<=0||height<=0||width>p.maximum||height>p.maximum||std::int64_t(width)*height>33554432){*error="Game surface exceeds the safe shader texture limit";return false;}
    Int framebuffer=0;g.GetIntegerv(0x8ca6,&framebuffer);
    if(framebuffer){*error="Presentation did not expose the default game framebuffer";return false;}
    if(g.modern){g.GetIntegerv(0x8caa,&framebuffer);if(framebuffer){*error="A non-default read framebuffer is active";return false;}}
    Int samples=0;g.GetIntegerv(0x80a9,&samples);
    if(samples>1){*error="Multisampled presentation requires a resolve path; shaders are bypassed";return false;}
    State saved(g);
    g.BindTexture(Texture2d,p.texture);
    if(p.width!=width||p.height!=height){
        g.TexParameteri(Texture2d,0x2801,0x2601);g.TexParameteri(Texture2d,0x2800,0x2601);
        g.TexParameteri(Texture2d,0x2802,0x812f);g.TexParameteri(Texture2d,0x2803,0x812f);
        g.TexImage2D(Texture2d,0,0x1908,width,height,0,0x1908,0x1401,nullptr);p.width=width;p.height=height;
    }
    g.CopyTexSubImage2D(Texture2d,0,0,0,0,0,width,height);
    g.UseProgram(p.program);g.Uniform1i(p.frame,0);g.Uniform2f(p.texel,1.f/width,1.f/height);
    std::array<float,controls.size()> weights{};
    for(std::size_t i=0;i<weights.size();++i)weights[i]=settings.values[i]/100.f;
    g.Uniform1fv(p.weights,Int(weights.size()),weights.data());
    if(g.core)g.BindVertexArray(p.vao);
    g.BindBuffer(ArrayBuffer,p.vbo);
    if(!p.bufferReady){constexpr float quad[]{-1,-1,1,-1,-1,1,1,1};g.BufferData(ArrayBuffer,sizeof(quad),quad,0x88e4);p.bufferReady=true;}
    g.VertexAttribPointer(0,2,0x1406,0,0,nullptr);g.EnableVertexAttribArray(0);
    g.Viewport(0,0,width,height);g.DrawArrays(0x0005,0,4);
    return true;
}
void GlPipeline::release(){
    auto &p=*impl;auto &g=p.g;
    if(p.texture&&g.DeleteTextures)g.DeleteTextures(1,&p.texture);
    if(p.vbo&&g.DeleteBuffers)g.DeleteBuffers(1,&p.vbo);
    if(p.vao&&g.DeleteVertexArrays)g.DeleteVertexArrays(1,&p.vao);
    if(p.program&&g.DeleteProgram)g.DeleteProgram(p.program);
    p.texture=p.vbo=p.vao=p.program=0;p.ready=p.bufferReady=false;p.width=p.height=0;
}
} // namespace BsiShaders
