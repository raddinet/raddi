# raddi/lib directory

*To successfully compile the project, copy third-party libraries into this directory as described*

## Libraries to acquire

* https://github.com/raddinet/libsodium - our fork of https://github.com/jedisct1/libsodium
* https://tukaani.org/xz
* https://sqlite.org/

See [raddi-redist-windows](https://github.com/raddinet/raddi-redist-windows) repository overview
for details on changes applied to our patched version of these DLLs used in Release builds.
Using those is recommended but not required.

## Source and header files

```raddi/lib/sqlite3.c``` — *use regular amalgamation distribution from [www.sqlite.org](https://www.sqlite.org/download.html)*  
```raddi/lib/include/sqlite3.h```  
```raddi/lib/include/sqlite3ext.h```

```raddi/lib/include/lzma.h```  
```raddi/lib/include/lzma/base.h```  
```raddi/lib/include/lzma/*.*``` — *copy all headers*

```raddi/lib/include/sodium.h```  
```raddi/lib/include/sodium/core.h```  
```raddi/lib/include/sodium/*.*``` — *copy all headers*

## Libraries

* **Release** builds link against DLLs and installed Visual Studio runtime.  
Available precompiled from: https://github.com/raddinet/raddi-redist-windows

* **Portable** builds link static libraries into executables themselves.  
These static libraries needs to be built with **LTCG**. Or you can reconfigure the project (not described here).

### sqlite3.lib (for Release builds)

Generate **sqlite3.lib** by running one of these commands:

* x86-32: ```lib.exe /def:sqlite3.def /out:sqlite3.lib /machine:x86```  
* x86-64: ```lib.exe /def:sqlite3.def /out:sqlite3.lib /machine:x64```

There is no need for sqlite3.lib on AArch64.  
Release builds link against the **winsqlite3.dll** that comes with Windows on ARM.

### libsodium

No special actions are required to link against libsodium.  
See live differences in our fork of libsodium here: https://github.com/jedisct1/libsodium/compare/master...raddinet:master

### liblzma

Use vs2017/xz_win.sln project, optionally add ARM64 platform, and rebuild all Debug and Release configurations.

Adjust project settings accordingly. Pay attention to Control Flow Guard and other options which are designed to increase security resilience of the software.

### x86-32

```raddi/lib/x86/Debug/liblzma.lib``` — *copy from:* ```windows\vs2017\Debug\Win32\liblzma.lib```  
```raddi/lib/x86/Debug/libsodium.lib``` — *copy from:* ```bin\Win32\Debug\v142\static\libsodium.lib```

```raddi/lib/x86/Release/liblzma.lib``` — *copy from:* ```windows\vs2017\Release\Win32\liblzma_dll\liblzma.lib```  
```raddi/lib/x86/Release/libsodium.lib``` — *copy from:* ```bin\Win32\Release\v142\dynamic\libsodium.lib```  
```raddi/lib/x86/Release/sqlite3.lib``` — *use the file generated as described [above](#sqlite3lib-for-release-builds)*

```raddi/lib/x86/Portable/liblzma.lib``` — *copy from:* ```windows\vs2017\Release\Win32\liblzma\liblzma.lib```  
```raddi/lib/x86/Portable/libsodium.lib``` — *copy from:* ```bin\Win32\Release\v142\ltcg\libsodium.lib```

### x86-64

```raddi/lib/x64/Debug/liblzma.lib``` — *copy from:* ```windows\vs2017\Debug\x64\liblzma.lib```  
```raddi/lib/x64/Debug/libsodium.lib``` — *copy from:* ```bin\x64\Debug\v142\static\libsodium.lib```

```raddi/lib/x64/Release/liblzma.lib``` — *copy from:* ```windows\vs2017\Release\x64\liblzma_dll\liblzma.lib```  
```raddi/lib/x64/Release/libsodium.lib``` — *copy from:* ```bin\x64\Release\v142\dynamic\libsodium.lib```  
```raddi/lib/x64/Release/sqlite3.lib``` — *use the file generated as described [above](#sqlite3lib-for-release-builds)*

```raddi/lib/x64/Portable/liblzma.lib``` — *copy from:* ```windows\vs2017\Release\x64\liblzma\liblzma.lib```  
```raddi/lib/x64/Portable/libsodium.lib``` — *copy from:* ```bin\x64\Release\v142\ltcg\libsodium.lib```

### AArch64

```raddi/lib/a64/Debug/liblzma.lib``` — *copy from:* ```windows\vs2017\Debug\ARM64\liblzma.lib```  
```raddi/lib/a64/Debug/libsodium.lib``` — *copy from:* ```bin\ARM64\Debug\v142\static\libsodium.lib```

```raddi/lib/a64/Release/liblzma.lib``` — *copy from:* ```windows\vs2017\Release\ARM64\liblzma_dll\liblzma.lib```   
```raddi/lib/a64/Release/libsodium.lib``` — *copy from:* ```bin\ARM64\Release\v142\dynamic\libsodium.lib```

```raddi/lib/a64/Portable/liblzma.lib``` — *copy from:* ```windows\vs2017\Release\ARM64\liblzma\liblzma.lib```  
```raddi/lib/a64/Portable/libsodium.lib``` — *copy from:* ```bin\ARM64\Release\v142\ltcg\libsodium.lib```

## Notes

* The source paths above are relative to the source repository
* **Release** builds may need .exp files too, same source and target
* **Release** builds of liblzma and libsodium place .dll into same path as .lib and .exp files