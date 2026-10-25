#include <QApplication>
#include <QDir>

#include "main_window.h"
#include "mvp/logging.h"

namespace {

// MVP_LOG_TARGET=console|file|both (default: file).
// MVP_LOG_FILE=<path> (default: <executable dir>/logs/player.log).
// Needs the QApplication instance for applicationDirPath().
void SetupLogging() {
    mvp::logging::Init();
    const QString target = qEnvironmentVariable("MVP_LOG_TARGET", "file");
    if (target.compare("console", Qt::CaseInsensitive) == 0) {
        return;
    }
    const QString default_path = QCoreApplication::applicationDirPath() + "/logs/player.log";
    const QString path = QDir::toNativeSeparators(qEnvironmentVariable("MVP_LOG_FILE", default_path));
    const bool file_ok = mvp::logging::EnableFileLogging(path.toStdString());
    if (file_ok && target.compare("both", Qt::CaseInsensitive) != 0) {
        mvp::logging::SetConsoleLoggingEnabled(false);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // Identity for QSettings (the settings page persists its switches).
    QCoreApplication::setOrganizationName(QStringLiteral("MyVideoPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("MyVideoPlayer"));
    SetupLogging();

    MainWindow window;
    window.show();

    return app.exec();
}
