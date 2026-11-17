#include "cli/cli_console.h"

#include <QtGlobal>
#include <QString>

#include <cstdio>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace CliConsole {

void attach()
{
#ifdef Q_OS_WIN
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD type = hout ? GetFileType(hout) : FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hout), _O_TEXT);
        if (fd >= 0) {
            FILE* fp = _fdopen(fd, "w");
            if (fp) {
                *stdout = *fp;
                setvbuf(stdout, nullptr, _IONBF, 0);
            }
        }
        HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
        if (herr && GetFileType(herr) == type) {
            const int efd = _open_osfhandle(reinterpret_cast<intptr_t>(herr), _O_TEXT);
            if (efd >= 0) {
                FILE* fp = _fdopen(efd, "w");
                if (fp) {
                    *stderr = *fp;
                    setvbuf(stderr, nullptr, _IONBF, 0);
                }
            }
        }
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void writeLine(const char* text)
{
    if (!text)
        return;
    fprintf(stdout, "%s\n", text);
    fflush(stdout);
}

void writeLine(const QString& text)
{
    const QByteArray utf8 = text.toUtf8();
    fprintf(stdout, "%s\n", utf8.constData());
    fflush(stdout);
}

} // namespace CliConsole
