#include "download.h"
#include <winhttp.h>

#include <cctype>
#include <string>

#include "../common/log.h"
#include "../node/server.h"

namespace {
    HINTERNET internet = NULL;
    struct DownloadDetails {
        HINTERNET    connection;
        std::wstring url;
    };
    void WINAPI HttpHandler (HINTERNET, DWORD_PTR, DWORD, LPVOID, DWORD);
}

bool InitializeDownload (const wchar_t * proxy, const wchar_t * user_agent) {
    auto proxy_type = WINHTTP_ACCESS_TYPE_NO_PROXY;
    if (proxy) {
        if (proxy [0] == L'\0') {
            proxy = WINHTTP_NO_PROXY_NAME;
            proxy_type = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
        } else {
            proxy_type = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        }
    }

    internet = WinHttpOpen (user_agent, proxy_type, proxy, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (internet) {
        if (WinHttpSetStatusCallback (internet, HttpHandler,
                                      WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS, NULL) != WINHTTP_INVALID_STATUS_CALLBACK) {
            return true;
        }
    }
    return false;
}

void TerminateDownload () {
    WinHttpSetStatusCallback (internet, NULL, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, NULL);
    WinHttpCloseHandle (internet);
}

void Download (wchar_t * url) {
    URL_COMPONENTS components;
    std::memset (&components, 0, sizeof components);
    components.dwStructSize = sizeof components;

    components.dwSchemeLength = (DWORD) -1;
    components.dwHostNameLength = (DWORD) -1;
    components.dwUserNameLength = (DWORD) -1;
    components.dwPasswordLength = (DWORD) -1;
    components.dwUrlPathLength = (DWORD) -1;

    if (WinHttpCrackUrl (url, 0, 0, &components)
        && components.lpszScheme && components.lpszHostName && components.lpszUrlPath) {

        components.lpszScheme [components.dwSchemeLength] = L'\0';
        components.lpszHostName [components.dwHostNameLength] = L'\0';

        if (auto connection = WinHttpConnect (internet, components.lpszHostName, components.nPort, 0)) {

            components.lpszUrlPath [0] = L'/';
            if (auto request = WinHttpOpenRequest (connection, NULL, components.lpszUrlPath, NULL,
                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   (components.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0)) {
                if (components.lpszUserName) {
                    components.lpszUserName [components.dwUserNameLength] = L'\0';
                    if (components.lpszPassword) {
                        components.lpszPassword [components.dwPasswordLength] = L'\0';
                    }
                    if (!WinHttpSetCredentials (request,
                                                WINHTTP_AUTH_TARGET_SERVER, WINHTTP_AUTH_SCHEME_BASIC,
                                                components.lpszUserName,
                                                components.lpszPassword ? components.lpszPassword : L"",
                                                NULL)) {
                        raddi::log::error (raddi::component::main, 0x24);
                    }
                }
                try {
                    if (!WinHttpSendRequest (request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                                             reinterpret_cast <DWORD_PTR> (new DownloadDetails {
                                                 connection, components.lpszScheme + std::wstring (L"://") + components.lpszHostName
                                                                           }))) {
                        raddi::log::error (raddi::component::main, 0x23);
                        WinHttpCloseHandle (request);
                        WinHttpCloseHandle (connection);
                    }
                } catch (const std::bad_alloc & x) {
                    raddi::log::error (5, -1, GetCurrentThreadId (), x.what ());
                }
            } else {
                raddi::log::error (raddi::component::main, 0x22, components.lpszHostName, (unsigned int) components.nPort, components.lpszUrlPath);
                WinHttpCloseHandle (connection);
            }
        } else
            raddi::log::error (raddi::component::main, 0x21, components.lpszHostName, (unsigned int) components.nPort);
    } else
        raddi::log::error (raddi::component::main, 0x20, url);
}

namespace {
    void WINAPI HttpHandler (HINTERNET request, DWORD_PTR context_, DWORD code, LPVOID data_, DWORD size) {
        auto context = reinterpret_cast <const DownloadDetails *> (context_);
        char buffer [8193];
        switch (code) {

            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            case WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION:
                raddi::log::error (raddi::component::main, 0x26, context->url);
                break;

                // successes return early here
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
                if (WinHttpReceiveResponse (request, NULL))
                    return;

                raddi::log::error (raddi::component::main, 0x25, context->url, "start");
                break;

            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
                size = sizeof buffer;
                if (WinHttpQueryHeaders (request, WINHTTP_QUERY_STATUS_CODE, NULL, buffer, &size, WINHTTP_NO_HEADER_INDEX)) {

                    auto status = std::wcstoul (reinterpret_cast <const wchar_t *> (buffer), nullptr, 10);
                    if (status == 200) {
                        if (WinHttpQueryDataAvailable (request, NULL))
                            return;

                        raddi::log::error (raddi::component::main, 0x25, context->url, "query");
                    } else
                        raddi::log::error (raddi::component::main, 0x27, context->url, status);
                } else
                    raddi::log::error (raddi::component::main, 0x25, context->url, "header");
                break;

            // NOTE: I really need to stop writing code like this

            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
                if (size) {
                    if (const auto data = static_cast <char *> (data_)) {
                        data [size] = '\0';

                        // separate rows
                        auto p = data;
                        while (*p) {

                            // left trim
                            while (std::isspace (*p)) {
                                ++p;
                            }
                            if (*p) {

                                // find new-line or end of the file
                                std::ptrdiff_t length;
                                if (auto e = std::strchr (p, L'\n')) {
                                    length = e - p;
                                } else {
                                    length = &data [size] - p;
                                }

                                // right trim
                                while (length && std::isspace (p [length - 1])) {
                                    --length;
                                }

                                wchar_t ip [48];
                                if (auto n = MultiByteToWideChar (CP_UTF8, 0, p, (int) length,
                                                                  ip, sizeof ip / sizeof ip [0] - 1)) {
                                    if (n >= 9) {
                                        ip [n] = L'\0';

                                        SOCKADDR_INET address;
                                        if (StringToAddress (address, ip)) {
                                            DownloadedCallback (address);
                                            raddi::log::event (raddi::component::main, 0x20, context->url, ip);
                                        } else {
                                            raddi::log::error (raddi::component::main, 0x28, context->url, ip);
                                        }
                                    }
                                }
                                p += length;
                            }
                        }
                    }

            case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
                    if (WinHttpReadData (request, buffer, sizeof buffer - 1, NULL))
                        return;

                    raddi::log::error (raddi::component::main, 0x25, context->url, "read");
                }
        }
        WinHttpCloseHandle (request);
        WinHttpCloseHandle (context->connection);
        delete context;
    }
}