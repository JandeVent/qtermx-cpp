#include "terminal_widget.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include "utf8_decoder.h"

namespace qtermx::gui {

namespace {

// The modifier Qt reports for the physical Ctrl key: ⌘ (MetaModifier)
// on macOS, ControlModifier elsewhere.
Qt::KeyboardModifiers ctrlMod()
{
#ifdef Q_OS_MACOS
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

} // namespace

TerminalWidget::TerminalWidget(Session* session, QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // IME (Chinese/Japanese/etc.) delivers composed text as
    // QInputMethodEvent — only widgets with input methods enabled
    // receive them (spec Q8).
    setAttribute(Qt::WA_InputMethodEnabled);
    // No background erase: paintEvent fills everything itself.
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_scrollbarStyle = std::make_unique<MinHandleStyle>();
    m_scrollbar = new QScrollBar(Qt::Vertical, this);
    m_scrollbar->setStyle(m_scrollbarStyle.get());
    connect(m_scrollbar, &QScrollBar::valueChanged, this, &TerminalWidget::onScrollbar);
    m_scrollbar->hide();

    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(kResizeDebounceMs);
    connect(m_resizeTimer, &QTimer::timeout, this, &TerminalWidget::applyResize);

    // Cursor blink: the widget's own phase, toggled at kCursorBlinkMs
    // while focused; the renderer ANDs it with the snapshot's DECTCEM
    // visibility, so `?25l`/`?25h` always win.
    m_cursorBlinkTimer = new QTimer(this);
    m_cursorBlinkTimer->setInterval(kCursorBlinkMs);
    connect(m_cursorBlinkTimer, &QTimer::timeout, this, &TerminalWidget::toggleCursorBlink);

    m_image = newBacking();
    if (session != nullptr) {
        setSession(session);
    }
}

TerminalWidget::~TerminalWidget()
{
    // Detach from the session before the widget dies — the reader
    // thread must never hand snapshots to a deleted widget.
    if (m_session != nullptr) {
        m_session->setSnapshotCallback(nullptr);
    }
}

void TerminalWidget::setSession(Session* session)
{
    m_session = session;
    // The callback runs on the reader thread — hand the snapshot to the
    // GUI thread (queued invocation, ADR-0005). The snapshot is copied
    // (immutable — the handoff needs no locks).
    session->setSnapshotCallback([this](const Snapshot& snapshot) {
        QMetaObject::invokeMethod(this, [this, snapshot] { applySnapshot(snapshot); },
                                  Qt::QueuedConnection);
    });
    m_lines = session->screen().lines;
    m_columns = session->screen().columns;
    rebuildBacking();
    if (!session->snapshots().empty()) {
        applySnapshot(session->snapshots().back());
    }
    if (hasFocus()) {
        m_cursorBlinkTimer->start();
    }
}

void TerminalWidget::setFont(const QFont& font)
{
    m_renderer.setFont(font);
    if (m_session != nullptr) {
        const int lines = std::max(1, static_cast<int>(height() / m_renderer.cellH()));
        const int columns = std::max(1, static_cast<int>(width() / m_renderer.cellW()));
        m_lines = lines;
        m_columns = columns;
        m_session->resize(lines, columns);
    }
    rebuildBacking();
    refresh();
}

void TerminalWidget::setPalette(const QColor& fg, const QColor& bg)
{
    m_renderer.setPalette(fg, bg);
    if (m_session != nullptr) {
        m_session->setPalette(fg.name(QColor::HexRgb).toStdString(),
                              bg.name(QColor::HexRgb).toStdString());
    }
    refresh();
}

QSize TerminalWidget::sizeHint() const
{
    // Logical size: the backing image is dpr-scaled, layout wants
    // logical (device-independent) pixels.
    const double dpr = devicePixelRatioF();
    return QSize(static_cast<int>(std::round(m_image.width() / dpr)),
                 static_cast<int>(std::round(m_image.height() / dpr)));
}

// -- Snapshot handling (GUI thread) ---------------------------------------

void TerminalWidget::applySnapshot(const Snapshot& snapshot)
{
    // GUI thread: merge into the viewport rows, repaint, mirror the
    // scrollbar and mode flags. `render` repaints only the snapshot's
    // dirty rows into the backing image (the merged viewport carries
    // the others); `requestRepaint` limits the paint event to the
    // region they cover — partial rendering end to end. New output
    // invalidates the selection first (the dirty-row repaint below
    // would leave the reversed cells behind).
    if (snapshot.full || snapshot.contentChanged) {
        clearSelection(); // the text under it changed (scroll rule)
    }
    m_lastSnapshot = snapshot;
    std::unique_ptr<std::vector<Row>> merged = mergeViewport(snapshot, m_viewportRows.get());
    if (merged != nullptr) {
        m_viewportRows = std::move(merged);
    }
    const std::vector<int>* rowIndices = snapshot.full ? nullptr : &snapshot.dirtyRows;
    m_renderer.render(m_image, snapshot, m_viewportRows.get(), rowIndices,
                      m_selection.has_value() ? &*m_selection : nullptr, std::nullopt,
                      m_cursorStyle);
    mirrorFlags(snapshot);
    requestRepaint(snapshot);
}

void TerminalWidget::mirrorFlags(const Snapshot& snapshot)
{
    // The scrollbar and input-path mode flags, mirrored from the
    // snapshot payload.
    m_decCkm = snapshot.decCkm;
    m_bracketedPaste = snapshot.bracketedPaste;
    m_mouse1000 = snapshot.mouse1000;
    m_mouse1002 = snapshot.mouse1002;
    m_mouse1003 = snapshot.mouse1003;
    m_mouse1006 = snapshot.mouse1006;
    m_focusReport = snapshot.focusReport;
    m_altScreen = snapshot.altScreen;
    m_scrollbackLen = snapshot.scrollbackLen;
    m_offset = snapshot.viewportOffset;
    // DECTCEM overwrites the blink phase on every snapshot: `?25h`
    // re-anchors it visible (new output snaps the cursor solid),
    // `?25l` forces it hidden — the timer free-runs underneath.
    m_cursorBlink = snapshot.cursorVisible;
    updateScrollbar();
}

void TerminalWidget::updateScrollbar()
{
    m_scrollbar->blockSignals(true);
    m_scrollbar->setRange(0, m_scrollbackLen);
    // The handle represents the viewport: pageStep = visible lines, so
    // the handle is the on-screen fraction of the history (and
    // groove-clicks page by a screenful, not 10 lines).
    m_scrollbar->setPageStep(m_lines);
    if (!m_scrollbar->isSliderDown()) {
        // The handle follows the mouse while dragging — snapping it to
        // the last processed offset mid-drag makes it resist.
        m_scrollbar->setValue(m_scrollbackLen - m_offset); // top = oldest
        m_lastScrollTarget = m_offset;
    }
    m_scrollbar->blockSignals(false);
    m_scrollbar->setVisible(m_scrollbackLen > 0);
    positionScrollbar();
}

void TerminalWidget::positionScrollbar()
{
    // Right edge, full height.
    const int extent = m_scrollbar->sizeHint().width();
    m_scrollbar->setGeometry(width() - extent, 0, extent, height());
}

void TerminalWidget::onScrollbar(int value)
{
    clearSelection(); // the viewport content changes under it
    if (m_session != nullptr) {
        const int target = m_scrollbackLen - value;
        m_session->scroll(target - m_lastScrollTarget);
        m_lastScrollTarget = target;
    }
}

// -- Partial rendering ------------------------------------------------------

QRect TerminalWidget::snapshotRect(const Snapshot& snapshot) const
{
    // The pixel rect a snapshot changed — `update()` gets exactly this,
    // so a paint event covers only the damaged region, not the whole
    // frame (partial rendering). `full` snapshots repaint the whole
    // grid; snapshots with no dirty rows changed nothing visible and
    // repaint nothing.
    const int widthPx = static_cast<int>(std::round(m_columns * m_renderer.cellW()));
    const int heightPx = static_cast<int>(m_lines * m_renderer.cellH());
    if (snapshot.full) {
        return QRect(0, 0, widthPx, heightPx);
    }
    if (snapshot.dirtyRows.empty()) {
        return QRect();
    }
    const int first = *std::min_element(snapshot.dirtyRows.begin(), snapshot.dirtyRows.end());
    const int last = *std::max_element(snapshot.dirtyRows.begin(), snapshot.dirtyRows.end());
    return QRect(0, static_cast<int>(first * m_renderer.cellH()), widthPx,
                 static_cast<int>((last - first + 1) * m_renderer.cellH()));
}

void TerminalWidget::requestRepaint(const Snapshot& snapshot)
{
    // Schedule a repaint of exactly the region `snapshot` changed.
    const QRect rect = snapshotRect(snapshot);
    if (!rect.isEmpty()) {
        update(rect);
    }
}

void TerminalWidget::toggleCursorBlink()
{
    // Blink tick: flip the cursor phase and repaint only the cursor row
    // (partial rendering — the renderer ANDs the phase with the
    // snapshot's DECTCEM visibility, so a cursor the app hid stays
    // hidden).
    m_cursorBlink = !m_cursorBlink;
    repaintCursor();
}

// -- Backend hooks -----------------------------------------------------------

QImage TerminalWidget::newBacking() const
{
    // A fresh backing image at the widget's device-pixel ratio:
    // rendered 1:1 with the physical pixels so the blit never upscales
    // (a 1× raster blitted to a 2× Retina surface is blurry).
    const double dpr = devicePixelRatioF();
    QImage image(std::max(1, static_cast<int>(std::round(m_columns * m_renderer.cellW() * dpr))),
                 std::max(1, static_cast<int>(std::round(m_lines * m_renderer.cellH() * dpr))),
                 QImage::Format_RGB32);
    image.setDevicePixelRatio(dpr);
    image.fill(m_renderer.defaultBgColor()); // the terminal background, not pure black
    return image;
}

void TerminalWidget::rebuildBacking()
{
    m_image = newBacking();
}

void TerminalWidget::rerenderFull()
{
    // Render the whole frame from the merged viewport — the source of
    // truth. Never from the last snapshot alone: an incremental one
    // would paint only its dirty rows into the image, blanking every
    // other row.
    if (m_lastSnapshot.has_value()) {
        m_renderer.render(m_image, *m_lastSnapshot, m_viewportRows.get(), nullptr,
                          m_selection.has_value() ? &*m_selection : nullptr, m_cursorBlink,
                          m_cursorStyle);
    }
}

void TerminalWidget::refresh()
{
    // Re-render the backing with the current selection and repaint —
    // the image is the source of truth (the blit only copies it), so a
    // selection change must re-render it. The blink phase rides along
    // (cursor_visible override), so a re-render doesn't un-blink a
    // cursor that was mid-hidden-phase.
    rerenderFull();
    update();
}

void TerminalWidget::repaintCursor()
{
    // Re-render only the cursor row with the current blink phase and
    // cursor style — the minimal repaint: one row's raster, one row's
    // update rect. Off-viewport cursors and app-hidden cursors (DECTCEM
    // `?25l`) draw nothing.
    if (!m_lastSnapshot.has_value() || !m_lastSnapshot->cursorVisible) {
        return;
    }
    const int row = m_lastSnapshot->cursorRow + m_lastSnapshot->viewportOffset;
    if (row < 0 || row >= m_lines) {
        return;
    }
    const std::vector<int> rowIndices = {row};
    m_renderer.render(m_image, *m_lastSnapshot, m_viewportRows.get(), &rowIndices,
                      m_selection.has_value() ? &*m_selection : nullptr, m_cursorBlink,
                      m_cursorStyle);
    update(QRect(0, static_cast<int>(row * m_renderer.cellH()),
                 static_cast<int>(std::round(m_columns * m_renderer.cellW())),
                 static_cast<int>(m_renderer.cellH())));
}

void TerminalWidget::resizeBacking()
{
    rebuildBacking();
    // Best-effort repaint of the last state — no stale frame at the old
    // grid size while the reader resizes (the next snapshot is full and
    // replaces this).
    rerenderFull();
}

// -- Input -------------------------------------------------------------------

void TerminalWidget::keyPressEvent(QKeyEvent* event)
{
    if (event == nullptr || m_session == nullptr) {
        return;
    }
    // Activity reset: typing snaps the cursor solid immediately (the
    // echoed output re-anchors it again via the snapshot).
    m_cursorBlink = true;
    repaintCursor();
    const auto data = InputEncoder::encodeKey(*event, m_decCkm, m_scrollbackLen);
    if (!data.has_value()) {
        handleLocalKey(event);
        return;
    }
    m_session->sendData(data->toStdString());
    m_session->scrollToBottom();
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent* event)
{
    // IME input (spec Q8): forward the committed text to the child as
    // UTF-8. The composition (preedit) has no on-screen representation
    // in this terminal, so only the commit string is sent — the event
    // is still accepted so the IME keeps working.
    if (event == nullptr || m_session == nullptr) {
        return;
    }
    const QString text = event->commitString();
    if (!text.isEmpty()) {
        m_session->sendData(text.toUtf8().toStdString());
        m_session->scrollToBottom();
    }
    event->accept();
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    // IME geometry queries (spec Q8): the candidate window anchors to
    // the cursor cell, in widget coordinates.
    if (query == Qt::ImCursorRectangle) {
        if (m_lastSnapshot.has_value()) {
            const int row = m_lastSnapshot->cursorRow + m_offset; // grid → viewport
            const int col = m_lastSnapshot->cursorCol;
            if (row >= 0 && row < m_lines) {
                return QRect(static_cast<int>(std::round(col * m_renderer.cellW())),
                             static_cast<int>(std::round(row * m_renderer.cellH())),
                             static_cast<int>(std::round(m_renderer.cellW())),
                             static_cast<int>(std::round(m_renderer.cellH())));
            }
        }
        return QRect(0, 0, 0, 0);
    }
    if (query == Qt::ImEnabled) {
        return true;
    }
    if (query == Qt::ImFont) {
        return m_renderer.font();
    }
    return QWidget::inputMethodQuery(query);
}

void TerminalWidget::handleLocalKey(QKeyEvent* event)
{
    // Local keys — handled by the terminal itself, never sent to the
    // child (xterm's 'local' action category).
    if (m_session == nullptr) {
        return;
    }
    const int qkey = event->key();
    if (qkey == Qt::Key_PageUp || qkey == Qt::Key_PageDown) {
        clearSelection(); // the viewport content changes
        const int sign = qkey == Qt::Key_PageUp ? 1 : -1; // scroll(n): + is up
        m_session->scroll(sign * m_lines);
    } else if (qkey == Qt::Key_V &&
               (event->modifiers() & (Qt::ControlModifier | ctrlMod())) != 0) {
        paste();
    } else if (qkey == Qt::Key_Insert &&
               (event->modifiers() & Qt::ShiftModifier) != 0) {
        paste();
    } else if (qkey == Qt::Key_C &&
               (event->modifiers() & (Qt::ControlModifier | ctrlMod())) != 0) {
        // ⌘+C on macOS (Qt reports ⌘ as Control) / Ctrl+Shift+C
        // elsewhere: copy the selection. Plain Ctrl+C is a control code
        // (SIGINT) and never reaches this branch.
        copySelection();
    }
}

void TerminalWidget::copySelection()
{
    // Copy the selection — a local action: the selection is the
    // terminal's, not the child's, so no bytes are sent (xterm's
    // 'local' copy).
    if (!m_selection.has_value() || m_viewportRows == nullptr) {
        return;
    }
    QClipboard* clipboard = QApplication::clipboard();
    if (clipboard == nullptr) {
        return;
    }
    clipboard->setText(QString::fromStdString(selectedText(*m_viewportRows, *m_selection)));
}

void TerminalWidget::clearSelection()
{
    // The viewport content changed under the selection: it selects
    // *visible* rows, and scrolling or new output changes what the rows
    // show — the GUI holds no scrollback text to re-identify
    // (ADR-0005), so keeping the selection would copy the wrong text.
    if (m_selection.has_value()) {
        m_selection.reset();
        refresh();
    }
}

void TerminalWidget::paste()
{
    if (m_session == nullptr) {
        return;
    }
    QClipboard* clipboard = QApplication::clipboard();
    if (clipboard == nullptr) {
        return;
    }
    const QString text = clipboard->text();
    m_session->sendData(InputEncoder::encodePaste(text, m_bracketedPaste).toStdString());
    m_session->scrollToBottom();
}

void TerminalWidget::wheelEvent(QWheelEvent* event)
{
    if (event == nullptr || m_session == nullptr) {
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        return;
    }
    if (mouseEnabledImpl()) {
        // The app asked for the mouse: the wheel is its input (htop
        // scrolls its own list) — never the viewport's. SGR (?1006)
        // encodes wheel as buttons 64/65, legacy X10 as 4/5 (xterm
        // parity); wheel releases are not reported (xterm doesn't).
        m_wheelAccum = 0;
        sendMouse(event, delta > 0 ? InputEncoder::MouseAction::WheelUp
                                   : InputEncoder::MouseAction::WheelDown,
                  0);
        return;
    }
    clearSelection();
    if (m_altScreen) {
        // A full-screen app without mouse tracking (nano, man, less
        // with a mouse-less config…) has no scrollback to scroll — the
        // wheel becomes Up/Down arrows, so the cursor moves line by
        // line. Sub-notch deltas bank up, so a trackpad swipe scrolls
        // at a human rate (one line per 120° notch).
        m_wheelAccum += delta;
        const int pages = m_wheelAccum / 120;
        m_wheelAccum %= 120;
        if (pages != 0) {
            const QByteArray data = InputEncoder::encodeArrowKey(
                pages > 0 ? Qt::Key_Up : Qt::Key_Down, m_decCkm);
            for (int i = 0; i < std::abs(pages); ++i) {
                m_session->sendData(data.toStdString());
            }
        }
        return;
    }
    // Sub-notch trackpad deltas bank up in row-sized units (one row per
    // 120/WHEEL_ROWS degrees), so a swipe scrolls smoothly — one row at
    // a time, proportional to the gesture; a physical mouse with 120°
    // notches still scrolls WHEEL_ROWS rows per notch.
    m_wheelAccum += delta;
    const int rows = m_wheelAccum / (120 / kWheelRows);
    m_wheelAccum %= (120 / kWheelRows);
    if (rows != 0) {
        m_session->scroll(rows);
    }
}

// -- Mouse -------------------------------------------------------------------

bool TerminalWidget::mouseEnabledImpl() const
{
    // Mouse tracking active: the child owns the mouse (any of
    // `?1000`/`?1002`/`?1003`), so clicks and wheel forward to it and
    // selection is disabled.
    return m_mouse1000 || m_mouse1002 || m_mouse1003;
}

std::pair<int, int> TerminalWidget::cellAt(const QPointF& pos) const
{
    // The viewport (row, col) under a widget position, clamped. The
    // column divides by the *float* cell width — the renderer paints
    // cell C at C × cellW (fractional advance), so truncating cellW to
    // int first drifts the hit-test right of the painted grid (the
    // Python oracle divides by the float cell_w). The position is
    // truncated to int first, mirroring Python's int(pos.x()) // cell_w.
    const int row = static_cast<int>(pos.y()) / static_cast<int>(m_renderer.cellH());
    const int col = static_cast<int>(
        std::floor(static_cast<int>(pos.x()) / m_renderer.cellW()));
    return {std::clamp(row, 0, m_lines - 1), std::clamp(col, 0, m_columns - 1)};
}

void TerminalWidget::sendMouse(QSinglePointEvent* event, InputEncoder::MouseAction action, int button)
{
    // Forward a mouse event to the child — SGR (`?1006`) or X10
    // (`?1000`), the modern-terminal set. Coordinates are 1-based;
    // modifiers map to the xterm bits (4 shift, 8 alt, 16 ctrl — ⌘
    // counts as ctrl on macOS, matching kitty).
    if (m_session == nullptr) {
        return;
    }
    const QPointF pos = event->position();
    // 1-based, dividing by the float cell width (see cellAt — the
    // renderer's grid is fractional, an int cellW drifts the protocol
    // coordinates off the painted cells).
    const int col = std::clamp(
        static_cast<int>(std::floor(static_cast<int>(pos.x()) / m_renderer.cellW())) + 1, 1,
        m_columns);
    const int row = std::clamp(static_cast<int>(pos.y()) / static_cast<int>(m_renderer.cellH()) + 1,
                               1, m_lines);
    int mods = 0;
    const Qt::KeyboardModifiers m = event->modifiers();
    if ((m & Qt::ShiftModifier) != 0) {
        mods += 4;
    }
    if ((m & Qt::AltModifier) != 0) {
        mods += 8;
    }
    if ((m & (Qt::ControlModifier | ctrlMod())) != 0) {
        mods += 16;
    }
    const QByteArray data = m_mouse1006 ? InputEncoder::encodeSgrMouse(col, row, button, action, mods)
                                        : InputEncoder::encodeMouseX10(col, row, button, action, mods);
    m_session->sendData(data.toStdString());
}

void TerminalWidget::recordPress(QMouseEvent* event)
{
    // Anchor the click-count clock at the press — Qt counts
    // double-clicks from consecutive *press* positions, and a release
    // position (e.g. a drag's end) must not look like a click.
    m_lastClickPos = event->position().toPoint();
    m_lastClickTimeMs = QDateTime::currentMSecsSinceEpoch();
}

int TerminalWidget::nextClickCount(QMouseEvent* event)
{
    // Qt's click counting: a press at the same position within the
    // double-click interval of the previous press is click #2, #3…
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QPoint pos = event->position().toPoint();
    if (pos == m_lastClickPos &&
        now - m_lastClickTimeMs < QApplication::doubleClickInterval()) {
        return m_clickCount + 1;
    }
    return 1;
}

void TerminalWidget::selectionPress(QMouseEvent* event, int count)
{
    // Start (or extend) the selection at the click cell: a single click
    // cancels any selection (selection is drag-driven), a double-click
    // selects the word, a triple-click the line; Alt switches to
    // rectangular mode. The drag anchor is recorded here and never
    // changes for the whole drag — extend() gets it explicitly.
    m_mouseDragging = true;
    const auto [row, col] = cellAt(event->position());
    const bool rectangular = (event->modifiers() & Qt::AltModifier) != 0;
    m_pressRectangular = rectangular;
    if (count == 1) {
        // A bare click selects nothing — it cancels the selection. The
        // drag anchor is the press cell; the first cell-changing move
        // then creates the selection (drag-only selection).
        if (m_selection.has_value()) {
            m_selection.reset();
            refresh();
        }
        m_pressAnchor = {row, col};
        return;
    }
    if (count >= 3) {
        m_selection = line(row, m_columns);
    } else {
        m_selection = word(row, col, m_viewportRows != nullptr ? *m_viewportRows
                                                               : std::vector<Row>{});
    }
    // The drag anchor: for word/line selections it's the selection's
    // start (dragging extends the word/line from its beginning).
    m_pressAnchor = {m_selection->row1, m_selection->col1};
    refresh();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr || m_session == nullptr) {
        return;
    }
    event->accept();
    m_clickCount = nextClickCount(event);
    recordPress(event);
    if (mouseEnabledImpl()) {
        // The app owns the mouse: forward the press, never select.
        int button = -1;
        if (event->button() == Qt::LeftButton) {
            button = 0;
        } else if (event->button() == Qt::MiddleButton) {
            button = 1;
        } else if (event->button() == Qt::RightButton) {
            button = 2;
        }
        if (button >= 0) {
            m_mouseDragButton = button;
            sendMouse(event, InputEncoder::MouseAction::Press, m_mouseDragButton);
            m_mouseDragging = true;
        }
        return;
    }
    if (event->button() == Qt::LeftButton) {
        selectionPress(event, m_clickCount);
    } else if (event->button() == Qt::MiddleButton) {
        paste(); // middle-click paste (kitty/wezterm behavior)
    }
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Qt delivers the second press of a double click as this event (not
    // a press) — treat it as press #2.
    if (event == nullptr || m_session == nullptr) {
        return;
    }
    event->accept();
    if (mouseEnabledImpl()) {
        int button = 0;
        if (event->button() == Qt::LeftButton) {
            button = 0;
        } else if (event->button() == Qt::MiddleButton) {
            button = 1;
        } else if (event->button() == Qt::RightButton) {
            button = 2;
        }
        m_mouseDragButton = button;
        sendMouse(event, InputEncoder::MouseAction::Press, m_mouseDragButton);
        m_mouseDragging = true;
        return;
    }
    m_clickCount = 2;
    recordPress(event);
    selectionPress(event, 2);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event == nullptr || m_session == nullptr) {
        return;
    }
    event->accept();
    if (mouseEnabledImpl()) {
        // `?1003` tracks every motion, `?1002` only while a button is
        // held; `?1000` alone sends no motion at all.
        if (m_mouse1003) {
            sendMouse(event, InputEncoder::MouseAction::Motion, m_mouseDragButton);
        } else if (m_mouse1002 && m_mouseDragging) {
            sendMouse(event, InputEncoder::MouseAction::Motion, m_mouseDragButton);
        }
        return;
    }
    if (m_mouseDragging && m_pressAnchor.has_value()) {
        const auto [row, col] = cellAt(event->position());
        if (row == m_pressAnchor->first && col == m_pressAnchor->second) {
            return; // same-cell jitter: still a click, not a drag
        }
        m_selection = extend(m_pressAnchor->first, m_pressAnchor->second, row, col,
                             m_pressRectangular);
        refresh();
    }
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event == nullptr || m_session == nullptr) {
        return;
    }
    event->accept();
    if (mouseEnabledImpl()) {
        if (m_mouseDragging) {
            sendMouse(event, InputEncoder::MouseAction::Release, m_mouseDragButton);
            m_mouseDragging = false;
        }
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_mouseDragging = false;
        m_pressAnchor.reset();
        // A release away from the press is a drag, not a click — the
        // next press must not count as a double-click, so re-dragging
        // the same range backwards stays a fresh drag.
        if (cellAt(event->position()) != cellAt(QPointF(m_lastClickPos))) {
            m_lastClickPos = QPoint();
            m_lastClickTimeMs = 0;
        }
    }
}

void TerminalWidget::closeEvent(QCloseEvent* event)
{
    // Detach from the session before the widget dies — the reader
    // thread must never hand snapshots to a deleted widget.
    m_cursorBlinkTimer->stop();
    if (m_session != nullptr) {
        m_session->setSnapshotCallback(nullptr);
    }
    QWidget::closeEvent(event);
}

void TerminalWidget::focusInEvent(QFocusEvent* event)
{
    // Focus starts the blink (xterm: the cursor blinks only while the
    // terminal is focused) and restores the block cursor — the phase
    // re-anchors solid first, so the cursor appears solid and starts
    // blinking from there.
    QWidget::focusInEvent(event);
    m_cursorBlink = true;
    m_cursorStyle = kCursorBlock;
    repaintCursor();
    m_cursorBlinkTimer->start();
    // Focus reporting (?1004): the app asked for focus in/out events.
    if (m_focusReport && m_session != nullptr) {
        m_session->sendData("\x1b[I");
    }
}

void TerminalWidget::focusOutEvent(QFocusEvent* event)
{
    // Unfocused: stop the blink and freeze the cursor as a hollow
    // rectangle around the cell — no flicker in the background, and the
    // character underneath stays visible.
    QWidget::focusOutEvent(event);
    m_cursorBlinkTimer->stop();
    m_cursorBlink = true;
    m_cursorStyle = kCursorOutline;
    repaintCursor();
    // Focus reporting (?1004): the app asked for focus in/out events.
    if (m_focusReport && m_session != nullptr) {
        m_session->sendData("\x1b[O");
    }
}

void TerminalWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    positionScrollbar();
    m_resizeTimer->start(); // debounced → pty winsize
}

void TerminalWidget::applyResize()
{
    if (m_session == nullptr) {
        return;
    }
    // The scrollbar extent is always reserved — hidden or not — so a
    // scrollbar appearing never shrinks the grid and clips content.
    const int extent = m_scrollbar->sizeHint().width();
    const int widthPx = std::max(1, width() - extent);
    const int heightPx = std::max(1, height());
    const int lines = std::max(1, static_cast<int>(heightPx / m_renderer.cellH()));
    const int columns = std::max(1, static_cast<int>(widthPx / m_renderer.cellW()));
    if (lines == m_lines && columns == m_columns) {
        return; // unchanged: no resize to post (spec §7)
    }
    m_lines = lines;
    m_columns = columns;
    resizeBacking();
    update();
    m_session->resize(lines, columns);
}

void TerminalWidget::changeEvent(QEvent* event)
{
    // Rebuild the backing when the widget moves to a screen with a
    // different scale — otherwise the raster would be at the old ratio
    // there (blurry again).
    QWidget::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::DevicePixelRatioChange) {
        rebuildBacking();
        rerenderFull();
        update();
    }
}

void TerminalWidget::showEvent(QShowEvent* event)
{
    applyResize();
    QWidget::showEvent(event);
}

bool TerminalWidget::focusNextPrevChild(bool nextChild)
{
    // Tab/Shift+Tab must reach the shell, not move focus.
    (void)nextChild;
    return false;
}

// -- Painting ------------------------------------------------------------------

void TerminalWidget::paintEvent(QPaintEvent* event)
{
    // Fill the whole dirty area with the terminal background first: the
    // grid may not tile the widget (slivers at bottom/right), and
    // WA_OpaquePaintEvent means no background erase (no flicker).
    QPainter painter(this);
    const QRect rect = event != nullptr ? event->rect() : this->rect();
    painter.fillRect(rect, m_renderer.defaultBgColor());
    const double dpr = devicePixelRatioF();
    // A backing built before the widget was shown on a scaled screen is
    // 1x — rebuild it lazily rather than upscale the blit.
    const bool dprMismatch = m_image.devicePixelRatio() != dpr;
    if (dprMismatch) {
        rebuildBacking();
    }
    const QRect grid(0, 0, static_cast<int>(std::round(m_image.width() / dpr)),
                     static_cast<int>(std::round(m_image.height() / dpr)));
    // The whole grid damaged — the backing may be stale (the compositor
    // dropped the window surface on display sleep/wake, the rebuild just
    // cleared it): re-render the frame instead of blitting it. Partial
    // repaints blit (the common path).
    if (dprMismatch || rect.contains(grid)) {
        rerenderFull();
    }
    // Blit only the damaged region — the source rect is in the image's
    // device pixels, the target in logical coordinates, so Qt maps 1:1
    // physical pixels (partial rendering).
    const QRect src = rect.intersected(grid);
    if (!src.isEmpty()) {
        const QRect srcDevice(static_cast<int>(std::round(src.left() * dpr)),
                              static_cast<int>(std::round(src.top() * dpr)),
                              static_cast<int>(std::round(src.width() * dpr)),
                              static_cast<int>(std::round(src.height() * dpr)));
        painter.drawImage(src.topLeft(), m_image, srcDevice);
    }
    painter.end();
}

bool TerminalWidget::hasText(const std::string& text) const
{
    // Whether the merged viewport shows `text` on row 0 (the widget
    // tests' content probe — the Python reads the model's render()).
    return hasTextRow(0, text);
}

bool TerminalWidget::hasTextRow(int row, const std::string& text) const
{
    if (m_viewportRows == nullptr || row < 0 || row >= static_cast<int>(m_viewportRows->size())) {
        return false;
    }
    std::string rowText;
    for (const Cell& cell : (*m_viewportRows)[row].cells) {
        rowText += encodeUtf8(cell.data);
    }
    return rowText.find(text) != std::string::npos;
}

} // namespace qtermx::gui

// moc handled by AUTOMOC