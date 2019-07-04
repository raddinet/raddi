#include "download.h"

#include <cctype>
#include <string>

#include "../common/log.h"
#include "../node/server.h"

Download::Download (const wchar_t * proxy, const wchar_t * user_agent) : provider ("downloader") {
    auto proxy_type = WINHTTP_ACCESS_TYPE_NO_PROXY;
    if (proxy) {
        if (proxy [0] == L'\0') {
            proxy = WINHTTP_NO_PROXY_NAME;
            proxy_type = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
        } else {
            proxy_type = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        }
    }

    this->internet = WinHttpOpen (user_agent, proxy_type, proxy, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (this->internet) {
        if (WinHttpSetStatusCallback (internet, Download::Context::HttpHandlerFwd,
                                      WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS, NULL) != WINHTTP_INVALID_STATUS_CALLBACK) {
            return;
        }
    }
    throw raddi::log::exception (raddi::component::main, 10);
}

Download::~Download () {
    WinHttpSetStatusCallback (this->internet, NULL, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, NULL);
    WinHttpCloseHandle (this->internet);
}

bool Download::download (wchar_t * url, Callback * callback) {
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

        if (auto connection = WinHttpConnect (this->internet, components.lpszHostName, components.nPort, 0)) {

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
                        this->report (raddi::log::level::error, 0x24);
                    }
                }

                if (WinHttpSendRequest (request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                                        reinterpret_cast <DWORD_PTR> (new Context (connection, this, callback,
                                                                                   components.lpszScheme + std::wstring (L"://") + components.lpszHostName)))) {
                    
                    InterlockedIncrement (&this->pending);
                    return true;

                } else {
                    this->report (raddi::log::level::error, 0x23);
                    WinHttpCloseHandle (request);
                    WinHttpCloseHandle (connection);
                }
            } else {
                this->report (raddi::log::level::error, 0x22, components.lpszHostName, (unsigned int) components.nPort, components.lpszUrlPath);
                WinHttpCloseHandle (connection);
            }
        } else
            this->report (raddi::log::level::error, 0x21, components.lpszHostName, (unsigned int) components.nPort);
    } else
        this->report (raddi::log::level::error, 0x20, url);

    return false;
}

bool Download::done () {
    return InterlockedCompareExchange (&this->pending, 0, 0) == 0;
}

void Download::Context::HttpHandler (HINTERNET request, DWORD code, char * data, DWORD size) {
    char buffer [8193];
    switch (code) {

        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        case WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION:
            this->report (raddi::log::level::error, 0x26);
            break;

        // successes return early here
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if (WinHttpReceiveResponse (request, NULL))
                return;

            this->report (raddi::log::level::error, 0x25, "start");
            break;

        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            size = sizeof buffer;
            if (WinHttpQueryHeaders (request, WINHTTP_QUERY_STATUS_CODE, NULL, buffer, &size, WINHTTP_NO_HEADER_INDEX)) {

                auto status = std::wcstoul (reinterpret_cast <const wchar_t *> (buffer), nullptr, 10);
                if (status == 200) {
                    if (WinHttpQueryDataAvailable (request, NULL))
                        return;

                    this->report (raddi::log::level::error, 0x25, "query");
                } else
                    this->report (raddi::log::level::error, 0x27, status);
            } else
                this->report (raddi::log::level::error, 0x25, "header");
            break;

        // NOTE: I really need to stop writing code like this

        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            if (size) {
                data [size] = '\0';

                // separate rows
                if (auto p = data) {
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
                            p [length] = '\0';

                            if (!this->callback->downloaded (this->identity.instance, p))
                                goto cancelled;

                            p += length;
                        }
                    }
                }

        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
                if (WinHttpReadData (request, buffer, sizeof buffer - 1, NULL))
                    return;

                this->report (raddi::log::level::error, 0x25, "read");
            }
    }

cancelled:
    WinHttpCloseHandle (request);
    delete this;
}

Download::Context::~Context () {
    WinHttpCloseHandle (this->connection);
    InterlockedDecrement (&this->download->pending);
}
