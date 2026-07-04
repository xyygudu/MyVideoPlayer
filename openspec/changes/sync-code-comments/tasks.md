## 1. 过时引用搜索

- [x] 1.1 搜索 `grep "PlaybackGraphBuilder\|ISourceNode\|Probe\|NodeConfig"` 等残留引用，列出所有需要更新的位置

## 2. DemuxNode 注释更新

- [x] 2.1 `demux_node.h` 类注释重写：去掉 Configure/ISourceNode 引用，描述构造器注入 + InitStreamInfo 自动探测
- [x] 2.2 `demux_node.cc` InitStreamInfo 方法注释更新
- [x] 2.3 `demux_node.cc` OpenFile/FindStreams/MakeStreamFormat 注释检查更新

## 3. MediaPlayer 注释更新

- [x] 3.1 `media_player.cc` BuildGraph 的阶段注释更新（验证后发现已由 commit 同步，无需额外修改）
- [x] 3.2 `media_player.cc` streams_ 成员注释添加

## 4. 其他头文件注释检查

- [x] 4.1 `decoder_node.h` 检查类注释：移除 NodeConfig 引用，替换为 Negotiate/Prepare 职责描述
- [x] 4.2 `video_sink_node.h` 检查类注释：无过时引用
- [x] 4.3 `audio_sink_node.h` 检查类注释：无过时引用

## 5. 验证

- [x] 5.1 编译验证（0 error）
- [x] 5.2 `git diff` 确认无代码逻辑变更（仅注释变化）
