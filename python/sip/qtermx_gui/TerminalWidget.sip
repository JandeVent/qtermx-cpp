// TerminalWidget — the CPU-rendered terminal widget (terminal_widget.h,
// Qt layer). The main public API: a QWidget that renders snapshots,
// encodes keys, and posts commands to the Session.

class TerminalWidget : QWidget
{
%TypeHeaderCode
#include "terminal_widget.h"
%End

public:
    // /KeepReference/ pins the Session for the widget's lifetime: the
    // widget stores a raw Session* and touches it in key events, resize
    // and its destructor — a GC'd session would leave a dangling
    // pointer behind.
    TerminalWidget(Session *session /KeepReference/ = nullptr, QWidget *parent = nullptr);
    ~TerminalWidget();

    // -- Session ---------------------------------------------------------
    void setSession(Session *session);

    // -- Theming ---------------------------------------------------------
    void setFont(const QFont &font);
    void setPalette(const QColor &fg, const QColor &bg);

    // -- Geometry --------------------------------------------------------
    QSize sizeHint() const;

    // -- Test seams (public for tests) -----------------------------------
    const QImage &backingImage() const;
    int backingWidth() const;
    int backingHeight() const;
    double cellW() const;
    double cellH() const;
    int gridLines() const;
    int gridColumns() const;
    bool decCkm() const;
    bool bracketedPaste() const;
    bool mouseEnabled() const;
    bool focusReport() const;
    bool altScreen() const;
    int scrollbackLen() const;
    int viewportOffset() const;
    bool hasSelection() const;
    bool hasText(const std::string &text) const;
    bool hasTextRow(int row, const std::string &text) const;
    bool scrollbarVisible() const;

    // Tab/Shift+Tab must reach the shell, not move focus.
    bool focusNextPrevChildPublic(bool nextChild);
};