#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    // Start the session after the window is shown, so the initial full
    // snapshot arrives queued (ADR-0005).
    w.startSession();
    return QCoreApplication::exec();
}