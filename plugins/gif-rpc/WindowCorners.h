#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>

// No subclassing, hooks, window-style changes or loader-time work. Native DWM
// preferences alone do not reliably round the player's frameless Qt window.
namespace GifRpcCorners {
using SetAttribute = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
constexpr DWORD CornerPreferenceAttribute = 33;
inline int preference(int setting) { return setting < 0 ? 0 : setting ? 2 : 1; }
inline bool eligible(HWND window, DWORD expectedPid) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    return pid==expectedPid&&IsWindowVisible(window)&&!GetWindow(window,GW_OWNER)
        &&!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD);
}
struct Context { DWORD pid; SetAttribute set; int preference; unsigned applied=0; };
inline BOOL CALLBACK visit(HWND window, LPARAM parameter) {
    auto &c=*reinterpret_cast<Context *>(parameter);
    if(!eligible(window,c.pid))return TRUE;
    if(SUCCEEDED(c.set(window,CornerPreferenceAttribute,&c.preference,sizeof(c.preference))))++c.applied;
    return TRUE;
}
class Controller {
    struct Window {
        HWND handle;
        HRGN original=nullptr;
        bool clipped=false;
    };
    std::vector<Window> windows;
    HMODULE library=nullptr;
    SetAttribute set=nullptr;
    bool touched=false;
    int setting=-1;
    static constexpr const wchar_t *ownerKey=L"BSI.GifRpc.CornerRegion.Owner.V2";
    bool owns(const Window &w) const { return IsWindow(w.handle)&&GetPropW(w.handle,ownerKey)==reinterpret_cast<HANDLE>(const_cast<Controller *>(this)); }
    void restore(Window &w) {
        if(w.clipped&&owns(w)) {
            // SetWindowRgn takes ownership only on success. Keep our snapshot.
            HRGN copy=w.original?CreateRectRgn(0,0,0,0):nullptr;
            if(w.original&&(!copy||CombineRgn(copy,w.original,nullptr,RGN_COPY)==ERROR)) {
                if(copy)DeleteObject(copy);return;
            }
            if(!SetWindowRgn(w.handle,copy,TRUE)) { if(copy)DeleteObject(copy);return; }
            w.clipped=false;
        }
    }
    void discard(Window &w) {
        if(w.original)DeleteObject(w.original);
        if(owns(w))RemovePropW(w.handle,ownerKey);
    }
    static bool fullscreen(HWND h,const RECT &r) {
        MONITORINFO m{};m.cbSize=sizeof(m);
        return GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&m)
            &&r.left<=m.rcMonitor.left&&r.top<=m.rcMonitor.top
            &&r.right>=m.rcMonitor.right&&r.bottom>=m.rcMonitor.bottom;
    }
    void apply(HWND h) {
        if(set) { const int p=preference(setting);set(h,CornerPreferenceAttribute,&p,sizeof(p)); }
        if(setting!=1)return;
        auto it=windows.begin();
        for(;it!=windows.end();++it)if(it->handle==h&&owns(*it))break;
        if(it==windows.end()) {
            if(GetPropW(h,ownerKey))return; // Never take over another controller.
            Window w{h};
            w.original=CreateRectRgn(0,0,0,0);
            if(!w.original)return;
            if(GetWindowRgn(h,w.original)==ERROR){DeleteObject(w.original);w.original=nullptr;}
            if(!SetPropW(h,ownerKey,reinterpret_cast<HANDLE>(this))){if(w.original)DeleteObject(w.original);return;}
            // Allocate before publishing the state; no orphaned window property
            // if vector allocation throws (the worker catches C++ exceptions).
            try { windows.push_back(w); }
            catch (...) { RemovePropW(h,ownerKey);if(w.original)DeleteObject(w.original);throw; }
            it=windows.end()-1;
        }
        RECT r{};
        if(!GetWindowRect(h,&r)||IsIconic(h)||IsZoomed(h)||fullscreen(h,r)) { restore(*it);return; }
        const int width=r.right-r.left,height=r.bottom-r.top;
        if(width<=0||height<=0)return;
        using DpiFn=UINT (WINAPI *)(HWND);
        const auto dpiFn=reinterpret_cast<DpiFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
        const UINT dpi=dpiFn?dpiFn(h):96;
        int diameter=MulDiv(24,dpi?dpi:96,96); // 12 logical-pixel corner radius.
        if(diameter>width)diameter=width;if(diameter>height)diameter=height;
        HRGN rounded=CreateRoundRectRgn(0,0,width+1,height+1,diameter,diameter);
        if(!rounded)return;
        if(it->original&&CombineRgn(rounded,rounded,it->original,RGN_AND)==ERROR){DeleteObject(rounded);return;}
        HRGN actual=CreateRectRgn(0,0,0,0);
        if(actual&&GetWindowRgn(h,actual)!=ERROR&&EqualRgn(actual,rounded)) {
            DeleteObject(actual);DeleteObject(rounded);return; // No repeated WM_SIZE/repaint.
        }
        if(actual)DeleteObject(actual);
        if(SetWindowRgn(h,rounded,TRUE))it->clipped=true;
        else DeleteObject(rounded);
    }
    static BOOL CALLBACK update(HWND h,LPARAM p) {
        auto &self=*reinterpret_cast<Controller *>(p);
        // Never allow C++ exceptions to cross a Windows callback boundary.
        try { if(eligible(h,GetCurrentProcessId()))self.apply(h); } catch (...) {}
        return TRUE;
    }
public:
    Controller()=default;
    Controller(const Controller &)=delete;
    Controller &operator=(const Controller &)=delete;
    ~Controller() {
        for(auto &w:windows){restore(w);discard(w);}
        if(library)FreeLibrary(library);
    }
    unsigned refresh(int requested) {
        if(requested<0&&!touched)return 0;
        setting=requested;
        for(auto it=windows.begin();it!=windows.end();) {
            if(!owns(*it)){discard(*it);it=windows.erase(it);continue;}
            if(requested!=1||!eligible(it->handle,GetCurrentProcessId()))restore(*it);
            // On a failed restore keep the snapshot so a later refresh can retry.
            if(requested!=1&&!it->clipped){discard(*it);it=windows.erase(it);}else ++it;
        }
        if(!library) {
            library=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(library)set=reinterpret_cast<SetAttribute>(GetProcAddress(library,"DwmSetWindowAttribute"));
        }
        EnumWindows(update,reinterpret_cast<LPARAM>(this));
        touched=true;
        unsigned count=0;for(const auto &w:windows)if(w.clipped&&owns(w))++count;
        return count;
    }
};
}
