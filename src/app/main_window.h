#ifndef MVP_APP_MAIN_WINDOW_H_
#define MVP_APP_MAIN_WINDOW_H_

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <memory>

#include "mvp/media_player.h"

class VideoWidget;
class EffectPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

  private slots:
    void OnOpenFile();
    void OnPlayPause();
    void OnSliderMoved(int value);
    void OnTimerTick();

  private:
    void SetupUi();
    QString FormatTime(double seconds) const;

    VideoWidget* video_widget_;
    EffectPanel* effect_panel_;
    QPushButton* play_pause_btn_;
    QPushButton* open_btn_;
    QSlider* progress_slider_;
    QLabel* time_label_;
    QTimer* timer_;

    std::unique_ptr<mvp::MediaPlayer> player_;
    bool slider_pressed_;
};

#endif  // MVP_APP_MAIN_WINDOW_H_
