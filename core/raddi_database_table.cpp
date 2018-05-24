#include "raddi_database_table.h"
#include "raddi_database_shard.h"

std::wstring raddi::db::table_directory_path (const std::wstring & table) const {
    return this->path + L"\\" + table + L"\\";
}