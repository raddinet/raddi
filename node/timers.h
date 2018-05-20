#ifndef RADDI_TIMERS_H
#define RADDI_TIMERS_H

#include <windows.h>

// Windows API Timer setting helper functions

BOOL SetPeriodicWaitableTimer (HANDLE hTimer, DWORD period);
BOOL ScheduleWaitableTimer (HANDLE hTimer, LONGLONG offset);
BOOL ScheduleTimerToLocalMidnight (HANDLE hTimer, LONGLONG offset = 0);

#endif
