#ifndef ERRORBOX_TCC
#define ERRORBOX_TCC

#include "../common/platform.h"

extern const VS_FIXEDFILEINFO * const version;

template <typename... Args>
int ErrorBox (HWND hWnd, raddi::log::level level, unsigned int id, Args ...args) {
    raddi::log::api_error error;
    raddi::log::report (raddi::component::main, level, id, args...);

    std::wstring string = raddi::log::internal::load_string (raddi::component::main, level, id);

    raddi::log::internal::capture_overriden_api_error (error, args...);
    raddi::log::internal::replace_arguments (string, sizeof... (args), args...);
    raddi::log::internal::replace_argument (string, L"ERR", error, false);

    if (hWnd == HWND_DESKTOP) {
        std::wstring prefix = raddi::log::internal::load_string (raddi::component::main, raddi::log::level::stop, 0);
        wchar_t version_string [16];
        if (::version) {
            std::swprintf (version_string, sizeof version_string / sizeof version_string [0], L"%u.%u",
                           HIWORD (::version->dwProductVersionMS), LOWORD (::version->dwProductVersionMS));
        } else {
            version_string [0] = L'?';
            version_string [1] = L'!';
            version_string [2] = L'\0';
        }

        raddi::log::internal::replace_argument (prefix, L"1", version_string, false);
        raddi::log::internal::replace_argument (prefix, L"2", ARCHITECTURE, false);
        raddi::log::internal::replace_argument (prefix, L"3", BUILD_TIMESTAMP, false);
        raddi::log::internal::replace_argument (prefix, L"ERR", error, false);

        string = prefix + string;
    }

    auto icon = 0;
    switch (level) {
        case raddi::log::level::all:
        case raddi::log::level::note:
        case raddi::log::level::event:
        case raddi::log::level::disabled:
            icon = MB_ICONINFORMATION;
            break;
        case raddi::log::level::data:
        case raddi::log::level::error:
            icon = MB_ICONWARNING;
            break;
        case raddi::log::level::stop:
            icon = MB_ICONERROR;
            break;
    }

    MessageBox (hWnd, string.c_str (), L"RADDI", icon);
    SetLastError (error.code);
    return error.code;
}

#endif
