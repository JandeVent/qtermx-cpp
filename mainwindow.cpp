#include "mainwindow.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
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

    // Set monospace font like pyqtermx: platform's native monospace,
    // falling back to Menlo (macOS) or DejaVu Sans Mono (Linux).
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (font.family().isEmpty()) {
        font = QFont("Menlo", 12);
    } else {
        font.setPixelSize(12);
    }
    m_terminal->setFont(font);
}

MainWindow::~MainWindow() = default;

void MainWindow::startSession()
{
    m_session->start();
}