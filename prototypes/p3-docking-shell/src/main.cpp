#include "shell.hpp"

#include <QApplication>
#include <QStringList>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    meshify::p3::Shell shell;
    shell.show();
    // Used by CI to check a release archive starts: open the window, then quit.
    if (QApplication::arguments().contains("--smoke-test")) {
        QTimer::singleShot(500, &app, &QApplication::quit);
    }
    return app.exec();
}
