// A dependent DLL that imports version APIs and invokes them under loader lock.
// A LoadLibrary-only host does not exercise this ordering.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
static BOOL passed=FALSE;
extern "C" __declspec(dllexport) BOOL BsiLoaderTestPassed() { return passed; }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH];
        const UINT n=GetSystemDirectoryW(path,MAX_PATH);
        if(!n||n+14>=MAX_PATH)return FALSE;
        wcscat_s(path,L"\\kernel32.dll");
        DWORD unused=0;
        const DWORD size=GetFileVersionInfoSizeW(path,&unused);
        void *data=size?HeapAlloc(GetProcessHeap(),0,size):nullptr;
        if(data) {
            LPVOID info=nullptr;UINT length=0;
            passed=GetFileVersionInfoW(path,0,size,data)
                &&VerQueryValueW(data,L"\\",&info,&length)&&info&&length>=sizeof(VS_FIXEDFILEINFO);
            HeapFree(GetProcessHeap(),0,data);
        }
        return passed;
    }
    return TRUE;
}
