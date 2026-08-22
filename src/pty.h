#ifndef QTERMX_PTY_H
#define QTERMX_PTY_H

// The pty layer — Qt-free (ADR-0005). A narrow `Pty` interface: spawn a
// child with its own session (setsid), exchange bytes over a real
// pseudo-terminal, set the window size, and reap the exit. It knows
// nothing about the emulator, the screen, or Qt — the reader thread (the
// single writer) drives it, and tests drive it directly against fake
// child programs (port of pyqtermx ptyspawn.py).
//
// The master fd is non-blocking: the reader thread select()s first, then
// reads; `read()` returns nullopt when the child has exited (EIO).

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qtermx {

// The terminal type the child sees (spec: TUIs behave differently
// without it).
inline constexpr const char* kDefaultTerm = "xterm-256color";

// Truecolor advertisement: apps (vim ≥ 8.1 with termguicolors, fish,
// git-delta, bat, …) gate `38;2`/`48;2` output on `COLORTERM=truecolor`
// — TERM=xterm-256color alone does not tell them we can render RGB.
inline constexpr const char* kColorterm = "truecolor";

// The narrow pty surface the reader thread drives (Python's PtyLike
// protocol) — Session depends on this, not on Pty directly, so tests
// can substitute a pipe-based fake.
class PtyLike {
public:
    virtual ~PtyLike() = default;
    virtual int masterFd() const = 0;
    virtual std::optional<std::string> read() = 0;
    virtual void sendData(const std::string& data) = 0;
    virtual void setWindowSize(int rows, int cols) = 0;
    virtual void close() = 0;
};

class Pty : public PtyLike {
public:
    // Spawn a child with its own session. `command` defaults to $SHELL.
    // `env` entries are merged over the parent's environment; the child
    // always sees TERM=xterm-256color, COLORTERM=truecolor and
    // COLUMNS/LINES forced (the parent's values are irrelevant to the
    // session). `cwd` chdirs the child before exec.
    Pty(std::vector<std::string> command = {},
        std::vector<std::pair<std::string, std::string>> env = {},
        std::string cwd = "", int rows = 24, int cols = 80);
    ~Pty();

    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    // -- Read API --------------------------------------------------------

    // The master fd — the reader thread's select() surface.
    int masterFd() const override { return m_masterFd; }

    // The child's pid — its session/process-group id (the child
    // setsid()s, so pid == pgid == sid; job control compares the
    // terminal's foreground group against it).
    int pid() const { return m_pid; }

    // One non-blocking read of the child's output. Returns nullopt when
    // the child has exited (EIO on Linux, a 0-byte EOF read on macOS) or
    // the pty is closed; an empty string on a spurious EAGAIN (a select
    // race).
    std::optional<std::string> read() override;

    // Write bytes to the child (its stdin). A non-blocking master can
    // take a short write when its buffer fills — loop until everything
    // is written, retrying EAGAIN.
    void sendData(const std::string& data) override;

    // -- Size ------------------------------------------------------------

    // TIOCSWINSZ: the size the child sees (and SIGWINCHes on).
    void setWindowSize(int rows, int cols) override;

    // -- Lifecycle -------------------------------------------------------

    // Whether the child is still alive (WNOHANG check).
    bool isRunning();

    // Whether a foreground job currently owns the terminal. The shell is
    // its own process-group leader (setsid + tcsetpgrp at spawn); while
    // it sits idle at the prompt the foreground process group is the
    // shell's own group. When the user runs a job the shell foregrounds
    // it via tcsetpgrp, so the foreground group differs from the shell's
    // group. Note: a stopped job (Ctrl-Z) returns the foreground group
    // to the shell, so this reports false for stopped jobs.
    bool hasForegroundJob();

    // The name of the process that currently owns the terminal's
    // foreground process group — `vim`, `sleep`, `node`, …. Returns
    // nullopt when the shell is idle at its prompt (no job), when the
    // child has exited, or on platforms where the process name cannot be
    // resolved. Resolution is platform-specific: Linux reads
    // /proc/<pgid>/comm; macOS asks libproc for the process's executable
    // path and takes its basename.
    std::optional<std::string> foregroundProgram();

    // Reap the child (WNOHANG): its exit status, or nullopt while it is
    // still running. Callers poll this — it never blocks. A signal death
    // reports -sig (Python's waitstatus_to_exitcode).
    std::optional<int> wait();

    // Send a signal to the child.
    void signal(int sig);

    // Close the master and stop the child (spec US 21). Closing the
    // master first delivers EOF/SIGHUP to a well-behaved child (the
    // normal terminal way); a child still alive afterwards gets SIGTERM,
    // a bounded wait, then SIGKILL as the fallback.
    void close() override { close(3.0, 2.0); }
    void close(double terminateTimeout, double killTimeout);

private:
    void setWindowSizeFd(int rows, int cols, int fd);
    void waitBounded(double timeout);

    int m_masterFd = -1;
    int m_pid = -1;
    bool m_closed = false;
    std::optional<int> m_exitStatus;
    int m_rows = 24;
    int m_cols = 80;
};

} // namespace qtermx

#endif // QTERMX_PTY_H