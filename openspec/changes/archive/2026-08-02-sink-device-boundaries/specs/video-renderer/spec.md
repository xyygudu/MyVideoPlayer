## ADDED Requirements

### Requirement: 渲染器状态仅由渲染线程修改
`VideoRenderer` 的窗口尺寸等渲染状态 SHALL 仅被持有渲染线程的节点在该线程上修改，SHALL NOT 被 UI 线程或其他控制线程直接写入。

理由：这些字段在渲染时被读取。跨线程无同步读写是数据竞争；把修改点收敛到渲染线程即可消除竞态，而无需将每个字段原子化 —— 原子化只是掩盖症状，不改变"谁有权修改"这一职责问题。

#### Scenario: 尺寸变化经由渲染线程落地
- **WHEN** 窗口被缩放
- **THEN** 新尺寸经命令传递到 VideoSinkNode，由其渲染线程调用 `Resize`，UI 线程不触碰 VideoRenderer

#### Scenario: 无需原子成员即无竞态
- **WHEN** 检查 VideoRenderer 的尺寸字段
- **THEN** 它们只有一个写者（渲染线程），普通标量类型即满足线程安全
