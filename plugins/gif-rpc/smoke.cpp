// Isolated test host. Its filename deliberately exercises the real HD-Player
// worker path; the customizer's own selftest previously skipped that path.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <vector>

int wmain(int argc, wchar_t **argv) {
    if(argc!=2)return 2;
    HMODULE proxy=LoadLibraryW(argv[1]);
    if(!proxy){std::printf("LoadLibrary error: %lu\n",GetLastError());return 3;}
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
    // Includes delayed initialization, INI parsing, missing/available Discord
    // pipe handling and normal process exit, without needing an emulator/VM.
    Sleep(3500);
    std::puts("PASS: x64 proxy names/ordinals, real version queries and HD-Player-named worker survive.");
    return 0;
}
