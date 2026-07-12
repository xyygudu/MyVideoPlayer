#ifndef MVP_APP_EFFECT_PANEL_H_
#define MVP_APP_EFFECT_PANEL_H_

#include <string>

#include <QWidget>

#include "mvp/effect_types.h"

namespace mvp {
class MediaPlayer;
}

class QTabWidget;
class QVBoxLayout;

/// Right-side panel. Currently hosts a single "Effects" tab that renders
/// one group per registered effect (from MediaPlayer::EffectInfos()): a
/// checkable QGroupBox (enable/disable) containing one control per
/// parameter, chosen by EffectParam::type.
///
/// The panel only reads/writes effect state through MediaPlayer's public
/// API — it has no knowledge of the graph, IEffectNode, or EffectManager.
class EffectPanel : public QWidget {
    Q_OBJECT

  public:
    explicit EffectPanel(QWidget* parent = nullptr);

    /// Clears and rebuilds all controls from player->EffectInfos(). Call
    /// after Open()/Close() so the panel never shows stale effects or
    /// holds onto values from a previous source (see design.md).
    void RefreshFromPlayer(mvp::MediaPlayer* player);

  private:
    void RebuildEffectsTab();
    QWidget* BuildControlForParam(const std::string& effect_id, const mvp::EffectParam& param);

    mvp::MediaPlayer* player_{nullptr};
    QTabWidget* tab_widget_{nullptr};
    QWidget* effects_tab_{nullptr};
    QVBoxLayout* effects_layout_{nullptr};
};

#endif  // MVP_APP_EFFECT_PANEL_H_
