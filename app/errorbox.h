#ifndef ERRORBOX_H
#define ERRORBOX_H

#include <windows.h>
#include "../common/log.h"

template <typename... Args>
int ErrorBox (HWND hWnd, raddi::log::level level, unsigned int id, Args ...args);

template <typename... Args>
int ErrorBox (unsigned int id, Args ...args) {
    return ErrorBox (HWND_DESKTOP, raddi::log::level::error, id, args...);
}
template <typename... Args>
int StopBox (unsigned int id, Args ...args) {
    return ErrorBox (HWND_DESKTOP, raddi::log::level::stop, id, args...);
}

#include "errorbox.tcc"
#endif
