#include "Fixture.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace DllStartupImport;
namespace fs = std::filesystem;
static int failures = 0;
void check(bool b, const char *what)
{
    std::cout << (b ? "PASS: " : "FAIL: ") << what << '\n';
    if (!b) {
        ++failures;
        if (std::getenv("GITHUB_ACTIONS"))
            std::cout << "::error::" << what << '\n';
    }
}
Bytes readFile(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
void writeFile(const fs::path &p, const Bytes &b)
{
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
    if (!f)
        throw std::runtime_error("fixture write failed");
}
#ifdef _WIN32
DWORD launch(const fs::path &file)
{
    // Custom environment: deliberately NO interpreter, Qt, SDK or BSI paths.
    wchar_t root[MAX_PATH]{};
    GetWindowsDirectoryW(root, MAX_PATH);
    std::wstring env = L"PATH=" + std::wstring(root) + L"\\System32";
    env.push_back(0);
    env += L"SystemRoot=" + std::wstring(root);
    env.push_back(0);
    env.push_back(0);
    std::wstring command = L"\"" + file.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = DWORD(sizeof(si));
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(file.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(), file.parent_path().c_str(),
                        &si, &pi))
        return DWORD(-1);
    CloseHandle(pi.hThread);
    const DWORD wait = WaitForSingleObject(pi.hProcess, 10000);
    if (wait != WAIT_OBJECT_0)
        TerminateProcess(pi.hProcess, 99);
    DWORD code = 99;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return code;
}
#endif
int main(int argc, char **argv)
{
    // Test-only data adapter for independent PE parsing of disposable copies.
    if (argc == 6 && std::string(argv[1]) == "--prepare") {
        const auto p = prepare(readFile(fs::u8path(argv[2])), readFile(fs::u8path(argv[3])), argv[4]);
        if (!p.ok) {
            std::cerr << p.error << '\n';
            return 1;
        }
        writeFile(fs::u8path(argv[5]), p.output);
        std::cout << p.importsBefore << " -> " << p.importsAfter << '\n';
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--fixtures") {
        fs::create_directories(fs::u8path(argv[2]));
        writeFile(fs::u8path(argv[2]) / "host.exe", DllStartupFixture::image());
        writeFile(fs::u8path(argv[2]) / "probe.dll", DllStartupFixture::image(true));
        return 0;
    }
    const auto exe = DllStartupFixture::image(), dll = DllStartupFixture::image(true);
    auto p = prepare(exe, dll, "bsi-startup-probe.dll");
    check(p.ok && !p.alreadyApplied && p.importsBefore == 1 && p.importsAfter == 2,
          "append a direct startup dependency with NO export requirement");
    if (!p.ok) {
        std::cerr << p.error << '\n';
        return 1;
    }
    check(std::equal(exe.begin() + 0x400, exe.end(), p.output.begin() + 0x400),
          "all original section/overlay bytes preserved");
    auto repeat = prepare(p.output, dll, "bsi-startup-probe.dll");
    check(repeat.ok && repeat.alreadyApplied && repeat.output == p.output,
          "identical repeat is byte-idempotent");
    check(!prepare(p.output, dll, "different.dll").ok, "different second DLL requires restore");
    for (const auto &name : {"../bad.dll", "bad\\file.dll", "C:bad.dll", "bad name.dll", "bad.exe", "CON.dll",
                             "nul.x.dll", "com1.dll", ".dll", "bad\xC3\xA9.dll"})
        check(!prepare(exe, dll, name).ok, "unsafe/non-ASCII/reserved DLL name refused before any file I/O");
    auto bad = dll;
    DllStartupFixture::put(bad, 0x96, 0x22, 2);
    check(!prepare(exe, bad, "a.dll").ok, "renamed EXE is not accepted as a DLL");
    bad = dll;
    DllStartupFixture::put(bad, 0x84, 0xaa64, 2);
    check(!prepare(exe, bad, "a.dll").ok, "ARM64 DLL refused for AMD64 player");
    for (const auto &original : {exe, dll}) {
        for (std::size_t n : {0u, 63u, 127u, 255u, 511u, 1023u}) {
            auto shortFile = original;
            shortFile.resize(n);
            check(!prepare(shortFile, dll, "a.dll").ok && !prepare(exe, shortFile, "a.dll").ok,
                  "truncated PE input fails closed");
        }
    }
    bad = exe;
    DllStartupFixture::put(bad, 0x98 + 32, 0);
    check(!prepare(bad, dll, "a.dll").ok, "zero alignment refused");
    bad = exe;
    bad[0x1d8] = 1;
    check(!prepare(bad, dll, "a.dll").ok, "occupied section-header padding never overwritten");
    bad = exe;
    DllStartupFixture::put(bad, 0x98 + 60, 0x1d8);
    check(!prepare(bad, dll, "a.dll").ok, "no section-header space refused");
    bad = exe;
    DllStartupFixture::put(bad, 0x98 + 120, 0xfffffff0);
    check(!prepare(bad, dll, "a.dll").ok, "out-of-file import RVA refused");
    bad = exe;
    DllStartupFixture::put(bad, 0x188 + 40 + 12, 0x1000);
    check(!prepare(bad, dll, "a.dll").ok, "overlapping virtual sections refused");
    bad = exe;
    DllStartupFixture::put(bad, 0x98 + 124, 20);
    check(!prepare(bad, dll, "a.dll").ok, "unterminated bounded import directory refused");
    bad = exe;
    DllStartupFixture::put(bad, 0x640, 0x7fffffffffffffffULL, 8);
    check(!prepare(bad, dll, "a.dll").ok, "overflowing thunk/name RVA refused");
    check(!prepare(exe, dll, "KERNEL32.dll").ok, "original dependency is not duplicated");
    bad = exe;
    DllStartupFixture::put(bad, 0x98 + 200, 0x2000);
    DllStartupFixture::put(bad, 0x98 + 204, 8);
    check(!prepare(bad, dll, "a.dll").ok, "unsupported bound-import cache is not silently rewritten");
    bad = p.output;
    bad[0xa00] ^= 1;
    check(!prepare(bad, dll, "bsi-startup-probe.dll").ok,
          "foreign/corrupt marker is not accepted as an installed patch");
    bad = p.output;
    DllStartupFixture::put(bad, 0xa00 + 16 + 20, 0x3000);
    check(!prepare(bad, dll, "bsi-startup-probe.dll").ok, "altered startup thunks fail closed on repeat");
    std::uint32_t random = 0x8e9100ad;
    bool fuzzOk = true;
    for (int iteration = 0; iteration < 2000; ++iteration) {
        auto mutant = exe;
        for (int j = 0; j < 4; ++j) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const auto at = std::size_t(random % mutant.size());
            mutant[at] ^= std::uint8_t(random >> 24);
        }
        const auto saved = mutant;
        const auto candidate = prepare(mutant, dll, "a.dll");
        if (mutant != saved)
            fuzzOk = false;
        if (candidate.ok) {
            const auto again = prepare(candidate.output, dll, "a.dll");
            if (!again.ok || !again.alreadyApplied || again.output != candidate.output)
                fuzzOk = false;
        }
    }
    check(fuzzOk, "2000 deterministic malformed-header/import mutations are bounded, input-immutable and "
                  "idempotent on acceptance");
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 5) {
        check(false, "Windows runtime fixture paths supplied");
        return 1;
    }
    const auto temp =
        fs::temp_directory_path() / (L"bsi-dll-startup-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directory(temp);
    const auto target = temp / L"HD-Player.exe", payload = temp / L"bsi-startup-probe.dll";
    const auto host = readFile(fs::u8path(argv[1]));
    writeFile(target, host);
    check(launch(target) == 7, "unpatched host has no hidden DLL dependency");
    for (int kind = 2; kind <= 3; ++kind) {
        const auto input = readFile(fs::u8path(argv[kind]));
        const auto plan = prepare(host, input, "bsi-startup-probe.dll");
        if (!plan.ok)
            std::cerr << plan.error << '\n';
        check(plan.ok, "prepare real MSVC host and regular DLL");
        if (!plan.ok)
            continue;
        writeFile(target, plan.output);
        writeFile(payload, input);
        for (int run = 0; run < 3; ++run)
            check(launch(target) == 0, "fresh normal process launch attaches DLL, without calling an export "
                                       "or requiring interpreters");
        fs::remove(payload);
        check(launch(target) != 0, "missing required DLL fails startup (documented dependency)");
    }
    const auto declined = readFile(fs::u8path(argv[4]));
    const auto declinedPlan = prepare(host, declined, "bsi-startup-probe.dll");
    check(declinedPlan.ok, "prepare harmless DLL whose initialization declines");
    if (declinedPlan.ok) {
        writeFile(target, declinedPlan.output);
        writeFile(payload, declined);
        check(launch(target) != 0, "DLL initialization failure is not claimed as successful startup");
    }
    fs::remove_all(temp);
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << (failures ? "DLL STARTUP TESTS FAILED" : "DLL STARTUP TESTS PASSED") << '\n';
    return failures ? 1 : 0;
}
