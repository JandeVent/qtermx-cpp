#ifndef QTERMX_GUI_TERMINAL_WIDGET_H
#define QTERMX_GUI_TERMINAL_WIDGET_H

// The terminal widget (Slice B) — renders snapshots and encodes keys,
// and nothing else (port of pyqtermx widget.py).
//
// The GUI never reads the model (ADR-0005): snapshots arrive from the
// reader thread through a queued invocation; the widget mirrors the
// input-path mode flags (dec_ckm, bracketed_paste) and the scrollbar
// state from the snapshot payloads, posts commands (send/resize/scroll)
// back, and derives its grid geometry from its own size and the font
// metrics.
//
// The CPU paint backend: snapshots render into a persistent QImage
// (partial snapshots repaint only their dirty rows), `paintEvent` blits
// the damaged region. Paint events are scheduled with update(QRect)
// limited to the region the snapshot changed (partial rendering). A
// full repaint re-renders the backing from the merged viewport instead
// of blitting it: the frame heals itself from the last snapshot.

#include <optional>
#include <string>
#include <vector>

#include <QImage>
#include <QPoint>
#include <QProxyStyle>
#include <QRect>
#include <QScrollBar>
#include <QStyleOption>
#include <QTimer>
#include <QWidget>

#include "input_encoder.h"
#include "renderer.h"
#include "screen.h"
#include "selection.h"
#include "session.h"

namespace qtermx::gui {

// Wheel: scroll this many viewport rows per 120° notch.
inline constexpr int kWheelRows = 3;

// Resize debounce: wait this long of stable size before resizing the
// pty.
inline constexpr int kResizeDebounceMs = 50;

// Cursor blink: toggle the cursor's visibility at this cadence while
// the widget has focus (xterm behavior — unfocused cursors stay solid).
inline constexpr int kCursorBlinkMs = 500;

// The default terminal geometry until a resize arrives.
inline constexpr int kDefaultLines = 24;
inline constexpr int kDefaultColumns = 80;

// The scrollbar handle's minimum length in pixels. Qt sizes the handle
// as pageStep / (range + pageStep) of the track, clamped to the
// platform's PM_ScrollBarSliderMin — with a long scrollback that
// minimum is a sliver. This floor keeps the handle grabbable no matter
// how much history accumulates.
inline constexpr int kScrollbarMinHandlePx = 40;

// Fold a snapshot into the widget's persistent viewport grid. `full`
// snapshots replace everything; incremental snapshots overwrite only
// their dirty rows. nullptr until the first `full` snapshot arrives —
// the session always leads with one.
inline std::vector<Row>* mergeViewport(const Snapshot& snapshot, std::vector<Row>* prev)
{
    if (snapshot.full) {
        return new std::vector<Row>(snapshot.rows);
    }
    if (prev == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < snapshot.dirtyRows.size() && i < snapshot.rows.size(); ++i) {
        const int k = snapshot.dirtyRows[i];
        if (k >= 0 && k < static_cast<int>(prev->size())) {
            (*prev)[k] = snapshot.rows[i];
        }
    }
    return prev;
}

// Enforce a minimum scrollbar handle length, native look intact.
class MinHandleStyle : public QProxyStyle {
public:
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override
    {
        if (metric == PM_ScrollBarSliderMin) {
            return kScrollbarMinHandlePx;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

class TerminalWidget : public QWidget {
    Q_OBJECT

public:
    explicit TerminalWidget(Session* session = nullptr, QWidget* parent = nullptr);
    ~TerminalWidget() override;

    // Attach the session: adopt its geometry, repaint its latest state,
    // and deliver every following snapshot on the GUI thread (queued —
    // the callback runs on the reader thread, ADR-0005).
    void setSession(Session* session);

    // Replace the glyph font, re-derive the grid geometry from the new
    // cell metrics, rebuild the backing, and re-render the last
    // snapshot. The pty is resized to the new grid size (debounced like
    // a widget resize).
    void setFont(const QFont& font);

    // Replace the terminal's default colors and repaint the last
    // snapshot with them. The emulator is told too, so OSC 10/11 color
    // queries (TUI theme detection) report the themed colors.
    void setPalette(const QColor& fg, const QColor& bg);

    QSize sizeHint() const override;

    // -- Test seams (the Python tests read the private state the same
    //    way — the m_ prefix waiver applies) ------------------------------

    const QImage& backingImage() const { return m_image; }
    int backingWidth() const { return m_image.width(); }
    int backingHeight() const { return m_image.height(); }
    double cellW() const { return m_renderer.cellW(); }
    double cellH() const { return m_renderer.cellH(); }
    int gridLines() const { return m_lines; }
    int gridColumns() const { return m_columns; }
    bool decCkm() const { return m_decCkm; }
    bool bracketedPaste() const { return m_bracketedPaste; }
    bool mouseEnabled() const { return mouseEnabledImpl(); }
    bool focusReport() const { return m_focusReport; }
    bool altScreen() const { return m_altScreen; }
    int scrollbackLen() const { return m_scrollbackLen; }
    int viewportOffset() const { return m_offset; }
    bool hasSelection() const { return m_selection.has_value(); }
    const std::optional<Selection>& selection() const { return m_selection; }
    const std::vector<Row>* viewportRows() const { return m_viewportRows; }
    bool cursorBlinkTimerActive() const { return m_cursorBlinkTimer->isActive(); }
    bool scrollbarVisible() const { return m_scrollbar->isVisible(); }
    int scrollbarWidth() const { return m_scrollbar->sizeHint().width(); }
    int scrollbarX() const { return m_scrollbar->x(); }
    int scrollbarHeight() const { return m_scrollbar->height(); }
    // Whether the merged viewport shows `text` on row 0 (the widget
    // tests' content probe).
    bool hasText(const std::string& text) const;
    // Whether the merged viewport row `row` shows `text`.
    bool hasTextRow(int row, const std::string& text) const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool focusNextPrevChild(bool nextChild) override;

public:
    // Tab/Shift+Tab must reach the shell, not move focus (public for
    // the tests — the Python test calls it directly).
    bool focusNextPrevChildPublic(bool nextChild) { return focusNextPrevChild(nextChild); }

private:
    // -- Snapshot handling (GUI thread) -----------------------------------

    void applySnapshot(const Snapshot& snapshot);
    void mirrorFlags(const Snapshot& snapshot);
    void updateScrollbar();
    void positionScrollbar();
    void onScrollbar(int value);

    // -- Partial rendering -------------------------------------------------

    QRect snapshotRect(const Snapshot& snapshot) const;
    void requestRepaint(const Snapshot& snapshot);
    void toggleCursorBlink();

    // -- Backend hooks ------------------------------------------------------

    QImage newBacking() const;
    void rebuildBacking();
    void rerenderFull();
    void refresh();
    void repaintCursor();
    void resizeBacking();

    // -- Input ---------------------------------------------------------------

    void handleLocalKey(QKeyEvent* event);
    void copySelection();
    void clearSelection();
    void paste();
    bool mouseEnabledImpl() const;
    std::pair<int, int> cellAt(const QPointF& pos) const;
    void sendMouse(QSinglePointEvent* event, InputEncoder::MouseAction action, int button);
    void recordPress(QMouseEvent* event);
    int nextClickCount(QMouseEvent* event);
    void selectionPress(QMouseEvent* event, int count);
    void applyResize();

    Session* m_session = nullptr;
    TerminalRenderer m_renderer;
    int m_lines = kDefaultLines;
    int m_columns = kDefaultColumns;
    std::optional<Snapshot> m_lastSnapshot;
    std::vector<Row>* m_viewportRows = nullptr; // merged viewport (owned)

    // Mouse: the selection (viewport coordinates) and the press/drag
    // state. The selection is a *local* action — the terminal renders
    // it and copies it, never sends bytes.
    std::optional<Selection> m_selection;
    bool m_mouseDragging = false;
    int m_mouseDragButton = 0;
    std::optional<std::pair<int, int>> m_pressAnchor;
    bool m_pressRectangular = false;
    int m_clickCount = 1;
    QPoint m_lastClickPos;
    qint64 m_lastClickTimeMs = 0;

    QScrollBar* m_scrollbar = nullptr;
    MinHandleStyle* m_scrollbarStyle = nullptr;
    QTimer* m_resizeTimer = nullptr;
    QTimer* m_cursorBlinkTimer = nullptr;
    bool m_cursorBlink = true;
    const char* m_cursorStyle = kCursorBlock;

    // Input-path mode flags, mirrored from snapshots (spec Q8).
    bool m_decCkm = false;
    bool m_bracketedPaste = false;
    bool m_mouse1000 = false;
    bool m_mouse1002 = false;
    bool m_mouse1003 = false;
    bool m_mouse1006 = false;
    bool m_focusReport = false; // ?1004 — focus in/out events to the app
    bool m_altScreen = false;
    // Sub-notch wheel deltas (trackpad) banked for the alt-screen
    // page-key path — one full 120° notch pages the app once.
    int m_wheelAccum = 0;
    int m_scrollbackLen = 0;
    int m_offset = 0;
    // The viewport offset the scrollbar last asked the session to
    // reach — deltas are computed against this, not the last snapshot's
    // offset, so a fast drag's events don't compound.
    int m_lastScrollTarget = 0;

    QImage m_image;
};

} // namespace qtermx::gui

#endif // QTERMX_GUI_TERMINAL_WIDGET_H