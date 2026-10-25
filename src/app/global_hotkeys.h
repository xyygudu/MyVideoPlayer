#ifndef MVP_APP_GLOBAL_HOTKEYS_H_
#define MVP_APP_GLOBAL_HOTKEYS_H_

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QVector>

/// System-wide hotkeys (they work while another application has the focus,
/// as recorders need): RegisterHotKey on the GUI thread, whose WM_HOTKEY
/// messages Qt's event dispatcher hands to this native event filter.
///
/// GUI thread only.
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

  public:
    explicit GlobalHotkeys(QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    /// Registers Ctrl+Shift+`function_key` (1..24 for F1..F24). False when
    /// another application already owns the combination.
    bool RegisterCtrlShiftF(int id, int function_key);

    /// "Ctrl+Shift+F9".
    static QString Text(int function_key);

    bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;

  signals:
    void Activated(int id);

  private:
    QVector<int> registered_;
};

#endif  // MVP_APP_GLOBAL_HOTKEYS_H_
