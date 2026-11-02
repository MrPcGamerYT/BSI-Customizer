// Native one-file launcher. Uses only Windows APIs and a statically linked CRT.
// The embedded CAB is data, not an installer. No services, shortcuts, runtime
// installers, registry installation records, or persistent runtime cache.
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <new>
#include <cwctype>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <fdi.h>
#include "payload.h"

namespace {
struct Failure { std::wstring message; };
[[noreturn]] void fail(const std::wstring &message, DWORD code = 0)
{
    throw Failure { message + (code ? L" (Windows error " + std::to_wstring(code) + L")" : L"") };
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct LocalMemory {
    void *value = nullptr;
    ~LocalMemory() { if (value) LocalFree(value); }
};

bool usable(HANDLE h) { return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN; }
void output(DWORD stream, const std::wstring &message)
{
    HANDLE h = GetStdHandle(stream);
    if (!usable(h)) return;
    const int count = WideCharToMultiByte(CP_UTF8, 0, message.data(), int(message.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return;
    std::string text(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, message.data(), int(message.size()), text.data(), count, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(h, text.data(), DWORD(text.size()), &written, nullptr);
}
std::wstring quote(const std::wstring &argument)
{
    // CommandLineToArgvW/MS CRT round-trip, including empty arguments,
    // embedded quotes, Unicode and trailing backslashes.
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(2 * slashes, L'\\');
    return result + L'"';
}
std::wstring lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
    return value;
}
std::wstring fullPath(const std::wstring &path)
{
    DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!size || size > 32760) fail(L"Invalid extraction path", GetLastError());
    std::vector<wchar_t> buffer(size);
    if (!GetFullPathNameW(path.c_str(), size, buffer.data(), nullptr)) fail(L"Cannot resolve extraction path", GetLastError());
    return buffer.data();
}

class PrivateDirectory {
    LocalMemory descriptor;
    SECURITY_ATTRIBUTES attributes { sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE };
public:
    std::wstring path;
    bool removeOnExit = true;
    bool elevated = false;
    PrivateDirectory()
    {
        Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) fail(L"Cannot inspect the process token", GetLastError());
        TOKEN_ELEVATION elevation {};
        DWORD size = 0;
        if (!GetTokenInformation(token.value, TokenElevation, &elevation, sizeof(elevation), &size)) fail(L"Cannot inspect elevation", GetLastError());
        elevated = elevation.TokenIsElevated != 0;
        std::wstring sddl;
        if (elevated) {
            // Owner and inheritable DACL exclude the unelevated user token.
            // Never load elevated DLLs from a writable user-owned _MEI/cache dir.
            sddl = L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
        } else {
            GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
            std::vector<BYTE> data(size);
            if (!size || !GetTokenInformation(token.value, TokenUser, data.data(), size, &size)) fail(L"Cannot inspect the user identity", GetLastError());
            LocalMemory sid;
            if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid,
                                        reinterpret_cast<LPWSTR *>(&sid.value))) fail(L"Cannot encode the user identity", GetLastError());
            const std::wstring user = static_cast<wchar_t *>(sid.value);
            sddl = L"O:" + user + L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + user + L")";
        }
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor.value, nullptr))
            fail(L"Cannot prepare private runtime permissions", GetLastError());
        attributes.lpSecurityDescriptor = descriptor.value;
    }
    static void removeTree(const std::wstring &root) noexcept
    {
        WIN32_FIND_DATAW item {};
        HANDLE search = FindFirstFileW((root + L"\\*").c_str(), &item);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!wcscmp(item.cFileName, L".") || !wcscmp(item.cFileName, L"..")) continue;
                const std::wstring file = root + L"\\" + item.cFileName;
                if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // Do not recurse through junctions/symlinks.
                    if (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(file.c_str());
                    else removeTree(file);
                } else {
                    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
                    DeleteFileW(file.c_str());
                }
            } while (FindNextFileW(search, &item));
            FindClose(search);
        }
        RemoveDirectoryW(root.c_str());
    }
    ~PrivateDirectory() {
        if (!removeOnExit || path.empty()) return;
        for (int attempt = 0; attempt < 6; ++attempt) {
            removeTree(path);
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
            Sleep(50); // antivirus scanners can briefly retain a file handle
        }
        output(STD_ERROR_HANDLE, L"Runtime cleanup deferred: " + path + L"\n");
    }
    void create(const std::wstring &destination)
    {
        if (!destination.empty()) {
            const std::wstring requested = fullPath(destination);
            // Extraction mode never merges into/overwrites an existing folder.
            if (!CreateDirectoryW(requested.c_str(), &attributes)) fail(L"Choose a new extraction directory with an existing parent", GetLastError());
            path = requested;
            return;
        }
        wchar_t buffer[32768] {};
        std::wstring parent;
        if (elevated) {
            const UINT count = GetWindowsDirectoryW(buffer, DWORD(std::size(buffer)));
            if (!count || count >= std::size(buffer)) fail(L"Cannot locate Windows temporary storage", GetLastError());
            parent = std::wstring(buffer) + L"\\Temp";
        } else {
            const DWORD count = GetTempPathW(DWORD(std::size(buffer)), buffer);
            if (!count || count >= std::size(buffer)) fail(L"Cannot locate temporary storage", GetLastError());
            parent = buffer;
        }
        for (int attempt = 0; attempt < 10; ++attempt) {
            BYTE random[16] {};
            if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) fail(L"Cannot generate a private runtime directory");
            const wchar_t digits[] = L"0123456789abcdef";
            std::wstring name = parent + L"\\BSI-Customizer-";
            for (BYTE value : random) { name += digits[value >> 4]; name += digits[value & 15]; }
            if (CreateDirectoryW(name.c_str(), &attributes)) { path = name; return; }
            if (GetLastError() != ERROR_ALREADY_EXISTS) fail(L"Cannot create a private runtime directory", GetLastError());
        }
        fail(L"Cannot allocate a unique runtime directory");
    }
    void ensureParents(const std::wstring &relative)
    {
        size_t at = relative.find(L'\\');
        while (at != std::wstring::npos) {
            const std::wstring folder = path + L"\\" + relative.substr(0, at);
            if (!CreateDirectoryW(folder.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
                fail(L"Cannot create a runtime subdirectory", GetLastError());
            const DWORD flags = GetFileAttributesW(folder.c_str());
            if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
                fail(L"Unsafe runtime subdirectory");
            at = relative.find(L'\\', at + 1);
        }
    }
    SECURITY_ATTRIBUTES *security() { return &attributes; }
};

std::wstring safeEntry(const char *name)
{
    std::wstring result;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p) {
        // The build rejects non-ASCII archive names; no ANSI/code-page ambiguity.
        if (*p < 32 || *p >= 127 || strchr(":*?\"<>|", *p)) fail(L"Invalid runtime archive entry");
        result += *p == '/' ? L'\\' : wchar_t(*p);
        if (result.size() > 220) fail(L"Runtime archive path is too long");
    }
    if (result.empty() || result.front() == L'\\' || result.back() == L'\\') fail(L"Invalid runtime archive path");
    size_t start = 0;
    while (start < result.size()) {
        size_t end = result.find(L'\\', start);
        if (end == std::wstring::npos) end = result.size();
        const auto part = result.substr(start, end - start);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
            fail(L"Unsafe runtime archive path");
        const auto device = lower(part.substr(0, part.find(L'.')));
        if (device == L"con" || device == L"prn" || device == L"aux" || device == L"nul"
            || (device.size() == 4 && (device.substr(0, 3) == L"com" || device.substr(0, 3) == L"lpt")
                && device[3] >= L'0' && device[3] <= L'9')) fail(L"Reserved runtime archive path");
        start = end + 1;
    }
    return result;
}

const BYTE *payload = nullptr;
DWORD payloadSize = 0;
struct Stream { HANDLE file = INVALID_HANDLE_VALUE; size_t position = 0; bool memory = false; };
FNALLOC(cabAlloc) { return HeapAlloc(GetProcessHeap(), 0, cb); }
FNFREE(cabFree) { if (pv) HeapFree(GetProcessHeap(), 0, pv); }
FNOPEN(cabOpen)
{
    (void)oflag; (void)pmode;
    if (strcmp(pszFile, "payload.cab")) return -1;
    auto *stream = new (std::nothrow) Stream;
    if (!stream) return -1;
    stream->memory = true;
    return reinterpret_cast<INT_PTR>(stream);
}
FNREAD(cabRead)
{
    auto *s = reinterpret_cast<Stream *>(hf);
    if (!s || !s->memory || s->position > payloadSize) return UINT(-1);
    const size_t count = std::min<size_t>(cb, payloadSize - s->position);
    std::memcpy(pv, payload + s->position, count);
    s->position += count;
    return UINT(count);
}
FNWRITE(cabWrite)
{
    auto *s = reinterpret_cast<Stream *>(hf);
    if (!s || s->memory) return UINT(-1);
    DWORD written = 0;
    return WriteFile(s->file, pv, cb, &written, nullptr) ? written : UINT(-1);
}
FNCLOSE(cabClose)
{
    std::unique_ptr<Stream> s(reinterpret_cast<Stream *>(hf));
    if (!s) return -1;
    return s->memory || CloseHandle(s->file) ? 0 : -1;
}
FNSEEK(cabSeek)
{
    auto *s = reinterpret_cast<Stream *>(hf);
    if (!s || !s->memory) return -1;
    const int64_t base = seektype == SEEK_SET ? 0 : seektype == SEEK_CUR ? int64_t(s->position)
                           : seektype == SEEK_END ? payloadSize : -1;
    const int64_t next = base + dist;
    if (base < 0 || next < 0 || next > payloadSize) return -1;
    s->position = size_t(next);
    return long(next);
}
struct Extraction {
    PrivateDirectory *directory;
    std::set<std::wstring> names;
    uint64_t total = 0;
    std::wstring error;
};
FNFDINOTIFY(cabNotify)
{
    auto *context = static_cast<Extraction *>(pfdin->pv);
    try {
        if (fdint == fdintCOPY_FILE) {
            const auto relative = safeEntry(pfdin->psz1);
            if (!context->names.insert(lower(relative)).second || context->names.size() > 10000
                || pfdin->cb < 0 || (context->total += uint64_t(pfdin->cb)) > 1024ull * 1024 * 1024)
                fail(L"Invalid or oversized runtime archive");
            context->directory->ensureParents(relative);
            auto stream = std::make_unique<Stream>();
            stream->file = CreateFileW((context->directory->path + L"\\" + relative).c_str(), GENERIC_WRITE, 0,
                                      context->directory->security(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (stream->file == INVALID_HANDLE_VALUE) fail(L"Cannot extract a runtime file", GetLastError());
            return reinterpret_cast<INT_PTR>(stream.release());
        }
        if (fdint == fdintCLOSE_FILE_INFO) return cabClose(pfdin->hf) == 0 ? TRUE : FALSE;
        if (fdint == fdintNEXT_CABINET) fail(L"Multi-part runtime archives are not supported");
        return 0;
    } catch (const Failure &error) { context->error = error.message; }
      catch (...) { context->error = L"Cannot allocate runtime extraction data"; }
    return -1;
}
void verifyPayload(HINSTANCE instance)
{
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(101), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(instance, resource) : nullptr;
    payload = loaded ? static_cast<const BYTE *>(LockResource(loaded)) : nullptr;
    payloadSize = resource ? SizeofResource(instance, resource) : 0;
    if (!payload || !payloadSize || payloadSize > 1024u * 1024 * 1024) fail(L"Missing or invalid embedded runtime");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) fail(L"Cannot verify embedded runtime");
    BYTE digest[32] {};
    const NTSTATUS status = BCryptHash(algorithm, nullptr, 0, const_cast<BYTE *>(payload), payloadSize, digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) fail(L"Cannot hash embedded runtime");
    const char hex[] = "0123456789abcdef";
    std::string actual;
    for (BYTE b : digest) { actual += hex[b >> 4]; actual += hex[b & 15]; }
    if (actual != kPayloadSha256) fail(L"The portable EXE is damaged. Download a fresh copy.");
}
void extract(PrivateDirectory &directory)
{
    ERF error {};
    HFDI fdi = FDICreate(cabAlloc, cabFree, cabOpen, cabRead, cabWrite, cabClose, cabSeek, cpuUNKNOWN, &error);
    if (!fdi) fail(L"Cannot initialize the Windows cabinet reader");
    Extraction context { &directory, {}, 0, {} };
    char name[] = "payload.cab", location[] = "";
    const BOOL ok = FDICopy(fdi, name, location, 0, cabNotify, nullptr, &context);
    FDIDestroy(fdi);
    if (!ok) fail(context.error.empty() ? L"Cannot unpack the embedded runtime (cabinet error " + std::to_wstring(error.erfOper) + L")" : context.error);
    if (!context.names.count(L"bsi-customizer.exe") || !context.names.count(L"qt.conf")) fail(L"The embedded application is incomplete");
}
std::vector<wchar_t> childEnvironment()
{
    LPWCH block = GetEnvironmentStringsW();
    if (!block) fail(L"Cannot read the process environment", GetLastError());
    std::vector<std::wstring> entries;
    for (const wchar_t *p = block; *p; p += wcslen(p) + 1) {
        const std::wstring entry(p);
        const std::wstring key = lower(entry.substr(0, entry.find(L'=')));
        if (key == L"qt_plugin_path" || key == L"qt_qpa_platform_plugin_path" || key == L"qml2_import_path"
            || key == L"qml_import_path" || key == L"bsi_portable" || key == L"bsi_launcher_path") continue;
        entries.push_back(entry);
    }
    FreeEnvironmentStringsW(block);
    entries.push_back(L"BSI_PORTABLE=1");
    // Resolve our actual loaded module, never argv[0], cwd or an inherited path.
    std::vector<wchar_t> module(32768);
    const DWORD length=GetModuleFileNameW(nullptr,module.data(),DWORD(module.size()));
    if (!length || length>=module.size()) fail(L"Cannot locate the portable launcher",GetLastError());
    entries.push_back(L"BSI_LAUNCHER_PATH=" + std::wstring(module.data(),length));
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> result;
    for (const auto &entry : entries) {
        result.insert(result.end(), entry.begin(), entry.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}
DWORD launch(const std::wstring &root, int count, wchar_t **arguments)
{
    const std::wstring exe = root + L"\\BSI-Customizer.exe";
    std::wstring command = quote(exe);
    for (int i = 1; i < count; ++i) command += L" " + quote(arguments[i]);
    auto environment = childEnvironment();
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    if (usable(GetStdHandle(STD_OUTPUT_HANDLE)) || usable(GetStdHandle(STD_ERROR_HANDLE))) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    PROCESS_INFORMATION process {};
    // Preserve the caller's working directory so relative probe/export paths
    // still mean the same thing. Qt's own paths come from the adjacent qt.conf.
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT,
                        environment.data(), nullptr, &startup, &process)) fail(L"Cannot start BSI Customizer", GetLastError());
    Handle thread { process.hThread }, child { process.hProcess };
    if (WaitForSingleObject(child.value, INFINITE) != WAIT_OBJECT_0) fail(L"Cannot wait for BSI Customizer", GetLastError());
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(child.value, &exitCode)) fail(L"Cannot read the application result", GetLastError());
    return exitCode;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    int count = 0;
    LocalMemory argv;
    argv.value = CommandLineToArgvW(GetCommandLineW(), &count);
    auto **arguments = static_cast<wchar_t **>(argv.value);
    bool diagnostic = false;
    for (int i = 1; arguments && i < count; ++i) if (!wcscmp(arguments[i], L"--deployment-test")) diagnostic = true;
    try {
        if (!arguments || !count) fail(L"Cannot read the command line");
        if (count >= 2 && !wcscmp(arguments[1], L"--portable-help")) {
            const std::wstring help = L"BSI Customizer " + std::wstring(kAppVersion)
                + L"\n\nThis is a portable application, not an installer.\n"
                  L"The runtime is unpacked privately and removed on normal exit.\n\n"
                  L"To inspect licenses or use replacement Qt DLLs:\n"
                  L"  --extract-to <new-directory>\n"
                  L"Then run BSI-Customizer.exe from that folder.\n";
            output(STD_OUTPUT_HANDLE, help);
            if (!usable(GetStdHandle(STD_OUTPUT_HANDLE))) MessageBoxW(nullptr, help.c_str(), L"BSI Customizer Portable", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        const bool explicitExtraction = count >= 2 && !wcscmp(arguments[1], L"--extract-to");
        if (explicitExtraction && count != 3) fail(L"Usage: --extract-to <new-directory>");
        verifyPayload(instance);
        PrivateDirectory directory;
        directory.create(explicitExtraction ? arguments[2] : L"");
        extract(directory);
        if (explicitExtraction) {
            directory.removeOnExit = false;
            output(STD_OUTPUT_HANDLE, L"EXTRACTED: " + directory.path + L"\n");
            return 0;
        }
        return int(launch(directory.path, count, arguments));
    } catch (const Failure &error) {
        output(STD_ERROR_HANDLE, L"BSI Customizer Portable: " + error.message + L"\n");
        if (!diagnostic && !usable(GetStdHandle(STD_ERROR_HANDLE))) MessageBoxW(nullptr, error.message.c_str(), L"BSI Customizer Portable", MB_OK | MB_ICONERROR);
    } catch (...) {
        output(STD_ERROR_HANDLE, L"BSI Customizer Portable: unexpected launch error\n");
        if (!diagnostic && !usable(GetStdHandle(STD_ERROR_HANDLE))) MessageBoxW(nullptr, L"Unexpected launch error.", L"BSI Customizer Portable", MB_OK | MB_ICONERROR);
    }
    return 1;
}
