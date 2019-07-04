#ifndef RADDI_DOWNLOAD_H
#define RADDI_DOWNLOAD_H

#include <winsock2.h>
#include <ws2ipdef.h>
#include <winhttp.h>

#include "../common/log.h"

// Download
//  - facility for HTTP-downloading bootstrap text files containing IP addresses
//
class Download
    : raddi::log::provider <raddi::component::main> {

    HINTERNET internet = NULL;
    std::size_t pending = 0;

public:
    Download (const wchar_t * proxy, const wchar_t * user_agent);
    ~Download ();

    // Callback
    //  - classes, that need to receive downloaded text, implement this interface
    //
    class Callback {
    public:

        // downloaded
        //  - override is called for every truncated ASCII line from the downloaded file 'url'
        //  - return: true to continue downloading additional lines for this file
        //            false to stop
        //
        virtual bool downloaded (const std::wstring & url, const char * line) = 0;
    };

    // download
    //  - starts asynchronous download of 'url' file,
    //    to be asynchronously reported to 'callback' class
    //  - WARNING: the action is destructive on the string provided in 'url'
    //  - returns: true when download successfully started, false otherwise
    //
    bool download (wchar_t * url, Callback * callback);

    // done
    //  - returns true when all downloads completed
    //
    bool done ();

private:

    // Context
    //  - context of a single download in progress
    //
    class Context
        : raddi::log::provider <raddi::component::main> {

        HINTERNET    connection;
        Download *   download;
        Callback *   callback;

        void HttpHandler (HINTERNET, DWORD, char *, DWORD);

    public:
        Context (HINTERNET connection, Download * download, Callback * callback, const std::wstring & url)
            : provider ("download", url)
            , connection (connection)
            , download (download)
            , callback (callback) {};

        ~Context ();

        static void WINAPI HttpHandlerFwd (HINTERNET request, DWORD_PTR context, DWORD code, LPVOID data, DWORD size) {
            reinterpret_cast <Download::Context *> (context)->HttpHandler (request, code, static_cast <char *> (data), size);
        }
    };
};

#endif

