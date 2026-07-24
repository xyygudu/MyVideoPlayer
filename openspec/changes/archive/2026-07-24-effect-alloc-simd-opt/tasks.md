## 1. MakeWritable 零拷贝修复

- [x] 1.1 `media_frame.h` 将 `MakeWritable()` 拆为 `&&`/`const &` 两个 ref-qualified 重载声明
- [x] 1.2 `media_frame.cc` 实现 `&&` 重载：先 `av_frame_is_writable(frame_.get())`，为真则 `return std::move(*this)`，为假则复用现有深拷贝逻辑；`const &` 重载保留现有实现不变
- [x] 1.3 `color_effect_node.cc` 调用点改为 `std::move(input.AsFrame()).MakeWritable()`
- [x] 1.4 冒烟验证：非默认颜色参数下连续播放，确认无崩溃、画面正确——已通过实际播放验证（用户本机播放 1080p 素材，`ColorEffectNode`/`TransformEffectNode` 临时 perf 日志确认无崩溃、耗时符合预期）

## 2. MediaFramePool

- [x] 2.1 `media_frame.h` 新增 `MediaFramePool` 类声明（move-only，持有 `AVBufferPool*`）
- [x] 2.2 `media_frame.cc` 实现 `MediaFramePool::Acquire`：尺寸/格式命中当前池则复用，否则 `av_buffer_pool_uninit` 旧池 + 按新规格 `av_buffer_pool_init` 重建；使用 `av_image_get_buffer_size`/`av_image_fill_arrays` 装配单块连续缓冲区（对齐 32 字节），与 `AVFrame::buf[0]` 绑定所有权
- [x] 2.3 `media_frame.cc` 实现析构/移动特殊成员函数（`~MediaFramePool` 调用 `av_buffer_pool_uninit`）
- [x] 2.4 `transform_effect_node.h` 新增私有成员 `MediaFramePool output_pool_;`
- [x] 2.5 `transform_effect_node.cc` 的 `Process()` 调用点，将 `MediaFrame::CreateSameFormat(src_mf, src_mf.pts())` 替换为 `output_pool_.Acquire(src_mf.width(), src_mf.height(), src_mf.format(), src_mf.pts())`
- [x] 2.6 冒烟验证：连续 seek/切换分辨率场景下画面正确、无崩溃、无内存泄漏——已通过实际播放验证（`pool_us` 实测稳定在个位数微秒，确认 `MediaFramePool` 复用生效，未观察到崩溃或异常）

## 3. SIMD 基础设施（已实现并实测，第 7 节已完整撤销）

- [x] 3.1 新增 `src/media/simd/cpu_features.h`：声明 `bool HasAvx2()` / `bool HasSsse3()`（LUT 的 pshufb 向量化需要 SSSE3，比最初设想的"纯 SSE2"多了一级特性检测）
- [x] 3.2 新增 `src/media/simd/cpu_features.cc`：`__cpuid`/`__cpuidex` 检测 AVX2（含 OSXSAVE/XGETBV 检查）和 SSSE3，`static` 局部变量缓存结果
- [x] 3.3 CMakeLists.txt 新增 `pixel_ops_scalar.cc`/`pixel_ops_sse2.cc`/`pixel_ops_ssse3.cc`/`pixel_ops_avx2.cc` 源文件（新增 `pixel_ops_internal.h` 私有头共享标量原语），并对 `pixel_ops_avx2.cc` 单独设置 `/arch:AVX2` 编译选项（`set_source_files_properties`）

## 4. Color 路径 SIMD（nibble-split LUT，已实现并实测，第 7 节已完整撤销）

- [x] 4.1 `pixel_ops_ssse3.cc` 实现 `ApplyLut` 的 SSSE3 nibble-split `pshufb` 版本（16 组 16 项查表 + 比较混合）——命名调整为 `pixel_ops_ssse3.cc`（而非最初设想的 `pixel_ops_sse2.cc`），因为 `pshufb` 需要 SSSE3，纯 SSE2 无法表达
- [x] 4.2 `pixel_ops_avx2.cc` 实现 AVX2 版本（`_mm256_shuffle_epi8`，LUT 切片通过 `_mm256_broadcastsi128_si256` 复制到两个 128-bit 通道）
- [x] 4.3 `pixel_ops.cc` 的 `ApplyLut` 改为分发入口：函数级 `static` 缓存 `HasAvx2()`/`HasSsse3()` 结果，选择 avx2/ssse3/标量实现
- [x] 4.4 临时验证程序（`build/simd_check/main.cc`，构建后即删除）：对比标量与 SIMD 路径在随机 LUT + 随机像素输入下的输出——实测完全一致（max_diff=0），验证通过后已清理该临时文件

## 5. Transform 双线性路径 SIMD（已实现并实测，第 7 节已完整撤销）

- [x] 5.1 `pixel_ops_sse2.cc`（4 像素/批，纯 SSE2）/`pixel_ops_avx2.cc`（8 像素/批）实现向量化的仿射逆映射坐标计算（利用同一行内 `sx`/`sy` 关于 `dst_x` 为线性函数的精确恒等式），texel 实际内存读取与双线性混合复用未改动的标量 `SampleBilinear`
- [x] 5.2 `pixel_ops.cc` 的 `RemapPlane`/`RemapInterleavedPlane` 改为分发入口（`HasAvx2() ? Avx2 : Sse2`，SSE2 是 x64 ABI 保证的基线，无需运行时检测）
- [x] 5.3 临时验证程序：对比标量与 SIMD 路径在旋转（0°/37.5°/90°/15°/200°）+ 缩放 + 平移组合参数下的输出——实测完全一致（max_diff=0）

## 6. 构建与回归验证（内存分配修复部分）

- [x] 6.1 `cmake --build build`，零错误零警告
- [x] 6.2 冒烟测试：1080p 与 4K 素材，播放 Transform+Color 组合效果（非默认参数），观察卡顿是否改善——已在用户本机用 1080p 旋转素材验证；结论：内存分配问题已解决（`pool_us` 微秒级），剩余耗时符合 Debug 编译配置下标量像素运算的预期基准（与独立基准程序测出的数字吻合），4K/Release 下的实际流畅度验证留待用户后续在可用的 Release 构建下确认（见 `docs/improvements/pixel-ops-simd.md`）
- [x] 6.3 更新 `docs/improvements/pipeline-interface-design.md`（新增第 4 节）记录本次排查中发现但不在本次范围内修复的 `MediaBuffer` 构造函数成员初始化顺序问题（`media_type_(frame.type())` 读到 move 后的值）

## 7. SIMD 撤销（实测收益不成立后完整回退）

- [x] 7.1 加了 per-frame 耗时调试日志（`TransformEffectNode`/`ColorEffectNode`，临时，标注 `TEMP DEBUG`）后，用户实测 1080p 旋转场景 `bilinear` 路径单帧 `pixels_us=428587`（428ms），怀疑 SIMD 未生效或有回归
- [x] 7.2 用临时独立基准程序（构建/运行后已清理）在项目实际 Debug 编译选项（`/Od /RTC1 /MDd`）和 Release 风格选项（`/O2 /MD`）下分别测 scalar/SSE2/SSSE3/AVX2：Debug 下 AVX2 比标量慢 4 倍（复现了 428ms），Release 下 AVX2 相对标量只快 ~8%（RemapPlane）甚至更慜（ApplyLut）
- [x] 7.3 判定 SIMD 收益不成立，`pixel_ops.cc` 的 `ApplyLut`/`RemapPlane`/`RemapInterleavedPlane` 回退为纯标量实现（保留 `RowOffsets` 按行预计算这个纯代数优化，非 SIMD）
- [x] 7.4 删除 `pixel_ops_scalar.cc`/`pixel_ops_sse2.cc`/`pixel_ops_ssse3.cc`/`pixel_ops_avx2.cc`/`pixel_ops_internal.h`/`src/media/simd/`，CMakeLists.txt 移除 `/arch:AVX2` 相关设置
- [x] 7.5 删除本变更下的 `specs/pixel-ops/spec.md`（SIMD 行为从未真正对外生效，不再作为本变更修改的能力）
- [x] 7.6 `cmake --build build` 验证回退后零错误
- [x] 7.7 记录本次 SIMD 尝试与放弃的完整数据到 `docs/improvements/pixel-ops-simd.md`，供未来重新评估参考

