#include "file.h"
#include <winioctl.h>

file::~file () {
    this->close ();
}

bool file::open (const wchar_t * path, mode m, access a, share s, buffer buffering) noexcept {
    HANDLE h = CreateFile (path, (DWORD) a, (DWORD) s,
                           NULL, (DWORD) m, (DWORD) buffering, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        this->close ();
        this->handle = h;
        return true;
    } else
        return false;
}

bool file::compress () noexcept {
    DWORD result = 0u;
    USHORT type = COMPRESSION_FORMAT_DEFAULT;
    return DeviceIoControl (this->handle, FSCTL_SET_COMPRESSION,
                            &type, sizeof type, NULL, 0u, &result, NULL);
}

void file::close () noexcept {
    if (this->handle != INVALID_HANDLE_VALUE) {
        CloseHandle (this->handle);
        this->handle = INVALID_HANDLE_VALUE;
    }
}

void file::flush () const noexcept {
    if (!this->closed ()) {
        FlushFileBuffers (this->handle);
    }
}

std::uintmax_t file::seek_ (std::uintmax_t offset, int whence) const noexcept {
    LARGE_INTEGER target;
    LARGE_INTEGER result;

    target.QuadPart = offset;
    if (SetFilePointerEx (this->handle, target, &result, whence))
        return (std::uintmax_t) result.QuadPart;
    else
        return (std::uintmax_t) -1;
}

std::uintmax_t file::seek (std::uintmax_t offset) noexcept {
    return this->seek_ (offset, FILE_BEGIN);
}
std::uintmax_t file::tail () noexcept {
    return this->seek_ (0, FILE_END);
}
std::uintmax_t file::tell () const noexcept {
    return this->seek_ (0, FILE_CURRENT);
}

std::uintmax_t file::size () const noexcept {
    LARGE_INTEGER result;
    if (GetFileSizeEx (this->handle, &result))
        return (std::uintmax_t) result.QuadPart;
    else
        return (std::uintmax_t) -1;
}

bool file::resize (std::uintmax_t length) noexcept {
    return this->seek (length) != (std::uintmax_t) -1
        && SetEndOfFile (this->handle);
}

bool file::write (const void * data, std::size_t size) noexcept {
    DWORD written;
    return size <= MAXDWORD
        && WriteFile (this->handle, data, (DWORD) size, &written, NULL)
        && written == size;
}

bool file::read (void * data, std::size_t size) noexcept {
    static const auto chunk = 0x8000'0000;

    DWORD red;
    while (size > chunk) {
        if (ReadFile (this->handle, data, chunk, &red, NULL) && (red == chunk)) {
            size -= chunk;
            data = reinterpret_cast <char *> (data) + chunk;
        } else
            return false;
    }
    return ReadFile (this->handle, data, (DWORD) size, &red, NULL)
        && red == size;
}

bool file::read (std::uintmax_t offset, void * data, std::size_t size) noexcept {
    static const auto chunk = 0x8000'0000;

    DWORD red;
    OVERLAPPED o;

    o.hEvent = NULL;
    o.Offset = offset & 0xFFFFFFFF;
    o.OffsetHigh = offset >> 32;

    while (size > chunk) {
        if (ReadFile (this->handle, data, chunk, &red, &o) && (red == chunk)) {
            size -= chunk;
            data = reinterpret_cast <char *> (data) + chunk;

            o.Offset += chunk;
            if (o.Offset < chunk) {
                o.OffsetHigh += 1;
            }
        } else
            return false;
    }
    return ReadFile (this->handle, data, (DWORD) size, &red, &o)
        && red == size;
}

bool file::zero (std::uintmax_t offset, std::uintmax_t length) noexcept {
    DWORD n;
    FILE_ZERO_DATA_INFORMATION zero;

    zero.FileOffset.QuadPart = offset;
    zero.BeyondFinalZero.QuadPart = offset + length;

    return DeviceIoControl (this->handle, FSCTL_SET_ZERO_DATA, &zero, (DWORD) sizeof zero, NULL, 0, &n, NULL);
}

bool file::unlink (const std::wstring & path) {
    return DeleteFile (path.c_str ());
}
