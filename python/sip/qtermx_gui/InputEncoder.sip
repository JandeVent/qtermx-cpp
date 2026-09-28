// InputEncoder — QKeyEvent -> terminal bytes (input_encoder.h).
// All methods are static and pure.

class InputEncoder
{
%TypeHeaderCode
#include "input_encoder.h"
%End

public:
    enum class MouseAction
    {
        Press,
        Release,
        Motion,
        WheelUp,
        WheelDown,
    };

    static QByteArray encodeArrowKey(int qkey, bool decCkm = false);

    // Returns bytes, or None when the GUI must act itself (viewport
    // scroll, paste, copy).
    static PyObject *encodeKey(const QKeyEvent &event, bool decCkm = false,
                               int scrollbackLen = 0);
%MethodCode
    const std::optional<QByteArray> result = InputEncoder::encodeKey(*a0, a1, a2);
    if (!result.has_value()) {
        Py_RETURN_NONE;
    }
    return PyBytes_FromStringAndSize(result->constData(),
                                     static_cast<Py_ssize_t>(result->size()));
%End

    static QByteArray encodePaste(const QString &text, bool bracketedPaste = false);
    static QByteArray encodeMouseX10(int x, int y, int button,
                                     MouseAction action, int mods = 0);
    static QByteArray encodeSgrMouse(int x, int y, int button,
                                     MouseAction action, int mods = 0);
};