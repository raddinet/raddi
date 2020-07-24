raddi/lib directory
===================

To compile, copy third-party libraries here so this directory content looks like:

raddi/lib/sqlite3.c
raddi/lib/include/sqlite3.h
raddi/lib/include/sqlite3ext.h

raddi/lib/include/lzma.h
raddi/lib/include/lzma/base.h
raddi/lib/include/lzma/... - copy all headers

raddi/lib/include/sodium.h
raddi/lib/include/sodium/core.h
raddi/lib/include/sodium/... - copy all headers

x86-32

raddi/lib/x86/Debug/liblzma.lib
 - copy from: xz\windows\vs2017\Debug\Win32\liblzma.lib
raddi/lib/x86/Debug/libsodium.lib

raddi/lib/x86/Release/liblzma.lib - NOTE: DLL
 - copy from: xz\windows\vs2017\Release\Win32\liblzma_dll\liblzma.lib
raddi/lib/x86/Release/libsodium.lib - NOTE: DLL
raddi/lib/x86/Release/sqlite3.lib - NOTE: DLL
 - make using: lib.exe /def:sqlite3.def /out:sqlite3.lib

raddi/lib/x86/Portable/liblzma.lib - NOTE: LTCG
 - copy from: xz\windows\vs2017\Release\Win32\liblzma\liblzma.lib 
raddi/lib/x86/Portable/libsodium.lib - NOTE: LTCG

x86-64

raddi/lib/x64/Debug/liblzma.lib
 - copy from: xz\windows\vs2017\Debug\x64\liblzma.lib
raddi/lib/x64/Debug/libsodium.lib

raddi/lib/x64/Release/liblzma.lib - NOTE: DLL
 - copy from: xz\windows\vs2017\Release\x64\liblzma_dll\liblzma.lib 
raddi/lib/x64/Release/libsodium.lib - NOTE: DLL
raddi/lib/x64/Release/sqlite3.lib - NOTE: DLL
 - make using: lib.exe /def:sqlite3.def /out:sqlite3.lib /machine:x64

raddi/lib/x64/Portable/liblzma.lib - NOTE: LTCG
 - copy from: xz\windows\vs2017\Release\x64\liblzma\liblzma.lib 
raddi/lib/x64/Portable/libsodium.lib - NOTE: LTCG

AArch64
- no SQLite here, we can link Release build against the one that comes with Windows

raddi/lib/a64/Debug/liblzma.lib
 - copy from: xz\windows\vs2017\Debug\ARM64\liblzma.lib
raddi/lib/a64/Debug/libsodium.lib

raddi/lib/a64/Release/liblzma.lib - NOTE: DLL
 - copy from: xz\windows\vs2017\Release\ARM64\liblzma_dll\liblzma.lib 
raddi/lib/a64/Release/libsodium.lib - NOTE: DLL

raddi/lib/a64/Portable/liblzma.lib - NOTE: LTCG
 - copy from: xz\windows\vs2017\Release\ARM64\liblzma\liblzma.lib 
raddi/lib/a64/Portable/libsodium.lib - NOTE: LTCG
