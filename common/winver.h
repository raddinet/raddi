#ifndef RADDI_WINVER_H
#define RADDI_WINVER_H

#include <windows.h>
#include <cstdint>

// winver
//  - Windows version, simplified for out needs and quick comparison
//  - 5=XP, 6=Vista, 7=7, 8=8, 9=8.1, 10=10, 11=11
//
extern std::uint8_t winver;

// winbuild
//  - real Windows build number
//  - this is not subject to compatibility shim lying, thus prefer IsWindowsBuildOrGreater
//
extern std::uint16_t winbuild;

// InitializeWinVer
//  - initializes 'winver' and 'winbuild'
//
void InitializeWinVer ();

// IsWindowsBuildOrGreater 
//  - uses standard VersionHelpers.h API to compare for BUILD number
//
bool IsWindowsBuildOrGreater (WORD wMajorVersion, WORD wMinorVersion, DWORD dwBuildNumber);

#endif
