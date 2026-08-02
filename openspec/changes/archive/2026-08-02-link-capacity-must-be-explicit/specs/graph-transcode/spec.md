## ADDED Requirements

### Requirement: 转码图各链路声明缓冲量
转码图的每一条连接 SHALL 显式声明容量。Decoder→Encoder 与 Encoder→Mux 两条链路 SHALL NOT 依赖任何隐式默认值。

Decoder→Encoder 的深度 SHALL 按"避免编码器饥饿"选取，而非沿用播放图的深度 —— 后者服务于 A/V 同步前瞻，转码无此需求。编码单帧的耗时远高于解码单帧，少量缓冲即足够；继续加深不带来收益，因为编码器自身持有的前瞻缓冲远大于链路缓冲。

#### Scenario: 转码不因缺失背压而耗尽内存
- **WHEN** 转码一个高分辨率长片，编码速度显著慢于解码
- **THEN** 解码线程被链路背压限速，峰值内存保持在与缓冲深度相称的量级，SHALL NOT 随片长增长

#### Scenario: 背压不降低转码吞吐
- **WHEN** 为 Decoder→Encoder 链路加上容量限制
- **THEN** 转码总耗时与输出内容不变 —— 编码器本就是瓶颈，解码器领先与否不影响完成时间
