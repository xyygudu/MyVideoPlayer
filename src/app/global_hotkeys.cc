#include "global_hotkeys.h"

#include <QCoreApplication>

#include <spdlog/spdlog.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

GlobalHotkeys::GlobalHotkeys(QObject* parent) : QObject(parent) {
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeys::~GlobalHotkeys() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
    for (int id : registered_) {
        UnregisterHotKey(nullptr, id);
    }
}

bool GlobalHotkeys::RegisterCtrlShiftF(int id, int function_key) {
    const UINT modifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
    if (!RegisterHotKey(nullptr, id, modifiers, VK_F1 + function_key - 1)) {
        SPDLOG_WARN("GlobalHotkeys: {} is taken by another application ({})", Text(function_key).toStdString(),
                    GetLastError());
        return false;
    }
    registered_.push_back(id);
    return true;
}

QString GlobalHotkeys::Text(int function_key) { return QStringLiteral("Ctrl+Shift+F%1").arg(function_key); }

bool GlobalHotkeys::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) {
    (void)result;
    if (event_type != "windows_generic_MSG" && event_type != "windows_dispatcher_MSG") {
        return false;
    }
    const auto* msg = static_cast<const MSG*>(message);
    // Thread hotkeys (no window) arrive with a null hwnd.
    if (msg->message != WM_HOTKEY || msg->hwnd != nullptr || !registered_.contains(static_cast<int>(msg->wParam))) {
        return false;
    }
    emit Activated(static_cast<int>(msg->wParam));
    return true;
}
