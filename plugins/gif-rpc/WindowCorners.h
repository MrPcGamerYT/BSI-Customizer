#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// No subclassing, hooks, region clipping, border changes or loader-time work.
// Windows 11 DWM may ignore this preference for fullscreen/maximized windows.
namespace GifRpcCorners {
using SetAttribute = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
constexpr DWORD CornerPreferenceAttribute = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
inline int preference(int setting) { return setting < 0 ? 0 : setting ? 2 : 1; }
struct Context { DWORD pid; SetAttribute set; int preference; unsigned applied=0; };
inline BOOL CALLBACK visit(HWND window, LPARAM parameter) {
    auto &c=*reinterpret_cast<Context *>(parameter);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid!=c.pid||!IsWindowVisible(window)||GetWindow(window,GW_OWNER)
        ||(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD))return TRUE;
    if(SUCCEEDED(c.set(window,CornerPreferenceAttribute,&c.preference,sizeof(c.preference))))++c.applied;
    return TRUE;
}
class Controller {
    HMODULE library=nullptr;
    SetAttribute set=nullptr;
    bool touched=false;
public:
    Controller()=default;
    Controller(const Controller &)=delete;
    Controller &operator=(const Controller &)=delete;
    ~Controller() { if(library)FreeLibrary(library); }
    void refresh(int setting) {
        // Existing installations without this setting remain entirely untouched.
        if(setting<0&&!touched)return;
        if(!library) {
            library=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(library)set=reinterpret_cast<SetAttribute>(GetProcAddress(library,"DwmSetWindowAttribute"));
        }
        if(!set)return; // Unsupported Windows: leave window styles unchanged.
        Context context{GetCurrentProcessId(),set,preference(setting)};
        EnumWindows(visit,reinterpret_cast<LPARAM>(&context));
        touched=true;
    }
};
}
