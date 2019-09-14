#include "SQLite.hpp"

#ifdef USING_WINSQLITE
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <cstdio>
#include <limits>

bool SQLite::Initialize () { return sqlite3_initialize () == SQLITE_OK; }
void SQLite::Terminate () { sqlite3_shutdown (); }

SQLite::Exception::Exception (std::string_view op, int error, int extended)
    : std::runtime_error (std::string (op))
    , error (error)
    , extended (extended ? extended : error) {}
SQLite::Exception::Exception (std::string_view op, sqlite3 * db)
    : std::runtime_error (std::string (op) + ": " + sqlite3_errmsg (db))
    , error (sqlite3_errcode (db))
    , extended (sqlite3_extended_errcode (db)) {}
SQLite::Exception::Exception (std::string_view op, sqlite3 * db, std::string_view query)
    : std::runtime_error (std::string (op) + ": " + sqlite3_errmsg (db) + " IN " + std::string (query))
    , error (sqlite3_errcode (db))
    , extended (sqlite3_extended_errcode (db)) {}
SQLite::InStatementException::InStatementException (std::string_view op, const Statement & statement)
    : Exception (op, sqlite3_db_handle (statement.stmt), sqlite3_sql (statement.stmt))
    , statement (statement) {};
SQLite::EmptyResultException::EmptyResultException (const Statement & statement)
    : Exception ("no data", sqlite3_db_handle (statement.stmt), sqlite3_sql (statement.stmt))
    , statement (statement) {};
SQLite::PrepareException::PrepareException (std::wstring_view query, sqlite3 * db)
    : Exception ("prepare", db)
    , query (query) {}
SQLite::BadBlobAssignmentException::BadBlobAssignmentException (std::size_t retrieved, std::size_t required)
    : Exception ("blob: size of types in assignment mismatch", SQLITE_MISMATCH)
    , retrieved (retrieved)
    , required (required) {}

SQLite::Statement::Statement ()
    : stmt (nullptr)
    , bi (0)
    , row (0) {}

SQLite::Statement::Statement (sqlite3_stmt * stmt)
    : stmt (stmt)
    , bi (0)
    , row (0) {}

SQLite::Statement::Statement (SQLite::Statement && s) noexcept
    : stmt (s.stmt)
    , bi (s.bi)
    , row (s.row) { s.stmt = nullptr; }

SQLite::Statement & SQLite::Statement::operator = (SQLite::Statement && s) noexcept {
    std::swap (this->stmt, s.stmt);
    std::swap (this->bi, s.bi);
    std::swap (this->row, s.row);
    return *this;
}

SQLite::Statement::~Statement () {
    sqlite3_finalize (this->stmt);
}

bool SQLite::Statement::empty () const {
    return sqlite3_sql (this->stmt) == nullptr;
}

void SQLite::Statement::execute () {
    this->row = 0;
    if (sqlite3_step (this->stmt) != SQLITE_DONE)
        throw SQLite::InStatementException ("not executed", *this);
    if (sqlite3_reset (this->stmt) != SQLITE_OK)
        throw SQLite::InStatementException ("reset", *this);
}

bool SQLite::Statement::next () {
    switch (sqlite3_step (this->stmt)) {
        case SQLITE_ROW:
            ++this->row;
            return true;
        case SQLITE_DONE:
            this->reset ();
            return false;
        default:
            throw SQLite::InStatementException ("step", *this);
    }
}

void SQLite::Statement::reset () {
    this->bi = 0;
    this->row = 0;
    if (sqlite3_reset (this->stmt) != SQLITE_OK)
        throw SQLite::InStatementException ("reset", *this);
}

int SQLite::Statement::width () const {
    auto n = sqlite3_data_count(this->stmt);
    return n ? n : sqlite3_column_count (this->stmt);
}
SQLite::Type SQLite::Statement::type (int i) const {
    return (SQLite::Type) sqlite3_column_type (this->stmt, i);
}
std::wstring SQLite::Statement::name (int i) const {
    if (auto psz = static_cast <const wchar_t *> (sqlite3_column_name16 (this->stmt, i))) {
        return psz;
    } else
        throw std::bad_alloc ();
}
bool SQLite::Statement::null (int i) const {
    return sqlite3_column_type (this->stmt, i) == SQLITE_NULL;
}
template <> int SQLite::Statement::get <int> (int column) const {
    return sqlite3_column_int (this->stmt, column);
}
template <> long SQLite::Statement::get <long> (int column) const {
    return sqlite3_column_int (this->stmt, column);
}
template <> double SQLite::Statement::get <double> (int column) const {
    return sqlite3_column_double (this->stmt, column);
}
template <> long long SQLite::Statement::get <long long> (int column) const {
    return sqlite3_column_int64 (this->stmt, column);
}
template <> std::size_t SQLite::Statement::get <std::size_t> (int column) const {
    if (sizeof (std::size_t) == sizeof (int))
        return (std::size_t) this->get <int> (column);
    else
        return (std::size_t) this->get <long long> (column);
}

template <> std::wstring_view SQLite::Statement::get <std::wstring_view> (int column) const {
    if (auto ptr = sqlite3_column_text16 (this->stmt, column)) {
        auto size = sqlite3_column_bytes16 (this->stmt, column) / 2;
        auto data = static_cast <const wchar_t *> (ptr);

        return std::wstring_view (data, size);
    } else
        return std::wstring_view ();
}
template <> std::wstring SQLite::Statement::get <std::wstring> (int column) const {
    return std::wstring (this->get <std::wstring_view> (column));
}

void SQLite::Statement::unbind () {
    this->bi = 0;
    this->row = 0;
    if (sqlite3_clear_bindings (this->stmt) != SQLITE_OK)
        throw SQLite::InStatementException ("unbind", *this);
}

void SQLite::Statement::bind (int value) {
    if (sqlite3_bind_int (this->stmt, ++this->bi, value) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}
void SQLite::Statement::bind (double value) {
    if (sqlite3_bind_double (this->stmt, ++this->bi, value) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}
void SQLite::Statement::bind (long long value) {
    if (sqlite3_bind_int64 (this->stmt, ++this->bi, value) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}
void SQLite::Statement::bind (unsigned long long value) {
    if (sqlite3_bind_int64 (this->stmt, ++this->bi, (long long) value) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}
void SQLite::Statement::bind (std::string_view value) {
    if (sqlite3_bind_text (this->stmt, ++this->bi, value.data (), (int) value.size (), SQLITE_TRANSIENT) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}
void SQLite::Statement::bind (std::wstring_view value) {
    if (sqlite3_bind_text16 (this->stmt, ++this->bi, value.data (), (int) (value.size () * sizeof (wchar_t)), SQLITE_TRANSIENT) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}
void SQLite::Statement::bind (std::nullptr_t) {
    if (sqlite3_bind_null (this->stmt, ++this->bi) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}
void SQLite::Statement::bind (Blob object) {
    return this->bind_blob (object.data, object.size);
}
void SQLite::Statement::bind (const std::vector <unsigned char> & value) {
    return this->bind_blob (value.data (), value.size ());
}
void SQLite::Statement::bind_blob (const void * data, std::size_t size) {
    static const unsigned char empty [1] = { 0 };
    if (sqlite3_bind_blob (this->stmt, ++this->bi, size ? data : empty, (int) size, SQLITE_TRANSIENT) != SQLITE_OK)
        throw SQLite::InStatementException ("bind", *this);
}

const void * SQLite::Statement::get_blob (int column, std::size_t & size) const {
    size = sqlite3_column_bytes (this->stmt, column);
    return sqlite3_column_blob (this->stmt, column);
}
template <> std::vector <unsigned char> SQLite::Statement::get <std::vector <unsigned char>> (int column) const {
    std::size_t size;
    if (auto data = static_cast <const unsigned char *> (this->get_blob (column, size))) {
        return std::vector <unsigned char> (data, data + size);
    } else
        return std::vector <unsigned char> ();
}
template <> SQLite::Blob SQLite::Statement::get <SQLite::Blob> (int column) const {
    std::size_t size;
    if (auto data = static_cast <const unsigned char *> (this->get_blob (column, size))) {
        return Blob { data, size };
    } else
        return Blob { nullptr, 0 };
}

bool SQLite::open (const wchar_t * filename) {
    sqlite3 * newdb = nullptr;
    if (sqlite3_open16 (filename, &newdb) == SQLITE_OK) {
        sqlite3_busy_timeout (newdb, 1000);
        this->close ();
        this->db = newdb;
        return true;
    } else
        return false;
}
void SQLite::close () {
    if (this->db) {
        sqlite3_close_v2 (this->db);
        this->db = nullptr;
    }
}

SQLite::Statement SQLite::prepare (std::wstring_view query) {
    sqlite3_stmt * statement;
    if (sqlite3_prepare16_v2 (this->db, query.data (), (int) (query.size () * sizeof (wchar_t)), &statement, nullptr) == SQLITE_OK) {
        return statement;
    } else
        throw SQLite::PrepareException (query, this->db);
}

bool SQLite::begin (SQLite::TransactionType mode) {
    try {
        switch (mode) {
            case TransactionType::deferred:
                this->execute (L"BEGIN DEFERRED TRANSACTION");
                break;
            case TransactionType::immediate:
                this->execute (L"BEGIN IMMEDIATE TRANSACTION");
                break;
            case TransactionType::exclusive:
                this->execute (L"BEGIN EXCLUSIVE TRANSACTION");
                break;
        }
        return true;
    } catch (SQLite::InStatementException &) {
        // TODO: which error codes are relevant? SQLITE_BUSY?
        return false;
    }
}
bool SQLite::commit () {
    try {
        this->execute (L"COMMIT");
        return true;
    } catch (SQLite::InStatementException &) {
        // TODO: which error codes are relevant? SQLITE_BUSY?
        return false;
    }
}
bool SQLite::rollback () {
    try {
        this->execute (L"ROLLBACK");
        return true;
    } catch (SQLite::InStatementException &) {
        // TODO: which error codes are relevant? SQLITE_BUSY?
        return false;
    }
}

std::size_t SQLite::changes () const {
    return sqlite3_changes (this->db);
}
long long SQLite::last_insert_rowid () const {
    return sqlite3_last_insert_rowid (this->db);
}

int SQLite::error () const {
    return sqlite3_errcode (this->db);
}
std::wstring SQLite::error_message () const {
    if (auto msg16 = (const wchar_t *) sqlite3_errmsg16 (this->db)) {
        try {
            std::vector <wchar_t> buffer;
            buffer.resize (12 + std::wcslen (msg16));

            _snwprintf (&buffer [0], buffer.size (), L"%02X:%02X %s",
                        sqlite3_errcode (this->db), sqlite3_extended_errcode (this->db) >> 8, msg16);
            return std::wstring (buffer.begin (), buffer.end ());

        } catch (const std::bad_alloc &) {
            // continue below...
        }
    }

    wchar_t msg [16];
    _snwprintf (msg, sizeof msg / sizeof msg [0], L"%02X:%02X (OOM)",
                sqlite3_errcode (this->db), sqlite3_extended_errcode (this->db) >> 8);
    return msg;
}
