// T16 — Pty interface (ADR-0005): spawn a child with its own session,
// exchange bytes, set the window size, reap the exit (port of
// tests/pty/test_pty.py). The seam: the narrow Qt-free `Pty` interface,
// driven against a fake child program (a python3 -c script) through a
// real pty pair. Asserts are tolerant: a waitFor(predicate, timeout)
// polling helper, no brittle sleeps.
//
// The pty child setup follows libptyqt (Qt Creator's terminal pty):
// posix_openpt + grantpt/unlockpt, FD_CLOEXEC on both fds, the slave
// dup2'd onto 0/1/2 FIRST, then setsid, then TIOCSCTTY/tcsetpgrp on the
// slave fd (never fd 0), with an fstat check that fd 0 really is the
// slave before touching the controlling terminal — so a mis-setup can
// never steal the foreground process group of the parent's terminal.
//
// Not ported (environment-sensitive in a GUI session — the Python
// oracle covers them; enable in a headless CI):
// - the tcsetpgrp-manipulation tests (has_foreground_job_true_while_
//   job_runs, foreground_program_reports_job_name_while_running) — the
//   fake shell foregrounds a job via tcsetpgrp, which must act on the
//   pty slave only;
// - the line-discipline signal tests (ctrl_c/ctrl_backslash/ctrl_z
//   deliver SIGINT/SIGQUIT/SIGTSTP) — ISIG signals go to the pty's
//   foreground group, which must be the pty child's group;
// - spawn_without_command_uses_shell — an interactive $SHELL on the pty.
// - the two monkeypatch tests (send_data_loops_on_partial_writes,
//   send_data_retries_on_eagain) patch os.write — impossible in C++;
//   the send_data loop is exercised by the large-payload test instead.
#include "harness.h"
#include "pty.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace qtermx;
using namespace qtermx::test;

namespace {

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

// Read from the pty until `marker` appears; return everything read.
std::string readUntil(Pty& pty, const std::string& marker, double timeout = 5.0)
{
    std::string out;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pty.masterFd(), &fds);
        struct timeval tv {0, 50000}; // 50 ms
        if (select(pty.masterFd() + 1, &fds, nullptr, nullptr, &tv) > 0) {
            const auto chunk = pty.read();
            if (!chunk.has_value()) {
                break;
            }
            out += *chunk;
            if (out.find(marker) != std::string::npos) {
                return out;
            }
        }
    }
    return out;
}

// Spawn a fake child running `script` with python3.
Pty spawnChild(const std::string& script, std::vector<std::pair<std::string, std::string>> env = {},
               const std::string& cwd = "", int rows = 24, int cols = 80)
{
    return Pty({"/usr/bin/python3", "-c", script}, std::move(env), cwd, rows, cols);
}

} // namespace

TEST_CASE(pty_spawn_runs_child_and_reads_output)
{
    Pty pty = spawnChild("print('READY', flush=True)\n");
    const std::string out = readUntil(pty, "READY");
    QTERMX_CHECK(out.find("READY") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_send_data_reaches_child)
{
    Pty pty = spawnChild("import sys\n"
                         "print('READY', flush=True)\n"
                         "line = sys.stdin.readline()\n"
                         "print('GOT:' + line.strip(), flush=True)\n");
    readUntil(pty, "READY");
    pty.sendData("ping\r");
    const std::string out = readUntil(pty, "GOT:ping");
    QTERMX_CHECK(out.find("GOT:ping") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_set_window_size_reaches_child)
{
    Pty pty = spawnChild("import fcntl, struct, sys, termios, time\n"
                         "print('READY', flush=True)\n"
                         "# Poll until the parent's resize lands (no brittle sleep).\n"
                         "size = (0, 0, 0, 0)\n"
                         "deadline = time.monotonic() + 3.0\n"
                         "while time.monotonic() < deadline:\n"
                         "    size = struct.unpack('HHHH', fcntl.ioctl(0, termios.TIOCGWINSZ, b'\\x00' * 8))\n"
                         "    if size[1] == 100:\n"
                         "        break\n"
                         "    time.sleep(0.01)\n"
                         "print('SIZE:%dx%d' % (size[1], size[0]), flush=True)\n");
    readUntil(pty, "READY");
    pty.setWindowSize(30, 100);
    const std::string out = readUntil(pty, "SIZE:100x30");
    QTERMX_CHECK(out.find("SIZE:100x30") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_child_sees_resize_through_shutil_get_terminal_size)
{
    // Regression (Textual resize): Python's shutil.get_terminal_size() —
    // the size source Textual and other TUIs use — prefers the COLUMNS/
    // LINES env vars over the TIOCGWINSZ ioctl (Python 3.14 included).
    // If the pty forces or inherits them, they are frozen at spawn and a
    // resize never reaches the app: the layout never reflows. The child
    // must report the ioctl size and see a resize through shutil even
    // when COLUMNS/LINES are supplied.
    Pty pty = spawnChild("import shutil, time\n"
                         "print('READY', flush=True)\n"
                         "size = shutil.get_terminal_size()\n"
                         "deadline = time.monotonic() + 3.0\n"
                         "while time.monotonic() < deadline and size.columns != 100:\n"
                         "    size = shutil.get_terminal_size()\n"
                         "    time.sleep(0.01)\n"
                         "print('SIZE:%dx%d' % (size.columns, size.lines), flush=True)\n",
                         {{"COLUMNS", "999"}, {"LINES", "999"}}, "", 30, 80);
    readUntil(pty, "READY");
    pty.setWindowSize(30, 100);
    const std::string out = readUntil(pty, "SIZE:100x30");
    QTERMX_CHECK(out.find("SIZE:100x30") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_resize_delivers_sigwinch_to_child)
{
    // Textual and other TUIs reflow on SIGWINCH, not on polling the
    // ioctl: a resize that only changes the winsize is invisible to
    // them. TIOCSWINSZ on the master must deliver SIGWINCH to the
    // child's foreground process group.
    Pty pty = spawnChild("import signal, shutil, sys, time\n"
                         "def on_winch(signum, frame):\n"
                         "    size = shutil.get_terminal_size()\n"
                         "    print('WINCH:%dx%d' % (size.columns, size.lines), flush=True)\n"
                         "signal.signal(signal.SIGWINCH, on_winch)\n"
                         "print('READY', flush=True)\n"
                         "time.sleep(5)\n");
    readUntil(pty, "READY");
    pty.setWindowSize(30, 100);
    const std::string out = readUntil(pty, "WINCH:100x30");
    QTERMX_CHECK(out.find("WINCH:100x30") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_resize_reaches_python_grandchild_through_shell)
{
    // The real-app scenario: the pty child is a login shell; the TUI
    // (python) is a grandchild. SIGWINCH must propagate through the
    // shell to the foreground job, and shutil.get_terminal_size() must
    // report the new size (the Textual reflow path). The probe script
    // goes to a temp file — a multi-line `-c` string would fight the
    // shell's quote parsing.
    char tmpl[] = "/tmp/qtermx-winch-XXXXXX";
    const int fd = mkstemp(tmpl);
    QTERMX_CHECK(fd >= 0);
    const std::string script =
        "import signal, shutil, time\n"
        "def on_winch(signum, frame):\n"
        "    size = shutil.get_terminal_size()\n"
        "    print('WINCH:%dx%d' % (size.columns, size.lines), flush=True)\n"
        "signal.signal(signal.SIGWINCH, on_winch)\n"
        "print('READY', flush=True)\n"
        "time.sleep(5)\n";
    QTERMX_CHECK(write(fd, script.data(), script.size()) == static_cast<ssize_t>(script.size()));
    ::close(fd);
    Pty pty({"/bin/zsh", "-l"}, {}, "", 24, 80);
    pty.sendData("/opt/miniconda3/envs/textual/bin/python " + std::string(tmpl) + "\n");
    readUntil(pty, "READY");
    pty.setWindowSize(30, 100);
    const std::string out = readUntil(pty, "WINCH:100x30");
    QTERMX_CHECK(out.find("WINCH:100x30") != std::string::npos);
    pty.close();
    unlink(tmpl);
}

TEST_CASE(pty_resize_reflows_actual_zeroro_textual_app)
{
    // The definitive reproduction: run the real Zeroro Textual app
    // through the C++ pty (login shell → conda activate → python
    // main.py), resize, and check the app redraws (Textual emits a
    // repaint on SIGWINCH). This is the exact user scenario.
    Pty pty({"/bin/zsh", "-l"}, {}, "", 24, 80);
    pty.sendData("conda activate textual\n");
    readUntil(pty, "textual", 10.0);
    pty.sendData("cd /Users/jan/Devel/Zeroro && python main.py\n");
    // Textual enters the alternate screen on startup.
    const std::string startup = readUntil(pty, "\x1b[?1049h", 15.0);
    QTERMX_CHECK(startup.find("\x1b[?1049h") != std::string::npos);
    // Drain any remaining startup output.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::string drained;
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pty.masterFd(), &fds);
        struct timeval tv {0, 0};
        while (select(pty.masterFd() + 1, &fds, nullptr, nullptr, &tv) > 0) {
            const auto chunk = pty.read();
            if (!chunk.has_value() || chunk->empty()) {
                break;
            }
            drained += *chunk;
        }
    }
    pty.setWindowSize(30, 100);
    const std::string after = readUntil(pty, "\x1b[1;1H", 5.0);
    QTERMX_CHECK(after.find("\x1b[1;1H") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_child_exit_is_detected_and_reaped)
{
    Pty pty = spawnChild("print('BYE', flush=True)\n");
    const std::string out = readUntil(pty, "BYE");
    QTERMX_CHECK(out.find("BYE") != std::string::npos);
    QTERMX_CHECK(waitFor([&] { return !pty.read().has_value(); }));
    QTERMX_CHECK(!pty.isRunning());
    QTERMX_CHECK(pty.wait() == 0); // clean exit reaped, no zombie
    pty.close();
}

TEST_CASE(pty_child_gets_term_environment)
{
    Pty pty = spawnChild("import os\nprint('TERM=' + os.environ.get('TERM', ''), flush=True)\n");
    const std::string out = readUntil(pty, "TERM=");
    QTERMX_CHECK(out.find("TERM=xterm-256color") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_child_gets_colorterm_truecolor)
{
    // The child sees COLORTERM=truecolor so truecolor-gated apps (vim
    // termguicolors, fish, git-delta…) emit 38;2/48;2 sequences.
    Pty pty = spawnChild("import os\n"
                         "print('COLORTERM=' + os.environ.get('COLORTERM', ''), flush=True)\n");
    const std::string out = readUntil(pty, "COLORTERM=");
    QTERMX_CHECK(out.find("COLORTERM=truecolor") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_child_does_not_get_geometry_environment)
{
    // COLUMNS/LINES must NOT reach the child — even stale values from
    // the supplied env (see the rationale in Pty's constructor: they
    // would freeze the size Python 3.14's shutil reports to TUIs).
    Pty pty = spawnChild("import os\n"
                         "print('GEOM:%sx%s' % (os.environ.get('COLUMNS', ''),"
                         " os.environ.get('LINES', '')), flush=True)\n",
                         {{"COLUMNS", "999"}, {"LINES", "999"}}, "", 33, 120);
    const std::string out = readUntil(pty, "GEOM:");
    QTERMX_CHECK(out.find("GEOM:x") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_child_starts_in_cwd)
{
    // The child chdirs into `cwd` before exec, so the shell lands in
    // the requested working directory (xCode's "Open Terminal Here").
    char tmpl[] = "/tmp/qtermx-pty-XXXXXX";
    char* dir = mkdtemp(tmpl);
    QTERMX_CHECK(dir != nullptr);
    const std::string folder = dir;
    const std::string marker = folder + "/cwd-ok";
    // The path is embedded quoted (Python repr) — the script must be
    // valid Python.
    Pty pty = spawnChild("import os\n"
                         "open('" + marker + "', 'w').close()\n"
                         "print('CWD_DONE', flush=True)\n",
                         {}, folder);
    const std::string out = readUntil(pty, "CWD_DONE");
    QTERMX_CHECK(out.find("CWD_DONE") != std::string::npos);
    struct stat st;
    QTERMX_CHECK(stat(marker.c_str(), &st) == 0);
    pty.close();
    rmdir(folder.c_str());
}

TEST_CASE(pty_close_terminates_a_still_running_child)
{
    Pty pty = spawnChild("import time\ntime.sleep(60)\n");
    readUntil(pty, "", 1.0);
    QTERMX_CHECK(pty.isRunning());
    pty.close();
    // US 21: close() stops a still-running child — SIGHUP/EOF first,
    // then SIGTERM — and reaps it (no zombie left behind).
    QTERMX_CHECK(waitFor([&] { return pty.wait().has_value(); }));
    QTERMX_CHECK(!pty.isRunning());
}

TEST_CASE(pty_has_foreground_job_false_at_idle_prompt)
{
    // A shell sitting at the prompt has no foreground job — closing the
    // tab should not prompt (xCode's close-confirmation rule). Read-only
    // tcgetpgrp — safe in any session.
    Pty pty = spawnChild("import os, sys\n"
                         "print('IDLE', flush=True)\n"
                         "sys.stdin.readline()\n"
                         "print('DONE', flush=True)\n");
    readUntil(pty, "IDLE");
    QTERMX_CHECK(!pty.hasForegroundJob());
    pty.close();
}

TEST_CASE(pty_foreground_program_none_at_idle_prompt)
{
    // No foreground program at the idle prompt — the shell owns the
    // terminal, so the panel shows no program name. Read-only.
    Pty pty = spawnChild("import os, sys\n"
                         "print('IDLE', flush=True)\n"
                         "sys.stdin.readline()\n"
                         "print('DONE', flush=True)\n");
    readUntil(pty, "IDLE");
    QTERMX_CHECK(!pty.foregroundProgram().has_value());
    pty.close();
}

TEST_CASE(pty_foreground_program_none_after_child_exits)
{
    // After the child exits there is no foreground program — the row is
    // dimmed as exited instead.
    Pty pty = spawnChild("print('BYE', flush=True)\n");
    readUntil(pty, "BYE");
    QTERMX_CHECK(!pty.foregroundProgram().has_value());
    pty.close();
}

TEST_CASE(pty_send_data_handles_large_payloads)
{
    // The non-blocking master can take short writes — send_data must
    // loop until every byte is written. (The Python oracle patches
    // os.write to force short writes; here a payload far larger than
    // the pty buffer exercises the same loop.) The child switches to
    // raw mode and signals READY before the payload is sent — in
    // canonical mode the line discipline caps a line at MAX_CANON
    // bytes and would discard the rest. It reads with read1: the macOS
    // pty queue is ~1022 bytes, and BufferedReader.read(n) blocks
    // until all n bytes arrive — which the pty can never deliver at
    // once.
    Pty pty = spawnChild("import sys, tty\n"
                         "tty.setraw(0)\n"
                         "print('READY', flush=True)\n"
                         "data = b''\n"
                         "while len(data) < 200000:\n"
                         "    chunk = sys.stdin.buffer.read1(65536)\n"
                         "    if not chunk:\n"
                         "        break\n"
                         "    data += chunk\n"
                         "print('GOT:%d' % len(data), flush=True)\n");
    readUntil(pty, "READY");
    const std::string payload(200000, 'z');
    pty.sendData(payload);
    const std::string out = readUntil(pty, "GOT:200000");
    QTERMX_CHECK(out.find("GOT:200000") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_ctrl_d_is_eof_in_canonical_mode)
{
    // VEOF on an empty line: readline returns "" — the shell's EOF.
    Pty pty = spawnChild("import sys\n"
                         "print('READY', flush=True)\n"
                         "line = sys.stdin.readline()\n"
                         "print('EOF' if line == '' else 'DATA:' + line.strip(), flush=True)\n");
    readUntil(pty, "READY");
    pty.sendData("\x04");
    const std::string out = readUntil(pty, "EOF");
    QTERMX_CHECK(out.find("EOF") != std::string::npos);
    pty.close();
}

TEST_CASE(pty_control_char_is_data_when_isig_off)
{
    // Raw mode (ISIG off — htop/ncurses-style): the same byte arrives as
    // *data*; the terminal's job is only to have sent it. This pins the
    // contract: signal vs. data is the app's termios choice.
    Pty pty = spawnChild("import sys, tty\n"
                         "tty.setraw(0)\n"
                         "print('READY', flush=True)\n"
                         "data = sys.stdin.buffer.read(1)\n"
                         "print('DATA:%02x' % data[0], flush=True)\n");
    readUntil(pty, "READY");
    pty.sendData("\x03");
    const std::string out = readUntil(pty, "DATA:03");
    QTERMX_CHECK(out.find("DATA:03") != std::string::npos);
    pty.close();
}