// Static imports are resolved before main, unlike the dynamic smoke host.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <cstdio>
int main() {
    wchar_t path[MAX_PATH];
    const UINT n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n||n+14>=MAX_PATH)return 2;
    wcscat_s(path,L"\\kernel32.dll");
    DWORD unused=0;
    if(!GetFileVersionInfoSizeW(path,&unused))return 3;
    std::puts("PASS: statically imported version.dll initialized before main on a 128 KiB host stack.");
    return 0;
}
