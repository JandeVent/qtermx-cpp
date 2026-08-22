#include "session.h"

#include <algorithm>
#include <set>

#include <sys/select.h>
#include <sys/time.h>

namespace qtermx {

Session::Session(PtyLike* pty, int lines, int columns, int scrollbackLimit,
                 int snapshotLimit, SnapshotCallback callback)
    : m_pty(pty)
    , m_lines(lines)
    , m_columns(columns)
    , m_snapshotLimit(snapshotLimit)
    , m_screen(lines, columns, scrollbackLimit)
    , m_emulator(m_screen, [this](const std::string& text) { m_pty->sendData(text); })
    , m_parser(&m_emulator)
    , m_callback(std::move(callback))
{
}

Session::~Session()
{
    close();
}

// -- Command API (any thread) -------------------------------------------

void Session::sendData(const std::string& data)
{
    Command cmd;
    cmd.kind = Command::kSend;
    cmd.data = data;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(std::move(cmd));
}

void Session::resize(int lines, int columns)
{
    Command cmd;
    cmd.kind = Command::kResize;
    cmd.a = lines;
    cmd.b = columns;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(std::move(cmd));
}

void Session::scroll(int n)
{
    Command cmd;
    cmd.kind = Command::kScroll;
    cmd.a = n;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(std::move(cmd));
}

void Session::scrollToBottom()
{
    Command cmd;
    cmd.kind = Command::kScrollToBottom;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(std::move(cmd));
}

void Session::setPalette(const std::string& fg, const std::string& bg)
{
    Command cmd;
    cmd.kind = Command::kPalette;
    cmd.data = fg;
    cmd.data2 = bg;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(std::move(cmd));
}

void Session::close(double timeout)
{
    {
        Command cmd;
        cmd.kind = Command::kClose;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(cmd));
    }
    if (m_thread.joinable()) {
        // Bounded wait for the reader to finish (the Python joins with
        // a timeout): the loop's 50 ms select keeps it live, so the
        // close command lands within the window; the join then
        // completes immediately.
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
        while (std::chrono::steady_clock::now() < deadline && !m_finished.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        m_thread.join();
    }
}

// -- Reader thread -------------------------------------------------------

void Session::start()
{
    m_thread = std::thread(&Session::run, this);
}

void Session::process(const std::string& data)
{
    // Chunking invariant (T6): feeding in pty-sized chunks produces
    // exactly the same result as one big feed — the conformance harness
    // and the bench `_feed` both rely on it.
    m_parser.feedBytes(data);
    m_parser.flush();
    emitSnapshot();
}

void Session::setSnapshotCallback(SnapshotCallback callback)
{
    m_callback = std::move(callback);
}

void Session::run()
{
    try {
        emitSnapshot(); // the initial full snapshot (blank screen)
        while (true) {
            if (!drainCommands()) {
                break; // a `close` was applied — stop the loop
            }
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_pty->masterFd(), &fds);
            struct timeval tv {0, 50000}; // 50 ms
            const int ready = select(m_pty->masterFd() + 1, &fds, nullptr, nullptr, &tv);
            if (ready < 0) {
                break; // pty closed under us, or never existed
            }
            if (ready > 0) {
                const auto data = m_pty->read();
                if (!data.has_value()) {
                    // Child exited — emit the final state and stop.
                    emitSnapshot();
                    break;
                }
                if (!data->empty()) {
                    process(*data);
                }
            }
            emitSnapshot();
        }
    } catch (...) {
        // The reader thread must never die with an exception escaping —
        // the session owns the model, and the GUI waits on snapshots.
    }
    // The cleanup tail runs on every exit path (the Python's `finally`):
    // the finished flag (isAlive() must go false) and the pty close
    // (EOF/SIGHUP to the child).
    m_finished.store(true);
    m_pty->close();
}

bool Session::drainCommands()
{
    // Apply every queued command in arrival order. False once a `close`
    // was applied (the loop must stop).
    while (true) {
        Command cmd;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty()) {
                return true;
            }
            cmd = m_queue.front();
            m_queue.erase(m_queue.begin());
        }
        switch (cmd.kind) {
        case Command::kClose:
            return false;
        case Command::kSend:
            m_pty->sendData(cmd.data);
            break;
        case Command::kResize:
            m_screen.resize(cmd.a, cmd.b);
            m_full = true;
            m_pty->setWindowSize(cmd.a, cmd.b);
            break;
        case Command::kScroll:
            m_screen.scroll(cmd.a);
            break;
        case Command::kScrollToBottom:
            m_screen.scrollToBottom();
            break;
        case Command::kPalette:
            m_emulator.setPalette(cmd.data, cmd.data2);
            break;
        }
    }
}

void Session::emitSnapshot()
{
    // Emit a snapshot when anything visible changed. Rows are frozen
    // objects — the handoff needs no locks and no copies.
    Screen& screen = m_screen;
    std::set<int> dirty = screen.takeDirtyRows();
    // True when the grid itself changed — before the cursor's repaint
    // rows join `dirty`: a cursor move repaints rows without changing
    // text, and the selection must survive that.
    const bool contentChanged = !dirty.empty();
    const int offset = screen.viewportOffset();
    const int cursorRow = screen.cursor.y;
    const int cursorCol = screen.cursor.x;
    const bool decCkm = screen.mode(1, true);
    const bool bracketedPaste = screen.mode(2004, true);
    const bool mouse1000 = screen.mode(1000, true);
    const bool mouse1002 = screen.mode(1002, true);
    const bool mouse1003 = screen.mode(1003, true);
    const bool mouse1006 = screen.mode(1006, true);
    const bool focusReport = screen.mode(1004, true);
    const bool reverse = screen.mode(5, true); // DECSCNM — a visible mode
    const bool cursorVisible = screen.mode(kDectcem, true); // DECTCEM — a visible mode
    const std::optional<std::string>& cursorColor = m_emulator.cursorColor();
    const bool full = m_full || offset != m_lastOffset || reverse != m_lastReverse;
    const bool modeChanged = decCkm != m_lastDecCkm || bracketedPaste != m_lastBracketedPaste ||
                             mouse1000 != m_lastMouse1000 || mouse1002 != m_lastMouse1002 ||
                             mouse1003 != m_lastMouse1003 || mouse1006 != m_lastMouse1006 ||
                             focusReport != m_lastFocusReport;
    if (cursorRow != m_lastCursorRow || cursorCol != m_lastCursorCol) {
        // A cursor move repaints its old and new rows.
        dirty.insert(m_lastCursorRow);
        dirty.insert(cursorRow);
    }
    if (cursorVisible != m_lastCursorVisible) {
        // A visibility flip repaints the cursor row too — the block is
        // painted over the row, so hiding it must repaint the row (like
        // a cursor move: no text changed, the selection lives).
        dirty.insert(m_lastCursorRow);
        dirty.insert(cursorRow);
    }
    if (cursorColor != m_lastCursorColor) {
        // An OSC 12 color change repaints the cursor row too — the
        // block color is visible state, like a visibility flip.
        dirty.insert(m_lastCursorRow);
        dirty.insert(cursorRow);
    }
    if (!full && dirty.empty() && !modeChanged) {
        return;
    }
    Snapshot snap;
    if (full) {
        // Initial state, resize, offset change, ED3: everything.
        snap.rows.reserve(screen.lines);
        for (int k = 0; k < screen.lines; ++k) {
            snap.rows.push_back(screen.viewportRow(k));
        }
    } else if (!dirty.empty()) {
        // A dirty grid row `y` shows at viewport row `y + offset`; rows
        // scrolled off the viewport need no repaint.
        std::set<int> vrows;
        for (const int y : dirty) {
            const int k = y + offset;
            if (k < screen.lines) {
                vrows.insert(k);
            }
        }
        if (vrows.empty() && !modeChanged) {
            return;
        }
        snap.dirtyRows.assign(vrows.begin(), vrows.end());
        snap.rows.reserve(vrows.size());
        for (const int k : vrows) {
            snap.rows.push_back(screen.viewportRow(k));
        }
    }
    // A mode change alone: the GUI updates its input mirror.
    snap.scrollbackLen = screen.scrollbackLen();
    snap.viewportOffset = offset;
    snap.cursorRow = cursorRow;
    snap.cursorCol = cursorCol;
    snap.decCkm = decCkm;
    snap.bracketedPaste = bracketedPaste;
    snap.mouse1000 = mouse1000;
    snap.mouse1002 = mouse1002;
    snap.mouse1003 = mouse1003;
    snap.mouse1006 = mouse1006;
    snap.focusReport = focusReport;
    snap.altScreen = screen.altScreen();
    snap.reverseVideo = reverse;
    snap.full = full;
    snap.contentChanged = contentChanged;
    snap.cursorVisible = cursorVisible;
    snap.cursorColor = cursorColor;
    m_snapshots.push_back(snap);
    // Trim old snapshots to prevent unbounded memory growth.
    if (m_snapshotLimit > 0 && static_cast<int>(m_snapshots.size()) > m_snapshotLimit) {
        m_snapshots.erase(m_snapshots.begin(),
                          m_snapshots.begin() + (m_snapshots.size() - m_snapshotLimit));
    }
    if (m_callback) {
        m_callback(snap);
    }
    m_lastOffset = offset;
    m_lastCursorRow = cursorRow;
    m_lastCursorCol = cursorCol;
    m_lastDecCkm = decCkm;
    m_lastBracketedPaste = bracketedPaste;
    m_lastMouse1000 = mouse1000;
    m_lastMouse1002 = mouse1002;
    m_lastMouse1003 = mouse1003;
    m_lastMouse1006 = mouse1006;
    m_lastFocusReport = focusReport;
    m_lastReverse = reverse;
    m_lastCursorVisible = cursorVisible;
    m_lastCursorColor = cursorColor;
    m_full = false;
}

} // namespace qtermx