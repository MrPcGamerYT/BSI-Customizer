#include "CrashDiagnostics.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
namespace {
wchar_t crashPath[32768] = {};
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
LONG WINAPI recordException(EXCEPTION_POINTERS *exception)
{
    // Avoid Qt, heap allocation, locks and minidumps in the failing process.
    const HANDLE file = CreateFileW(crashPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        char line[256];
        const int length = std::snprintf(line, sizeof(line),
            "Unhandled Windows exception: code=0x%08lX address=%p\r\n",
            exception->ExceptionRecord->ExceptionCode,
            exception->ExceptionRecord->ExceptionAddress);
        if (length > 0 && length < int(sizeof(line))) {
            DWORD written = 0;
            WriteFile(file, line, DWORD(length), &written, nullptr);
            FlushFileBuffers(file);
        }
        CloseHandle(file);
    }
    // Do not pretend an access violation is recoverable or hide it from WER.
    return previousFilter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}
}
#endif

void installCrashDiagnostics()
{
#ifdef Q_OS_WIN
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/diagnostics");
    if (!QDir().mkpath(directory)) return;
    const QString path = directory + QStringLiteral("/last-crash.log");
    if (path.size() >= int(sizeof(crashPath) / sizeof(wchar_t))) return;
    path.toWCharArray(crashPath);
    crashPath[path.size()] = 0;
    // Bound the log and keep the previous crash across ordinary restarts.
    QFile log(path);
    if (log.size() > 64 * 1024) {
        QFile::remove(path + QStringLiteral(".previous"));
        log.rename(path + QStringLiteral(".previous"));
        log.setFileName(path);
    }
    if (log.open(QIODevice::Append)) {
        log.write("Starting BSI Customizer ");
        log.write(QCoreApplication::applicationVersion().toUtf8());
        log.write("\r\n");
    }
    previousFilter = SetUnhandledExceptionFilter(recordException);
#endif
}
