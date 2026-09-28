// Snapshot — immutable bundle delivered to the GUI thread (session.h).
class Snapshot
{
%TypeHeaderCode
#include "session.h"
%End

public:
    Snapshot();

    std::vector<int> dirtyRows;
    std::vector<Row> rows;
    int scrollbackLen;
    int viewportOffset;
    int cursorRow;
    int cursorCol;
    bool decCkm;
    bool bracketedPaste;
    bool reverseVideo;
    bool mouse1000;
    bool mouse1002;
    bool mouse1003;
    bool mouse1006;
    bool focusReport;
    bool altScreen;
    bool full;
    bool contentChanged;
    bool cursorVisible;
    // Hand-written accessors: the generated member setter would reject
    // None (SIP_NOT_NONE), but cursorColor is std::optional.
    std::optional<std::string> cursorColor
    {
%GetCode
    if (!sipCpp->cursorColor.has_value()) {
        sipPy = Py_None;
        Py_INCREF(sipPy);
    } else {
        sipPy = PyUnicode_FromStringAndSize(sipCpp->cursorColor->data(),
                static_cast<Py_ssize_t>(sipCpp->cursorColor->size()));
    }
%End
%SetCode
    if (sipPy == Py_None) {
        sipCpp->cursorColor = std::nullopt;
    } else if (!PyUnicode_Check(sipPy)) {
        PyErr_SetString(PyExc_TypeError, "str or None expected");
        sipErr = 1;
    } else {
        Py_ssize_t len = 0;
        const char *utf8 = PyUnicode_AsUTF8AndSize(sipPy, &len);
        sipCpp->cursorColor = std::string(utf8, static_cast<size_t>(len));
    }
%End
    };
};
