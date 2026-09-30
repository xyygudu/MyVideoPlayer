## REMOVED Requirements

### Requirement: 硬件帧携带呈现纹理
**Reason**: 呈现纹理由设备环形池持有、帧只带裸指针,生命期无法跟踪;Stage B 起硬件纹理生命期全部由 FFmpeg 帧池引用计数管理。
**Migration**: GPU pass 输出为独立纹理的 D3D11 帧,可直接绑定;解码数组帧由渲染器的 `FramePresenter` 在呈现时拷贝。MediaFrame 不再有 `HwPresentationTexture/SetHwPresentationTexture`。
