#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Harmless startup-only fixture. No networking, hooks, threads or file writes.
// The no-export build must be rejected, not produce an empty import Windows skips.
#ifdef BSI_PROBE_EXPORT
extern "C" __declspec(dllexport) void BsiProbeInitialize()
{
    SetEnvironmentVariableW(L"BSI_DLLSTARTUP_EXPORTED_INIT", L"1");
}
#endif
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
#ifdef BSI_PROBE_DECLINE
        return FALSE;
#else
        SetEnvironmentVariableW(L"BSI_DLLSTARTUP_ATTACHED", L"1");
#endif
    }
    return TRUE;
}
