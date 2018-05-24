#include "raddi_database_shard.h"
#include "raddi_database_table.h"

std::wstring raddi::db::shard_instance_name (std::uint32_t base, const std::wstring & table) const {
    wchar_t name [256];
    _snwprintf (name, sizeof name / sizeof name [0],
                L"%s:%08x",
                table.c_str (), base);
    return name;
}
