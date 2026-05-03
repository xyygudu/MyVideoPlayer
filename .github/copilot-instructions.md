# Copilot Instructions

## 技术栈

- C++17
- FFmpeg 7.1 (D:\Install\ffmpeg-7.1.1-full_build-shared)
- SDL3 (D:\Install\SDL3-3.2.16)
- Qt 6.7.3 (D:\Install\Qt\6.7.3\msvc2022_64)
- spdlog (D:\Install\spdlog)
- 编译器：MSVC (Visual Studio 2022)
- 构建系统：CMake + Ninja

## 代码风格

- 遵循 Google C++ Style Guide
- 函数体超过 50 行必须考虑拆分封装

## 架构要求

- 编写代码时必须参考业界主流播放器架构（FFplay、MPV、VLC）的做法
- 如果主流架构之间实现差异较大，必须列出各主流方案与当前项目架构的对比差异
