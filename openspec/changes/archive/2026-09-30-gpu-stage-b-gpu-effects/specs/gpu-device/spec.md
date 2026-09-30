## MODIFIED Requirements

### Requirement: GpuDevice 接口与职责边界
系统 SHALL 提供 `mvp::gpu::GpuDevice` 抽象接口,职责限于设备生命周期、能力查询,以及创建后端专属对象的工厂:

- `PixelFormat Domain()`:该设备产生/消费的硬件帧域
- `AVBufferRef* DeviceRef()`:FFmpeg 设备上下文引用,调用方可经 `av_buffer_ref` 共享
- `bool SupportsDecoder(const AVCodec*)`:该编码器是否声明了此设备上的硬件解码配置
- `void RefineDecoderFrames(AVHWFramesContext*)`:解码帧池初始化前的后端细化钩子
- `void LockContext()` / `void UnlockContext()`:设备命令上下文锁(见"共享设备必须线程安全")
- `std::unique_ptr<VideoPass> CreateVideoPass(VideoKernel)`:GPU 视频处理 pass 工厂,设备不支持时返回 nullptr
- `std::unique_ptr<FramePresenter> CreatePresenter()`:呈现适配器工厂

接口 SHALL NOT 包含解码/编码/特效的业务逻辑;工厂创建的对象各自实现其逻辑,get_format 回调、帧池参数等使用方关注点 SHALL 由使用方自己持有。gpu 层 SHALL 提供 RAII 的 `ScopedContextLock`(空设备时为空操作)。

#### Scenario: 节点只依赖接口不依赖平台
- **WHEN** 任一节点使用图级 GPU 设备或其创建的 VideoPass
- **THEN** 节点代码只引用 `mvp::gpu` 接口类型,不出现任何平台类型(ID3D11*/VAAPI/…)与着色器代码

#### Scenario: 新平台后端以同一接口接入
- **WHEN** 需要支持新平台(如 Linux VAAPI)
- **THEN** 新增一个 GpuDevice 实现类(及其 VideoPass/FramePresenter 实现)并在工厂注册,graph 与节点代码零改动

### Requirement: 设备从渲染器后端提取并共享
渲染器 SHALL 通过 `GpuDevice::WrapExternal(native_device)` 包装其后端原生设备(如 SDL3 D3D11 后端的 ID3D11Device),使解码、特效与呈现共享同一设备。`WrapExternal` SHALL 返回 `std::shared_ptr<GpuDevice>`。

`WrapExternal` SHALL 返回 nullptr(不抛异常)当:传入空指针、设备不支持多线程保护、设备上下文分配失败、包装初始化失败。调用方 SHALL 在返回 nullptr 时继续以软件路径构图。

#### Scenario: 成功包装
- **WHEN** 渲染器后端为 D3D11 且 `WrapExternal` 收到其设备指针
- **THEN** 返回有效 GpuDevice,`Domain()` 为 kD3D11,`DeviceRef()` 非空

#### Scenario: 包装失败不阻塞构图
- **WHEN** `WrapExternal` 因平台无后端返回 nullptr
- **THEN** 管线继续以软件路径构建,日志记录原因

### Requirement: 共享设备必须线程安全
被包装的设备会被多个线程使用:解码线程提交解码与 GPU 特效,渲染线程提交呈现拷贝与绘制,硬件帧按引用计数在任意线程被释放。`WrapExternal` SHALL:

1. 对被包装设备开启运行时多线程保护(D3D11:`ID3D10Multithread::SetMultithreadProtected(TRUE)`);设备以单线程模式创建、无法开启保护时 SHALL 返回 nullptr。
2. 在 FFmpeg 设备上下文初始化之前,把一把**递归**锁交给 FFmpeg(D3D11:`AVD3D11VADeviceContext.lock/unlock/lock_ctx`),使 FFmpeg 的解码提交与下载在内部使用这把锁。该锁的生命期 SHALL 绑定 FFmpeg 设备上下文(在其最后一次释放时销毁),而非 GpuDevice 包装对象。

应用侧所有使用设备命令上下文的多调用序列(GPU pass、呈现拷贝、渲染器绘制)SHALL 经 `LockContext/UnlockContext` 使用同一把锁,持锁范围 SHALL 限于单次 GPU 操作。系统 SHALL NOT 存在第二把保护同一命令上下文的锁。

#### Scenario: 开启多线程保护
- **WHEN** 渲染器以线程安全模式创建了 D3D11 设备
- **THEN** `WrapExternal` 开启多线程保护并记录原状态,返回有效 GpuDevice

#### Scenario: 单线程设备被拒绝
- **WHEN** 设备以 `D3D11_CREATE_DEVICE_SINGLETHREADED` 创建
- **THEN** `WrapExternal` 返回 nullptr 并告警,管线以软件路径构建,不会进入跨线程死锁

#### Scenario: FFmpeg 与应用共用一把锁
- **WHEN** 解码线程的 FFmpeg 正在提交解码,渲染线程同时调用 `LockContext`
- **THEN** 渲染线程等待同一把锁;同一线程在持锁时再次加锁(如渲染器锁内调用 av_hwframe_transfer_data)不死锁

### Requirement: 设备析构顺序契约
GpuDevice SHALL 由渲染器与 graph 以 `shared_ptr` 共享持有,最后一个持有者释放时销毁;销毁时 SHALL 在锁内刷新命令上下文后释放 FFmpeg 设备上下文引用。帧、帧池与解码器经 `AVBufferRef` 持有 FFmpeg 设备上下文,可晚于 GpuDevice 包装对象释放,故设备锁 SHALL 随 FFmpeg 设备上下文释放(见"共享设备必须线程安全"),任何销毁顺序下都不出现悬空锁。编排器 SHALL 仍先销毁 graph 再关闭渲染器,使节点与帧在 SDL 渲染器销毁前释放。

#### Scenario: 关闭顺序
- **WHEN** MediaPlayer 关闭
- **THEN** graph 先于渲染器销毁;渲染器释放其持有的 GpuDevice 后才销毁 SDL 渲染器

#### Scenario: 帧晚于包装对象释放
- **WHEN** 某硬件帧在 GpuDevice 包装对象销毁后才被释放
- **THEN** 帧仍持有 FFmpeg 设备上下文引用,锁与设备有效,释放不崩溃

## ADDED Requirements

### Requirement: 解码帧池细化
`RefineDecoderFrames(AVHWFramesContext*)` SHALL 在解码帧池初始化前由解码器调用,由后端按设备能力调整帧池参数。D3D11 后端 SHALL 在设备支持对该 sw_format 做着色器采样时为 `BindFlags` 加上 `D3D11_BIND_SHADER_RESOURCE`,使 GPU pass 能直接读取解码数组纹理的切片;不支持时 SHALL 不修改。

#### Scenario: 解码纹理可被着色器读取
- **WHEN** 设备支持 NV12 着色器采样且解码器在 get_format 中细化帧池
- **THEN** 解码帧池纹理同时带 `D3D11_BIND_DECODER` 与 `D3D11_BIND_SHADER_RESOURCE`

#### Scenario: 不支持时保持原样
- **WHEN** 设备不支持对该格式采样
- **THEN** BindFlags 不变,GPU pass 读取前先把切片拷贝到可采样的暂存纹理

### Requirement: GPU 视频处理 pass
gpu 层 SHALL 定义 `VideoKernel` 枚举(`kColorAdjust`、`kAffineRemap`)、每个内核的 uniform 结构体(布局与着色器常量缓冲一致,16 字节对齐),以及接口 `VideoPass::Run(const MediaFrame& src, const void* uniforms, size_t size)`:对硬件帧的每个平面执行一次内核,返回新的硬件帧;失败返回无效帧,uniform 大小与内核不符时 SHALL 记录错误并返回无效帧。

D3D11 实现 SHALL:
- 以构建期编译的 HLSL 着色器执行,NV12 与 P010 的亮度平面与色度平面分别绘制
- 输出帧 SHALL 取自 pass 自有的 FFmpeg 帧池(独立纹理、`RENDER_TARGET | SHADER_RESOURCE`、尺寸对齐到 2),纹理生命期随帧引用计数
- 在设备锁内执行,锁外不触碰命令上下文
- 设备不支持 NV12 渲染目标时,`CreateVideoPass` 返回 nullptr

`kColorAdjust` SHALL 与 CPU 公式一致:`Y' = (Y - m)·contrast + m + brightness`,`C' = (C - m)·saturation + m`,m 为中性灰(8 位 128/255)。`kAffineRemap` SHALL 与 CPU 仿射逆映射一致(每个平面在自身像素空间计算),越界填充 Y=0、UV=中性灰。

#### Scenario: 硬件帧在 GPU 上调色
- **WHEN** 对 D3D11 NV12 帧运行 kColorAdjust(saturation=0)
- **THEN** 返回新的 D3D11 帧,尺寸与输入可见尺寸相同,色度平面全部为中性灰,输入帧不被修改

#### Scenario: uniform 大小不符
- **WHEN** 以错误大小的 uniform 调用 Run
- **THEN** 记录错误日志,返回无效帧,不提交任何 GPU 命令

#### Scenario: 输出纹理随帧释放
- **WHEN** pass 输出帧的最后一个引用被释放
- **THEN** 纹理回到 pass 的帧池供复用,不存在按固定环形复用的纹理

### Requirement: 共享立即上下文的状态隔离
GPU pass 与其他应用侧 GPU 代码 SHALL 在执行前保存其将修改的命令上下文管线状态,执行后恢复,使与之共享立即上下文的呈现后端(SDL3 只在创建设备时设置一次输入布局、顶点着色器等状态)不受影响。

#### Scenario: GPU 特效后 SDL 正常呈现
- **WHEN** 解码线程执行 GPU pass 后,渲染线程用 SDL 呈现下一帧
- **THEN** SDL 的输入布局、顶点着色器、常量缓冲、渲染目标、视口等状态与 pass 执行前相同,画面正确

### Requirement: 呈现适配器
`FramePresenter::BindableTexture(const AVFrame*)` SHALL 返回呈现后端可直接绑定的纹理:帧纹理为独立纹理、带着色器资源绑定、尺寸等于对齐到 2 的可见尺寸时返回帧自身纹理(零拷贝);否则把帧的可见区域拷贝到呈现器自有纹理并返回之。布局不支持(非 NV12/P010)时返回 nullptr。调用方 SHALL 持设备锁调用,并在同一锁内完成绘制。呈现器 SHALL 由渲染器持有。

#### Scenario: GPU 特效输出零拷贝呈现
- **WHEN** 帧来自 VideoPass 输出(独立纹理)
- **THEN** 返回帧自身纹理,不发生拷贝

#### Scenario: 解码数组纹理一次拷贝呈现
- **WHEN** 帧来自解码器(数组纹理切片)
- **THEN** 拷贝可见区域到呈现器自有纹理并返回之;下一帧复用同一纹理

## REMOVED Requirements

### Requirement: 呈现纹理生成服务
**Reason**: 呈现关注点错位到设备并由解码器调用;8 张环形纹理依赖"池大小 > 在途帧数"的隐式不变量,分辨率变化时在途指针悬空。
**Migration**: 呈现由渲染器持有的 `FramePresenter` 在渲染线程完成;GPU pass 输出为 FFmpeg 帧池分配的独立纹理,可直接绑定。
