// Screen — the terminal grid model: cells, cursor, modes, scrollback
// (screen.h). Session owns it; Python sees it through Session.screen().

class Screen
{
%TypeHeaderCode
#include "screen.h"
%End

public:
    Screen(int lines = 24, int columns = 80, int scrollbackLimit = 1000);

    // Public data members mirror the C++ API surface.
    int lines;
    int columns;
    int scrollbackLimit;
    Cursor cursor;

    // -- Read API -------------------------------------------------------
    Row &line(int y);
    std::string render() const;

    // -- Viewport (scrollback read API) ---------------------------------
    int scrollbackLen() const;
    int viewportOffset() const;
    bool altScreen() const;
    const Row &viewportRow(int k) const;
    void scroll(int n);
    void scrollToBottom();
    void clearScrollback();

    // -- Change tracking ------------------------------------------------
    // Returns a Python set[int]; the rows are consumed and cleared.
    PyObject *takeDirtyRows();
%MethodCode
    PyObject *result = PySet_New(nullptr);
    if (result == nullptr) {
        return nullptr;
    }
    for (int y : sipCpp->takeDirtyRows()) {
        PyObject *item = PyLong_FromLong(y);
        if (item == nullptr || PySet_Add(result, item) < 0) {
            Py_XDECREF(item);
            Py_DECREF(result);
            return nullptr;
        }
        Py_DECREF(item);
    }
    return result;
%End

    // -- Printing -------------------------------------------------------
    // C++ takes std::u32string_view; declaring std::u32string keeps the
    // binding simple (implicit conversion at the call site).
    void print(const std::u32string &text);

    // -- Graphic rendition ----------------------------------------------
    void resetRendition();
    void setFg(int color);
    void setBg(int color);
    void setBold(bool on = true);
    void setDim(bool on = true);
    void setItalic(bool on = true);
    void setUnderline(bool on = true);
    void setBlink(bool on = true);
    void setReverse(bool on = true);
    void setHidden(bool on = true);
    void setStrike(bool on = true);
    void setOverline(bool on = true);

    // -- Modes ----------------------------------------------------------
    bool mode(int number, bool privateMode = false) const;
    void setMode(int number, bool privateMode = false);
    void resetMode(int number, bool privateMode = false);

    // -- Scroll region --------------------------------------------------
    void setScrollRegion(int top, int bottom);

    // -- Cursor motion --------------------------------------------------
    void carriageReturn();
    void lineFeed();
    void backspace();
    void tab();
    void setTabStop();
    void clearTabStop(int mode);
    void tabForward(int n = 1);
    void tabBackward(int n = 1);
    void cursorUp(int n = 1);
    void cursorDown(int n = 1);
    void cursorForward(int n = 1);
    void cursorBackward(int n = 1);
    void cursorNextLine(int n = 1);
    void cursorPrecedingLine(int n = 1);
    void setCursor(int x, int y);
    void index();
    void reverseIndex();

    // -- Erase ----------------------------------------------------------
    void eraseInDisplay(int mode = 0);
    void eraseInLine(int mode = 0);
    void eraseChars(int n = 1);

    // -- Charsets -------------------------------------------------------
    void designateCharset(const std::string &designator, const std::string &charset);
    void shiftCharset(int level);

    // -- Save / restore -------------------------------------------------
    void saveState();
    void restoreState();

    // -- Alternate screen -----------------------------------------------
    // Returns (fg, bg) with reverse video applied.
    void effectiveRendition(int x, int y, int &fg /Out/, int &bg /Out/) const;
    void decaln();
    void enterAltScreen();
    void leaveAltScreen();

    // -- Resize ---------------------------------------------------------
    void resize(int lines, int columns);

    // -- Scrolling ------------------------------------------------------
    void scrollUp(int n = 1);
    void scrollDown(int n = 1);

    // -- Row ops --------------------------------------------------------
    void insertLines(int n = 1);
    void deleteLines(int n = 1);
    void insertChars(int n = 1);
    void deleteChars(int n = 1);
};