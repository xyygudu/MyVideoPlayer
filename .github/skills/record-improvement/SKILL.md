---
name: record-improvement
description: 记录代码改进点到 docs/improvements/ 目录。当发现当前实现有可优化空间但不需要立即修改时使用。Use when the user says "记录改进点", "记一下待优化", "record improvement", "backlog this improvement".
license: MIT
metadata:
  author: project
  version: "1.0"
---

# 记录改进点

当发现当前实现存在可优化空间但暂不修改时，将改进点结构化记录到 `docs/improvements/` 目录。

---

## 触发条件

- 用户明确说"记录改进点"、"待优化"、"backlog improvement"
- 代码审查时发现非阻塞性的优化方向
- 对标业界实现后发现差异点

## 输出格式

每个改进点必须包含以下四个部分：

```markdown
## N. 标题（简短描述问题本质）

### 问题

<当前实现的具体做法是什么，哪里不够好>

### 影响场景

- **场景A**：<在什么条件下触发，带来什么具体影响>
- **场景B**：...

### 改进建议（参考 XXX）

<参考哪个主流开源项目的哪个机制，给出伪代码或设计思路>
```

## 流程

1. **确定归属文件**：根据改进点所属模块，在 `docs/improvements/` 下找到或创建对应的 `.md` 文件
   - 命名规则：`<模块或主题>.md`，使用 kebab-case，如 `av-sync-and-pipeline.md`、`decoder-performance.md`
   
2. **检查是否重复**：读取目标文件，确认该改进点尚未记录

3. **追加内容**：在文件末尾追加新的改进点，编号递增

4. **对标引用**：必须注明参考的主流项目（FFplay / MPV / VLC / GStreamer 等）和具体机制名称

5. **确认**：告知用户已记录，以及文件路径

## 注意事项

- 不要在记录时直接修改源代码
- 每个文件聚焦一个主题领域，不要把不相关的改进点混在一起
- 改进建议中的代码示例用伪代码或简化代码即可，不需要完整可编译
- 如果用户同时要求记录并实现，先记录再走正常实现流程
