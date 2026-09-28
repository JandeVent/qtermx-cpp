// Session — headless single-writer terminal (session.h): pty + parser +
// screen + reader thread. Commands are posted from any thread; the
// reader thread owns the model.
//
// The Python binding covers the command API plus synchronous `process`
// for driving the pipeline without a thread. Snapshot callbacks are
// wired in C++ by TerminalWidget.setSession(); Python reads model state
// through `screen()`.

class Session
{
%TypeHeaderCode
#include "session.h"
%End

public:
    // `pty` is the concrete Pty (the C++ constructor takes any
    // PtyLike* — Pty is one). /KeepReference/ pins the Python Pty for
    // the session's lifetime: the C++ side stores a raw PtyLike* and
    // the reader thread dereferences it on every read — without the
    // pin, dropping the last Python Pty reference would delete the C++
    // object under the running thread (use-after-free).
    Session(Pty *pty /KeepReference/, int lines = 24, int columns = 80,
            int scrollbackLimit = 1000, int snapshotLimit = 1000);
    ~Session();

private:
    // Non-copyable (the C++ class deletes these) — declaring them here
    // in the private section tells SIP the class cannot be copied.
    Session(const Session &);
    Session &operator=(const Session &);

public:
    // -- Command API (any thread) ---------------------------------------
    void sendData(const std::string &data);
    void resize(int lines, int columns);
    void scroll(int n);
    void scrollToBottom();
    void setPalette(const std::string &fg, const std::string &bg);
    void close(double timeout = 5.0);

    // -- Reader thread --------------------------------------------------
    void start();
    bool isAlive() const;

    // Run the reader's per-read step synchronously: feed the data into
    // the parser, flush the write boundary, emit a snapshot. The
    // headless/benchmark seam — no thread involved.
    void process(const std::string &data);

    // -- Model access ---------------------------------------------------
    Screen &screen();

    // The pty the session drives — the same Python object passed to the
    // constructor (kept alive by /KeepReference/). Borrowed: the
    // session owns the reference, not the caller. Exposed as the
    // `pty` property for pyqtermx parity (`session.pty`); the getter
    // method keeps a distinct name because SIP forbids a property and
    // a callable sharing a name.
    Pty *ptyHandle() const;
%MethodCode
    sipRes = static_cast<Pty *>(sipCpp->pty());
%End

    %Property(name=pty, get=ptyHandle)
};