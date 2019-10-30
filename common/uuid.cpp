#include "uuid.h"
#include <windows.h>
#include <rpc.h>

// simply using what Windows have to offer

uuid::uuid () {
    (void) UuidCreate ((UUID *) this->data);
}
void uuid::null () {
    for (auto & byte : this->data) {
        byte = 0;
    }
}
bool uuid::is_null () const {
    for (auto & byte : this->data) {
        if (byte != 0)
            return false;
    }
    return true;
}

bool uuid::parse (const char * string) {
    if (*string == '{') {
        ++string;
    }
    return UuidFromStringA ((RPC_CSTR) string, (UUID *) this->data) == RPC_S_OK;
}
bool uuid::parse (const wchar_t * string) {
    if (*string == L'{') {
        ++string;
    }
    return UuidFromStringW ((RPC_WSTR) string, (UUID *) this->data) == RPC_S_OK;
}
std::string uuid::c_cstr () const {
    RPC_CSTR rpcs;
    std::string result;
    if (UuidToStringA ((UUID *) this->data, &rpcs) == RPC_S_OK) {
        result.assign (rpcs, rpcs + std::strlen ((char *) rpcs));
        RpcStringFreeA (&rpcs);
    }
    return result;
}
std::wstring uuid::c_wstr () const {
    RPC_WSTR rpcs;
    std::wstring result;
    if (UuidToStringW ((UUID *) this->data, &rpcs) == RPC_S_OK) {
        result.assign (rpcs, rpcs + std::wcslen ((wchar_t *) rpcs));
        RpcStringFreeW (&rpcs);
    }
    return result;
}
