#include "shell.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    meshify::p3::Shell shell;
    shell.show();
    return app.exec();
}
