#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

// ORIGINAL TEST HOST, NOT AN EMULATOR. Never links/calls ZLagImportAnchor,
// LoadLibrary or an initializer. Only a patched import may load the DLL.
// Run only in the opt-in disposable Windows job AFTER its outbound block.
int main()
{
    const HMODULE dll = GetModuleHandleW(L"Z-Lag.dll");
    if (!dll || !GetProcAddress(dll, "ZLagImportAnchor"))
        return 7;
    wchar_t exePath[MAX_PATH]{}, dllPath[MAX_PATH]{};
    const DWORD exeLength = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const DWORD dllLength = GetModuleFileNameW(dll, dllPath, MAX_PATH);
    if (!exeLength || exeLength >= MAX_PATH || !dllLength || dllLength >= MAX_PATH)
        return 8;
    auto *leaf = std::wcsrchr(exePath, L'\\');
    if (!leaf || _wcsicmp(leaf + 1, L"HD-Player.exe") != 0)
        return 9;
    *(leaf + 1) = 0;
    wchar_t expected[MAX_PATH]{};
    if (swprintf_s(expected, MAX_PATH, L"%lsZ-Lag.dll", exePath) < 0 || _wcsicmp(expected, dllPath) != 0)
        return 10;

    // The reviewed DLL writes this marker before its HWID request. Do not
    // enable networking, mock authorization, print any HWID or call its API.
    // The license-gated rendering/optimizer path is NOT exercised by this test.
    for (int attempt = 0; attempt < 200; ++attempt) {
        char data[8192]{};
        FILE *file = nullptr;
        if (fopen_s(&file, "zlag.log", "rb") == 0 && file) {
            const auto count = std::fread(data, 1, sizeof(data) - 1, file);
            std::fclose(file);
            data[count] = 0;
            if (std::strstr(data, "loaded by the host's normal import table")) {
                // Let startup run briefly with outbound access blocked, then
                // exit normally. No log contents or device IDs are printed.
                Sleep(500);
                std::puts("PASS: exact supplied DLL loaded; startup worker ran; anchor was not called");
                return 0;
            }
        }
        Sleep(25);
    }
    return 11;
}
