#include <QApplication>

#include "main_window.h"
#include "mvp/logging.h"

namespace {

// MVP_LOG_TARGET=console|file|both (default: file).
void SetupLogging() {
    mvp::logging::Init();
    const QString target = qEnvironmentVariable("MVP_LOG_TARGET", "file");
    if (target.compare("console", Qt::CaseInsensitive) == 0) {
        return;
    }
    const bool file_ok = mvp::logging::EnableFileLogging(
        "E:/WorkSpace/CppProjects/MyVideoPlayer/log/player.log");
    if (file_ok && target.compare("both", Qt::CaseInsensitive) != 0) {
        mvp::logging::SetConsoleLoggingEnabled(false);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    SetupLogging();

    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
