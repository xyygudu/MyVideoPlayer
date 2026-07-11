#include <QApplication>

#include "main_window.h"
#include "mvp/logging.h"

int main(int argc, char* argv[]) {
    mvp::logging::Init();

    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
