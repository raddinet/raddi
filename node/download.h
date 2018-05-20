#ifndef RADDI_DOWNLOAD_H
#define RADDI_DOWNLOAD_H

#include <winsock2.h>
#include <ws2ipdef.h>

// Download
//  - facility for HTTP-downloading bootstrap text files containing IP addresses
//  - TODO: class, inherit log::provider, 

bool InitializeDownload (const wchar_t * proxy, const wchar_t * user_agent);
void TerminateDownload ();
void Download (wchar_t * url);
void DownloadedCallback (const SOCKADDR_INET &);

#endif

