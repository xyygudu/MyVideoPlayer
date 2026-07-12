#include "effect_panel.h"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include "mvp/media_player.h"

namespace {

// Resolution of the QSlider used for kFloat/kInt params (QSlider is
// integer-only; params are mapped onto [0, kSliderSteps] and back).
constexpr int kSliderSteps = 1000;

float ParamFloat(const mvp::EffectParamValue& v) {
    if (std::holds_alternative<float>(v)) return std::get<float>(v);
    if (std::holds_alternative<int>(v)) return static_cast<float>(std::get<int>(v));
    return 0.0f;
}

int ToSliderPos(float value, float min_v, float max_v) {
    if (max_v <= min_v) return 0;
    float t = (value - min_v) / (max_v - min_v);
    return static_cast<int>(std::clamp(t, 0.0f, 1.0f) * kSliderSteps + 0.5f);
}

float FromSliderPos(int pos, float min_v, float max_v) {
    float t = static_cast<float>(pos) / static_cast<float>(kSliderSteps);
    return min_v + t * (max_v - min_v);
}

}  // namespace

EffectPanel::EffectPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    tab_widget_ = new QTabWidget(this);
    outer->addWidget(tab_widget_);

    effects_tab_ = new QWidget(tab_widget_);
    effects_layout_ = new QVBoxLayout(effects_tab_);
    effects_layout_->addStretch(1);
    tab_widget_->addTab(effects_tab_, QStringLiteral("Effects"));
}

void EffectPanel::RefreshFromPlayer(mvp::MediaPlayer* player) {
    player_ = player;
    RebuildEffectsTab();
}

void EffectPanel::RebuildEffectsTab() {
    while (effects_layout_->count() > 0) {
        QLayoutItem* item = effects_layout_->takeAt(0);
        delete item->widget();
        delete item;
    }

    if (!player_) {
        effects_layout_->addStretch(1);
        return;
    }

    for (const mvp::EffectInfo& info : player_->EffectInfos()) {
        auto* group = new QGroupBox(QString::fromStdString(info.display_name), effects_tab_);
        group->setCheckable(true);
        group->setChecked(info.enabled);
        std::string effect_id = info.effect_id;
        connect(group, &QGroupBox::toggled, this, [this, effect_id](bool checked) {
            if (player_) player_->SetEffectEnabled(effect_id, checked);
        });

        auto* form = new QFormLayout(group);
        for (const mvp::EffectParam& param : info.params) {
            form->addRow(QString::fromStdString(param.display_name),
                        BuildControlForParam(effect_id, param));
        }
        effects_layout_->addWidget(group);
    }
    effects_layout_->addStretch(1);
}

QWidget* EffectPanel::BuildControlForParam(const std::string& effect_id,
                                           const mvp::EffectParam& param) {
    switch (param.type) {
        case mvp::EffectParamType::kBool: {
            auto* box = new QCheckBox(effects_tab_);
            box->setChecked(std::get<bool>(param.value));
            std::string id = param.id;
            connect(box, &QCheckBox::toggled, this, [this, effect_id, id](bool checked) {
                if (player_) player_->SetEffectParam(effect_id, id, mvp::EffectParamValue(checked));
            });
            return box;
        }
        case mvp::EffectParamType::kEnum: {
            auto* combo = new QComboBox(effects_tab_);
            for (const std::string& label : param.enum_labels) {
                combo->addItem(QString::fromStdString(label));
            }
            combo->setCurrentIndex(std::get<int>(param.value));
            std::string id = param.id;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this, effect_id, id](int index) {
                        if (player_) player_->SetEffectParam(effect_id, id, mvp::EffectParamValue(index));
                    });
            return combo;
        }
        case mvp::EffectParamType::kInt:
        case mvp::EffectParamType::kFloat:
        default: {
            auto* container = new QWidget(effects_tab_);
            auto* row = new QHBoxLayout(container);
            row->setContentsMargins(0, 0, 0, 0);

            const float min_v = ParamFloat(param.min_value);
            const float max_v = ParamFloat(param.max_value);
            const float value = ParamFloat(param.value);
            const bool is_int = (param.type == mvp::EffectParamType::kInt);

            auto* slider = new QSlider(Qt::Horizontal, container);
            slider->setRange(0, kSliderSteps);
            slider->setValue(ToSliderPos(value, min_v, max_v));
            row->addWidget(slider, 1);

            auto* value_label = new QLabel(QString::number(value, 'f', is_int ? 0 : 2), container);
            value_label->setFixedWidth(48);
            row->addWidget(value_label);

            std::string id = param.id;
            connect(slider, &QSlider::valueChanged, this,
                    [this, effect_id, id, min_v, max_v, value_label, is_int](int pos) {
                        float v = FromSliderPos(pos, min_v, max_v);
                        if (is_int) v = std::round(v);
                        value_label->setText(QString::number(v, 'f', is_int ? 0 : 2));
                        if (!player_) return;
                        mvp::EffectParamValue pv = is_int
                            ? mvp::EffectParamValue(static_cast<int>(v))
                            : mvp::EffectParamValue(v);
                        player_->SetEffectParam(effect_id, id, pv);
                    });
            return container;
        }
    }
}
