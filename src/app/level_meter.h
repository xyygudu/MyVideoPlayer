#ifndef MVP_APP_LEVEL_METER_H_
#define MVP_APP_LEVEL_METER_H_

#include <QWidget>

/// A horizontal audio level bar. SetPeak() feeds the peak (0..1, linear)
/// measured since the last call; the bar shows it on a dB scale and falls
/// back smoothly, like the meters of OBS or a mixing desk.
class LevelMeter : public QWidget {
    Q_OBJECT

  public:
    explicit LevelMeter(QWidget* parent = nullptr);

    void SetPeak(float peak);
    void Reset();

    QSize sizeHint() const override { return {120, 8}; }

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    float level_{0.0f};  // Displayed level, 0..1 on the dB scale
};

#endif  // MVP_APP_LEVEL_METER_H_
