// Standard-library type conversions.
//
// SIP 6 (ABI 13) mapped-type contract:
//   %ConvertToTypeCode   — sipPy, sipCppPtr (Type **), sipIsErr.
//                          Assign: *sipCppPtr = new Type(...).
//   %ConvertFromTypeCode — sipCpp (Type *); return a new reference.
//
// vector<Cell>/vector<Row> use the generated type IDs (sipType_Cell /
// sipType_Row) and the module API (sipConvertToType / sipReleaseType /
// sipConvertFromNewType), in scope in the generated conversion files.

// ── std::string <-> str ─────────────────────────────────────────────
%MappedType std::string
{
%TypeHeaderCode
#include <string>
%End

%ConvertToTypeCode
    if (sipIsErr == nullptr) {
        return PyUnicode_Check(sipPy) ? 1 : 0;
    }
    if (!PyUnicode_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "str expected");
        *sipIsErr = 1;
        return 0;
    }
    Py_ssize_t len = 0;
    const char *utf8 = PyUnicode_AsUTF8AndSize(sipPy, &len);
    *sipCppPtr = new std::string(utf8, static_cast<size_t>(len));
    return 1;
%End

%ConvertFromTypeCode
    if (sipCpp == nullptr) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromStringAndSize(sipCpp->data(),
            static_cast<Py_ssize_t>(sipCpp->size()));
%End
};

// ── std::u32string <-> str ──────────────────────────────────────────
%MappedType std::u32string
{
%TypeHeaderCode
#include <string>

// Decode a std::u32string to a Python str (via UTF-8).
static PyObject *u32string_to_py(const std::u32string &s)
{
    std::string utf8;
    utf8.reserve(s.size());
    for (char32_t cp : s) {
        if (cp < 0x80) {
            utf8 += static_cast<char>(cp);
        } else if (cp < 0x800) {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            utf8 += static_cast<char>(0xF0 | (cp >> 18));
            utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return PyUnicode_FromStringAndSize(utf8.data(), static_cast<Py_ssize_t>(utf8.size()));
}

// Encode a Python str to a std::u32string (via UTF-8).
static std::u32string py_to_u32string(PyObject *obj)
{
    Py_ssize_t len = 0;
    const char *utf8 = PyUnicode_AsUTF8AndSize(obj, &len);
    if (utf8 == nullptr) {
        return {};
    }
    std::u32string result;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8);
    const unsigned char *end = p + len;
    while (p < end) {
        char32_t cp = 0;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = (*p++ & 0x1F) << 6;
            cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            cp = (*p++ & 0x0F) << 12;
            cp |= (*p++ & 0x3F) << 6;
            cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
            cp = (*p++ & 0x07) << 18;
            cp |= (*p++ & 0x3F) << 12;
            cp |= (*p++ & 0x3F) << 6;
            cp |= (*p++ & 0x3F);
        } else {
            ++p; // invalid lead byte — skip
            continue;
        }
        result += cp;
    }
    return result;
}
%End

%ConvertToTypeCode
    if (sipIsErr == nullptr) {
        return PyUnicode_Check(sipPy) ? 1 : 0;
    }
    if (!PyUnicode_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "str expected");
        *sipIsErr = 1;
        return 0;
    }
    *sipCppPtr = new std::u32string(py_to_u32string(sipPy));
    return 1;
%End

%ConvertFromTypeCode
    if (sipCpp == nullptr) {
        Py_RETURN_NONE;
    }
    return u32string_to_py(*sipCpp);
%End
};

// ── std::vector<int> <-> list[int] ──────────────────────────────────
%MappedType std::vector<int>
{
%TypeHeaderCode
#include <vector>
%End

%ConvertToTypeCode
    if (sipIsErr == nullptr) {
        return PyList_Check(sipPy) ? 1 : 0;
    }
    if (!PyList_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "list expected");
        *sipIsErr = 1;
        return 0;
    }
    std::vector<int> *vec = new std::vector<int>();
    Py_ssize_t n = PyList_GET_SIZE(sipPy);
    vec->reserve(static_cast<size_t>(n));
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PyList_GET_ITEM(sipPy, i);
        long value = PyLong_AsLong(item);
        if (value == -1 && PyErr_Occurred()) {
            delete vec;
            *sipIsErr = 1;
            return 0;
        }
        vec->push_back(static_cast<int>(value));
    }
    *sipCppPtr = vec;
    return 1;
%End

%ConvertFromTypeCode
    if (sipCpp == nullptr) {
        Py_RETURN_NONE;
    }
    PyObject *list = PyList_New(static_cast<Py_ssize_t>(sipCpp->size()));
    if (list == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < sipCpp->size(); ++i) {
        PyObject *item = PyLong_FromLong((*sipCpp)[i]);
        if (item == nullptr) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), item);
    }
    return list;
%End
};

// ── std::vector<Cell> <-> list[Cell] ────────────────────────────────
%MappedType std::vector<Cell>
{
%TypeHeaderCode
#include <vector>
#include "screen.h"
%End

%ConvertToTypeCode
    if (sipIsErr == nullptr) {
        return PyList_Check(sipPy) ? 1 : 0;
    }
    if (!PyList_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "list of Cell expected");
        *sipIsErr = 1;
        return 0;
    }
    std::vector<Cell> *vec = new std::vector<Cell>();
    Py_ssize_t n = PyList_GET_SIZE(sipPy);
    vec->reserve(static_cast<size_t>(n));
    for (Py_ssize_t i = 0; i < n; ++i) {
        int state = 0;
        int iserr = 0;
        void *cpp = sipConvertToType(PyList_GET_ITEM(sipPy, i), sipType_Cell,
                SIP_NULLPTR, SIP_NOT_NONE, &state, &iserr);
        if (iserr) {
            delete vec;
            *sipIsErr = 1;
            return 0;
        }
        vec->push_back(*reinterpret_cast<Cell *>(cpp));
        sipReleaseType(cpp, sipType_Cell, state);
    }
    *sipCppPtr = vec;
    return 1;
%End

%ConvertFromTypeCode
    if (sipCpp == nullptr) {
        Py_RETURN_NONE;
    }
    PyObject *list = PyList_New(static_cast<Py_ssize_t>(sipCpp->size()));
    if (list == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < sipCpp->size(); ++i) {
        Cell *copy = new Cell((*sipCpp)[i]);
        PyObject *item = sipConvertFromNewType(copy, sipType_Cell, SIP_NULLPTR);
        if (item == nullptr) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), item);
    }
    return list;
%End
};

// ── std::vector<Row> <-> list[Row] ──────────────────────────────────
%MappedType std::vector<Row>
{
%TypeHeaderCode
#include <vector>
#include "screen.h"
%End

%ConvertToTypeCode
    if (sipIsErr == nullptr) {
        return PyList_Check(sipPy) ? 1 : 0;
    }
    if (!PyList_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "list of Row expected");
        *sipIsErr = 1;
        return 0;
    }
    std::vector<Row> *vec = new std::vector<Row>();
    Py_ssize_t n = PyList_GET_SIZE(sipPy);
    vec->reserve(static_cast<size_t>(n));
    for (Py_ssize_t i = 0; i < n; ++i) {
        int state = 0;
        int iserr = 0;
        void *cpp = sipConvertToType(PyList_GET_ITEM(sipPy, i), sipType_Row,
                SIP_NULLPTR, SIP_NOT_NONE, &state, &iserr);
        if (iserr) {
            delete vec;
            *sipIsErr = 1;
            return 0;
        }
        vec->push_back(*reinterpret_cast<Row *>(cpp));
        sipReleaseType(cpp, sipType_Row, state);
    }
    *sipCppPtr = vec;
    return 1;
%End

%ConvertFromTypeCode
    if (sipCpp == nullptr) {
        Py_RETURN_NONE;
    }
    PyObject *list = PyList_New(static_cast<Py_ssize_t>(sipCpp->size()));
    if (list == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < sipCpp->size(); ++i) {
        Row *copy = new Row((*sipCpp)[i]);
        PyObject *item = sipConvertFromNewType(copy, sipType_Row, SIP_NULLPTR);
        if (item == nullptr) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), item);
    }
    return list;
%End
};


// ── std::vector<std::string> <-> list[str] ──────────────────────────
%MappedType std::vector<std::string>
{
%TypeHeaderCode
#include <string>
#include <vector>
%End

%ConvertToTypeCode
    if (sipIsErr == nullptr) {
        return PyList_Check(sipPy) ? 1 : 0;
    }
    if (!PyList_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "list of str expected");
        *sipIsErr = 1;
        return 0;
    }
    std::vector<std::string> *vec = new std::vector<std::string>();
    Py_ssize_t n = PyList_GET_SIZE(sipPy);
    vec->reserve(static_cast<size_t>(n));
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *item = PyList_GET_ITEM(sipPy, i);
        if (!PyUnicode_Check(item)) {
            PyErr_SetString(PyExc_TypeError, "list of str expected");
            delete vec;
            *sipIsErr = 1;
            return 0;
        }
        Py_ssize_t len = 0;
        const char *utf8 = PyUnicode_AsUTF8AndSize(item, &len);
        vec->emplace_back(utf8, static_cast<size_t>(len));
    }
    *sipCppPtr = vec;
    return 1;
%End

%ConvertFromTypeCode
    if (sipCpp == nullptr) {
        Py_RETURN_NONE;
    }
    PyObject *list = PyList_New(static_cast<Py_ssize_t>(sipCpp->size()));
    if (list == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < sipCpp->size(); ++i) {
        PyObject *item = PyUnicode_FromStringAndSize((*sipCpp)[i].data(),
                static_cast<Py_ssize_t>((*sipCpp)[i].size()));
        if (item == nullptr) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), item);
    }
    return list;
%End
};

// ── std::optional<bool> <-> bool | None ─────────────────────────────
%MappedType std::optional<bool>
{
%TypeHeaderCode
#include <optional>
%End

%ConvertToTypeCode
    if (sipIsErr == nullptr) {
        return sipPy == Py_None || PyBool_Check(sipPy);
    }
    if (sipPy == Py_None) {
        *sipCppPtr = new std::optional<bool>();
        return 1;
    }
    if (!PyBool_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "bool or None expected");
        *sipIsErr = 1;
        return 0;
    }
    *sipCppPtr = new std::optional<bool>(sipPy == Py_True);
    return 1;
%End

%ConvertFromTypeCode
    if (sipCpp == nullptr || !sipCpp->has_value()) {
        Py_RETURN_NONE;
    }
    if (*sipCpp) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
%End
};