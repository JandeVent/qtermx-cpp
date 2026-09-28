#include "pty.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#endif

// The process environment (POSIX). Declared here at file scope: macOS
// does not expose it in the global namespace via unistd.h.
extern char** environ;

namespace qtermx {

namespace {

// TIOCSCTTY — the child acquires the pty as its controlling terminal.
// Not exposed by all termios headers; the fallbacks are the raw ioctl
// numbers (macOS: `_IOR('t', 97, int)`; Linux: 0x540E).
#ifndef TIOCSCTTY
#ifdef __APPLE__
constexpr unsigned long kTiocsctty = 0x20047461;
#else
constexpr unsigned long kTiocsctty = 0x540E;
#endif
#else
constexpr unsigned long kTiocsctty = TIOCSCTTY;
#endif

// Retry a syscall on EINTR (the reader thread must never drop a read
// because a signal landed).
template <typename Fn>
auto retryEintr(Fn fn) -> decltype(fn())
{
    for (;;) {
        const auto r = fn();
        if (r == -1 && errno == EINTR) {
            continue;
        }
        return r;
    }
}

// The exit code a wait status reports — Python's
// os.waitstatus_to_exitcode: the exit status, or -sig for a signal
// death.
int waitStatusToExitCode(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    return 0;
}

// Resolve `command` to an executable path: a command containing a slash
// is used as-is; otherwise the PATH directories are searched (Python's
// execvpe semantics — macOS has no execvpe).
std::string resolvePath(const std::string& command)
{
    if (command.find('/') != std::string::npos) {
        return command;
    }
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) {
        return command;
    }
    std::string path(pathEnv);
    size_t start = 0;
    while (start <= path.size()) {
        const size_t colon = path.find(':', start);
        const std::string dir = colon == std::string::npos ? path.substr(start)
                                                           : path.substr(start, colon - start);
        const std::string candidate = dir.empty() ? command : dir + "/" + command;
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
        if (colon == std::string::npos) {
            break;
        }
        start = colon + 1;
    }
    return command;
}

// The canonical line discipline (libptyqt's termios setup): canonical
// mode with ISIG (Ctrl+C → SIGINT), ECHO, ICRNL, IXON. The child's
// termios choices (raw mode for htop/ncurses) override this at runtime.
void configureTermios(int fd)
{
    struct termios ttmode {};
    if (tcgetattr(fd, &ttmode) != 0) {
        return;
    }
    ttmode.c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT;
#ifdef IUTF8
    ttmode.c_iflag |= IUTF8;
#endif
    ttmode.c_oflag = OPOST | ONLCR;
    ttmode.c_cflag = CREAD | CS8 | HUPCL;
    ttmode.c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;
    ttmode.c_cc[VEOF] = 4;
    ttmode.c_cc[VEOL] = -1;
    ttmode.c_cc[VEOL2] = -1;
    ttmode.c_cc[VERASE] = 0x7f;
    ttmode.c_cc[VWERASE] = 23;
    ttmode.c_cc[VKILL] = 21;
    ttmode.c_cc[VREPRINT] = 18;
    ttmode.c_cc[VINTR] = 3;
    ttmode.c_cc[VQUIT] = 0x1c;
    ttmode.c_cc[VSUSP] = 26;
    ttmode.c_cc[VSTART] = 17;
    ttmode.c_cc[VSTOP] = 19;
    ttmode.c_cc[VLNEXT] = 22;
    ttmode.c_cc[VDISCARD] = 15;
    ttmode.c_cc[VMIN] = 1;
    ttmode.c_cc[VTIME] = 0;
#ifdef __APPLE__
    ttmode.c_cc[VDSUSP] = 25;
    ttmode.c_cc[VSTATUS] = 20;
#endif
    cfsetispeed(&ttmode, B38400);
    cfsetospeed(&ttmode, B38400);
    tcsetattr(fd, TCSANOW, &ttmode);
}

// The name of the process with the given pid, or nullopt when it cannot
// be resolved (process gone, unsupported platform). Linux: /proc/<pid>/
// comm (a single file read, no subprocess). macOS: libproc's
// proc_pidpath (no subprocess).
std::optional<std::string> processName(int pid)
{
#ifdef __linux__
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE* f = std::fopen(path, "r");
    if (f == nullptr) {
        return std::nullopt;
    }
    char buf[256];
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) {
        return std::nullopt;
    }
    buf[n] = '\0';
    // Strip the trailing newline.
    if (n > 0 && buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
    }
    return std::string(buf);
#elif defined(__APPLE__)
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    const int n = proc_pidpath(pid, buf, sizeof(buf));
    if (n <= 0) {
        return std::nullopt;
    }
    // The executable basename.
    const std::string path(buf, static_cast<size_t>(n));
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
#else
    (void)pid;
    return std::nullopt;
#endif
}

} // namespace

Pty::Pty(std::vector<std::string> command, std::vector<std::pair<std::string, std::string>> env,
         std::string cwd, int rows, int cols)
    : m_rows(rows)
    , m_cols(cols)
{
    // posix_openpt + grantpt + unlockpt + ptsname (libptyqt's sequence —
    // more portable than openpty, and O_NOCTTY on both fds keeps the
    // parent's terminal untouched).
    const int masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (masterFd < 0) {
        std::perror("posix_openpt");
        std::abort();
    }
    // The master is non-blocking: the reader thread select()s first,
    // then reads.
    const int flags = fcntl(masterFd, F_GETFL, 0);
    fcntl(masterFd, F_SETFL, flags | O_NONBLOCK);

    char* slaveName = ptsname(masterFd);
    if (slaveName == nullptr || grantpt(masterFd) != 0 || unlockpt(masterFd) != 0) {
        std::perror("grantpt/unlockpt");
        std::abort();
    }
    const int slaveFd = ::open(slaveName, O_RDWR | O_NOCTTY);
    if (slaveFd < 0) {
        std::perror("open slave");
        std::abort();
    }
    // Neither fd may leak into the child (libptyqt sets FD_CLOEXEC).
    fcntl(masterFd, F_SETFD, FD_CLOEXEC);
    fcntl(slaveFd, F_SETFD, FD_CLOEXEC);

    m_masterFd = masterFd;

    if (command.empty()) {
        const char* shell = std::getenv("SHELL");
        command = {shell != nullptr ? shell : "/bin/sh"};
        // Launch as login shell (-l) so the user's profile (~/.zshrc,
        // ~/.bash_profile, etc.) is sourced. Without this, a GUI app
        // (Finder/Dock launch) inherits only launchd's minimal PATH.
        command.insert(command.begin() + 1, "-l");
    }

    // The child always sees a compatible TERM — the parent's value is
    // irrelevant to the session (spec: TUIs behave differently).
    // COLORTERM tells truecolor-gated apps (vim termguicolors, fish,
    // git-delta…) that 38;2/48;2 will render correctly. COLUMNS/LINES
    // must NOT be set (and any inherited values are stripped below):
    // Python 3.14's shutil.get_terminal_size() — the size source of
    // Textual and other TUIs — prefers them over the TIOCGWINSZ ioctl,
    // and they are frozen at spawn, so a resize would never reach the
    // app. The ioctl is authoritative (xterm sets neither).
    std::vector<std::pair<std::string, std::string>> childEnv = std::move(env);
    childEnv.emplace_back("TERM", kDefaultTerm);
    childEnv.emplace_back("COLORTERM", kColorterm);

    // The line discipline on the master, and the initial size on both
    // fds (macOS only propagates TIOCSWINSZ from the slave before a
    // session exists — the child's dup2'd 0/1/2 carry it).
    configureTermios(masterFd);
    setWindowSizeFd(rows, cols, masterFd);
    setWindowSizeFd(rows, cols, slaveFd);

    const pid_t pid = fork();
    if (pid == -1) {
        std::perror("fork");
        ::close(masterFd);
        ::close(slaveFd);
        std::abort();
    }
    if (pid == 0) {
        // Child. Order matters (libptyqt configChildProcess): dup the
        // slave onto 0/1/2 FIRST, then setsid, then acquire the pty as
        // the controlling terminal and foreground process group — all
        // on the slave fd, never on fd 0 (which could still be the
        // parent's terminal if a dup failed — stealing its foreground
        // group would freeze the user's session).
        for (int target = 0; target <= 2; ++target) {
            if (dup2(slaveFd, target) == -1) {
                _exit(127);
            }
        }
        ::close(masterFd);
        // Belt and braces: verify fd 0 really is the pty slave before
        // touching the controlling terminal.
        struct stat st0 {};
        struct stat stSlave {};
        if (fstat(0, &st0) != 0 || fstat(slaveFd, &stSlave) != 0 || st0.st_rdev != stSlave.st_rdev) {
            _exit(127);
        }
        if (setsid() == -1) {
            _exit(127);
        }
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            _exit(127);
        }
        // The pty as the controlling terminal, the child as its
        // foreground process group: without these the line discipline
        // has no process group to signal, so ISIG chars are never
        // converted — Ctrl+C stays a byte instead of SIGINT.
        if (ioctl(slaveFd, kTiocsctty, 0) == -1) {
            _exit(127);
        }
        if (tcsetpgrp(slaveFd, getpid()) == -1) {
            _exit(127);
        }
        if (slaveFd > 2) {
            ::close(slaveFd);
        }
        // Build the environment: the parent's environ overlaid with the
        // forced entries.
        std::vector<std::string> storage;
        storage.reserve(childEnv.size());
        for (char** e = environ; *e != nullptr; ++e) {
            storage.emplace_back(*e);
        }
        for (const auto& [key, value] : childEnv) {
            // Replace an existing key, else append.
            const std::string entry = key + "=" + value;
            bool replaced = false;
            for (std::string& s : storage) {
                if (s.compare(0, key.size() + 1, key + "=") == 0) {
                    s = entry;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                storage.push_back(entry);
            }
        }
        // COLUMNS/LINES must not reach the child — strip any inherited
        // or supplied values (see above): they would freeze the size
        // TUIs report.
        storage.erase(std::remove_if(storage.begin(), storage.end(), [](const std::string& s) {
                          return s.compare(0, 8, "COLUMNS=") == 0 || s.compare(0, 6, "LINES=") == 0;
                      }),
                      storage.end());
        std::vector<char*> envp;
        envp.reserve(storage.size() + 1);
        for (std::string& s : storage) {
            envp.push_back(s.data());
        }
        envp.push_back(nullptr);
        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (std::string& s : command) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);
        // execvpe has no macOS equivalent — resolve the path ourselves.
        const std::string resolved = resolvePath(argv[0]);
        execve(resolved.c_str(), argv.data(), envp.data());
        _exit(127);
    }
    ::close(slaveFd);
    m_pid = static_cast<int>(pid);
    // The child setsid()s, so it is its own session leader and
    // process-group leader: pid == pgid == sid. `m_pid` is therefore the
    // shell's process-group id — job control compares the terminal's
    // foreground group against it.
}

Pty::~Pty()
{
    close();
}

// -- Read API ------------------------------------------------------------

std::optional<std::string> Pty::read()
{
    if (m_closed) {
        return std::nullopt;
    }
    char buf[65536];
    const ssize_t n = retryEintr([&] { return ::read(m_masterFd, buf, sizeof(buf)); });
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::string(); // spurious EAGAIN — a select race
        }
        // EIO (Linux) / EBADF: the child has exited.
        return std::nullopt;
    }
    if (n == 0 && !isRunning()) {
        // macOS EOF: the slave is gone, so the child is gone too.
        return std::nullopt;
    }
    return std::string(buf, static_cast<size_t>(n));
}

void Pty::sendData(const std::string& data)
{
    if (m_closed) {
        return;
    }
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = retryEintr([&] {
            return ::write(m_masterFd, data.data() + offset, data.size() - offset);
        });
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Buffer full — wait and retry.
                struct timespec ts {0, 10 * 1000 * 1000}; // 10 ms
                nanosleep(&ts, nullptr);
                continue;
            }
            return; // EIO/EBADF — the child is gone
        }
        offset += static_cast<size_t>(n);
    }
}

// -- Size ----------------------------------------------------------------

void Pty::setWindowSize(int rows, int cols)
{
    m_rows = rows;
    m_cols = cols;
    if (!m_closed) {
        setWindowSizeFd(rows, cols, m_masterFd);
    }
}

void Pty::setWindowSizeFd(int rows, int cols, int fd)
{
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    ioctl(fd, TIOCSWINSZ, &ws);
}

// -- Lifecycle -----------------------------------------------------------

bool Pty::isRunning()
{
    if (m_exitStatus.has_value()) {
        return false;
    }
    int status = 0;
    const pid_t r = retryEintr([&] { return waitpid(m_pid, &status, WNOHANG); });
    if (r == 0) {
        return true;
    }
    if (r == m_pid) {
        m_exitStatus = waitStatusToExitCode(status);
        return false;
    }
    // ECHILD — already reaped elsewhere.
    m_exitStatus = 0;
    return false;
}

bool Pty::hasForegroundJob()
{
    if (!isRunning()) {
        return false;
    }
    const pid_t foregroundPgid = tcgetpgrp(m_masterFd);
    if (foregroundPgid == -1) {
        return false;
    }
    if (foregroundPgid == m_pid) {
        return false; // idle at the prompt: the shell owns the terminal
    }
    return true;
}

std::optional<std::string> Pty::foregroundProgram()
{
    if (!isRunning()) {
        return std::nullopt;
    }
    const pid_t foregroundPgid = tcgetpgrp(m_masterFd);
    if (foregroundPgid == -1 || foregroundPgid == m_pid) {
        return std::nullopt; // idle at the prompt
    }
    return processName(static_cast<int>(foregroundPgid));
}

std::optional<int> Pty::wait()
{
    isRunning();
    return m_exitStatus;
}

void Pty::signal(int sig)
{
    if (kill(m_pid, sig) == -1 && errno == ESRCH) {
        // Already gone.
    }
}

void Pty::close(double terminateTimeout, double killTimeout)
{
    if (!m_closed) {
        m_closed = true;
        if (m_masterFd >= 0) {
            ::close(m_masterFd);
            m_masterFd = -1;
        }
    }
    if (isRunning()) {
        signal(SIGTERM);
        waitBounded(terminateTimeout);
    }
    if (isRunning()) {
        signal(SIGKILL);
        waitBounded(killTimeout);
    }
    wait();
}

void Pty::waitBounded(double timeout)
{
    // Poll wait() until the child exits or `timeout` passes.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (isRunning() && std::chrono::steady_clock::now() < deadline) {
        struct timespec ts {0, 10 * 1000 * 1000}; // 10 ms
        nanosleep(&ts, nullptr);
    }
}

} // namespace qtermx