//
// Win32 compatibility shims for the Westwood Online lobby code.
// See wolcompat.h. Only built on non-Windows targets.
//
// This file is part of Vanilla Conquer and is licensed under the GPLv3.
//
#include "wolcompat.h"

#ifndef _WIN32

#include "function.h"
#include "common/mssleep.h"

#include <chrono>
#include <time.h>

void GetSystemTime(SYSTEMTIME* st)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    st->wYear = (WORD)(t.tm_year + 1900);
    st->wMonth = (WORD)(t.tm_mon + 1);
    st->wDayOfWeek = (WORD)t.tm_wday;
    st->wDay = (WORD)t.tm_mday;
    st->wHour = (WORD)t.tm_hour;
    st->wMinute = (WORD)t.tm_min;
    st->wSecond = (WORD)t.tm_sec;
    st->wMilliseconds = 0;
}

DWORD timeGetTime(void)
{
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return (DWORD)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

void Sleep(DWORD ms)
{
    ms_sleep(ms);
}

short GetAsyncKeyState(int key)
{
    if (Keyboard != NULL && Keyboard->Down((unsigned short)key)) {
        return (short)0x8000;
    }
    return 0;
}

#endif // !_WIN32
