## Context

当前管线固定为 `DemuxNode -> DecoderNode -> {Video,Audio}SinkNode`，由 `MediaPlayer::Impl::BuildGraph`（[playback-graph-builder](../../specs/playback-graph-builder/spec.md)）以 inline `Connect()` 方式拼装。`INode` 已经提供了 `NodeType`/`ThreadingMode` 的通用抽象（[graph-node-lifecycle](../../specs/graph-node-lifecycle/spec.md)），`OutputPort::Push()` 已经区分 Passive（同步调用 `Process()`）与 Active（走 `Link`）两种下游调度方式（`src/media/graph/port.cc`）。硬件解码路径当前未接入（`DecoderNode` 无 hw_device_ctx/get_format 相关代码），所有解码帧均为软件帧。

`graph-transform-nodes` spec 中已有 `AVFilterNode` 的设计（包一整段 libavfilter 字符串），但那是面向未来 `graph-transcode` 场景的通用滤镜封装，参数以字符串描述、运行时改参需要 `avfilter_graph_send_command` 或整图重建，与本次"UI 面板要逐个展示特效名称+参数+实时滑块调节"的需求不匹配，因此本次改动**不复用/不修改** `AVFilterNode` 设计，而是新增一组专用于播放时实时调参的手写 CPU 特效节点。

## Goals / Non-Goals

**Goals:**
- 定义 `IEffectNode` 接口：在 `INode` 之上补充"参数反射"能力——每个参数携带类型标签（float/int/bool/enum）、当前值、默认值、范围，供 UI 层无需知道具体特效类型即可动态选择控件（滑块/数字框/勾选框/下拉框）并渲染。
- 每个 `IEffectNode` SHALL 支持启用/禁用（`IsEnabled()`/`SetEnabled()`），禁用时透传输入帧，不做任何处理。
- 实现 `TransformEffectNode`（旋转任意角度、水平翻转、垂直翻转、缩放、平移合并为一次像素重映射，使用反向仿射变换 + 双线性插值）与 `ColorEffectNode`（亮度/对比度/饱和度）。
- 两者默认 `ThreadingMode::kPassive`，与 `DecoderNode` 同线程执行，不引入新的队列/线程模型；保留"以后若某个特效耗时，可单独改为 `kActive`"的逃生舱（无需修改 `MediaGraph`/`Port` 代码）。
- 新增 `EffectManager`，作为 `MediaPlayer::Impl` 的成员，统一索引/查询/控制已注册的 `IEffectNode`，替代在 `MediaPlayer::Impl` 里直接维护裸指针 map。
- `BuildGraph` 在 video 分支中插入这两个节点，保持函数体 ≤ 40 行的既有约束（[playback-graph-builder](../../specs/playback-graph-builder/spec.md)）。
- Qt 右侧新增 `EffectPanel`（`QTabWidget` 承载，当前只有一个 "Effects" tab），实时读写特效参数，并根据参数类型选用合适的控件。

**Non-Goals:**
- 不实现基于 libavfilter 的滤镜（模糊、锐化等留待未来，若需要以 `IEffectNode` 的另一实现形式接入，不在本次范围）。
- 不实现 GPU/shader 特效（renderer 内部渲染阶段的特效留作后续优化方向）。
- 不实现特效的增删/重排（本次 `EffectManager` 只索引 `BuildGraph` 中固定接线好的两个节点，不支持运行时新增/移除/调整顺序；这是后续工作，`EffectManager` 的接口形状为此留了余地但本次不实现）。
- 不实现 Info/Playlist tab（仅 Effects tab）。
- 不处理硬件解码帧（当前无硬解路径，未来若恢复硬解，需要额外的 hwdownload 协商逻辑，不在本次范围）。

## Decisions

### 1. `IEffectNode` 接口：类型标签 + 变体值，而非裸 float

```cpp
enum class EffectParamType { kFloat, kInt, kBool, kEnum };

// 用 std::variant 而非 std::any：参数取值范围在设计期就已知（只有这四种），
// variant 是类型安全的"闭集合"，访问用 std::get/std::visit 有编译期检查；
// std::any 是"开放集合的类型擦除容器"，适合完全不知道未来会装什么类型的场景
// （比如插件系统的用户数据），这里不需要那种灵活性，反而会丢失类型检查。
//
// 注意 kEnum 不对应 variant 里的独立存储类型：它复用 int 这个 alternative，
// value 存的是 enum_labels 数组的下标，kEnum 只是"UI 该用下拉框展示"这个
// 解释方式的标签，不是存储类型，所以 variant 只需 3 个真实类型即可覆盖 4 种语义。
//
// 不含 double：当前所有参数都是围绕 0~1/-1~1/0~360 的小范围比例或角度，
// float32 精度（约 7 位有效数字）远超 UI 滑块 2~3 位小数的展示需求。若未来
// 出现确实需要 double 的参数，扩展路径是在此 variant 追加 double、
// EffectParamType 追加 kDouble，IEffectNode/EffectManager/EffectPanel 的公共
// 接口无需改动，只需在具体节点的 SetParam 分支和 EffectPanel 的控件工厂里
// 各加一个分支；代价是 variant 从 8 字节可能涨到 16 字节，
// std::atomic<EffectParamValue> 可能从无锁退化为内部锁保护（仍然正确，
// 只是不再无锁），现在没有具体需求，不预先做这个改动。
using EffectParamValue = std::variant<float, int, bool>;

struct EffectParam {
    std::string id;                        // "brightness"
    std::string display_name;              // "亮度"
    EffectParamType type;                  // 决定 UI 用什么控件展示
    EffectParamValue value;                // 当前值（调用 Params() 时的快照）
    EffectParamValue default_value;
    EffectParamValue min_value;            // kBool 时忽略
    EffectParamValue max_value;            // kBool 时忽略
    std::vector<std::string> enum_labels;  // 仅 kEnum 使用；value 是索引(int)
};

class IEffectNode : public INode {
  public:
    // 一次性返回全部参数的完整快照（含当前值），UI 用它一次性建好所有控件。
    virtual std::vector<EffectParam> Params() const = 0;

    // 按 id 设置单个参数，thread-safe（UI 线程调用，节点处理线程读取）。
    virtual void SetParam(const std::string& id, EffectParamValue value) = 0;

    // 开关：禁用时 Process() 直接透传输入帧，不做任何计算。
    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;
};
```

**简化实现：不引入共享基类/参数表机制，两个子类各自直接实现这 4 个方法**——每个参数就是一个具名的 `std::atomic<EffectParamValue>` 成员，`Params()` 直接手写返回一个字面量 `vector`，`SetParam()` 直接手写 if-else 按 id 分发。以 `ColorEffectNode`（3 个参数）为例：

```cpp
class ColorEffectNode : public IEffectNode {
  public:
    std::vector<EffectParam> Params() const override {
        return {
            {"brightness", "亮度", EffectParamType::kFloat, brightness_.load(), 0.0f, -1.0f, 1.0f, {}},
            {"contrast",   "对比度", EffectParamType::kFloat, contrast_.load(), 1.0f, 0.0f, 3.0f, {}},
            {"saturation", "饱和度", EffectParamType::kFloat, saturation_.load(), 1.0f, 0.0f, 3.0f, {}},
        };
    }

    void SetParam(const std::string& id, EffectParamValue value) override {
        if (id == "brightness") { if (std::holds_alternative<float>(value)) brightness_.store(value); }
        else if (id == "contrast") { if (std::holds_alternative<float>(value)) contrast_.store(value); }
        else if (id == "saturation") { if (std::holds_alternative<float>(value)) saturation_.store(value); }
        else { SPDLOG_WARN("ColorEffectNode: unknown param '{}'", id); }
    }

    bool IsEnabled() const override { return enabled_.load(); }
    void SetEnabled(bool enabled) override { enabled_.store(enabled); }

    void Process(MediaBuffer input, OutputCallback emit) override {
        if (!enabled_.load()) { emit(std::move(input)); return; }  // 禁用直通
        // ... 用 brightness_/contrast_/saturation_ 做实际像素处理 ...
    }

  private:
    std::atomic<EffectParamValue> brightness_{0.0f};
    std::atomic<EffectParamValue> contrast_{1.0f};
    std::atomic<EffectParamValue> saturation_{1.0f};
    std::atomic<bool> enabled_{true};
};
```

`TransformEffectNode` 是同样的写法，只是参数从 3 个变成 7 个（`rotate_deg_`/`flip_h_`/`flip_v_`/`scale_x_`/`scale_y_`/`translate_x_`/`translate_y_`）。

**优点**：
- 每个类从上到下自包含、线性可读——打开一个文件就能看到它有哪些参数、每个参数怎么被使用，不需要跳到基类去理解 `RegisterParam`/索引常量这些间接层。
- 没有额外的类型（`ParamSlot`）、没有运行时的索引查找表，调试时在 `SetParam` 的 if 分支上直接打断点就是命中位置，链路最短。
- 改动成本低：现在只有 2 个效果、共 10 个参数，`if-else` 分支不会超过一屏，不会撞到项目"函数超 50 行需重构"的红线。

**缺点（如实说明，不是没有代价）**：
- `Params()`/`SetParam()` 里的 if-else 分发模式在两个类里各写一遍，是重复代码，但重复的是"分发外壳"，不是业务逻辑，重复量很小（每个类十几行）。
- `id` 字符串在 `Params()`（构造 `EffectParam::id`）和 `SetParam()`（if 判断）两处各写一次，需要人工保持一致，拼写错漏只能在运行时通过"未知参数"警告日志发现，没有编译期检查。
- 以后如果效果种类明显增多（比如加到 5、6 个，或者同一个特效的参数超过 10 个），这种"每个类手写分发"的重复会变得烦人，那时候把公共的"按 id 查找存储表"抽成一个共享基类（也就是被否决的 `EffectNodeBase`/`RegisterParam` 那套）是一次局部重构，不影响 `IEffectNode` 这个公共接口和已经写好的调用方（`EffectManager`/`EffectPanel` 只认 `IEffectNode`，不关心子类内部怎么存参数），成本可控，不属于"现在就要设计对"的范畴——这正是本次先不做的原因：当前规模下提供不了足够的收益去抵消额外的间接层复杂度。

**备选方案**：`EffectNodeBase` + `ParamSlot` 参数表反射（按 id 统一查找、`RegisterParam` 声明）——已否决，在只有 2 个效果、10 个参数的规模下收益不明显，属于为尚未出现的重复预先抽象，推迟到真正出现"第 3 个效果"时再评估是否值得。

- **为什么每个参数都要有 `type` 标签**：UI 面板要为不同参数选择不同控件——`kFloat` 用连续滑块（显示小数）、`kInt` 用步进滑块/数字框（整数步长）、`kBool` 用勾选框、`kEnum` 用下拉框（`enum_labels` 提供选项文案，`value` 存选中索引）。没有类型标签，UI 只能靠猜或者为每个具体特效写 if-else，违反"UI 无需知道具体特效类型"的初衷。
- **为什么 `value` 和 `default_value` 分开**：`default_value` 是"重置"用的静态值（UI 在 `Close()`/重新 `Open()` 后把控件恢复到它），`value` 是运行时可变的当前值（每次 `Params()` 调用返回的快照）；两者语义不同，合并成一个字段会让"重置"操作无处下手。
- **节点内部存储：`std::atomic<EffectParamValue>`，不做类型擦除编码**。`std::variant<float,int,bool>` 的三个 alternative 都是 trivially copyable 的标量类型，`variant` 本身满足 `std::atomic<T>` 对 `T` 的全部要求（trivially copyable + trivially copy-constructible/assignable），因此 `std::atomic<std::variant<float,int,bool>>` 是合法的，在 x86-64/ARM64 上大小通常为 8 字节、大概率无锁（可用 `is_lock_free()` 确认）；即使某平台退化为内部锁保护，读写频率也只是"每帧一次"量级，代价可忽略。`SetParam` 校验传入 variant 的类型（`std::holds_alternative<T>`）是否匹配该参数声明的 `EffectParamType`，不匹配则记录 spdlog 警告并忽略；匹配则整体原子写入 variant。**不再把 bool/int 编码成 float**——那样会丢类型信息，靠"0.0f/1.0f 表示 bool"这种没有编译期检查的隐藏约定，是实现层面的取巧，不是类型安全。
- **为什么单独 `SetParam` 而非整体 `SetAllParams(struct)`**：滑块拖动天然是单参数增量更新，逐参数设置避免"传整个 struct 但只改了一个字段"的冗余搬运。
- **`enabled` 为什么不是 `Params()` 里的一项，而是独立的 `IsEnabled()`/`SetEnabled()`**：项目里 `INode` 已有先例——`virtual void SetPaused(bool paused)` 是一个独立于任何节点业务参数之外的控制面方法（`VideoSinkNode` 用它冻结/恢复渲染循环，属于"是否处理数据"这个正交轴，不是数据流本身）。`IEffectNode::SetEnabled` 是同一类概念（"这个节点要不要工作" vs. "工作时用什么参数"），保持独立是遵循项目已有的对称结构（架构审查里"是否保持对称性"这一项）。如果把 `enabled` 塞进 `Params()`，UI 仍然要在渲染时特殊识别 `id == "enabled"` 才能把它摘出来放到分组标题旁（而不是参数列表里）——特殊判断并没有消失，只是从"API 层一个具名方法"搬到了"UI 层认一个魔法字符串"，类型系统检查不到，反而更脆弱。
  **但 `IEffectNode` 不应该直接复用 `INode::SetPaused` 来表达启用/禁用**：两者虽然都是"控制面"，语义和触发轴完全不同——`SetPaused` 是播放器全局暂停状态级联下来的（`MediaPlayer::Pause()` → `MediaGraph::SetPaused()` → 所有节点），而特效启用/禁用是用户对单个特效的独立开关，与播放是否暂停无关（暂停时也应该能继续勾选/取消某个特效）。复用 `SetPaused` 会把两个正交概念糅进同一个方法，导致播放暂停的级联调用误触发特效开关状态，这才是真正的"和稀泥"。因此 `SetEnabled`/`IsEnabled` 保持独立声明，是"遵循已有对称结构"与"避免语义冲突"两条约束共同要求的结果。
- **启用/禁用的实现成本很低**：每个子类一个 `std::atomic<bool> enabled_{true}` 成员 + `Process()` 开头 `if (!enabled_.load()) { emit(std::move(input)); return; }`，两行代码，重复两次也无妨。

**备选方案**：像 `AVFilterNode` 一样用一个字符串描述整条链再重新 parse——已在探索阶段否决（重建/command 下发的复杂度与本次"实时滑块"需求不匹配）。用 `std::any` 替代 `std::variant`——已否决，参数类型集合在设计期完全已知，`any` 只会丢失类型安全并增加运行时 `any_cast` 失败的风险。用 `std::mutex` 替代 `atomic<EffectParamValue>`——已否决，读写路径是"UI 线程写一次、处理线程每帧读一次"的单生产者单消费者场景，原子操作已经足够且不会引入锁竞争阻塞处理线程（这类阻塞风险正是此前讨论"耗时特效影响同步"时要避免的），引入互斥锁是不必要的更重同步原语。

### 2. `TransformEffectNode`：任意角度旋转，反向仿射映射 + 双线性插值，画布尺寸固定不变

**为什么最初想只支持直角旋转**：直角旋转（90/180/270）是无损的像素排列——每个目标像素都精确对应唯一一个源像素，不需要在像素"之间"取值，实现是最简单的整数下标重排。任意角度旋转后，大多数目标像素的反向映射坐标落在源像素网格的中间（非整数坐标），必须做插值（不能直接下标访问），并且旋转后的矩形边界不再贴合原始画布，需要决定角落怎么处理。这两点是"直角旋转"和"任意角度旋转"实现复杂度的真正差异，但都是很成熟的标准技术（FFmpeg 的 `rotate` 滤镜、OpenCV 的 `warpAffine`、几乎所有图像编辑器的旋转工具都是这么做的），不是不能做，只是最初为了先交付一个最小版本而选择性跳过。既然你希望支持任意角度，这里直接采用标准做法：

1. **合成一个 2×3 仿射矩阵**：先绕画布中心旋转 `rotate_deg`，再应用 `flip_h`/`flip_v`（等价于给对应轴取负），再 `scale_x`/`scale_y`，再按归一化比例 `translate_x`/`translate_y` 平移。矩阵按"旋转 → 翻转 → 缩放 → 平移"的固定顺序复合，用户看到的效果始终一致，不会因为参数调节顺序不同产生歧义。
2. **对输出画布的每个像素 `(dstX, dstY)`，用矩阵的逆变换算出源坐标 `(srcX, srcY)`**（浮点数，不再是整数）。这一步是"反向映射"（backward mapping），是图像几何变换的标准做法——正向映射（把每个源像素投影到目标）会在目标画布上留空洞，反向映射保证每个目标像素都有确定的取值来源。
3. **双线性插值**：`srcX`/`srcY` 通常落在 4 个相邻源像素之间，取这 4 个像素按距离加权平均。对 Y/U/V 每个平面分别做（色度平面分辨率更低，插值开销更小）。
4. **画布尺寸固定为输入帧的原始宽高，不随角度变化**：如果反向映射得到的 `(srcX, srcY)` 落在源画布之外（旋转/缩放后角落露出画布边界），该目标像素填充"黑色"——注意 Y 平面填 0，但 **U/V 平面必须填 128（YUV 的中性色度值），而不是 0**，否则边界会出现明显的蓝绿色偏色而不是黑色。

    选择"画布尺寸固定+越界裁黑"而不是"画布跟着旋转角度动态扩大"，是因为后者需要在参数调节时动态改变协商后的 `MediaFormat`（宽高），意味着运行时重新触发 `Negotiate()`，牵扯下游（`VideoSinkNode` 渲染尺寸、`Link` 已缓冲的旧尺寸帧）的连锁改动，复杂度远超这个特效本身的价值。固定画布是绝大多数"视频旋转/变换"工具的实际做法（旋转飞控画面、监控画面校正等场景都是固定输出分辨率），也让 `Negotiate()` 保持原来的"输出格式=输入格式"这一最简单形式，不需要 90/270 特殊处理宽高互换（这是本次相对最初方案的简化：不再需要区分角度做宽高交换）。

**性能**：双线性插值比直接整数下标多几倍的读取和乘加，但仍然是逐像素 O(1) 运算，1080p 级别下预计仍在个位数毫秒量级，远低于 30/60fps 的帧预算，不需要现在做 SIMD 优化。

**备选方案**：
- 每个几何操作一个独立节点（`RotateNode`/`FlipNode`/`ScaleNode`/`TranslateNode`）——已在探索阶段否决，用户明确希望这几个操作算一类，且合并实现性能更好、UI 分组也更自然。
- 画布跟随旋转角度动态扩展——已否决，代价是引入运行时重新协商格式的复杂链路，收益不成比例。
- 最近邻采样（不插值，直接四舍五入取最近源像素）——比双线性插值实现更简单，但旋转/缩放后会有明显锯齿，画质不如双线性，双线性的额外代码量可控（一个 `SampleBilinear(plane, x, y)` 辅助函数），选择双线性。

### 3. `ColorEffectNode` 只处理 YUV 类 pixel format 的 Y/U/V 平面

亮度/对比度作用于 Y 平面（线性变换 `Y' = clamp((Y-128)*contrast + 128 + brightness*255)`），饱和度作用于 U/V 平面（`UV' = clamp((UV-128)*saturation + 128)`）。仅支持 `kYUV420P`/`kYUV422P`/`kYUV444P`/`kNV12`；`kRGB32` 暂不支持（Prepare 阶段检测到不支持的格式时記录 spdlog 警告并将该节点降级为透传，不阻塞播放）。

### 4. 两个节点默认 `ThreadingMode::kPassive`，不新增队列

延续 `Decoder(Active) -> [Passive Transform] -> [Passive Color] -> Sink(Active)` 的链路：中间两个节点没有自己的 `Link`，`OutputPort::Push()` 已有的 Passive 分支（同步调用 `Process()`）天然把它们焊接到 Decoder 线程；`VideoSinkNode` 输入端口自带的 `Link` 就是唯一的"渲染用队列"，不需要新增任何队列结构。若未来某个特效开销较大，只需把该节点的 `Threading()` 改为 `kActive`，`Connect()` 会自动在其前后建 `Link`，形成独立流水线阶段——这是 `graph-node-lifecycle` 既有机制的直接复用，不需要修改 `MediaGraph`/`Port`。

### 5. `BuildGraph` 通过私有辅助方法插入 effect 节点，维持函数体行数约束

`playback-graph-builder` spec 要求 `BuildGraph` ≤ 40 行。直接在 `BuildGraph` 里插入两个 `AddNode`+改 `Connect` 目标会超出。设计为新增一个私有方法 `WireVideoEffects(DecoderNode*, VideoSinkNode*) -> effect 输出端口`，封装"创建两个 effect 节点 + AddNode + 内部 Connect(Transform->Color)"，`BuildGraph` 只需把原来 `Connect(demux->Outputs()[0], vdec->Inputs()[0])` 和 `Connect(vdec->Outputs()[0], vsink->Inputs()[0])` 中间那一段替换为调用该辅助方法并把返回的端口用于最终 Connect。这符合项目里"函数超 50 行必须先拆分"的约束，也符合"新增功能不应让不相关模块产生跨越式耦合"的审查项。

### 6. 新增 `EffectManager`：索引已注册的 `IEffectNode`，是 `MediaPlayer::Impl` 的成员，不是 `MediaGraph` 的成员

业界几个参考做法：
- **OBS Studio**：每个 `obs_source_t` 内部维护一个 filter 链表，通过 `obs_source_filter_*` 系列 API 增删/枚举/排序，这个"链"是 source 自己的内部状态，不是一个独立于 source 之外的全局对象。
- **mpv**：`vf` 滤镜链（`struct vf_chain`）挂在视频解码上下文里，通过属性系统（`vf`、`vf-command`）操作，同样是"寄生"在播放上下文对象上，没有单独抽出一个全局"filter manager"。
- **GStreamer**：压根没有专门的"effect manager"概念，filter 就是 pipeline 里的普通 element，要枚举/控制就直接操作 `GstBin`/`GstElement`。

三者的共同点：**"管理特效"这件事，不是图执行引擎（`MediaGraph`/`GstPipeline`）的职责，而是"拥有这条特定业务管线的上层对象"（`obs_source_t`/`dec_video`/mpv 的播放上下文）的职责**。这与本项目 `MediaGraph` 当前的定位一致——`MediaGraph` 只认识 `INode`/`Port`/`Link`，完全不知道"特效"这个业务概念，这样保持了它的通用性（`graph-node-lifecycle`/`media-graph-core` 定义的都是与业务无关的执行引擎能力）。所以 `EffectManager` 不应该塞进 `MediaGraph`，而应该和 `Clock audio_clock_`、`VideoRenderer video_renderer_` 一样，作为 `MediaPlayer::Impl` 的成员——`MediaPlayer::Impl` 本来就是"给通用图赋予播放器业务语义"的那一层。

```cpp
// src/media/effect_manager.h（核心库，非 Qt 依赖）
struct EffectInfo {
    std::string effect_id;
    std::string display_name;
    bool enabled;
    std::vector<EffectParam> params;
};

class EffectManager {
  public:
    // 非拥有指针：节点的所有权始终在 MediaGraph::nodes_ 里，EffectManager 只是
    // 一份按 effect_id 索引的只读视图（对称于 DecoderNode::SetGraph 这种
    // "非拥有引用注入"的既有约定，见 graph-shared-resources）。
    void Register(std::string effect_id, std::string display_name,
                  graph::IEffectNode* node);
    void Clear();  // Close() 时随 graph_.reset() 一起调用

    std::vector<EffectInfo> Describe() const;
    bool SetParam(const std::string& effect_id, const std::string& param_id,
                  EffectParamValue value);
    bool SetEnabled(const std::string& effect_id, bool enabled);

  private:
    struct Entry { std::string display_name; graph::IEffectNode* node; };
    std::vector<std::pair<std::string, Entry>> effects_;  // 保留注册顺序
};
```

`WireVideoEffects`（Decision 5）创建节点后立即调用 `effect_manager_.Register("transform", "Transform", transform_node)` / `Register("color", "Color", color_node)`。`EffectManager` 不决定拓扑顺序（拓扑连接仍由 `WireVideoEffects` 里的 `Connect()` 决定），只负责"按 id 查找 + 汇总成 UI 友好的结构"——这样职责边界清晰：**图的连接方式属于 graph builder，特效的查询/控制属于 EffectManager**，两者都不越界。

**为什么现在不支持增删/重排**：`effects_` 用 `vector<pair<id, Entry>>` 而非直接 `unordered_map`，是刻意为将来的"支持重排"留了结构上的余地（vector 有序，重排只是调整元素顺序；`unordered_map` 天然无序，以后要加顺序还得再包一层）。但本次 `Register` 只在 `BuildGraph` 时调用两次，不提供运行时增删接口——这是 Non-Goals 里明确排除的范围，多做了就是过度设计。

### 7. `MediaPlayer` 新增的公共 API，委托给 `EffectManager`

```cpp
std::vector<EffectInfo> MediaPlayer::EffectInfos() const;
void MediaPlayer::SetEffectParam(const std::string& effect_id,
                                  const std::string& param_id,
                                  EffectParamValue value);
void MediaPlayer::SetEffectEnabled(const std::string& effect_id, bool enabled);
```

三者都是薄委托：`impl_->effect_manager_.Describe()` / `.SetParam(...)` / `.SetEnabled(...)`。`EffectPanel` 在 `Open()` 完成后调用 `EffectInfos()` 一次性拿到结构（含 `enabled` 状态），动态生成分组、启用勾选框和参数控件；勾选框/滑块的交互分别调用 `SetEffectEnabled`/`SetEffectParam`。

### 8. Qt 布局：`QSplitter` 左右分栏

`MainWindow::SetupUi` 改为顶层 `QSplitter(Qt::Horizontal)`：左侧沿用现有 `VideoWidget` + 控制栏的 `QVBoxLayout` 容器，右侧是新增的 `EffectPanel`（内部一个 `QTabWidget`，当前仅 "Effects" tab）。选用 `QSplitter` 是因为它原生支持用户拖拽调整两侧宽度，且不需要额外状态管理，符合 Qt 惯用法。

## Risks / Trade-offs

- **[风险] 手写像素处理只覆盖有限 pixel format** → 缓解：Prepare 阶段做格式检测，不支持的格式直接透传（降级而非崩溃/报错中断播放），后续按需扩展。
- **[风险] 双线性插值 + 越界裁黑逻辑比直角旋转的整数重排更复杂，单元测试需要覆盖边界采样（越界坐标、U/V 平面 128 填充）** → 缓解：tasks 中单独列出针对越界填充值的用例，避免偏色回归。
- **[风险] Passive 链路下如果未来叠加更多特效导致 decode 线程总耗时变长，可能影响音视频同步表现（画面卡顿而非同步计算出错）** → 缓解：架构已预留 `kActive` 逃生舱（见 Decision 4），无需现在处理，仅记录该 trade-off。
- **[风险] `EffectPanel` 与 `MediaGraph` 生命周期耦合**：`Open()`/`Close()` 时 effect 节点会随 graph 一起销毁重建，`EffectManager::Clear()` 必须在 `Close()` 中随 `graph_.reset()` 一起调用，否则会残留指向已销毁节点的悬空指针；`EffectPanel` 需要在 `OnOpenFile` 完成后重新拉取 `EffectInfos()` 并重建控件。

## Migration Plan

- 无数据迁移；`MediaPlayer::SetFilter` 为 BREAKING 移除，检查项目内外无调用方（当前实现是 no-op 占位，语义上没有真正的迁移成本）。
- 实现顺序：先落地 `IEffectNode`/`TransformEffectNode`/`ColorEffectNode`（可独立单测像素变换逻辑）-> `EffectManager` -> 接入 `BuildGraph` -> `MediaPlayer` 新增 API -> `EffectPanel`/`MainWindow` 布局改造。每一步都可独立编译验证，不需要一次性大改。

## Open Questions

- 参数默认值与范围的具体数值（如 `contrast` 的合理范围）留待 tasks 阶段结合实现调试确定，不影响本设计的接口形状。
- `translate_x/translate_y` 的单位是像素还是归一化比例（0~1）：像素在不同分辨率下语义不一致，倾向用归一化比例，具体在 specs 阶段定案。
- 越界采样的填充策略当前固定为"黑色"（Y=0, U=V=128），是否需要做成可配置（比如边缘拉伸 clamp-to-edge）留待后续按需求补充。
