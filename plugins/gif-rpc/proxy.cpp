#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <cstring>
#include "RpcPayload.h"

// Marker + explicit export identify OUR app-local payload. No other version.dll
// is modified implicitly. No Discord SDK, optimizer, hooks or hotkeys are used.
extern "C" __declspec(dllexport) const char *BsiGifRpcSchema() { return "BSI_GIF_RPC_PROXY_V2"; }
static HMODULE systemVersion=nullptr;
static INIT_ONCE versionOnce=INIT_ONCE_STATIC_INIT;
static HANDLE stopEvent=nullptr;
static BOOL CALLBACK loadSystemVersion(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    const UINT n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n||n+13>=MAX_PATH) return FALSE;
    std::wstring absolute(path,n);absolute+=L"\\version.dll";
    systemVersion=LoadLibraryExW(absolute.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    return systemVersion!=nullptr;
}
static FARPROC original(const char *name) {
    if(!InitOnceExecuteOnce(&versionOnce,loadSystemVersion,nullptr,nullptr)) return nullptr;
    FARPROC proc=GetProcAddress(systemVersion,name);
    if(!proc) SetLastError(ERROR_PROC_NOT_FOUND);
    return proc;
}
#define FWD(ret,name,args,callargs) extern "C" ret WINAPI BSI_##name args { \
    using Fn=ret (WINAPI*)args; auto fn=reinterpret_cast<Fn>(original(#name));return fn?fn callargs:ret(0); }
FWD(BOOL,GetFileVersionInfoByHandle,(DWORD a,HANDLE b,DWORD c,LPVOID d),(a,b,c,d))
FWD(DWORD,GetFileVersionInfoSizeA,(LPCSTR a,LPDWORD b),(a,b))
FWD(DWORD,GetFileVersionInfoSizeW,(LPCWSTR a,LPDWORD b),(a,b))
FWD(DWORD,GetFileVersionInfoSizeExA,(DWORD a,LPCSTR b,LPDWORD c),(a,b,c))
FWD(DWORD,GetFileVersionInfoSizeExW,(DWORD a,LPCWSTR b,LPDWORD c),(a,b,c))
FWD(BOOL,GetFileVersionInfoA,(LPCSTR a,DWORD b,DWORD c,LPVOID d),(a,b,c,d))
FWD(BOOL,GetFileVersionInfoW,(LPCWSTR a,DWORD b,DWORD c,LPVOID d),(a,b,c,d))
FWD(BOOL,GetFileVersionInfoExA,(DWORD a,LPCSTR b,DWORD c,DWORD d,LPVOID e),(a,b,c,d,e))
FWD(BOOL,GetFileVersionInfoExW,(DWORD a,LPCWSTR b,DWORD c,DWORD d,LPVOID e),(a,b,c,d,e))
FWD(BOOL,VerQueryValueA,(LPCVOID a,LPCSTR b,LPVOID *c,PUINT d),(a,b,c,d))
FWD(BOOL,VerQueryValueW,(LPCVOID a,LPCWSTR b,LPVOID *c,PUINT d),(a,b,c,d))
FWD(DWORD,VerInstallFileA,(DWORD a,LPSTR b,LPSTR c,LPSTR d,LPSTR e,LPSTR f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h))
FWD(DWORD,VerInstallFileW,(DWORD a,LPWSTR b,LPWSTR c,LPWSTR d,LPWSTR e,LPWSTR f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h))
FWD(DWORD,VerFindFileA,(DWORD a,LPSTR b,LPSTR c,LPSTR d,LPSTR e,PUINT f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h))
FWD(DWORD,VerFindFileW,(DWORD a,LPWSTR b,LPWSTR c,LPWSTR d,LPWSTR e,PUINT f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h))
FWD(DWORD,VerLanguageNameA,(DWORD a,LPSTR b,DWORD c),(a,b,c))
FWD(DWORD,VerLanguageNameW,(DWORD a,LPWSTR b,DWORD c),(a,b,c))

static std::wstring value(const std::wstring &path,const wchar_t *key,const wchar_t *fallback=L"") {
    wchar_t text[32768];GetPrivateProfileStringW(L"RPC",key,fallback,text,32768,path.c_str());return text;
}
static std::string utf8Hex(const std::wstring &path,const wchar_t *key) {
    const auto encoded=value(path,key);std::string result;
    if(encoded.size()%2) return {};
    const auto nibble=[](wchar_t c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    for(size_t i=0;i<encoded.size();i+=2){const int a=nibble(encoded[i]),b=nibble(encoded[i+1]);if(a<0||b<0)return {};result+=char((a<<4)|b);}
    return result;
}
static GifRpcPayload::Config config(const std::wstring &path) {
    GifRpcPayload::Config c;
    const auto id=value(path,L"AppId");
    c.appId.assign(id.begin(),id.end());
    c.enabled=value(path,L"Schema")==L"1"&&value(path,L"Enabled")==L"1"&&id.size()>=17&&id.size()<=20
        &&std::all_of(id.begin(),id.end(),[](wchar_t ch){return ch>='0'&&ch<='9';});
    c.timestamp=value(path,L"Timestamp",L"1")==L"1";
    c.interval=std::clamp<unsigned>(GetPrivateProfileIntW(L"RPC",L"Interval",5,path.c_str()),5,60);
    c.state=utf8Hex(path,L"State");c.largeImage=utf8Hex(path,L"LargeImage");c.largeText=utf8Hex(path,L"LargeText");
    c.smallImage=utf8Hex(path,L"SmallImage");c.smallText=utf8Hex(path,L"SmallText");
    c.label1=utf8Hex(path,L"Button1Label");c.url1=utf8Hex(path,L"Button1Url");
    c.label2=utf8Hex(path,L"Button2Label");c.url2=utf8Hex(path,L"Button2Url");
    const unsigned count=std::min<unsigned>(10,GetPrivateProfileIntW(L"RPC",L"DetailCount",0,path.c_str()));
    for(unsigned i=0;i<count;++i){const auto key=L"Detail"+std::to_wstring(i);c.details.push_back(utf8Hex(path,key.c_str()));}
    return c;
}
static bool frame(HANDLE pipe,unsigned opcode,const std::string &json) {
    if(json.size()>65536) return false;
    std::string bytes(8,'\0');const unsigned size=static_cast<unsigned>(json.size());
    for(unsigned i=0;i<4;++i){bytes[i]=char(opcode>>(i*8));bytes[4+i]=char(size>>(i*8));}
    bytes+=json;DWORD written=0;
    return WriteFile(pipe,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
}
static unsigned little(const std::string &s,size_t at) {
    unsigned v=0;for(unsigned i=0;i<4;++i)v|=unsigned(static_cast<unsigned char>(s[at+i]))<<(i*8);return v;
}
static void diagnostic(const std::wstring &path, const char *message) noexcept {
    HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA|GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size{};
    if(GetFileSizeEx(file,&size)&&size.QuadPart>65536){CloseHandle(file);return;}
    DWORD written=0;WriteFile(file,message,static_cast<DWORD>(strlen(message)),&written,nullptr);CloseHandle(file);
}
static unsigned workerImpl(void *module) {
    // Prevent explicit unload while our worker is running. No joins, waits,
    // LoadLibrary, IPC or C++ thread objects execute inside DllMain.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&BsiGifRpcSchema),&pinned)) return 0;
    wchar_t filename[32768];const DWORD length=GetModuleFileNameW(static_cast<HMODULE>(module),filename,32768);
    if(!length||length==32768) return 0;
    std::wstring settings(filename,length);settings.resize(settings.find_last_of(L"\\/")+1);settings+=L"bsi-gif-rpc.ini";
    std::wstring logPath=settings.substr(0,settings.find_last_of(L"\\/")+1)+L"bsi-gif-rpc.log";
    diagnostic(logPath,"BSI GIF RPC V2: worker started; proxy forwarding remains independent.\r\n");
    if(WaitForSingleObject(stopEvent,1500)!=WAIT_TIMEOUT)return 0;
    HANDLE pipe=INVALID_HANDLE_VALUE;std::string activeId,buffer;bool ready=false;unsigned index=0,nonce=0;
    ULONGLONG next=0,handshakeDeadline=0,reload=0; GifRpcPayload::Config current;const auto start=static_cast<std::int64_t>(std::time(nullptr));
    while(WaitForSingleObject(stopEvent,250)==WAIT_TIMEOUT) {
        if(GetTickCount64()>=reload){current=config(settings);reload=GetTickCount64()+5000;}
        const auto &c=current;
        if(!c.enabled||c.appId!=activeId){if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;ready=false;buffer.clear();activeId=c.appId;}
        if(!c.enabled)continue;
        if(pipe==INVALID_HANDLE_VALUE) {
            for(unsigned i=0;i<10;++i) {
                const auto name=L"\\\\.\\pipe\\discord-ipc-"+std::to_wstring(i);
                pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
                if(pipe==INVALID_HANDLE_VALUE)continue;
                DWORD mode=PIPE_READMODE_BYTE|PIPE_NOWAIT;
                if(!SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr)||!frame(pipe,0,"{\"v\":1,\"client_id\":"+GifRpcPayload::quote(c.appId)+"}")){
                    CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;continue;
                }
                ready=false;buffer.clear();handshakeDeadline=GetTickCount64()+10000;next=0;break;
            }
            if(pipe==INVALID_HANDLE_VALUE){WaitForSingleObject(stopEvent,1000);continue;}
        }
        bool valid=true;DWORD available=0;
        if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))valid=false;
        if(valid&&available) {
            char bytes[8192];DWORD count=0;
            if(!ReadFile(pipe,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr))valid=false;
            else buffer.append(bytes,count);
            if(buffer.size()>131088)valid=false;
        }
        while(valid&&buffer.size()>=8) {
            const unsigned op=little(buffer,0),size=little(buffer,4);
            if(size>65536){valid=false;break;}
            if(buffer.size()<size+8)break;
            const auto body=buffer.substr(8,size);buffer.erase(0,size+8);
            if(op==2){valid=false;break;}
            if(op==3&&!frame(pipe,4,body)){valid=false;break;}
            if(op==1&&body.find("\"READY\"")!=std::string::npos)ready=true;
            if(op==1&&body.find("\"ERROR\"")!=std::string::npos)OutputDebugStringW(L"BSI GIF RPC: Discord rejected an RPC request; check application ID, assets and permissions.\n");
        }
        if(!ready&&GetTickCount64()>handshakeDeadline)valid=false;
        if(valid&&ready&&GetTickCount64()>=next){valid=frame(pipe,1,GifRpcPayload::activity(c,index++,GetCurrentProcessId(),start,++nonce));next=GetTickCount64()+c.interval*1000ULL;}
        if(!valid){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;ready=false;buffer.clear();WaitForSingleObject(stopEvent,1000);}
    }
    if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);
    return 0;
}
static unsigned __stdcall worker(void *module) noexcept {
    // A malformed INI/allocation error must not unwind into the emulator.
    try { return workerImpl(module); }
    catch (...) { OutputDebugStringW(L"BSI GIF RPC: RPC worker stopped after a C++ exception.\n"); return 0; }
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        // Keep CRT thread notifications: this proxy uses the static CRT (/MT).
        wchar_t host[32768];const DWORD n=GetModuleFileNameW(nullptr,host,32768);
        if(!n||n==32768)return TRUE;
        const wchar_t *base=host;for(const wchar_t *p=host;*p;++p)if(*p==L'\\'||*p==L'/')base=p+1;
        if(_wcsicmp(base,L"HD-Player.exe")!=0)return TRUE;
        stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(stopEvent){uintptr_t thread=_beginthreadex(nullptr,0,worker,module,0,nullptr);if(thread)CloseHandle(reinterpret_cast<HANDLE>(thread));}
    } else if(reason==DLL_PROCESS_DETACH&&stopEvent)SetEvent(stopEvent);
    return TRUE;
}
