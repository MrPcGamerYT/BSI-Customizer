// ORIGINAL TEST HOST, NOT HD-PLAYER. No game, account, driver profile or BSI
// shader module. The optional import resolves an existing export but NEVER
// calls it. Actual supplied DLL initialization/hooks execute only in the
// explicit offline job, in a suspended-then-job-contained fresh child.
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>
#include "GlTextureQuality.h" // independent policy only; no BSI runtime/module

#if defined(BSI_IMPORT_DINPUT)
extern "C" __declspec(dllimport) void proxy_DirectInput8Create();
static void (*volatile importAnchor)() = &proxy_DirectInput8Create;
#elif defined(BSI_IMPORT_VERSION)
extern "C" __declspec(dllimport) void proxy_VerQueryValueW();
static void (*volatile importAnchor)() = &proxy_VerQueryValueW;
#endif
namespace {
constexpr unsigned side=64;
const wchar_t *diagnosticReport=nullptr;
unsigned diagnosticStage=0;
[[noreturn]] void fail(const char *what){
    const auto error=GetLastError();
    std::fprintf(stderr,"PROBE FAILURE: %s (Win32 %lu, stage %u)\n",what,error,diagnosticStage);
    if(diagnosticReport){
        FILE *file=nullptr;
        if(_wfopen_s(&file,diagnosticReport,L"wb")==0&&file){
            std::fprintf(file,"{\"kind\":\"original-probe-prerequisite-failure\",\"failureStage\":%u,\"win32Error\":%lu}\n",diagnosticStage,error);
            std::fclose(file);
        }
    }
    std::exit(2);
}
void require(bool okay,const char *what){if(!okay)fail(what);}
std::wstring exePath(){wchar_t out[32768]{};const auto n=GetModuleFileNameW(nullptr,out,32768);require(n>0&&n<32768,"probe EXE path");return {out,n};}
std::string json(const char *text){
    std::string out="\"";for(const unsigned char c:std::string(text?text:"")){
        if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c>=32&&c<127)out+=char(c);else out+='?';
    }return out+'"';
}
std::wstring quote(const std::wstring &s){require(s.find(L'"')==std::wstring::npos,"quoted test path");return L"\""+s+L"\"";}
int launch(const wchar_t *program,const wchar_t *report,const wchar_t *sampling){
    wchar_t approved[8]{};require(GetEnvironmentVariableW(L"BSI_POTATO_PROBE_APPROVED",approved,8)==1&&approved[0]==L'1',"explicit offline-probe approval");
    // The workflow verifies an outbound firewall block for THIS exact child
    // image before invoking us. A new job prevents any further process spawn.
    HANDLE job=CreateJobObjectW(nullptr,nullptr);require(job!=nullptr,"create bounded probe job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limit.BasicLimitInformation.ActiveProcessLimit=1;
    require(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit))!=0,"no child-process escape");
    wchar_t system[32768]{};
    require(GetEnvironmentVariableW(L"SystemRoot",system,32768)>0,"Windows root");
    // Construct, don't filter, the environment: no Actions/GitHub/Drive tokens,
    // runner service endpoints, user profile, SDK, Qt or interpreter PATH.
    const std::wstring root=std::filesystem::path(program).parent_path().wstring();
    const std::vector<std::wstring> vars{
        L"BSI_POTATO_PROBE_APPROVED=1",L"GALLIUM_DRIVER=llvmpipe",L"LIBGL_ALWAYS_SOFTWARE=1",
        L"MESA_SHADER_CACHE_DISABLE=true",L"PATH="+std::wstring(system)+L"\\System32;"+system,
        L"SystemRoot="+std::wstring(system),L"TEMP="+root,L"TMP="+root,L"USERPROFILE="+root};
    std::vector<wchar_t> env;for(const auto &v:vars){env.insert(env.end(),v.begin(),v.end());env.push_back(0);}env.push_back(0);
    std::wstring command=quote(program)+L" --probe "+quote(report)+L" "+quote(sampling);
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    require(CreateProcessW(program,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,
                           env.data(),root.c_str(),&start,&process)!=0,"create suspended probe");
    if(!AssignProcessToJobObject(job,process.hProcess)){
        TerminateProcess(process.hProcess,90);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);fail("contain child BEFORE it runs");
    }
    require(ResumeThread(process.hThread)!=DWORD(-1),"resume contained probe");CloseHandle(process.hThread);
    const auto waited=WaitForSingleObject(process.hProcess,30000);
    if(waited!=WAIT_OBJECT_0){TerminateJobObject(job,91);WaitForSingleObject(process.hProcess,5000);}
    DWORD code=92;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);CloseHandle(job);
    FILE *file=nullptr;
    const auto launchReport=std::wstring(report)+L".launch.json";
    require(_wfopen_s(&file,launchReport.c_str(),L"wb")==0&&file,"create parent-owned launch evidence");
    std::fprintf(file,"{\"createdSuspended\":true,\"containedBeforeResume\":true,\"processLimit\":1,\"timeout\":%s,\"childExitCode\":%lu}\n",waited==WAIT_OBJECT_0?"false":"true",code);
    std::fclose(file);
    if(waited!=WAIT_OBJECT_0)return 91;
    std::printf("Contained original-host exit: %lu\n",code);return code?1:0;
}
// Read-only, bounded inspection of common x64 detour/relay jumps. A positive
// result means this real entry leads into the exact loaded payload allocation;
// parameters/pixels below are separate evidence that interception executed.
// This is not used to patch, initialize, or transplant any payload code.
bool entryLeadsIntoOriginal(const void *entry){
    const auto original=GetModuleHandleW(L"BsiPotatoOriginal.dll");
    if(!original)return false;
    auto cursor=reinterpret_cast<std::uintptr_t>(entry);
    for(unsigned hop=0;hop<6;++hop){
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(reinterpret_cast<const void*>(cursor),&memory,sizeof(memory)))return false;
        if(memory.AllocationBase==original)return true;
        unsigned char bytes[16]{};SIZE_T read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(cursor),bytes,sizeof(bytes),&read)||read!=sizeof(bytes))return false;
        if(bytes[0]==0xe9){std::int32_t offset=0;std::memcpy(&offset,bytes+1,sizeof(offset));cursor=std::uintptr_t(std::intptr_t(cursor)+5+offset);}
        else if(bytes[0]==0xeb){cursor=std::uintptr_t(std::intptr_t(cursor)+2+std::int8_t(bytes[1]));}
        else if(bytes[0]==0xff&&bytes[1]==0x25){
            std::int32_t offset=0;std::memcpy(&offset,bytes+2,sizeof(offset));
            const auto location=std::uintptr_t(std::intptr_t(cursor)+6+offset);
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(location),&cursor,sizeof(cursor),&read)||read!=sizeof(cursor))return false;
        }else return false;
    }
    return false;
}
template<class T>T gl(const char *name){
    auto p=reinterpret_cast<void*>(wglGetProcAddress(name));const auto v=reinterpret_cast<std::intptr_t>(p);
    if(!p||v==1||v==2||v==3||v==-1)p=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"),name));
    require(p!=nullptr,name);return reinterpret_cast<T>(p);
}
#define LOAD(ret,name,...) const auto name=gl<ret(APIENTRY*)(__VA_ARGS__)>("gl" #name)
std::array<unsigned char,4> pixel(){std::array<unsigned char,4> p{};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
std::string color(const std::array<unsigned char,4> &p){return "["+std::to_string(p[0])+","+std::to_string(p[1])+","+std::to_string(p[2])+","+std::to_string(p[3])+"]";}
int probe(const wchar_t *report,bool sampler,bool independent=false){
    diagnosticReport=report;diagnosticStage=1;
    require(sizeof(void*)==8,"AMD64 probe");
    wchar_t approved[8]{};require(GetEnvironmentVariableW(L"BSI_POTATO_PROBE_APPROVED",approved,8)==1&&approved[0]==L'1',"offline child environment");
    bool imported=false;
#if defined(BSI_IMPORT_DINPUT) || defined(BSI_IMPORT_VERSION)
    imported=GetModuleHandleW(L"BsiPotatoOriginal.dll")!=nullptr&&importAnchor!=nullptr;
    require(imported,"original DLL loaded through normal PE import, no explicit initializer call");
#else
    require(GetModuleHandleW(L"BsiPotatoOriginal.dll")==nullptr,"negative control must not load a supplied DLL");
#endif
    diagnosticStage=10;
    wchar_t provider[32768]{};require(GetModuleFileNameW(GetModuleHandleW(L"opengl32.dll"),provider,32768)>0,"GL provider path");
    const auto expected=std::filesystem::path(exePath()).parent_path()/L"opengl32.dll";
    require(_wcsicmp(provider,expected.c_str())==0,"pinned test-local Mesa provider, never system/physical GPU replacement");
    diagnosticStage=20;
    HWND window=CreateWindowExW(0,L"STATIC",L"Original Potato DLL test-owned pixels",WS_POPUP,0,0,side,side,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"private test HWND");HDC dc=GetDC(window);require(dc!=nullptr,"private WGL DC");
    PIXELFORMATDESCRIPTOR format{};format.nSize=sizeof(format);format.nVersion=1;
    format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;format.cDepthBits=24;
    const auto index=ChoosePixelFormat(dc,&format);require(index&&SetPixelFormat(dc,index,&format),"software WGL pixel format");
    HGLRC context=wglCreateContext(dc);require(context&&wglMakeCurrent(dc,context),"real Mesa WGL context");
    const char *renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char *version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    require(renderer&&std::strstr(renderer,"llvmpipe"),"software llvmpipe ONLY; no physical-driver claim");
    diagnosticStage=30;
    LOAD(GLuint,CreateShader,GLenum);LOAD(void,ShaderSource,GLuint,GLsizei,const char*const*,const GLint*);
    LOAD(void,CompileShader,GLuint);LOAD(void,GetShaderiv,GLuint,GLenum,GLint*);
    LOAD(GLuint,CreateProgram);LOAD(void,AttachShader,GLuint,GLuint);LOAD(void,LinkProgram,GLuint);
    LOAD(void,GetProgramiv,GLuint,GLenum,GLint*);LOAD(void,UseProgram,GLuint);
    LOAD(GLint,GetUniformLocation,GLuint,const char*);LOAD(void,Uniform1i,GLint,GLint);LOAD(void,Uniform1f,GLint,GLfloat);
    LOAD(void,ActiveTexture,GLenum);LOAD(void,BindTexture,GLenum,GLuint);
    LOAD(void,GenVertexArrays,GLsizei,GLuint*);LOAD(void,BindVertexArray,GLuint);
    LOAD(void,DrawArrays,GLenum,GLint,GLsizei);LOAD(void,GenSamplers,GLsizei,GLuint*);
    LOAD(void,SamplerParameteri,GLuint,GLenum,GLint);LOAD(void,SamplerParameterf,GLuint,GLenum,GLfloat);
    LOAD(void,GetSamplerParameterfv,GLuint,GLenum,GLfloat*);LOAD(void,BindSampler,GLuint,GLuint);LOAD(void,DeleteSamplers,GLsizei,const GLuint*);
    const auto shader=[&](GLenum kind,const char *source){const auto s=CreateShader(kind);ShaderSource(s,1,&source,nullptr);CompileShader(s);GLint okay=0;GetShaderiv(s,0x8b81,&okay);require(okay!=0,"test shader compile");return s;};
    const char *vertex="#version 330\nout vec2 uv;uniform float sampleScale;void main(){vec2 p=vec2(float((gl_VertexID&1)*2-1),float((gl_VertexID>>1)*2-1));uv=(p+1.0)*0.5*sampleScale;gl_Position=vec4(p,0,1);}";
    const char *fragment="#version 330\nin vec2 uv;out vec4 outputColor;uniform sampler2D sourceColor;void main(){outputColor=texture(sourceColor,uv);}";
    const auto program=CreateProgram();AttachShader(program,shader(0x8b31,vertex));AttachShader(program,shader(0x8b30,fragment));LinkProgram(program);
    GLint linked=0;GetProgramiv(program,0x8b82,&linked);require(linked!=0,"test program link");UseProgram(program);Uniform1i(GetUniformLocation(program,"sourceColor"),0);
    const auto scaleLocation=GetUniformLocation(program,"sampleScale");require(scaleLocation>=0,"authored texture-gradient control");Uniform1f(scaleLocation,1.f);
    GLuint vao=0;GenVertexArrays(1,&vao);BindVertexArray(vao);
    diagnosticStage=40;
    ActiveTexture(0x84c0);GLuint texture=0;glGenTextures(1,&texture);BindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const unsigned char colors[7][4]{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{0,255,255,255},{255,0,255,255},{255,255,255,255}};
    const auto mipFor=[&](const std::array<unsigned char,4> &value){
        for(int i=0;i<7;++i)if(std::equal(value.begin(),value.end(),colors[i]))return i;
        return -1;
    };
    for(unsigned mip=0;mip<7;++mip){const auto length=side>>mip;std::vector<unsigned char> data(length*length*4);
        for(std::size_t i=0;i<data.size();i+=4)std::copy(colors[mip],colors[mip]+4,data.begin()+std::ptrdiff_t(i));
        glTexImage2D(GL_TEXTURE_2D,GLint(mip),0x8058,GLsizei(length),GLsizei(length),0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
    }
    GLuint nativeSampler=0;
    if(sampler){GenSamplers(1,&nativeSampler);SamplerParameteri(nativeSampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);SamplerParameteri(nativeSampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);BindSampler(0,nativeSampler);}
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glViewport(0,0,side,side);glReadBuffer(GL_BACK);glDrawBuffer(GL_BACK);
    ActiveTexture(0x84c3);
    const auto draw=[&]{glClear(GL_COLOR_BUFFER_BIT);DrawArrays(GL_TRIANGLE_STRIP,0,4);glFinish();};
    const auto params=[&]{std::array<float,4> p{};GLint active=0;glGetIntegerv(0x84e0,&active);ActiveTexture(0x84c0);
        glGetTexParameterfv(GL_TEXTURE_2D,0x8501,&p[0]);glGetTexParameterfv(GL_TEXTURE_2D,0x813a,&p[1]);
        if(sampler){GetSamplerParameterfv(nativeSampler,0x8501,&p[2]);GetSamplerParameterfv(nativeSampler,0x813a,&p[3]);}
        ActiveTexture(GLenum(active));return p;};
    draw();const auto before=pixel();const auto native=params();
    require(before==std::array<unsigned char,4>{{255,0,0,255}}&&native[0]==0&&native[1]==-1000,"native unmodified mip-zero baseline BEFORE any swap");
    Uniform1f(scaleLocation,16.f);draw();const auto nativeMinified=pixel();
    require(mipFor(nativeMinified)==4,"16-times texture gradients select native mip four before hooks");
    Uniform1f(scaleLocation,1.f);draw();require(pixel()==before,"restore baseline texture gradients");
    require(glGetError()==GL_NO_ERROR,"test-owned setup has no GL errors");
    if(independent){
        diagnosticStage=70;
        require(!imported,"independent comparison NEVER loads any original DLL");
        // A REAL, explicitly authored resource contract; these booleans refer
        // ONLY to this fixture, not to an unidentified player/game resource.
        BsiShaders::TextureQualityProof proof;
        proof.renderer=BsiShaders::Renderer::OpenGL;
        proof.use=BsiShaders::TextureUse::SampledColor;
        proof.gameSurfaceIdentified=proof.resourceOwnershipProven=true;
        proof.privateReplacementSupported=proof.externalOverridesReviewed=true;
        proof.visibleMipLevels=7;
        BsiShaders::GlQualitySampler source;
        source.minimumFilter=GL_NEAREST_MIPMAP_NEAREST;
        // The host authored both source objects and their complete defaults;
        // this is not a general sampler-cloning helper for foreign resources.
        const auto sourceState=[&]{
            std::array<GLfloat,30> result{};GLint activeUnit=0;glGetIntegerv(0x84e0,&activeUnit);ActiveTexture(0x84c0);
            constexpr GLenum keys[]{0x2801,0x2800,0x2802,0x2803,0x8072,0x813a,0x813b,0x8501,0x884c,0x884d};
            for(unsigned i=0;i<10;++i){glGetTexParameterfv(GL_TEXTURE_2D,keys[i],&result[i]);
                if(sampler)GetSamplerParameterfv(nativeSampler,keys[i],&result[i+10]);}
            glGetTexParameterfv(GL_TEXTURE_2D,0x1004,&result[20]);
            if(sampler)GetSamplerParameterfv(nativeSampler,0x1004,&result[24]);
            glGetTexParameterfv(GL_TEXTURE_2D,0x813c,&result[28]);glGetTexParameterfv(GL_TEXTURE_2D,0x813d,&result[29]);
            ActiveTexture(GLenum(activeUnit));return result;
        };
        const auto savedSource=sourceState();
        GLuint unrelated=0;GenSamplers(1,&unrelated);SamplerParameterf(unrelated,0x813a,2.f);SamplerParameterf(unrelated,0x8501,.25f);
        BindSampler(1,unrelated);
        const auto state=[&]{
            std::array<GLint,17> result{};
            glGetIntegerv(0x84e0,&result[0]);glGetIntegerv(0x8b8d,&result[1]);
            glGetIntegerv(0x8ca6,&result[2]);glGetIntegerv(0x8caa,&result[3]);glGetIntegerv(0x85b5,&result[4]);
            glGetIntegerv(GL_VIEWPORT,&result[5]);glGetIntegerv(GL_DEPTH_WRITEMASK,&result[9]);
            result[10]=glIsEnabled(GL_DEPTH_TEST);result[11]=glIsEnabled(GL_BLEND);result[12]=glIsEnabled(GL_CULL_FACE);
            ActiveTexture(0x84c0);glGetIntegerv(0x8069,&result[13]);glGetIntegerv(0x8919,&result[14]);
            ActiveTexture(0x84c1);glGetIntegerv(0x8919,&result[15]);glGetIntegerv(0x8069,&result[16]);
            ActiveTexture(GLenum(result[0]));return result;
        };
        const auto savedState=state();
        std::array<unsigned char,4> coarse[2]{},floorMinified[2]{},restored=before;
        bool exact=true,unrelatedUntouched=true,actualLods=true;unsigned cycles=0;
        for(unsigned mode=0;mode<2;++mode){
            const auto setting=mode?BsiShaders::TextureQuality::Strong:BsiShaders::TextureQuality::Light;
            const auto plan=BsiShaders::planGlTextureQuality(setting,proof,source);
            require(plan.changed(),"independent policy accepts this explicitly owned complete color mip chain");
            GLuint replacement=0;GenSamplers(1,&replacement);
            SamplerParameteri(replacement,GL_TEXTURE_MIN_FILTER,source.minimumFilter);SamplerParameteri(replacement,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            SamplerParameterf(replacement,0x813a,plan.descriptor.minimumLod);
            GLfloat readLod=0;GetSamplerParameterfv(replacement,0x813a,&readLod);
            actualLods=actualLods&&readLod==(mode?3.f:1.5f);
            for(unsigned cycle=0;cycle<16;++cycle){
                BindSampler(0,replacement);draw();coarse[mode]=pixel();
                GLfloat otherFloor=0,otherBias=0;GetSamplerParameterfv(unrelated,0x813a,&otherFloor);GetSamplerParameterfv(unrelated,0x8501,&otherBias);
                unrelatedUntouched=unrelatedUntouched&&otherFloor==2.f&&otherBias==.25f;
                // This is the actual Off operation: restore the exact original
                // binding BEFORE the next draw. No source object was rewritten.
                BindSampler(0,nativeSampler);draw();restored=pixel();
                exact=exact&&restored==before&&sourceState()==savedSource&&state()==savedState;
                ++cycles;
            }
            // A floor is NOT an additive bias: a native lambda of four stays
            // at mip four even with either tested floor (1.5 or 3.0).
            Uniform1f(scaleLocation,16.f);BindSampler(0,replacement);draw();floorMinified[mode]=pixel();
            BindSampler(0,nativeSampler);draw();exact=exact&&pixel()==nativeMinified;
            Uniform1f(scaleLocation,1.f);draw();restored=pixel();
            exact=exact&&restored==before&&sourceState()==savedSource&&state()==savedState;
            DeleteSamplers(1,&replacement);
        }
        // Another independently authored, DESKTOP-GL-only alternative: put
        // bias + floor in a private sampler on this explicitly known resource,
        // not into the application's objects. Never stack unknown overrides.
        // This is separate from (and does not modify) production's floor plan.
        GLuint biased=0;GenSamplers(1,&biased);
        SamplerParameteri(biased,GL_TEXTURE_MIN_FILTER,source.minimumFilter);SamplerParameteri(biased,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        SamplerParameterf(biased,0x813a,3.f);SamplerParameterf(biased,0x8501,3.f);
        GLfloat actualBias=0,actualFloor=0;GetSamplerParameterfv(biased,0x8501,&actualBias);GetSamplerParameterfv(biased,0x813a,&actualFloor);
        BindSampler(0,biased);draw();const auto biasedPixel=pixel();
        Uniform1f(scaleLocation,16.f);draw();const auto biasedMinified=pixel();
        BindSampler(0,nativeSampler);draw();const auto biasedOffMinified=pixel();
        Uniform1f(scaleLocation,1.f);draw();restored=pixel();
        const bool biasedOffExact=biasedOffMinified==nativeMinified&&restored==before&&sourceState()==savedSource&&state()==savedState;
        const bool biasMatches=actualBias==3.f&&actualFloor==3.f&&mipFor(biasedPixel)==3&&mipFor(biasedMinified)==6;
        DeleteSamplers(1,&biased);
        const bool gradientDifference=floorMinified[0]==nativeMinified&&floorMinified[1]==nativeMinified&&biasMatches;
        const bool depth=glIsEnabled(GL_DEPTH_TEST)!=0;
        const auto error=glGetError();
        const bool okay=coarse[0]!=before&&coarse[1]!=before&&exact&&unrelatedUntouched&&actualLods&&gradientDifference&&biasedOffExact&&!depth&&error==GL_NO_ERROR;
        FILE *file=nullptr;require(_wfopen_s(&file,report,L"wb")==0&&file,"create independent comparison report");
        const auto truth=[](bool value){return value?"true":"false";};
        std::fprintf(file,
            "{\"kind\":\"independent-owned-resource-WGL-alternative\",\"originalDllLoaded\":false,"
            "\"softwareRenderer\":true,\"depthTestEnabled\":%s,\"offExact\":%s,\"sourceParametersUnchanged\":%s,"
            "\"unrelatedSamplerUntouched\":%s,\"minimumLodsVerified\":%s,\"cycles\":%u,\"firstGlError\":%u,"
            "\"beforePixel\":%s,\"lightPixel\":%s,\"strongPixel\":%s,\"offPixel\":%s,"
            "\"nativeMinifiedPixel\":%s,\"lightFloorMinifiedPixel\":%s,\"strongFloorMinifiedPixel\":%s,"
            "\"privateBiasPixel\":%s,\"privateBiasMinifiedPixel\":%s,\"privateBiasOffMinifiedPixel\":%s,"
            "\"biasFloorDifferenceObserved\":%s,\"privateBiasOffExact\":%s,\"passed\":%s,"
            "\"gameExecuted\":false,\"physicalGpuTested\":false}\n",
            truth(depth),truth(exact),truth(sourceState()==savedSource),truth(unrelatedUntouched),truth(actualLods),cycles,error,
            color(before).c_str(),color(coarse[0]).c_str(),color(coarse[1]).c_str(),color(restored).c_str(),
            color(nativeMinified).c_str(),color(floorMinified[0]).c_str(),color(floorMinified[1]).c_str(),
            color(biasedPixel).c_str(),color(biasedMinified).c_str(),color(biasedOffMinified).c_str(),
            truth(gradientDifference),truth(biasedOffExact),truth(okay));
        std::fclose(file);BindSampler(1,0);DeleteSamplers(1,&unrelated);
        return okay?0:3;
    }
    const auto swap=reinterpret_cast<BOOL(WINAPI*)(HDC)>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"),"wglSwapBuffers")));
    require(swap!=nullptr,"the actual wglSwapBuffers entry used by this DLL");
    diagnosticStage=50;
    std::array<float,4> changed{},beforeDraw{};std::array<unsigned char,4> after{};unsigned attempts=0;
    for(;attempts<100;++attempts){
        require(swap(dc)!=0,"test swap original succeeds");beforeDraw=params();
        draw();changed=params();after=pixel();
        if(changed[0]!=0||changed[1]!=-1000||(!imported&&attempts>=2))break;
        Sleep(50);
    }
    // A second draw WITHOUT a new bind/swap observes persistent mutation.
    draw();const bool persistent=params()==changed;
    Uniform1f(scaleLocation,16.f);draw();const auto minifiedPixel=pixel();const auto minifiedMip=mipFor(minifiedPixel);
    Uniform1f(scaleLocation,1.f);draw();
    GLint active=0;glGetIntegerv(0x84e0,&active);const bool depth=glIsEnabled(GL_DEPTH_TEST)!=0;
    const auto swapHook=entryLeadsIntoOriginal(reinterpret_cast<const void*>(swap));
    const auto drawHook=entryLeadsIntoOriginal(reinterpret_cast<const void*>(DrawArrays));
    const auto bindExport=GetProcAddress(GetModuleHandleW(L"opengl32.dll"),"glBindTexture");
    const auto bindHook=entryLeadsIntoOriginal(reinterpret_cast<const void*>(BindTexture));
    const auto bindExportHook=entryLeadsIntoOriginal(reinterpret_cast<const void*>(bindExport));
    const bool bindEntriesDiffer=reinterpret_cast<const void*>(BindTexture)!=reinterpret_cast<const void*>(bindExport);
    // Test BOTH real entry paths. Mesa exposes a distinct imported export and
    // wglGetProcAddress dispatch stub; the original only detours the latter in
    // this provider. Record the uncovered alias, don't relabel it as hooked.
    ActiveTexture(0x84c0);GLuint bindOnly=0;glGenTextures(1,&bindOnly);BindTexture(GL_TEXTURE_2D,bindOnly);
    GLfloat bindBias=0,bindMinimum=0;glGetTexParameterfv(GL_TEXTURE_2D,0x8501,&bindBias);glGetTexParameterfv(GL_TEXTURE_2D,0x813a,&bindMinimum);
    GLuint exportOnly=0;glGenTextures(1,&exportOnly);glBindTexture(GL_TEXTURE_2D,exportOnly);
    GLfloat exportBias=0,exportMinimum=0;glGetTexParameterfv(GL_TEXTURE_2D,0x8501,&exportBias);glGetTexParameterfv(GL_TEXTURE_2D,0x813a,&exportMinimum);
    BindTexture(GL_TEXTURE_2D,texture);ActiveTexture(GLenum(active));
    const auto remainingError=glGetError();
    const bool mutated=changed[0]>0&&(sampler?changed[2]>0:true),pixelsChanged=before!=after;
    const auto sampledMip=mipFor(after);
    const bool drawMutation=beforeDraw==native&&changed!=native;
    const bool okay=imported?
        (mutated&&pixelsChanged&&sampledMip>0&&minifiedMip==6&&persistent&&drawMutation&&bindBias>0&&swapHook&&drawHook&&bindHook&&!depth&&active==0x84c3):
        (!mutated&&!pixelsChanged&&sampledMip==0&&minifiedMip==4&&!swapHook&&!drawHook&&!bindHook&&!depth&&active==0x84c3);
    diagnosticStage=60;
    FILE *file=nullptr;require(_wfopen_s(&file,report,L"wb")==0&&file,"create sanitized report");
    const auto truth=[](bool value){return value?"true":"false";};
    std::fprintf(file,
        "{\"kind\":\"original-supplied-DLL-WGL-software-probe\","
        "\"importLoaded\":%s,\"anchorCalled\":false,\"samplerBound\":%s,"
        "\"depthTestEnabled\":%s,\"activeUnitRestored\":%s,\"softwareRenderer\":true,"
        "\"renderer\":%s,\"version\":%s,\"beforePixel\":%s,\"afterPixel\":%s,"
        "\"nativeMinifiedPixel\":%s,\"minifiedPixel\":%s,\"minifiedMip\":%d,"
        "\"textureBias\":%.3f,\"textureMinLod\":%.3f,\"samplerBias\":%.3f,\"samplerMinLod\":%.3f,"
        "\"parametersPersistAfterDraw\":%s,\"pixelsChanged\":%s,\"sampledMip\":%d,"
        "\"drawMutationAfterSwap\":%s,\"bindOnlyBias\":%.3f,\"bindOnlyMinLod\":%.3f,"
        "\"swapEntryLeadsIntoOriginal\":%s,\"drawEntryLeadsIntoOriginal\":%s,\"bindEntryLeadsIntoOriginal\":%s,"
        "\"bindExportEntryLeadsIntoOriginal\":%s,\"bindEntriesDiffer\":%s,\"bindExportOnlyBias\":%.3f,\"bindExportOnlyMinLod\":%.3f,"
        "\"warmupAttempts\":%u,\"firstRemainingGlError\":%u,\"mechanismObserved\":%s,"
        "\"gameExecuted\":false,\"physicalGpuTested\":false}\n",
        truth(imported),truth(sampler),truth(depth),truth(active==0x84c3),json(renderer).c_str(),json(version).c_str(),
        color(before).c_str(),color(after).c_str(),color(nativeMinified).c_str(),color(minifiedPixel).c_str(),minifiedMip,
        changed[0],changed[1],changed[2],changed[3],truth(persistent),truth(pixelsChanged),sampledMip,
        truth(drawMutation),bindBias,bindMinimum,truth(swapHook),truth(drawHook),truth(bindHook),
        truth(bindExportHook),truth(bindEntriesDiffer),exportBias,exportMinimum,
        attempts<100?attempts+1:100,remainingError,truth(okay));
    std::fclose(file);
    // Original proxy owns live worker/hooks. Never FreeLibrary it beneath
    // callbacks or pretend Off restores its texture objects. Fresh process per
    // variant; the outer job kills all remaining work on timeout/exit.
    std::printf("%s: original-DLL=%d texture-bias=%.1f min=%.1f depth-disabled=%d pixels-changed=%d\n",okay?"PASS":"FAIL",int(imported),changed[0],changed[1],int(!depth),int(pixelsChanged));
    return okay?0:3;
}
} // namespace
int wmain(int argc,wchar_t **argv){
    if(argc==5&&std::wcscmp(argv[1],L"--launch")==0)return launch(argv[2],argv[3],argv[4]);
    if(argc==4&&std::wcscmp(argv[1],L"--probe")==0){
        const bool sampler=std::wcscmp(argv[3],L"sampler")==0||std::wcscmp(argv[3],L"owned-sampler")==0;
        const bool independent=std::wcscmp(argv[3],L"owned-texture")==0||std::wcscmp(argv[3],L"owned-sampler")==0;
        require(sampler||independent||std::wcscmp(argv[3],L"texture")==0,"known test case only");
        return probe(argv[2],sampler,independent);
    }
    std::fputs("Explicit offline test only: --launch test-host.exe report.json texture|sampler|owned-texture|owned-sampler\n",stderr);return 64;
}
