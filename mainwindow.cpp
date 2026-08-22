#include "mainwindow.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QSize>

#include "pty.h"
#include "session.h"
#include "terminal_widget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    const char *shell = std::getenv("SHELL");
    m_pty = std::make_unique<qtermx::Pty>(
        std::vector<std::string>{shell != nullptr ? shell : "/bin/zsh"},
        std::vector<std::pair<std::string, std::string>>{}, "", 24, 80);
    m_session = std::make_unique<qtermx::Session>(m_pty.get(), 24, 80);
    m_terminal = new qtermx::gui::TerminalWidget(m_session.get(), this);
    setCentralWidget(m_terminal);
    setWindowTitle("qtermx-cpp");
    resize(m_terminal->sizeHint() + QSize(0, 32));
    connect(qApp, &QApplication::aboutToQuit, this, [this] { m_session->close(); });
}

MainWindow::~MainWindow() = default;

void MainWindow::startSession()
{
    m_session->start();
}