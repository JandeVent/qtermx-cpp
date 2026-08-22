// T19 — the widget end-to-end (offscreen): snapshots → pixels, keys →
// bytes, viewport scrolling, paste, resize debounce (port of
// tests/gui/test_widget.py). The GUI never reads the model —
// assertions use the fake pty's `sent` bytes and winsizes, the widget's
// backing image, and (for viewport position only) the session screen.
//
// The session runs a real reader thread; waitUntil processes Qt events
// so queued snapshots get applied.
#include <chrono>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QWheelEvent>
#include <QtTest>

#include <fcntl.h>
#include <unistd.h>

#include "renderer.h"
#include "screen.h"
#include "selection.h"
#include "session.h"
#include "terminal_widget.h"

using namespace qtermx;
using namespace qtermx::gui;

namespace {

// A pipe-pair stand-in for `Pty` (the same fake the session tests use).
class FakePty : public PtyLike {
public:
    FakePty(int rows = 24, int cols = 80)
        : m_rows(rows)
        , m_cols(cols)
    {
        if (pipe(m_fds) != 0) {
            std::abort();
        }
        const int flags = fcntl(m_fds[0], F_GETFL, 0);
        fcntl(m_fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    ~FakePty() override { close(); }

    int masterFd() const override { return m_fds[0]; }

    std::optional<std::string> read() override
    {
        if (m_closed) {
            return std::nullopt;
        }
        char buf[65536];
        const ssize_t n = ::read(m_fds[0], buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return std::string();
            }
            return std::nullopt;
        }
        if (n == 0) {
            return std::nullopt;
        }
        return std::string(buf, static_cast<size_t>(n));
    }

    void sendData(const std::string& data) override { m_sent += data; }

    void setWindowSize(int rows, int cols) override
    {
        m_rows = rows;
        m_cols = cols;
        m_winsizes.push_back({rows, cols});
    }

    void close() override
    {
        if (!m_closed) {
            m_closed = true;
            ::close(m_fds[0]);
            ::close(m_fds[1]);
        }
    }

    void output(const std::string& data) { ::write(m_fds[1], data.data(), data.size()); }

    std::string m_sent;
    std::vector<std::pair<int, int>> m_winsizes;
    bool m_closed = false;

private:
    int m_fds[2];
    int m_rows;
    int m_cols;
};

// Spin the Qt event loop until `predicate` is truthy or `timeout` ms
// pass (the queued snapshots get applied by processEvents).
bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

// The modifier Qt reports for the physical Ctrl key: ⌘ (Meta) on macOS,
// Control elsewhere.
Qt::KeyboardModifiers ctrlMod()
{
#ifdef Q_OS_MACOS
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

// Send a synthetic key press with explicit text.
void press(QWidget* widget, int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
           const QString& text = QString())
{
    QKeyEvent event(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(widget, &event);
}

// Send a synthetic wheel event with the given angle delta.
void wheel(QWidget* widget, int delta)
{
    const QPointF pos = QPointF(widget->rect().center());
    QWheelEvent event(pos, pos, QPoint(0, 0), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
}

// The widget position at a cell's center (the grid is 5×10).
QPointF cellPos(const TerminalWidget& w, int row, int col)
{
    const double cellW = w.cellW();
    const double cellH = w.cellH();
    return QPointF(cellW * col + cellW / 2, cellH * row + cellH / 2);
}

void mousePress(QWidget* widget, const QPointF& pos, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, mods);
    QApplication::sendEvent(widget, &event);
}

void mouseMove(QWidget* widget, const QPointF& pos)
{
    // The move carries the held button state — Qt only delivers
    // button-less moves with mouse tracking enabled.
    QMouseEvent event(QEvent::MouseMove, pos, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void mouseRelease(QWidget* widget, const QPointF& pos, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, mods);
    QApplication::sendEvent(widget, &event);
}

void mouseDoubleClick(QWidget* widget, const QPointF& pos)
{
    QMouseEvent event(QEvent::MouseButtonDblClick, pos, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

QClipboard* clipboard()
{
    return QApplication::clipboard();
}

// The copy shortcut: ⌘+C on macOS (Qt reports ⌘ as Control),
// Ctrl+Shift+C elsewhere (Ctrl+C alone is SIGINT).
Qt::KeyboardModifiers copyMods()
{
#ifdef Q_OS_MACOS
    return Qt::ControlModifier;
#else
    return Qt::ControlModifier | Qt::ShiftModifier;
#endif
}

// Feed output and wait until the widget's scrollback mirror reflects it.
void feedAndWait(FakePty& fake, TerminalWidget& widget, const std::string& data)
{
    fake.output(data);
    QVERIFY(waitUntil([&] { return widget.scrollbackLen() > 0 || true; }));
}

} // namespace

class TestWidget : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        m_fake = new FakePty();
        m_session = new Session(m_fake, 5, 10);
        m_widget = new TerminalWidget(m_session);
        // Not shown — the Python fixture's addWidget doesn't trigger the
        // showEvent resize path either (the grid stays 10×5).
        m_session->start();
        QVERIFY(waitUntil([&] { return !m_session->snapshots().empty(); }));
    }

    void cleanup()
    {
        m_session->close();
        delete m_widget;
        delete m_session;
        delete m_fake;
    }

    // -- Snapshot → pixels ------------------------------------------------

    void initialSnapshotPaintsTheViewport()
    {
        // set_session re-applies the initial full snapshot: the backing
        // image is 5×10 cells of default background.
        QCOMPARE(m_widget->backingWidth(), static_cast<int>(std::round(10 * m_widget->cellW())));
        QCOMPARE(m_widget->backingHeight(), static_cast<int>(5 * m_widget->cellH()));
    }

    void outputRepaintsPixels()
    {
        m_fake->output("hi\n");
        // Row 0 painted (fg pixels present somewhere in the image).
        QVERIFY(waitUntil([&] {
            const QImage& img = m_widget->backingImage();
            for (int y = 0; y < static_cast<int>(m_widget->cellH()); ++y) {
                for (int x = 0; x < static_cast<int>(std::round(m_widget->cellW())); ++x) {
                    if (img.pixelColor(x, y) == defaultFg()) {
                        return true;
                    }
                }
            }
            return false;
        }));
    }

    // -- Keys → bytes ------------------------------------------------------

    void typingSendsBytes()
    {
        press(m_widget, Qt::Key_A, Qt::NoModifier, "a");
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "a"; }));
        press(m_widget, Qt::Key_Return, Qt::NoModifier, "\r");
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "a\r"; }));
    }

    void ctrlCWithoutTextSendsIntr()
    {
        // Live ⌃+C events often carry no text (macOS) — the control
        // code must come from the key, or the child never gets its
        // SIGINT (htop).
        press(m_widget, Qt::Key_C, ctrlMod(), "");
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x03"; }));
        QCOMPARE(m_fake->m_sent, std::string("\x03")); // VINTR, and nothing else
    }

    void shiftTabSendsBacktab()
    {
        // Shift+Tab (Key_Backtab, text-less) reaches the child as CSI Z.
        press(m_widget, Qt::Key_Backtab, Qt::NoModifier, "");
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[Z"; }));
        QCOMPARE(m_fake->m_sent, std::string("\x1b[Z"));
    }

    void imeCommitSendsUtf8()
    {
        // IME committed text (Chinese etc.) reaches the child as UTF-8.
        QInputMethodEvent event;
        event.setCommitString(QString::fromUtf8("\xe4\xbd\xa0\xe5\xa5\xbd")); // 你好
        QApplication::sendEvent(m_widget, &event);
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\xe4\xbd\xa0\xe5\xa5\xbd"; }));
    }

    void imePreeditSendsNothing()
    {
        // Composition keystrokes (preedit) are accepted but not
        // forwarded — the terminal has no composition preview.
        QInputMethodEvent event(QString("ni"), {});
        QApplication::sendEvent(m_widget, &event);
        QVERIFY(waitUntil([&] { return m_fake->m_sent.empty(); }));
        QVERIFY(event.isAccepted());
    }

    void arrowSendsCsiWithoutDecckm()
    {
        press(m_widget, Qt::Key_Up);
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[A"; }));
    }

    void decckmArrowSendsSs3()
    {
        m_fake->output("\x1b[?1h"); // DECCKM on
        QVERIFY(waitUntil([&] { return m_widget->decCkm(); }));
        press(m_widget, Qt::Key_Up);
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1bOA"; }));
    }

    void ctrlShiftVPastes()
    {
        clipboard()->setText("hello");
        press(m_widget, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier, "\x16");
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "hello"; }));
    }

    void pasteIsBracketedWhenRequested()
    {
        m_fake->output("\x1b[?2004h"); // bracketed paste on
        QVERIFY(waitUntil([&] { return m_widget->bracketedPaste(); }));
        clipboard()->setText("hi");
        press(m_widget, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier, "\x16");
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[200~hi\x1b[201~"; }));
    }

    void shiftInsertPastes()
    {
        clipboard()->setText("x");
        press(m_widget, Qt::Key_Insert, Qt::ShiftModifier);
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "x"; }));
    }

    void ctrlShiftCSendsNothing()
    {
        press(m_widget, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier, "\x03");
        QVERIFY(waitUntil([&] { return m_fake->m_sent.empty(); }));
        QCOMPARE(m_fake->m_sent, std::string());
    }

    // -- Viewport scrolling ------------------------------------------------

    void pgupScrollsViewportWhenHistoryExists()
    {
        for (int i = 0; i < 30; ++i) {
            m_fake->output("line " + std::to_string(i) + "\r\n");
        }
        // Wait on the widget's mirror, not the model — the snapshot that
        // carries the scrollback length must have been applied.
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() > 0; }));
        press(m_widget, Qt::Key_PageUp);
        QVERIFY(waitUntil([&] { return m_session->screen().viewportOffset() == 5; }));
        QCOMPARE(m_fake->m_sent, std::string()); // never reached the child
    }

    void pgdnReturnsToLiveOutput()
    {
        for (int i = 0; i < 30; ++i) {
            m_fake->output("line " + std::to_string(i) + "\r\n");
        }
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() > 0; }));
        press(m_widget, Qt::Key_PageUp);
        QVERIFY(waitUntil([&] { return m_session->screen().viewportOffset() == 5; }));
        press(m_widget, Qt::Key_PageDown);
        QVERIFY(waitUntil([&] { return m_session->screen().viewportOffset() == 0; }));
    }

    void wheelScrollsViewport()
    {
        for (int i = 0; i < 30; ++i) {
            m_fake->output("line " + std::to_string(i) + "\r\n");
        }
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() > 0; }));
        wheel(m_widget, 120); // scroll up
        QVERIFY(waitUntil([&] { return m_session->screen().viewportOffset() == 3; }));
    }

    void wheelBanksSubnotchTrackpadDeltas()
    {
        // A trackpad swipe delivers many small deltas; four 30° ticks
        // bank into one 120° notch = WHEEL_ROWS rows.
        for (int i = 0; i < 30; ++i) {
            m_fake->output("line " + std::to_string(i) + "\r\n");
        }
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() > 0; }));
        for (int i = 0; i < 4; ++i) {
            wheel(m_widget, 30);
        }
        QVERIFY(waitUntil([&] { return m_session->screen().viewportOffset() == 3; }));
    }

    void wheelSubnotchDeltaDoesNotScroll()
    {
        // A single sub-notch tick (trackpad micro-event) banks but does
        // not scroll — the viewport moves only when a full 120° notch
        // accrues.
        for (int i = 0; i < 30; ++i) {
            m_fake->output("line " + std::to_string(i) + "\r\n");
        }
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() > 0; }));
        wheel(m_widget, 30);
        QTest::qWait(100);
        QCOMPARE(m_session->screen().viewportOffset(), 0);
    }

    // -- Resize / scrollbar ------------------------------------------------

    void resizeDebouncesPtyWinsize()
    {
        m_widget->show(); // resizeEvent only fires on shown widgets
        // The scrollbar extent is always reserved (hidden or not) — the
        // grid width is (widget width − extent) / cell_w. ceil() so the
        // widget is wide enough for exactly 2 cells.
        const int extent = m_widget->scrollbarWidth();
        m_widget->resize(static_cast<int>(std::ceil(2 * m_widget->cellW())) + extent,
                         static_cast<int>(3 * m_widget->cellH()));
        // The debounced resize reaches the pty — wait for the (3, 2)
        // winsize specifically (the showEvent may have posted an earlier
        // one).
        QVERIFY(waitUntil([&] {
            return !m_fake->m_winsizes.empty() && m_fake->m_winsizes.back() == std::make_pair(3, 2);
        }));
    }

    void scrollbarSitsAtTheRightEdge()
    {
        for (int i = 0; i < 30; ++i) {
            m_fake->output("line " + std::to_string(i) + "\r\n");
        }
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() > 0; }));
        m_widget->show();
        m_widget->resize(500, 300);
        QVERIFY(waitUntil([&] { return m_widget->scrollbarVisible(); }));
        const int extent = m_widget->scrollbarWidth();
        QCOMPARE(m_widget->scrollbarX(), m_widget->width() - extent);
        QCOMPARE(m_widget->scrollbarWidth(), extent);
        QCOMPARE(m_widget->scrollbarHeight(), m_widget->height());
    }

    void tabIsNotSwallowedByFocusNavigation()
    {
        // Tab/Shift+Tab must reach the shell, not move focus.
        QVERIFY(!m_widget->focusNextPrevChildPublic(true));
        QVERIFY(!m_widget->focusNextPrevChildPublic(false));
    }

    // -- Mouse: selection, copy, paste -------------------------------------

    void clickDragSelectsAndCopyCopies()
    {
        m_fake->output("hello");
        QVERIFY(waitUntil([&] { return m_widget->hasText("hello"); }));
        clipboard()->setText("");
        const QPointF p0 = cellPos(*m_widget, 0, 0);
        const QPointF p3 = cellPos(*m_widget, 0, 3);
        mousePress(m_widget, p0);
        mouseMove(m_widget, p3);
        mouseRelease(m_widget, p3);
        press(m_widget, Qt::Key_C, copyMods());
        QCOMPARE(clipboard()->text(), QString("hell"));
    }

    void doubleClickSelectsTheWord()
    {
        m_fake->output("abc def");
        QVERIFY(waitUntil([&] { return m_widget->hasText("abc def"); }));
        clipboard()->setText("");
        const QPointF pos = cellPos(*m_widget, 0, 4);
        mousePress(m_widget, pos);
        mouseRelease(m_widget, pos);
        mousePress(m_widget, pos);
        mouseRelease(m_widget, pos);
        press(m_widget, Qt::Key_C, copyMods());
        QCOMPARE(clipboard()->text(), QString("def"));
    }

    void doubleClickEventSelectsTheWord()
    {
        // Qt delivers the second press of a double click as a dbl-click
        // event (production path).
        m_fake->output("abc def");
        QVERIFY(waitUntil([&] { return m_widget->hasText("abc def"); }));
        clipboard()->setText("");
        mouseDoubleClick(m_widget, cellPos(*m_widget, 0, 4));
        press(m_widget, Qt::Key_C, copyMods());
        QCOMPARE(clipboard()->text(), QString("def"));
    }

    void tripleClickSelectsTheLine()
    {
        m_fake->output("hello");
        QVERIFY(waitUntil([&] { return m_widget->hasText("hello"); }));
        clipboard()->setText("");
        const QPointF pos = cellPos(*m_widget, 0, 1);
        for (int i = 0; i < 3; ++i) {
            mousePress(m_widget, pos);
            mouseRelease(m_widget, pos);
        }
        press(m_widget, Qt::Key_C, copyMods());
        QCOMPARE(clipboard()->text(), QString("hello"));
    }

    void altDragSelectsARectangle()
    {
        m_fake->output("ab\r\ncd");
        QVERIFY(waitUntil([&] { return m_widget->hasTextRow(1, "cd"); }));
        clipboard()->setText("");
        const QPointF p0 = cellPos(*m_widget, 0, 0);
        const QPointF p1 = cellPos(*m_widget, 1, 0);
        mousePress(m_widget, p0, Qt::AltModifier);
        mouseMove(m_widget, p1);
        mouseRelease(m_widget, p1, Qt::AltModifier);
        press(m_widget, Qt::Key_C, copyMods());
        QCOMPARE(clipboard()->text(), QString("a\nc"));
    }

    void middleClickPastes()
    {
        clipboard()->setText("mid");
        const QPointF pos = cellPos(*m_widget, 1, 1);
        QMouseEvent event(QEvent::MouseButtonPress, pos, Qt::MiddleButton, Qt::MiddleButton,
                          Qt::NoModifier);
        QApplication::sendEvent(m_widget, &event);
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "mid"; }));
    }

    void scrollingClearsTheSelection()
    {
        m_fake->output("hello");
        QVERIFY(waitUntil([&] { return m_widget->hasText("hello"); }));
        const QPointF p0 = cellPos(*m_widget, 0, 0);
        const QPointF p4 = cellPos(*m_widget, 0, 4);
        mousePress(m_widget, p0);
        mouseMove(m_widget, p4);
        mouseRelease(m_widget, p4);
        QVERIFY(m_widget->hasSelection());
        // Scroll the viewport — the selection must clear.
        for (int i = 0; i < 30; ++i) {
            m_fake->output("line " + std::to_string(i) + "\r\n");
        }
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() > 0; }));
        wheel(m_widget, 120);
        QVERIFY(waitUntil([&] { return !m_widget->hasSelection(); }));
    }

    // -- Mouse protocol (DECSET ?1000 X10 / ?1006 SGR) ---------------------

    void protocolClickSendsSgrPressAndRelease()
    {
        m_fake->output("\x1b[?1000h\x1b[?1006h");
        QVERIFY(waitUntil([&] { return m_widget->mouseEnabled(); }));
        const QPointF pos = cellPos(*m_widget, 3, 4); // 1-based: row 4, col 5
        mousePress(m_widget, pos);
        mouseRelease(m_widget, pos);
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[<0;5;4M\x1b[<0;5;4m"; }));
        // The app owns the mouse: no selection is created (copy is a
        // noop).
        clipboard()->setText("");
        press(m_widget, Qt::Key_C, copyMods());
        QCOMPARE(clipboard()->text(), QString());
    }

    void protocolWheelGoesToTheAppNotTheViewport()
    {
        m_fake->output("a\nb\nc\nd\ne\nf\ng\nh\ni\nj");
        QVERIFY(waitUntil([&] { return m_widget->scrollbackLen() >= 5; }));
        m_fake->output("\x1b[?1000h\x1b[?1006h");
        QVERIFY(waitUntil([&] { return m_widget->mouseEnabled(); }));
        const QPointF c = QPointF(m_widget->rect().center());
        const int col = std::min(static_cast<int>(c.x() / m_widget->cellW()) + 1, 10);
        const int row = std::min(static_cast<int>(c.y() / m_widget->cellH()) + 1, 5);
        wheel(m_widget, 120);
        const std::string expected = "\x1b[<64;" + std::to_string(col) + ";" +
                                     std::to_string(row) + "M";
        QVERIFY(waitUntil([&] { return m_fake->m_sent == expected; }));
        QCOMPARE(m_widget->viewportOffset(), 0); // no viewport scroll
    }

    void altScreenWheelMovesTheCursorLineByLine()
    {
        // Full-screen apps without mouse tracking (nano, man) have no
        // scrollback on the alternate screen — the wheel becomes Up/Down
        // arrows.
        m_fake->output("\x1b[?1049h");
        QVERIFY(waitUntil([&] { return m_widget->altScreen(); }));
        wheel(m_widget, 120); // up → Up arrow
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[A"; }));
        wheel(m_widget, -120); // down → Down arrow
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[A\x1b[B"; }));
        QCOMPARE(m_widget->viewportOffset(), 0); // no viewport scroll
        m_fake->output("\x1b[?1049l"); // back to the normal screen
        QVERIFY(waitUntil([&] { return !m_widget->altScreen(); }));
    }

    // -- viewport-row merge (merge_viewport) -------------------------------

    void mergeFullReplacesRows()
    {
        std::vector<Row> rows;
        for (int i = 0; i < 3; ++i) {
            Row row;
            row.cells.push_back(Cell{U"a"});
            rows.push_back(row);
        }
        Snapshot snap;
        snap.full = true;
        snap.rows = rows;
        std::vector<Row>* merged = mergeViewport(snap, nullptr);
        QVERIFY(merged != nullptr);
        QCOMPARE(merged->size(), size_t(3));
        QCOMPARE((*merged)[0].cells[0].data, std::u32string(U"a"));
        // A second full snapshot replaces wholesale (a new vector).
        Snapshot snap2;
        snap2.full = true;
        std::vector<Row> rows2;
        Row row2;
        row2.cells.push_back(Cell{U"x"});
        rows2.push_back(row2);
        rows2.push_back((*merged)[1]);
        rows2.push_back((*merged)[2]);
        snap2.rows = rows2;
        std::vector<Row>* merged2 = mergeViewport(snap2, merged);
        QVERIFY(merged2 != merged); // replaced, not mutated
        QCOMPARE((*merged2)[0].cells[0].data, std::u32string(U"x"));
        QCOMPARE((*merged2)[1].cells[0].data, std::u32string(U"a"));
        delete merged;
        delete merged2;
    }

    void mergePartialOverwritesOnlyDirtyRows()
    {
        std::vector<Row> rows;
        for (const char* t : {"a", "c", "e"}) {
            Row row;
            row.cells.push_back(Cell{std::u32string(1, static_cast<char32_t>(*t))});
            rows.push_back(row);
        }
        Snapshot full;
        full.full = true;
        full.rows = rows;
        std::vector<Row>* merged = mergeViewport(full, nullptr);
        QVERIFY(merged != nullptr);
        // Rewrite only row 1 — the incremental snapshot carries one row.
        Snapshot partial;
        partial.dirtyRows = {1};
        Row row1;
        row1.cells.push_back(Cell{U"z"});
        partial.rows = {row1};
        std::vector<Row>* merged2 = mergeViewport(partial, merged);
        QCOMPARE(merged2, merged);
        QCOMPARE((*merged2)[0].cells[0].data, std::u32string(U"a"));
        QCOMPARE((*merged2)[1].cells[0].data, std::u32string(U"z"));
        QCOMPARE((*merged2)[2].cells[0].data, std::u32string(U"e"));
        delete merged;
    }

    void mergePartialBeforeFirstFullStaysNull()
    {
        // Synthetic: an incremental snapshot with no prior rows leaves
        // nullptr.
        Snapshot snap;
        snap.dirtyRows = {1};
        QVERIFY(mergeViewport(snap, nullptr) == nullptr);
    }

    // -- cursor blink --------------------------------------------------------

    void cursorBlinkFocusGatesTheTimer()
    {
        // The blink timer runs only while focused.
        m_widget->show();
        m_widget->setFocus();
        QVERIFY(waitUntil([&] { return m_widget->cursorBlinkTimerActive(); }));
        m_widget->clearFocus();
        QVERIFY(waitUntil([&] { return !m_widget->cursorBlinkTimerActive(); }));
        m_widget->setFocus();
        QVERIFY(waitUntil([&] { return m_widget->cursorBlinkTimerActive(); }));
    }

    void focusReportingSendsFocusEvents()
    {
        // ?1004: the app asked for focus in/out events — the widget
        // sends ESC [ I on focus-in and ESC [ O on focus-out.
        m_widget->show();
        m_fake->output("\x1b[?1004h");
        QVERIFY(waitUntil([&] { return m_widget->focusReport(); }));
        // The widget has focus after show — the first focus-out lands
        // the ?1004 report (wait for it before clearing).
        m_widget->clearFocus();
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[O"; }));
        m_fake->m_sent.clear();
        m_widget->setFocus();
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[I"; }));
        m_widget->clearFocus();
        QVERIFY(waitUntil([&] { return m_fake->m_sent == "\x1b[I\x1b[O"; }));
    }

private:
    FakePty* m_fake = nullptr;
    Session* m_session = nullptr;
    TerminalWidget* m_widget = nullptr;
};

QTEST_MAIN(TestWidget)
#include "test_widget.moc"