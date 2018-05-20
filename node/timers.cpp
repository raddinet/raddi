#include "timers.h"

BOOL SetPeriodicWaitableTimer (HANDLE hTimer, DWORD period) {
    if (!period)
        return false;

    union {
        LARGE_INTEGER li;
        FILETIME ft;
    } due;

    GetSystemTimeAsFileTime (&due.ft);
    due.li.QuadPart += period * 1'000'0LL;

    return SetWaitableTimer (hTimer, &due.li, period, NULL, NULL, FALSE);
}

BOOL ScheduleWaitableTimer (HANDLE hTimer, LONGLONG offset) {
    union {
        LARGE_INTEGER li;
        FILETIME ft;
    } due;

    GetSystemTimeAsFileTime (&due.ft);
    due.li.QuadPart += offset;

    return SetWaitableTimer (hTimer, &due.li, 0, NULL, NULL, FALSE);
}

BOOL ScheduleTimerToLocalMidnight (HANDLE hTimer, LONGLONG offset) {
    union {
        LARGE_INTEGER li;
        FILETIME ft;
    } due;

    FILETIME ft;
    SYSTEMTIME st;
    GetLocalTime (&st);

    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;

    if (SystemTimeToFileTime (&st, &ft)) {
        if (LocalFileTimeToFileTime (&ft, &due.ft)) {
            due.li.QuadPart += 86400'000'000'0 + offset;
            return SetWaitableTimer (hTimer, &due.li, 0, NULL, NULL, FALSE);
        }
    }
    return FALSE;
}
