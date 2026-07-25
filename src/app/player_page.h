#ifndef MVP_APP_PLAYER_PAGE_H_
#define MVP_APP_PLAYER_PAGE_H_

#include <memory>

#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include "mvp/media_player.h"

class VideoWidget;
class EffectPanel;
class IconButton;

/// Player page: video canvas + playback control bar + EffectPanel,
/// extracted (behavior-wise unchanged) from the old MainWindow. Owns the
/// mvp::MediaPlayer instance. See the player-ui spec's modified delta —
/// only the hosting widget and control-bar styling changed.
class PlayerPage : public QWidget {
    Q_OBJECT

  public:
    explicit PlayerPage(QWidget* parent = nullptr);
    ~PlayerPage() override;

  protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

  private slots:
    void OnOpenFile();
    void OnPlayPause();
    void OnSliderMoved(int value);
    void OnTimerTick();

  private:
    void SetupUi();
    QWidget* BuildControlBar();
    QString FormatTime(double seconds) const;

    VideoWidget* video_widget_{nullptr};
    EffectPanel* effect_panel_{nullptr};
    IconButton* play_pause_btn_{nullptr};
    IconButton* open_btn_{nullptr};
    QSlider* progress_slider_{nullptr};
    QLabel* time_label_{nullptr};
    QTimer* timer_{nullptr};

    std::unique_ptr<mvp::MediaPlayer> player_;
    bool slider_pressed_{false};
};

#endif  // MVP_APP_PLAYER_PAGE_H_
