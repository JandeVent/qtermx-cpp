#ifndef QTERMX_SESSION_H
#define QTERMX_SESSION_H

// The session — reader thread, command queue, snapshots (ADR-0005).
// The single writer of terminal state. A dedicated reader thread owns
// the emulator and the screen: it reads the pty and applies every
// command from the queue (`send_data`, `resize`, `scroll`,
// `scroll_to_bottom`, `close`) in arrival order, serialized with output
// reads. The GUI thread never reads or writes the model — it renders
// from snapshots and posts commands. Qt-free: the GUI layer (Slice B)
// bridges `Snapshot` delivery to queued signals (port of pyqtermx
// session.py).

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "emulator.h"
#include "parser.h"
#include "pty.h"
#include "screen.h"

namespace qtermx {

// What the GUI renders from — immutable, handoff-race-free.
//
// - `dirtyRows`: viewport-row indices whose content changed (empty when
//   `full`); `rows` holds their frozen `Row`s at the same indices.
// - `scrollbackLen` / `viewportOffset`: the scrollbar's range and
//   position (ADR-0006).
// - `cursorRow`/`cursorCol`: (row, col) in grid coordinates; the GUI
//   maps it through the offset.
// - `cursorVisible` / `cursorColor`: the cursor is not hidden (DECTCEM
//   `?25`, shown by default) — the GUI skips the block when false — and
//   the OSC 12 cursor color (`#rrggbb`, nullopt = the default inverted
//   block) the renderer paints the cursor with.
// - `decCkm` / `bracketedPaste` / `reverseVideo`: the input-path and
//   rendition mode flags the GUI needs — it cannot read the model.
//   `reverseVideo` (`?5`) is a *visible* mode: a change forces a full
//   repaint.
// - `mouse1000`/`mouse1002`/`mouse1003`/`mouse1006`: the mouse-tracking
//   modes (`?1000` X10, `?1002` button-event, `?1003` any-event, `?1006`
//   SGR) — the widget routes clicks and wheel to the child when any is
//   set, and picks the encoding by `?1006`.
// - `altScreen`: the alternate screen is active (`?1049`) — no
//   scrollback exists there (ADR-0006), so the wheel becomes Up/Down
//   arrows to the app (line-by-line cursor moves).
// - `full`: the whole viewport must repaint (initial state, resize,
//   offset change, `?5` change, ED3).
// - `contentChanged`: cell content actually changed — the grid had
//   dirty rows before the cursor's old/new rows were added for its
//   repaint. A cursor move alone repaints rows without changing text;
//   the widget uses the flag to clear a selection only when the text
//   under it changed (the same rule as scrolling, ADR-0005).
struct Snapshot {
    std::vector<int> dirtyRows;
    std::vector<Row> rows;
    int scrollbackLen = 0;
    int viewportOffset = 0;
    int cursorRow = 0;
    int cursorCol = 0;
    bool decCkm = false;
    bool bracketedPaste = false;
    bool reverseVideo = false;
    bool mouse1000 = false;
    bool mouse1002 = false;
    bool mouse1003 = false;
    bool mouse1006 = false;
    bool altScreen = false;
    bool full = false;
    bool contentChanged = false;
    bool cursorVisible = true;
    std::optional<std::string> cursorColor;
};

// The headless single-writer core: pty + parser + screen + thread.
// Commands are posted from any thread; snapshots are delivered from the
// reader thread via the optional `snapshotCallback` and appended to
// `snapshots` (the test seam).
class Session {
public:
    using SnapshotCallback = std::function<void(const Snapshot&)>;

    Session(PtyLike* pty, int lines = 24, int columns = 80, int scrollbackLimit = 1000,
            SnapshotCallback callback = nullptr);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // -- Command API (any thread) ---------------------------------------

    void sendData(const std::string& data);
    void resize(int lines, int columns);
    void scroll(int n);
    void scrollToBottom();
    // Replace the default foreground/background colors the emulator
    // reports to OSC 10/11 color queries (hex `#rrggbb` — the
    // `QColor.name(HexRgb)` form). Posted to the reader thread like
    // every command, so theme detection by TUI apps in the child
    // reports the themed colors.
    void setPalette(const std::string& fg, const std::string& bg);
    // Close the pty and stop the reader thread (idempotent). The reader
    // applies the close command itself; join() waits for it.
    void close(double timeout = 5.0);

    // -- Reader thread ---------------------------------------------------

    void start();
    bool isAlive() const { return m_thread.joinable() && !m_finished.load(); }

    // Run the reader thread's per-read step synchronously: feed the data
    // into the parser, flush the write boundary, emit a snapshot. The
    // thread's loop is `select` on the pty + this call — driving it
    // directly is the benchmark seam (bytes → Snapshot, no thread).
    void process(const std::string& data);

    // Attach a snapshot consumer after construction (the GUI bridge
    // wires itself to an already-running session).
    void setSnapshotCallback(SnapshotCallback callback);

    // The snapshots emitted so far (test seam).
    const std::vector<Snapshot>& snapshots() const { return m_snapshots; }

    // The model — the tests read it only after the expected snapshot
    // arrives (the reader thread is quiescent between emissions).
    Screen& screen() { return m_screen; }

private:
    void run();
    bool drainCommands();
    void emit();

    struct Command {
        enum Kind { kSend, kResize, kScroll, kScrollToBottom, kPalette, kClose } kind;
        std::string data;  // send payload / palette fg
        std::string data2; // palette bg
        int a = 0;         // resize lines / scroll n
        int b = 0;         // resize columns
    };

    PtyLike* m_pty;
    int m_lines;
    int m_columns;
    Screen m_screen;
    Emulator m_emulator;
    Parser m_parser;
    SnapshotCallback m_callback;
    std::vector<Snapshot> m_snapshots;
    std::mutex m_mutex; // guards the command queue
    std::vector<Command> m_queue;
    std::thread m_thread;
    std::atomic<bool> m_finished{false};

    // Change tracking for emit() (the Python _last_* mirrors).
    bool m_full = true;
    int m_lastOffset = 0;
    int m_lastCursorRow = 0;
    int m_lastCursorCol = 0;
    bool m_lastDecCkm = false;
    bool m_lastBracketedPaste = false;
    bool m_lastMouse1000 = false;
    bool m_lastMouse1002 = false;
    bool m_lastMouse1003 = false;
    bool m_lastMouse1006 = false;
    bool m_lastReverse = false;
    bool m_lastCursorVisible = true;
    std::optional<std::string> m_lastCursorColor;
};

} // namespace qtermx

#endif // QTERMX_SESSION_H