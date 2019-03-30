#ifndef SQLITE_HPP
#define SQLITE_HPP

#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

class SQLite {
public:
    enum class Type {
        Integer = 1,
        Float = 2,
        Text = 3,
        Blob = 4,
        Null = 5,
    };

    class Statement;
    struct Exception : std::runtime_error {
        const int error;
        const int extended;
    protected:
        Exception (std::string_view op, int error, int extended = 0);
        Exception (std::string_view op, sqlite3 * db);
        Exception (std::string_view op, sqlite3 * db, std::string_view query);
    };
    struct BadBlobAssignmentException : Exception {
        const std::size_t retrieved; // actual blob size retrieved (amount of bytes in the database column)
        const std::size_t required;  // blob size required by assignment (size of assigned-to type)
        BadBlobAssignmentException (std::size_t retrieved, std::size_t required);
    };
    struct InStatementException : Exception {
        const Statement & statement;
        InStatementException (std::string_view op, const Statement & statement);
    };
    struct PrepareException : Exception {
        const std::wstring query;
        PrepareException (std::wstring_view query, sqlite3 * db);
    };

    struct Blob {
        const void * data;
        std::size_t  size;
    public:
        Blob (const void * data, std::size_t size) : data (data), size (size) {};

        template <typename T> explicit Blob (const T * object) : data (object), size (sizeof (T)) {}
        template <typename T> explicit Blob (const T & object) : data (&object), size (sizeof (T)) {}

        template <typename T>
        operator const T & () const {
            if (this->size == sizeof (T))
                return *reinterpret_cast <const T*> (this->data);

            throw BadBlobAssignmentException (this->size, sizeof (T));
        }
    };

    class Statement {
        friend class SQLite;
    private:
        sqlite3_stmt * stmt;
        int            bi;
        Statement (sqlite3_stmt *);
    public:
        Statement ();
        Statement (Statement &&);
        Statement & operator = (Statement &&);
        ~Statement ();

        // empty
        //  - true if query is empty, other operations will throw

        bool empty () const;

        // insert/update/delete
        //  - binds to 1st, 2nd, 3rd, etc... parameters
        //  - call unbind or reset (does not unbind) to start at 1st again

        void unbind ();

        void bind (int);
        void bind (double);
        void bind (long long);
        void bind (unsigned long long);
        void bind (std::string_view string); // UTF-8!
        void bind (std::wstring_view string);
        void bind (std::nullptr_t);

        // blobs

        void bind (Blob);
        void bind (const std::vector <unsigned char> & blob);
        void bind_blob (const void * data, std::size_t size);

        /*template <std::size_t N>
        void bind (const std::uint8_t (&blob) [N]) {
            return this->bind_blob (blob, N);
        }*/
        
        // forwards

        void bind (unsigned  int v) { this->bind ((long long) v); };
        void bind (long v) { this->bind ((long long) v); };
        void bind (unsigned long v) { this->bind ((long long) v); };
        void bind (long double v) { this->bind ((double) v); };

        // binding variable number of arguments

        template <typename A, typename B, typename... Args>
        void bind (A a, B b, Args... args) {
            this->bind (a);
            this->bind (b);
            this->bind (args...);
        }

        void execute (); // fails for select

        template <typename A, typename... Args>
        void execute (A a, Args... args) {
            this->reset ();
            this->bind (a);
            this->bind (args...);
            return this->execute ();
        }

        template <typename... Args>
        void operator () (Args&&... args) {
            return this->execute (std::forward<Args> (args)...);
        }

        // select

        unsigned long long row; // current row, starts with 1

        bool next (); // true if next row loaded, false at the end
        void reset (); // reset to run again (call .next)

        int          width () const; // number of columns
        std::wstring name (int) const; // column name
        bool         null (int) const; // is column NULL
        Type         type (int) const; // column type

        // get
        //  - int, double, wstring, vector <unsigned char>

        template <typename T> T get (int) const;
        // template <typename T> blob <T> get (int) const;
        template <typename T> T get (const std::wstring & column) const {
            const auto n = this->width ();
            for (int i = 0; i != n; ++i) {
                if (column == this->name (i))
                    return this->get <T> (i);
            }
            throw SQLite::InStatementException ("no such column", *this);
        }
        const void * get_blob (int, std::size_t & size) const;

        // query

        template <typename R, typename... Args>
        R query (Args... args) {
            this->bind (args...);
            if (this->next ()) {
                auto value = this->get <R> (0);
                this->reset ();
                return value;
            } else
                throw SQLite::InStatementException ("no data", *this);
        }
    private:
        void bind () {}
    };

public:
    static bool Initialize ();
    static void Terminate ();

    // open
    //  - opens new connection to db file (closes previous connection)
    //  - on failure any previous connection remains valid
    //
    bool open (const wchar_t *);

    // close
    //  - closes connection to the database file
    //
    void close ();

    // prepare
    //  - creates new statement based on SQL query
    //
    Statement prepare (std::wstring_view);

    enum class TransactionType {
        deferred,
        immediate,
        exclusive
    };

    // begin/commit/rollback
    //  - 
    //
    bool begin (TransactionType = TransactionType::deferred);
    bool commit ();
    bool rollback ();

    // execute
    //  - executes string query
    //  - returns number of changes
    //
    template <typename... Args>
    std::size_t execute (std::wstring_view string, Args... args) {
        auto q = this->prepare (string);
        q.execute (args...);
        return this->changes ();
    }

    // query
    //  - executes string query
    //  - returns value of the first column of the first row
    //
    template <typename R, typename... Args>
    R query (std::wstring_view string, Args... args) {
        return this->prepare (string).query <R> (args...);
    }

    // changes
    //  - returns number of affected rows for INSERT, UPDATE and DELETE
    //
    std::size_t changes () const;

    // last_insert_rowid
    //  - returns ROWID of last inserted row; not thread safe
    //
    long long last_insert_rowid () const;

    // error/error_message
    //  - returns last error code or formatted error message
    //
    int          error () const;
    std::wstring error_message () const;

private:
    sqlite3 * db = nullptr;
};

#endif

