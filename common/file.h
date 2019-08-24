#ifndef RADDI_FILE_H
#define RADDI_FILE_H

#include <windows.h>
#include <string>

// file
//  - simple filesystem file abstraction for future porting
//  - at this point mostly to simplify destruction
//  - even on 64-bit Windows HANDLE size is 32-bit
//     - using int32 in attempt to save 8 bytes per shard, as there will be a lot of them
//
class file {
    std::int32_t handle = 0;

public:
    enum class access : DWORD {
        query = FILE_READ_ATTRIBUTES,
        read  = GENERIC_READ,
        write = GENERIC_READ | GENERIC_WRITE,
    };
    enum class share : DWORD {
        none = 0,
        read = FILE_SHARE_READ,
        full = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
    };
    enum class buffer : DWORD {
        none = FILE_FLAG_NO_BUFFERING,
        normal = FILE_ATTRIBUTE_NORMAL,
        random = FILE_FLAG_RANDOM_ACCESS,
        temporary = FILE_ATTRIBUTE_TEMPORARY,
        sequential = FILE_FLAG_SEQUENTIAL_SCAN,
    };
    enum class mode {
        open = OPEN_EXISTING,
        always = OPEN_ALWAYS,
        create = CREATE_ALWAYS,
    };

public:
    file () = default;
    file (file && other) noexcept : handle (other.handle) {
        other.handle = 0;
    }
    file & operator = (file && other) noexcept {
        this->close ();
        this->handle = other.handle;
        other.handle = 0;
        return *this;
    }
    ~file ();

    // open
    //  - opens or creates new file handle according to provided arguments
    //
    bool open (const wchar_t *, mode, access, share, buffer = buffer::normal) noexcept;
    bool open (const std::wstring & path, mode m, access a, share s, buffer b = buffer::normal) noexcept {
        return this->open (path.c_str (), m, a, s, b);
    }

    // create
    //  - opens new, overwrites existing file (if exists), write access, no sharing
    //
    bool create (const std::wstring & path, buffer buffering = buffer::normal) noexcept {
        return this->open (path, mode::create, access::write, share::none, buffering);
    }

    // created
    //  - returns true, if previous call to 'open' with 'mode::always' resulted in
    //    creation of a new file
    //  - must be called immediately after 'open' otherwise result is undefined
    //
    bool created () const noexcept {
        return GetLastError () == 0;
    }

    // close
    //  - release the file handle
    //
    void close () noexcept;
    bool closed () const noexcept { return !this->handle; }
    void flush () const noexcept;

    // seek/tail/tell
    //  - seeks to the particular 'offset', or to the end (tail)
    //  - returns current position in the file
    //
    std::uintmax_t seek (std::uintmax_t offset) noexcept;
    std::uintmax_t tail () noexcept;
    std::uintmax_t tell () const noexcept;

    // size
    //  - retrieves file size even if open only for 'query'
    //
    std::uintmax_t size () const noexcept;

    // resize
    //  - truncates or zero-fills file to make it length-bytes long
    //
    bool resize (std::uintmax_t length) noexcept;

    // compress
    //  - attempts to have the OS compress this file
    //
    bool compress () noexcept;

    // read
    //  - reads 'length' of data into the 'buffer' from either
    //    current file pointer position, or provided offset
    //
    bool read (void * buffer, std::size_t length) noexcept;
    bool read (std::uintmax_t offset, void * buffer, std::size_t length) noexcept;

    template <typename T>
    bool read (T & object) noexcept {
        return this->read (&object, sizeof object);
    }
    template <typename T>
    bool read (std::uintmax_t offset, T & object) noexcept {
        return this->read (offset, &object, sizeof object);
    }

    // write
    //  - writes 'size' bytes from 'data' into the file's current position
    //    possibly extending the file size
    //
    bool write (const void * data, std::size_t size) noexcept;

    template <typename T>
    bool write (const T & object) noexcept {
        return this->write (&object, sizeof object);
    }

    // zero
    //  - writes zeros into the file into specified range
    //
    bool zero (std::uintmax_t offset, std::uintmax_t length) noexcept;

public:

    // unlink
    //  - marks file for deletion when all handles are closed
    //
    static bool unlink (const std::wstring & path);

private:
    std::uintmax_t seek_ (std::uintmax_t offset, int) const noexcept;

    operator HANDLE () const noexcept {
        return reinterpret_cast <HANDLE> (static_cast <std::intptr_t> (this->handle));
    }
};

// translate
//  - for passing file opening options as a log function parameter
//
inline std::wstring translate (enum class file::access a, const std::wstring &) {
    switch (a) {
        case file::access::query: return L"query";
        case file::access::read: return L"read";
        case file::access::write: return L"write";
    }
    return std::to_wstring ((int) a);
}
inline std::wstring translate (enum class file::share s, const std::wstring &) {
    switch (s) {
        case file::share::none: return L"none";
        case file::share::read: return L"read";
        case file::share::full: return L"full";
    }
    return std::to_wstring ((int) s);
}
inline std::wstring translate (enum class file::mode m, const std::wstring &) {
    switch (m) {
        case file::mode::open: return L"open";
        case file::mode::always: return L"open always";
        case file::mode::create: return L"create";
    }
    return std::to_wstring ((int) m);
}
inline std::wstring translate (enum class file::buffer b, const std::wstring &) {
    switch (b) {
        case file::buffer::none: return L"no buffering";
        case file::buffer::normal: return L"normal buffering";
        case file::buffer::random: return L"random access";
        case file::buffer::sequential: return L"sequential scan";
        case file::buffer::temporary: return L"temporary file";
    }
    return std::to_wstring ((int) b);
}

#endif
