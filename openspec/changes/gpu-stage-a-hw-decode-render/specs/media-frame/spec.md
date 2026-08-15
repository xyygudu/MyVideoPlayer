## MODIFIED Requirements

### Requirement: PixelFormat 和 SampleFormat 枚举
PixelFormat SHALL 区分两类取值:软件格式(kYUV420P/kYUV422P/kYUV444P/kNV12/kRGB32,像素数据在系统内存)与硬件帧域(kD3D11/kCuda/kQsv/kVAAPI/kVideoToolbox,像素数据在 GPU 内存,镜像 FFmpeg 的硬件 AVPixelFormat 变体)。硬件帧域命名的是持有数据的设备,节点对全部硬件域统一按"硬件帧"处理。

#### Scenario: 硬件域参与格式协商
- **WHEN** 端口 caps 的 pixel_formats 包含硬件域枚举值
- **THEN** 该端口可接受/生产对应设备的硬件帧,与软格式在同一维度参与兼容性判断

### Requirement: MediaFrame 硬件感知访问
MediaFrame SHALL 提供 `IsHardware()`(帧数据在 GPU 内存时为真)与 `HwSwFormat()`(硬件帧底下的软件格式,非硬件帧返回 -1)。

系统 SHALL 提供 `TransferToSoftware(const MediaFrame&)`,将硬件帧显式下载为系统内存帧(av_hwframe_transfer_data),失败返回无效帧。域转换 SHALL 只发生在显式调用点(特效节点边界、渲染器回退路径),不存在隐式转换。

#### Scenario: 硬件帧识别
- **WHEN** 解码器输出 D3D11VA 帧
- **THEN** 对应 MediaFrame 的 IsHardware() 为真,HwSwFormat() 为 NV12/P010 等实际布局

#### Scenario: 下载失败返回无效帧
- **WHEN** av_hwframe_transfer_data 失败
- **THEN** TransferToSoftware 返回 IsValid() 为假的 MediaFrame,调用方保持原帧不变
