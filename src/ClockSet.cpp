#include "ClockSet.h"

#include <QCoreApplication>
#include <QDir>
#include <QObject>

#ifdef Q_OS_WIN
// windows.h must come first: shellapi.h needs its types.
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace ClockSet
{
#ifdef Q_OS_WIN

bool supported()
{
    return true;
}

int runAdjust(qint64 ms)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    const LONGLONG shifted = static_cast<LONGLONG>(u.QuadPart) + static_cast<LONGLONG>(ms) * 10000LL; // 100 ns units
    if (shifted <= 0)
        return 2;
    u.QuadPart = static_cast<ULONGLONG>(shifted);
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&ft, &st))
        return 3;
    return SetSystemTime(&st) ? 0 : 1;
}

bool requestAdjust(qint64 ms, QString *error)
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString args = QStringLiteral("--adjust-clock-ms %1").arg(ms);
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = reinterpret_cast<LPCWSTR>(exe.utf16());
    sei.lpParameters = reinterpret_cast<LPCWSTR>(args.utf16());
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess)
    {
        if (error)
            *error = QObject::tr("Windows did not allow it (the administrator prompt was declined?).");
        return false;
    }
    WaitForSingleObject(sei.hProcess, 30000);
    DWORD code = 99;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0)
    {
        if (error)
            *error = QObject::tr("Setting the clock failed (code %1).").arg(code);
        return false;
    }
    return true;
}

#else

bool supported()
{
    return false;
}

int runAdjust(qint64)
{
    return 1;
}

bool requestAdjust(qint64, QString *error)
{
    if (error)
        *error = QObject::tr("Setting the clock from here is only available on Windows.");
    return false;
}

#endif
} // namespace ClockSet
