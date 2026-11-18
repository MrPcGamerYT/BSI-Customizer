#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// No dependency on the probe DLL is linked into this host. The patched import
// alone must attach it, with no Python, Node, PowerShell or BSI process running.
int main()
{
    wchar_t value[8]{};
    if (!GetModuleHandleW(L"bsi-startup-probe.dll"))
        return 7;
    if (GetEnvironmentVariableW(L"BSI_DLLSTARTUP_ATTACHED", value, 8) != 1 || value[0] != L'1')
        return 8;
    if (GetEnvironmentVariableW(L"BSI_DLLSTARTUP_EXPORTED_INIT", value, 8))
        return 9;
    return 0;
}
