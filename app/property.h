#ifndef PROPERTY_H
#define PROPERTY_H

#include "data.h"

template <typename T>
class property {
    LPARAM window;
    UINT   id;
    T      value;
public:
    property (UINT id, T value) : window (0), id (id) {
        try {
            this->value = database.property.load.query <T> (0, id);
        } catch (const SQLite::InStatementException &) {
            this->value = value;
        }
    }
    property (LPARAM window, UINT id, T value) : window (window), id (id) {
        try {
            this->value = database.property.load.query <T> (window, id);
        } catch (const SQLite::InStatementException &) {
            this->value = value;
        }
    }
    void operator = (const T & v) {
        this->value = v;
        database.property.save (this->window, this->id, this->value);
    }
    operator const T & () const { return this->value; }

    property & operator = (const property & other) {
        this->value = other.value;
        database.property.save (this->window, this->id, this->value);
        return *this;
    }
private:
    property (const property &) = delete;
};

#endif
