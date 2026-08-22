#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <memory>

namespace qtermx {
class Pty;
class Session;
namespace gui {
class TerminalWidget;
}
} // namespace qtermx

// The runnable terminal — thin glue: pty + session + widget in a
// window (port of pyqtermx __main__.py). All behavior lives in the
// tested layers; this class only wires them together.
//
// Lifecycle (spec §9): window close → Session::close (the reader thread
// stops and the master closes, delivering EOF/SIGHUP to the child).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Start the session's reader thread — called after the window is
    // shown, so the initial full snapshot arrives queued (ADR-0005).
    void startSession();

private:
    std::unique_ptr<qtermx::Pty> m_pty;
    std::unique_ptr<qtermx::Session> m_session;
    qtermx::gui::TerminalWidget *m_terminal = nullptr;
};
#endif // MAINWINDOW_H