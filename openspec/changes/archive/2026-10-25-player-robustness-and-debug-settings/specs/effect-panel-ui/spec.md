## ADDED Requirements

### Requirement: 每个特效组提供「重置参数」按钮
每个特效分组 SHALL 在参数控件下方提供「重置参数」按钮，点击后 SHALL 调用 `MediaPlayer::ResetEffectParams(effect_id)`，并在当前槽函数返回后（排队调用）按 `EffectInfos()` 重建全部控件，使滑块/复选框/下拉框回到默认值。分组被禁用（未勾选）时按钮随分组一起禁用。

#### Scenario: 重置后控件回到默认值
- **WHEN** 拖动亮度、对比度滑块后点击颜色分组的「重置参数」
- **THEN** 两个滑块回到默认位置，暂停中的画面同步恢复
