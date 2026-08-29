#include <QApplication>

#include "main_window.h"
#include "mvp/logging.h"

int main(int argc, char* argv[]) {
    mvp::logging::Init();
    // Write all logs here so a hung/frozen run can be inspected after the
    // fact (debug output to the console is lost on exit/crash).
    mvp::logging::EnableFileLogging(
        "E:/WorkSpace/CppProjects/MyVideoPlayer/build/bin/player.log");

    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
