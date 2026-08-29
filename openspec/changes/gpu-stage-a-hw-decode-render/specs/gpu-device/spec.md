## Purpose

Defines the graph-level GPU device abstraction: a platform-agnostic interface
wrapping FFmpeg's hardware device context, created by the pipeline builder
from the renderer backend's native device, injected into MediaGraph as a
shared resource, and consumed by nodes during negotiation (GStreamer-context
style). Also owns the single bidirectional mapping between FFmpeg
AVPixelFormat and the project PixelFormat enum.

## ADDED Requirements

### Requirement: GpuDevice 接口与职责边界
系统 SHALL 提供 `mvp::gpu::GpuDevice` 抽象接口,职责限于设备生命周期与能力查询:

- `PixelFormat Domain()`:该设备产生/消费的硬件帧域
- `AVBufferRef* DeviceRef()`:FFmpeg 设备上下文引用,调用方可经 `av_buffer_ref` 共享
- `bool SupportsDecoder(const AVCodec*)`:该编码器是否声明了此设备上的硬件解码配置

接口 SHALL NOT 包含解码/编码/特效的任何逻辑;get_format 回调、帧上下文创建等使用方关注点 SHALL 由使用方自己持有。

#### Scenario: 节点只依赖接口不依赖平台
- **WHEN** 任一节点使用图级 GPU 设备
- **THEN** 节点代码只引用 `mvp::gpu::GpuDevice`,不出现任何平台类型(ID3D11*/VAAPI/…)

#### Scenario: 新平台后端以同一接口接入
- **WHEN** 需要支持新平台(如 Linux VAAPI)
- **THEN** 新增一个 GpuDevice 实现类并在工厂注册,graph 与节点代码零改动

### Requirement: 设备从渲染器后端提取并共享
管线编排器 SHALL 通过 `GpuDevice::WrapExternal(native_device)` 包装渲染器后端的原生设备(如 SDL3 D3D11 后端的 ID3D11Device),使解码与呈现共享同一设备。

`WrapExternal` SHALL 返回 nullptr(不抛异常)当:传入空指针、设备上下文分配失败、包装初始化失败。调用方 SHALL 在返回 nullptr 时继续以软件路径构图。

#### Scenario: 成功包装
- **WHEN** 渲染器后端为 D3D11 且 `WrapExternal` 收到其设备指针
- **THEN** 返回有效 GpuDevice,`Domain()` 为 kD3D11,`DeviceRef()` 非空

#### Scenario: 包装失败不阻塞构图
- **WHEN** `WrapExternal` 因平台无后端返回 nullptr
- **THEN** 管线继续以软件路径构建,日志记录原因

### Requirement: 设备析构顺序契约
FFmpeg 释放 AVHWDeviceContext 时会释放被包装的设备接口,因此持有设备的 MediaGraph SHALL 先于提供设备的渲染器析构。该契约 SHALL 在注入点以注释固化。

#### Scenario: 关闭顺序
- **WHEN** MediaPlayer 关闭
- **THEN** graph 先于渲染器销毁,设备接口引用计数始终不为零直至渲染器关闭

### Requirement: 硬件解码能力探测
`SupportsDecoder` SHALL 通过 `avcodec_get_hw_config` 遍历该编码器的硬件配置，当存在 `AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX` 且 `device_type` 与本后端设备类型一致的配置时返回 true；遍历尽仍无匹配返回 false。

#### Scenario: 支持硬解的编码器
- **WHEN** 编码器(如 h264)声明了 D3D11VA 硬件配置
- **THEN** D3D11 后端实例的 `SupportsDecoder(codec)` 返回 true

#### Scenario: 不支持硬解的编码器
- **WHEN** 编码器没有本设备类型的硬件配置
- **THEN** 返回 false,调用方协商软格式

### Requirement: 呈现纹理生成服务
GpuDevice SHALL 提供 `CopyForPresentation(const AVFrame* hw_frame)`,把硬件解码帧复制到设备拥有的、可被呈现 API 直接绑定的独立纹理(GPU 侧 blit),返回非拥有指针。解码器帧是数组纹理(呈现 API 无法包装),故 SHALL 通过独立纹理池桥接;池大小 SHALL 大于管线在途帧数,保证纹理复用前所有引用已呈现完毕。

`CopyForPresentation` SHALL 在帧布局不支持时返回 nullptr,由调用方在自身线程做软件转换。该函数 SHALL 只在解码线程调用——设备的命令上下文非线程安全,所有设备操作 SHALL 收敛到提交解码工作的同一线程(ffmpeg CLI 单线程模型)。

#### Scenario: 解码帧被复制为可绑定纹理
- **WHEN** 解码线程对 D3D11 硬件帧调用 CopyForPresentation 且 sw_format 为 NV12/P010
- **THEN** 返回池中独立纹理指针,内容为解码帧的 GPU 拷贝,可被 SDL3 外部纹理绑定呈现

#### Scenario: 不支持布局返回 nullptr
- **WHEN** 硬件帧 sw_format 非 NV12/P010
- **THEN** 返回 nullptr,调用方在解码线程 av_hwframe_transfer_data 下载为软件帧

### Requirement: 像素格式映射唯一收口
系统 SHALL 提供 `gpu::FromAvPixelFormat` / `gpu::ToAvPixelFormat` 作为 FFmpeg AVPixelFormat 与项目 PixelFormat 之间的双向映射唯一实现点。未建模的格式 SHALL 映射为 `PixelFormat::kUnknown` / `AV_PIX_FMT_NONE`。其他模块 SHALL NOT 各自维护映射表。

#### Scenario: 硬件帧域映射
- **WHEN** `FromAvPixelFormat(AV_PIX_FMT_D3D11)`
- **THEN** 返回 `PixelFormat::kD3D11`;`ToAvPixelFormat(PixelFormat::kD3D11)` 返回 `AV_PIX_FMT_D3D11`

#### Scenario: 未建模格式
- **WHEN** 输入格式不在映射表内
- **THEN** 返回 kUnknown / AV_PIX_FMT_NONE,调用方据此走回退分支
