#include "effect_manager.h"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace mvp {

void EffectManager::Register(std::string effect_id, std::string display_name,
                             graph::IEffectNode* node) {
    effects_.emplace_back(std::move(effect_id),
                          Entry{std::move(display_name), node});
}

void EffectManager::Clear() {
    effects_.clear();
}

std::vector<EffectInfo> EffectManager::Describe() const {
    std::vector<EffectInfo> result;
    result.reserve(effects_.size());
    for (const auto& [effect_id, entry] : effects_) {
        EffectInfo info;
        info.effect_id = effect_id;
        info.display_name = entry.display_name;
        info.enabled = entry.node->IsEnabled();
        info.params = entry.node->Params();
        result.push_back(std::move(info));
    }
    return result;
}

bool EffectManager::SetParam(const std::string& effect_id, const std::string& param_id,
                             EffectParamValue value) {
    graph::IEffectNode* node = Find(effect_id);
    if (!node) {
        SPDLOG_WARN("EffectManager: SetParam on unknown effect_id '{}'", effect_id);
        return false;
    }
    node->SetParam(param_id, value);
    return true;
}

bool EffectManager::SetEnabled(const std::string& effect_id, bool enabled) {
    graph::IEffectNode* node = Find(effect_id);
    if (!node) {
        SPDLOG_WARN("EffectManager: SetEnabled on unknown effect_id '{}'", effect_id);
        return false;
    }
    node->SetEnabled(enabled);
    return true;
}

graph::IEffectNode* EffectManager::Find(const std::string& effect_id) const {
    auto it = std::find_if(effects_.begin(), effects_.end(),
                           [&](const auto& kv) { return kv.first == effect_id; });
    return (it != effects_.end()) ? it->second.node : nullptr;
}

}  // namespace mvp
