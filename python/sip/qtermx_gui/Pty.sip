// Pty — the pseudo-terminal layer (pty.h).
class Pty
{
%TypeHeaderCode
#include "pty.h"
%End

public:
    // Spawn the default shell ($SHELL) at the given size.
    Pty(int rows = 24, int cols = 80);
%MethodCode
    sipCpp = new Pty({}, {}, "", a0, a1);
%End

    // Spawn `command` (argv) on the pty at the given size; `cwd`
    // chdirs the child before exec (the core Pty already supports it).
    Pty(std::vector<std::string> command, int rows = 24, int cols = 80,
        const std::string &cwd = "");
%MethodCode
    sipCpp = new Pty(*a0, {}, *a3, a1, a2);
%End

    ~Pty();

private:
    // Non-copyable (the C++ class deletes these).
    Pty(const Pty &);
    Pty &operator=(const Pty &);

public:
    // -- Read API --------------------------------------------------------
    int masterFd() const;
    int pid() const;

    // One non-blocking read of the child's output.
    // Returns str, or None when the child has exited.
    PyObject *read();
%MethodCode
    std::optional<std::string> data = sipCpp->read();
    if (!data.has_value()) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromStringAndSize(data->data(), static_cast<Py_ssize_t>(data->size()));
%End

    // -- Write API -------------------------------------------------------
    void sendData(const std::string &data);

    // -- Size ------------------------------------------------------------
    void setWindowSize(int rows, int cols);

    // -- Lifecycle -------------------------------------------------------
    bool isRunning();
    bool hasForegroundJob();

    // The process owning the terminal's foreground group.
    // Returns str, or None when the shell is idle / child exited.
    PyObject *foregroundProgram();
%MethodCode
    std::optional<std::string> name = sipCpp->foregroundProgram();
    if (!name.has_value()) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromStringAndSize(name->data(), static_cast<Py_ssize_t>(name->size()));
%End

    // Reap the child (WNOHANG). Returns int (exit status, -sig for a
    // signal death), or None while it is still running.
    PyObject *wait();
%MethodCode
    std::optional<int> status = sipCpp->wait();
    if (!status.has_value()) {
        Py_RETURN_NONE;
    }
    return PyLong_FromLong(*status);
%End

    void signal(int sig);
    void close();
    void close(double terminateTimeout, double killTimeout);
};