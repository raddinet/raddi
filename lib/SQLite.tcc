#ifndef SQLITE_TCC
#define SQLITE_TCC

template <> int SQLite::Statement::get <int> (int column) const;
template <> long SQLite::Statement::get <long> (int column) const;
template <> double SQLite::Statement::get <double> (int column) const;
template <> long long SQLite::Statement::get <long long> (int column) const;

template <> std::size_t SQLite::Statement::get <std::size_t> (int column) const;
template <> std::wstring SQLite::Statement::get <std::wstring> (int column) const;
template <> std::vector <unsigned char> SQLite::Statement::get <std::vector <unsigned char>> (int column) const;

template <typename T> T SQLite::Statement::get <T> (int) const = delete;

#endif
