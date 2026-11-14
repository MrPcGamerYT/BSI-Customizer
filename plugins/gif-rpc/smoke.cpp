// Isolated test host. Its filename deliberately exercises the real HD-Player
// worker path; the customizer's own selftest previously skipped that path.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <cstdint>

int wmain(int argc, wchar_t **argv) {
    if(argc!=2)return 2;
    HMODULE proxy=LoadLibraryW(argv[1]);
    if(!proxy){std::printf("LoadLibrary error: %lu\n",GetLastError());return 3;}
    // Release immediately, before the worker has necessarily begun executing.
    const auto schemaAddress=GetProcAddress(proxy,"BsiGifRpcSchema");
    FreeLibrary(proxy);
    HMODULE retained=nullptr;
    if(!schemaAddress||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(schemaAddress),&retained)||retained!=proxy)return 13;
    wchar_t path[MAX_PATH];UINT n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n||n+20>=MAX_PATH)return 4;
    wcscat_s(path,L"\\version.dll");
    HMODULE system=LoadLibraryW(path);if(!system)return 5;
    const char *names[]={"GetFileVersionInfoA","GetFileVersionInfoByHandle","GetFileVersionInfoExA","GetFileVersionInfoExW",
        "GetFileVersionInfoSizeA","GetFileVersionInfoSizeExA","GetFileVersionInfoSizeExW","GetFileVersionInfoSizeW",
        "GetFileVersionInfoW","VerFindFileA","VerFindFileW","VerInstallFileA","VerInstallFileW",
        "VerLanguageNameA","VerLanguageNameW","VerQueryValueA","VerQueryValueW"};
    for(WORD i=0;i<17;++i){
        const auto ordinal=MAKEINTRESOURCEA(i+1);
        if(!GetProcAddress(proxy,names[i])||GetProcAddress(proxy,names[i])!=GetProcAddress(proxy,ordinal)
            ||GetProcAddress(system,names[i])!=GetProcAddress(system,ordinal)){
            std::printf("Export/ordinal mismatch: %s @%u\n",names[i],i+1);return 6;
        }
        // Verify even undocumented APIs without guessing a signature or invoking
        // them with unsafe arguments: each entry must be exactly a RIP-relative
        // indirect tail jump to the corresponding original function.
        const auto *code=reinterpret_cast<const unsigned char *>(GetProcAddress(proxy,names[i]));
        if(code[0]!=0xff||code[1]!=0x25)return 14;
        std::int32_t displacement=0;std::memcpy(&displacement,code+2,sizeof(displacement));
        FARPROC target=nullptr;std::memcpy(&target,code+6+displacement,sizeof(target));
        if(target!=GetProcAddress(system,names[i]))return 15;
    }
    using Size=DWORD(WINAPI*)(LPCWSTR,LPDWORD);
    using Info=BOOL(WINAPI*)(LPCWSTR,DWORD,DWORD,LPVOID);
    using Query=BOOL(WINAPI*)(LPCVOID,LPCWSTR,LPVOID*,PUINT);
    DWORD unused=0;
    DWORD size=reinterpret_cast<Size>(GetProcAddress(proxy,"GetFileVersionInfoSizeW"))(path,&unused);
    if(!size)return 7;
    std::vector<BYTE> data(size);
    if(!reinterpret_cast<Info>(GetProcAddress(proxy,"GetFileVersionInfoW"))(path,0,size,data.data()))return 8;
    LPVOID result=nullptr;UINT length=0;
    if(!reinterpret_cast<Query>(GetProcAddress(proxy,"VerQueryValueW"))(data.data(),L"\\",&result,&length)||!length)return 9;
    // The proxy is already mapped when Windows resolves this DLL's imports.
    // Its DllMain calls version APIs before LoadLibrary returns.
    HMODULE dependent=LoadLibraryW(BSI_LOADER_TEST);
    using Passed=BOOL(*)();
    const auto passed=dependent?reinterpret_cast<Passed>(GetProcAddress(dependent,"BsiLoaderTestPassed")):nullptr;
    if(!passed||!passed()){std::printf("Dependent DLL init failed: %lu\n",GetLastError());return 10;}
    FreeLibrary(dependent);

    // Compare error behavior to System32 concurrently. In particular the proxy
    // must not change last-error merely by resolving its forwarding target.
    auto proxySize=reinterpret_cast<Size>(GetProcAddress(proxy,"GetFileVersionInfoSizeW"));
    auto systemSize=reinterpret_cast<Size>(GetProcAddress(system,"GetFileVersionInfoSizeW"));
    std::atomic<bool> equivalent{true};
    std::vector<std::thread> threads;
    for(unsigned t=0;t<8;++t)threads.emplace_back([&] {
        for(unsigned i=0;i<100;++i) {
            DWORD a=0,b=0;
            SetLastError(0x12345678);const auto sa=systemSize(path,&a);const auto ea=GetLastError();
            SetLastError(0x12345678);const auto sb=proxySize(path,&b);const auto eb=GetLastError();
            if(sa!=sb||a!=b||ea!=eb)equivalent=false;
            SetLastError(0x12345678);const auto fa=systemSize(L"Z:\\bsi-nonexistent-file.dll",&a);const auto e1=GetLastError();
            SetLastError(0x12345678);const auto fb=proxySize(L"Z:\\bsi-nonexistent-file.dll",&b);const auto e2=GetLastError();
            if(fa!=fb||a!=b||e1!=e2)equivalent=false;
        }
    });
    for(auto &thread:threads)thread.join();
    if(!equivalent)return 11;

    // Exercise a stack-passed fifth argument (x64 register-only tests miss it).
    using InfoEx=BOOL(WINAPI*)(DWORD,LPCWSTR,DWORD,DWORD,LPVOID);
    const auto infoEx=reinterpret_cast<InfoEx>(GetProcAddress(proxy,"GetFileVersionInfoExW"));
    if(!infoEx(FILE_VER_GET_NEUTRAL,path,0,size,data.data()))return 12;
    // Includes delayed initialization, INI parsing, missing/available Discord
    // pipe handling and normal process exit, without needing an emulator/VM.
    Sleep(3500);
    std::puts("PASS: x64 names/ordinals, dependent DllMain, concurrent error parity, stack arguments, unload and worker startup.");
    return 0;
}
