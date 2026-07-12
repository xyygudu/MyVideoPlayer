#ifndef MVP_EFFECT_TYPES_H_
#define MVP_EFFECT_TYPES_H_

#include <string>
#include <variant>
#include <vector>

namespace mvp {

/// Identifies which UI control an EffectParam should be rendered with.
enum class EffectParamType {
    kFloat,  // Continuous slider
    kInt,    // Stepped slider / spin box
    kBool,   // Checkbox
    kEnum,   // Combo box; value is an index into EffectParam::enum_labels
};

/// Closed set of value types an effect parameter can hold. A variant (not
/// std::any) because the type set is fixed at design time.
using EffectParamValue = std::variant<float, int, bool>;

/// Snapshot of a single effect parameter, returned by IEffectNode::Params()
/// and, transitively, by MediaPlayer::EffectInfos().
struct EffectParam {
    std::string id;             // Stable identifier, e.g. "brightness"
    std::string display_name;   // UI label, e.g. "亮度"
    EffectParamType type{EffectParamType::kFloat};
    EffectParamValue value{0.0f};        // Current value (snapshot)
    EffectParamValue default_value{0.0f};
    EffectParamValue min_value{0.0f};    // Ignored when type == kBool
    EffectParamValue max_value{0.0f};    // Ignored when type == kBool
    std::vector<std::string> enum_labels;  // Only used when type == kEnum
};

/// Snapshot of one effect's identity + parameters, returned by
/// MediaPlayer::EffectInfos() for the UI layer to build its controls.
struct EffectInfo {
    std::string effect_id;
    std::string display_name;
    bool enabled{true};
    std::vector<EffectParam> params;
};

}  // namespace mvp

#endif  // MVP_EFFECT_TYPES_H_
