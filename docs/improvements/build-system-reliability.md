# 构建系统可靠性待改进点

> 记录时间：2026-08-02
> 关联讨论：change link-capacity-must-be-explicit 验证过程中暴露的构建腐化

---

## 1. Ninja 的头文件依赖跟踪依赖控制台代码页，会静默失效

### 问题

`build/CMakeFiles/rules.ninja` 中：

```
msvc_deps_prefix = 注意: 包含文件:
deps = msvc
```

这个前缀是**本地化的非 ASCII 字符串**。Ninja 靠**逐字节匹配**它来从 cl.exe 的 `/showIncludes` 输出里提取头文件依赖。

而 cl.exe **即使 stdout 被重定向，仍按控制台输出代码页编码**。于是：

- `cmake --preset default` 在 CP936 的 shell 里运行 → 存下 GBK 字节的前缀
- 后续构建若在代码页不同的 shell 里运行（例如执行过 `chcp 65001` 或 `[Console]::OutputEncoding = UTF8`）→ 字节对不上 → **ninja 为该次编译的所有对象记录 0 条依赖，且不报任何错**

实测确认（touch `media_graph.h` 后只有 2 个文件重编，`media_graph.cc` 自己都没重编）：

```
media_player.cc.obj:     #deps 104
media_graph.cc.obj:      #deps 0    ← 依赖丢失
video_sink_node.cc.obj:  #deps 0    ← 依赖丢失
```

### 影响场景

- **改头文件不重编**：得到 ABI 不一致的 .obj，表现为莫名其妙的 `LNK2019` 未解析符号
- **验证结果被污染**：跑测试时用的是旧二进制。本项目已因此产生过一次假故障（转码只产出 673 字节的"回归"，实为链接失败后测了旧 DLL）
- **腐化是渐进的**：只有在受污染 shell 里编译过的那些 .obj 丢依赖，其余正常，因此现象时有时无、难以归因
- **无告警**：ninja、cl.exe、CMake 三方都不报错

### 改进建议（参考 CMake 官方做法）

**根治方案** —— 让前缀变成纯 ASCII，从此与代码页无关：

1. 通过 VS Installer 安装 **English 语言包**（安装后 `VC/Tools/MSVC/<ver>/bin/Hostx64/x64/` 下会出现 `1033` 目录，当前只有 `2052`）
2. `CMakePresets.json` 中已预置 `"VSLANG": "1033"`，装好语言包后自动生效，前缀变为 `Note: including file:`
3. 需删除 `build/CMakeFiles/<ver>-msvc6/`（缓存了检测结果）并重新 configure

在此之前的**约束性缓解**（已实施）：

- `tasks.json` / `build.bat` 统一走 `cmake --build --preset default`，让 PATH/INCLUDE/LIB/VSLANG 由 preset 提供，不依赖当前 shell
- 纪律：不要在会用于构建的 shell 里改代码页

**自检命令**（健康状态为 37 个对象中仅 `qrc_icons.cpp.obj` 为 0）：

```powershell
ninja -C build -t deps | Select-String '#deps 0,'
```

**修复命令**（在 CP936 shell 中执行）：

```powershell
cmake --build --preset default --clean-first
```
