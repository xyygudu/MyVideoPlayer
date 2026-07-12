## ADDED Requirements

### Requirement: IEffectNode 定义类型化参数反射接口
系统 SHALL 定义 `EffectParamType` 枚举（`kFloat`/`kInt`/`kBool`/`kEnum`），标识参数应使用的控件类型。系统 SHALL 定义 `EffectParamValue` 为 `std::variant<float, int, bool>`。系统 SHALL 定义 `EffectParam` 结构体，包含：
- `id`（内部标识）、`display_name`（UI 展示名）、`type`（EffectParamType）
- `value`（当前值快照）、`default_value`
- `min_value`、`max_value`（对 `kBool` 无意义）
- `enum_labels`（`std::vector<std::string>`，仅 `type == kEnum` 时使用，`value` 存整数索引）

系统 SHALL 定义 `IEffectNode` 接口，继承 `INode`，作为所有可实时调参的特效节点的公共基类。IEffectNode SHALL 声明：
- `std::vector<EffectParam> Params() const`：返回该节点全部参数的有序快照（含当前值）。
- `void SetParam(const std::string& id, EffectParamValue value)`：按 id 设置参数值，SHALL 线程安全（可被 UI 线程调用，同时被节点自身处理线程读取）。若传入的 variant 类型与该参数声明的 `EffectParamType` 不匹配，SHALL 记录 spdlog 警告并忽略该次设置。
- `bool IsEnabled() const` / `void SetEnabled(bool enabled)`：控制该节点是否生效，线程安全。禁用时 `Process()` SHALL 直接透传输入 buffer，不做任何计算。

IEffectNode 的 `Type()` SHALL 返回 `NodeType::kTransform`。IEffectNode SHALL 不强制 `ThreadingMode`，具体子类可选择 `kPassive`（默认，与上游节点同线程同步执行）或 `kActive`（拥有独立线程，通过 `MediaGraph`/`Port` 既有机制自动获得前后 `Link`），复用现有调度机制，不需要新增队列或线程管理代码。

#### Scenario: UI 通过参数类型选择控件
- **WHEN** 调用某 IEffectNode 的 `Params()`
- **THEN** 返回的每个 `EffectParam` 包含 `type`，UI 据此选择控件：`kFloat` 用连续滑块、`kInt` 用步进控件、`kBool` 用勾选框、`kEnum` 用下拉框（选项来自 `enum_labels`）

#### Scenario: 跨线程设置参数立即对下一帧生效
- **WHEN** UI 线程调用 `SetParam("brightness", 0.3f)`，此时节点处理线程正在处理某一帧
- **THEN** 当前帧不受影响（读取的是设置前或设置后的有效值，不发生数据竞争），下一帧处理时读取到新值 0.3f

#### Scenario: 未知参数 id 被安全忽略
- **WHEN** 调用 `SetParam("nonexistent", 1.0f)`
- **THEN** 节点不崩溃，SHALL 通过 spdlog 记录一条警告日志，参数状态不变

#### Scenario: 参数类型不匹配被安全忽略
- **WHEN** 对声明为 `kBool` 的参数（如 `flip_h`）调用 `SetParam("flip_h", 3.5f)`（传入 float 而非 bool）
- **THEN** 节点记录 spdlog 警告并忽略该次设置，参数保持原值不变

#### Scenario: 禁用节点后数据直通
- **WHEN** 调用 `SetEnabled(false)`
- **THEN** 后续 `Process()` 调用直接将输入 buffer 原样传递给下游，不执行任何像素运算

### Requirement: TransformEffectNode 合并几何变换，支持任意角度旋转
系统 SHALL 定义 `TransformEffectNode`（实现 IEffectNode，`ThreadingMode::kPassive`），将旋转、水平翻转、垂直翻转、缩放、平移合并为一次像素重映射处理，避免多趟遍历同一帧。

TransformEffectNode SHALL 提供以下参数：
- `rotate_deg`（`kFloat`）：任意角度（0.0~360.0，连续值），默认 0.0
- `flip_h` / `flip_v`（`kBool`）：是否水平/垂直翻转，默认 false
- `scale_x` / `scale_y`（`kFloat`）：缩放比例，默认 1.0
- `translate_x` / `translate_y`（`kFloat`）：归一化平移比例（相对帧宽高，-1.0~1.0），默认 0.0

TransformEffectNode SHALL 按"旋转 → 翻转 → 缩放 → 平移"的固定顺序复合出一个 2×3 仿射矩阵，对输出画布每个像素用矩阵的逆变换计算源坐标（反向映射），非整数源坐标 SHALL 使用双线性插值采样。输出帧尺寸 SHALL 恒等于输入帧尺寸，不随 `rotate_deg`/`scale_x`/`scale_y` 变化（Negotiate 阶段输出 MediaFormat 与输入相同）。反向映射坐标落在源画布之外的像素 SHALL 填充黑色：Y 平面填 0，U/V 平面填 128。

#### Scenario: 任意角度旋转不改变输出尺寸
- **WHEN** `rotate_deg` 设为 37.5，输入帧为 1920×1080
- **THEN** 输出帧尺寸仍为 1920×1080，画面内容围绕帧中心旋转 37.5 度，未被源画面覆盖的角落填充黑色

#### Scenario: 越界采样按平面填充正确的中性值
- **WHEN** 旋转/缩放导致某目标像素的反向映射坐标落在源画布之外
- **THEN** 该像素在 Y 平面填 0，在 U/V 平面填 128（避免出现非黑色的偏色边框）

#### Scenario: 多种几何操作组合在一次遍历完成
- **WHEN** 同时设置 `flip_h=true`、`rotate_deg=180`、`scale_x=1.2`
- **THEN** TransformEffectNode::Process() 只对输入帧遍历一次即可产出正确变换后的输出帧，不产生中间帧拷贝

#### Scenario: 默认参数下为恒等变换
- **WHEN** 所有参数保持默认值（rotate_deg=0.0, flip_h=false, flip_v=false, scale=1.0, translate=0.0）
- **THEN** 输出帧与输入帧像素内容一致（允许因内存拷贝产生的新对象，但像素值不变）

### Requirement: ColorEffectNode 调整亮度对比度饱和度
系统 SHALL 定义 `ColorEffectNode`（实现 IEffectNode，`ThreadingMode::kPassive`），对 YUV 类帧的 Y 平面做亮度/对比度线性变换，对 U/V 平面做饱和度缩放。

ColorEffectNode SHALL 提供参数（均为 `kFloat`）：`brightness`（默认 0.0）、`contrast`（默认 1.0）、`saturation`（默认 1.0）。

变换公式：
- `Y' = clamp(0, 255, (Y - 128) * contrast + 128 + brightness * 255)`
- `U' = clamp(0, 255, (U - 128) * saturation + 128)`，V 同理

ColorEffectNode SHALL 仅支持 `kYUV420P`/`kYUV422P`/`kYUV444P`/`kNV12` 这几种 pixel format；遇到不支持的格式（如 `kRGB32`）时 SHALL 在 Prepare 阶段记录 spdlog 警告并将该节点行为降级为透传（不修改帧数据），不得阻塞或中断播放。

#### Scenario: 提高亮度
- **WHEN** `brightness` 设为 0.2，输入帧某像素 Y=100
- **THEN** 输出该像素 Y 值约为 clamp(0,255, (100-128)*1.0+128+0.2*255) = 151（钳制到 [0,255]）

#### Scenario: 不支持的像素格式降级为透传
- **WHEN** 输入帧 pixel_format 为 `kRGB32`
- **THEN** ColorEffectNode::Prepare() 记录警告日志，Process() 直接透传原始帧，不做任何像素修改，不返回错误

#### Scenario: 默认参数下为恒等变换
- **WHEN** brightness=0.0, contrast=1.0, saturation=1.0
- **THEN** 输出帧与输入帧像素值一致

