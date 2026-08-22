// T17 — Reader thread, command queue & snapshots (ADR-0005) (port of
// tests/session/test_session.py). The seam: `Session` (reader thread as
// the single writer) driven through a pipe-based FakePty for
// deterministic queue/threading tests. Assertions read the model only
// after the expected snapshot arrives (the reader thread is quiescent
// between emissions), and check snapshot payloads (dirty rows, viewport,
// cursor) as the GUI would receive them.
//
// The session is Qt-free — snapshot delivery here is a plain callback +
// an append-only list; the GUI layer (Slice B) bridges it to Qt signals.
//
// Not ported: the end-to-end tests that spawn a real pty child
// (test_child_output_lands_on_screen, test_close_stops_thread_and_
// closes_pty, test_child_exit_stops_reader_and_reaps,
// test_scrollback_len_reaches_snapshot, the fake-shell session tests,
// test_osc_color_query_answered_through_real_pty) — environment-
// sensitive in a GUI session, like the pty tests; the FakePty tests
// cover the same session logic deterministically.
#include "harness.h"
#include "session.h"
#include "test_pipeline.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "utf8_decoder.h"

using namespace qtermx;
using namespace qtermx::test;

namespace {

// A pipe-pair stand-in for `Pty`: the test writes the child's "output"
// to the pipe; everything the session sends lands in `sent`; resizes
// are recorded. read() returns nullopt once closed.
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

    // Simulate the child writing `data`.
    void output(const std::string& data) { ::write(m_fds[1], data.data(), data.size()); }

    std::string m_sent;
    std::vector<std::pair<int, int>> m_winsizes;
    bool m_closed = false;

private:
    int m_fds[2];
    int m_rows;
    int m_cols;
};

// Poll `predicate` until it is truthy or `timeout` seconds pass.
bool waitFor(const std::function<bool()>& pred, double timeout = 5.0)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

// The row's cells as text (trailing blanks stripped).
std::string rowText(const Row& row)
{
    std::string text;
    for (const Cell& cell : row.cells) {
        text += encodeUtf8(cell.data);
    }
    const size_t last = text.find_last_not_of(' ');
    return last == std::string::npos ? "" : text.substr(0, last + 1);
}

} // namespace

// -- Deterministic queue/threading tests (FakePty) ----------------------

TEST_CASE(session_initial_snapshot_is_full)
{
    FakePty fake;
    Session session(&fake, 3, 4);
    session.start();
    QTERMX_CHECK(waitFor([&] { return !session.snapshots().empty(); }));
    const Snapshot& snap = session.snapshots()[0];
    QTERMX_CHECK(snap.full);
    QTERMX_CHECK(snap.dirtyRows.empty());
    QTERMX_CHECK(snap.cursorRow == 0 && snap.cursorCol == 0);
    QTERMX_CHECK(snap.scrollbackLen == 0);
    QTERMX_CHECK(snap.rows.size() == 3);
    session.close();
}

TEST_CASE(session_output_produces_incremental_snapshots)
{
    FakePty fake;
    Session session(&fake, 3, 4);
    session.start();
    fake.output("AB\n");
    QTERMX_CHECK(waitFor([&] {
        return !session.snapshots().empty() && !session.snapshots().back().dirtyRows.empty();
    }));
    const Snapshot& snap = session.snapshots().back();
    QTERMX_CHECK(!snap.full);
    QTERMX_CHECK(snap.dirtyRows.size() == 2 && snap.dirtyRows[0] == 0 && snap.dirtyRows[1] == 1);
    QTERMX_CHECK(snap.contentChanged); // the grid text changed
    QTERMX_CHECK(rowText(snap.rows[0]) == "AB");
    QTERMX_CHECK(snap.cursorRow == 1 && snap.cursorCol == 2);
    session.close();
}

TEST_CASE(session_send_data_posts_bytes_to_child)
{
    FakePty fake;
    Session session(&fake);
    session.start();
    session.sendData("ping\r");
    QTERMX_CHECK(waitFor([&] { return fake.m_sent == "ping\r"; }));
    session.close();
}

TEST_CASE(session_process_runs_the_read_step_synchronously)
{
    // The benchmark seam: `process` performs the reader thread's
    // per-read step (feed, flush, emit) in the caller's thread — the
    // same observable behavior as a pty read, without the thread.
    FakePty fake;
    Session session(&fake, 3, 4);
    session.process(""); // the initial full emit, as _run does on start
    session.process("AB\n");
    QTERMX_CHECK(!session.snapshots().empty()); // emitted synchronously
    const Snapshot& snap = session.snapshots().back();
    QTERMX_CHECK(!snap.full);
    QTERMX_CHECK(snap.dirtyRows.size() == 2 && snap.dirtyRows[0] == 0 && snap.dirtyRows[1] == 1);
    QTERMX_CHECK(rowText(snap.rows[0]) == "AB");
    QTERMX_CHECK(snap.cursorRow == 1 && snap.cursorCol == 2);
    session.close();
}

TEST_CASE(session_resize_reflows_and_reaches_pty)
{
    FakePty fake;
    Session session(&fake, 10, 80);
    session.start();
    fake.output("ABCDEFGH");
    QTERMX_CHECK(waitFor([&] { return session.snapshots().size() > 1; }));
    session.resize(10, 4);
    QTERMX_CHECK(waitFor([&] { return session.snapshots().back().full; })); // resize → full repaint
    QTERMX_CHECK(fake.m_winsizes.size() == 1 && fake.m_winsizes[0] == std::make_pair(10, 4));
    QTERMX_CHECK(session.screen().columns == 4);
    const auto lines = splitLines(session.screen().render());
    QTERMX_CHECK(lines[0] == "ABCD");
    QTERMX_CHECK(lines[1] == "EFGH");
    session.close();
}

TEST_CASE(session_scroll_commands_move_offset_and_emit_full)
{
    FakePty fake;
    Session session(&fake, 5, 4);
    session.start();
    // CRLF lines — one clean row each (LF-only would wrap mid-line).
    // The last line has no terminator, so no trailing row is consumed.
    std::string flood;
    for (int i = 0; i < 29; ++i) {
        flood += "L" + std::to_string(i) + "\r\n";
    }
    flood += "L29";
    fake.output(flood);
    QTERMX_CHECK(waitFor([&] {
        return !session.snapshots().empty() && session.snapshots().back().scrollbackLen == 25;
    }));

    session.scroll(5);
    QTERMX_CHECK(waitFor([&] { return session.snapshots().back().viewportOffset == 5; }));
    const Snapshot& snap = session.snapshots().back();
    QTERMX_CHECK(snap.full);
    QTERMX_CHECK(snap.scrollbackLen == 25);
    QTERMX_CHECK(rowText(session.screen().viewportRow(0)) == "L20");

    session.scrollToBottom();
    QTERMX_CHECK(waitFor([&] { return session.snapshots().back().viewportOffset == 0; }));
    QTERMX_CHECK(rowText(session.screen().viewportRow(0)) == "L25");
    session.close();
}

TEST_CASE(session_cursor_move_is_snapshotted)
{
    FakePty fake;
    Session session(&fake, 10, 80);
    session.start();
    fake.output("AB\r\n");
    QTERMX_CHECK(waitFor([&] {
        return !session.snapshots().empty() &&
               session.snapshots().back().cursorRow == 1 && session.snapshots().back().cursorCol == 0;
    }));
    fake.output("\x1b[5;5H"); // CUP → cursor (4, 4)
    QTERMX_CHECK(waitFor([&] {
        return session.snapshots().back().cursorRow == 4 && session.snapshots().back().cursorCol == 4;
    }));
    const Snapshot& snap = session.snapshots().back();
    QTERMX_CHECK(!snap.full);
    QTERMX_CHECK(snap.dirtyRows.size() == 2 && snap.dirtyRows[0] == 1 && snap.dirtyRows[1] == 4);
    QTERMX_CHECK(!snap.contentChanged); // no text changed — only the cursor moved
    session.close();
}

TEST_CASE(session_cursor_visibility_flip_is_snapshotted)
{
    // DECTCEM ?25: a visibility flip repaints the cursor row (the block
    // is painted over it) without changing text — the selection survives.
    FakePty fake;
    Session session(&fake, 10, 80);
    session.start();
    fake.output("AB\r\n");
    QTERMX_CHECK(waitFor([&] {
        return !session.snapshots().empty() &&
               session.snapshots().back().cursorRow == 1 && session.snapshots().back().cursorCol == 0;
    }));
    fake.output("\x1b[?25l"); // hide the cursor
    QTERMX_CHECK(waitFor([&] { return !session.snapshots().back().cursorVisible; }));
    const Snapshot& snap = session.snapshots().back();
    QTERMX_CHECK(!snap.full);
    QTERMX_CHECK(snap.dirtyRows.size() == 1 && snap.dirtyRows[0] == 1); // cursor row repaints
    QTERMX_CHECK(!snap.contentChanged); // no text changed
    fake.output("\x1b[?25h"); // show it again
    QTERMX_CHECK(waitFor([&] { return session.snapshots().back().cursorVisible; }));
    QTERMX_CHECK(session.snapshots().back().dirtyRows.size() == 1 &&
                 session.snapshots().back().dirtyRows[0] == 1);
    session.close();
}

TEST_CASE(session_decset_mouse_modes_reach_snapshots)
{
    FakePty fake;
    Session session(&fake, 5, 10);
    session.start();
    QTERMX_CHECK(waitFor([&] { return !session.snapshots().empty() && session.snapshots().back().full; }));
    fake.output("\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h");
    QTERMX_CHECK(waitFor([&] {
        return !session.snapshots().empty() && session.snapshots().back().mouse1000 &&
               session.snapshots().back().mouse1006;
    }));
    const Snapshot& snap = session.snapshots().back();
    QTERMX_CHECK(snap.mouse1000 && snap.mouse1002);
    QTERMX_CHECK(snap.mouse1003 && snap.mouse1006);
    // DECRST clears the mirror; the other modes stay.
    fake.output("\x1b[?1006l");
    QTERMX_CHECK(waitFor([&] { return !session.snapshots().back().mouse1006; }));
    QTERMX_CHECK(session.snapshots().back().mouse1000);
    session.close();
}

TEST_CASE(session_mouse_modes_default_to_off)
{
    FakePty fake;
    Session session(&fake, 3, 4);
    session.start();
    QTERMX_CHECK(waitFor([&] { return !session.snapshots().empty(); }));
    const Snapshot& snap = session.snapshots()[0];
    QTERMX_CHECK(!snap.mouse1000 && !snap.mouse1002);
    QTERMX_CHECK(!snap.mouse1003 && !snap.mouse1006);
    session.close();
}

// -- OSC color queries (Phase 5) ----------------------------------------

TEST_CASE(session_osc_query_reply_reaches_child)
{
    // The emulator answers the child's color query through the pty —
    // the detection path TUI apps rely on for light/dark theme.
    FakePty fake;
    Session session(&fake);
    session.start();
    fake.output("\x1b]11;?\x07");
    QTERMX_CHECK(waitFor([&] { return fake.m_sent == "\x1b]11;rgb:1010/1010/1010\x07"; }));
    session.close();
}

TEST_CASE(session_set_palette_command_updates_color_replies)
{
    // `set_palette` (posted like every command) makes queries report
    // the themed colors — a light-theme app must not answer "dark".
    FakePty fake;
    Session session(&fake);
    session.start();
    session.setPalette("#ffffff", "#000000");
    session.sendData("\x00"); // marker — queued after set_palette
    // The palette command is applied before the marker is written
    // (queue order), so once the marker lands the new colors are in.
    QTERMX_CHECK(waitFor([&] { return fake.m_sent.find("\x00") != std::string::npos; }));
    fake.output("\x1b]10;?\x07\x1b]11;?\x07");
    QTERMX_CHECK(waitFor([&] {
        return fake.m_sent ==
               "\x00\x1b]10;rgb:ffff/ffff/ffff\x07\x1b]11;rgb:0000/0000/0000\x07";
    }));
    session.close();
}

TEST_CASE(session_osc12_cursor_color_flows_into_snapshots)
{
    // OSC 12 from the child lands in snapshots as `cursor_color` — the
    // renderer's block color (visible state, like DECTCEM). The change
    // repaints the cursor row without touching text (selection survives).
    FakePty fake;
    std::vector<Snapshot> snapshots;
    Session session(&fake, 24, 80, 1000, 1000, [&](const Snapshot& s) { snapshots.push_back(s); });
    session.start();
    fake.output("x\x1b]12;#1a1a1a\x07");
    QTERMX_CHECK(waitFor([&] {
        return !snapshots.empty() && snapshots.back().cursorColor == std::optional<std::string>("#1a1a1a");
    }));
    QTERMX_CHECK(snapshots.back().contentChanged); // the "x" changed text
    QTERMX_CHECK(snapshots.back().cursorRow == 0 && snapshots.back().cursorCol == 1);
    fake.output("\x1b]12;#ffffff\x07");
    QTERMX_CHECK(waitFor([&] {
        return !snapshots.empty() && snapshots.back().cursorColor == std::optional<std::string>("#ffffff");
    }));
    // The color-only change repaints the cursor row: dirty rows carry
    // it, text stays untouched.
    QTERMX_CHECK(!snapshots.back().contentChanged);
    QTERMX_CHECK(snapshots.back().cursorRow == 0 && snapshots.back().cursorCol == 1);
    fake.output("\x1b]112\x07");
    QTERMX_CHECK(waitFor([&] { return !snapshots.back().cursorColor.has_value(); }));
    session.close();
}