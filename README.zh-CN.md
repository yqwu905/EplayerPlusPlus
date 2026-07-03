# ImageCompare

[English](README.md) | 简体中文

跨平台桌面工具，**并排浏览、逐像素对比多个图片文件夹**。基于 Qt 6 与 C++17。

[![Build & Release](https://github.com/yqwu905/EplayerPlusPlus/actions/workflows/release.yml/badge.svg)](https://github.com/yqwu905/EplayerPlusPlus/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/yqwu905/EplayerPlusPlus)](https://github.com/yqwu905/EplayerPlusPlus/releases/latest)

## 功能

- **任意多个文件夹并排浏览** —— 每个文件夹显示为一列可上下滚动的缩略图，跨列对齐方便对比；超过可视宽度后横向滚动，不再限制为 6 个。
- **三种选图方式**：
  - **单击** —— 仅选中当前列的一张图。
  - **Ctrl + 单击** —— 选中当前图，以及其他列中相同**索引**位置的图。
  - **Alt + 单击** —— 选中当前图，以及其他列中相同**文件名**的图。
- **逐像素容差差异图** —— 在任意两张选中图之间生成可视化差异：差值超过阈值的像素涂红、低于阈值涂蓝、完全相同则灰度化。阈值通过工具栏滑动条实时调节。
- **A / B / C / D / E / F 标记** —— 标记数据持久化在源文件夹**之外**，因此只读或共享文件夹也能正常打标；后台异步写入"日志 + 快照"，崩溃也不丢标记。
- **文件夹列表持久化** —— 关闭后再启动自动恢复，基于 `QSettings`。
- **Fluent 2 风格** —— 自定义样式表全局应用，三平台外观一致。

## 下载

Windows / macOS / Linux 三平台的安装包与便携包发布在 [Releases 页面](https://github.com/yqwu905/EplayerPlusPlus/releases/latest)：

| 平台 | 文件 |
|---|---|
| Windows 10 / 11 (x64) | `ImageCompare-windows-x64-installer.exe`（Inno Setup 安装版）或 `ImageCompare-windows-x64.zip`（绿色版） |
| macOS 12+ (x64) | `ImageCompare-macos-x64.dmg` |
| Linux (x64) | `ImageCompare-linux-x64.AppImage` |

## 从源码构建

依赖：**Qt 6.7+**（含 `qtimageformats` 模块）、**CMake 3.20+**、C++17 编译器（MSVC 2019+ / AppleClang 14+ / GCC 11+ / Clang 14+）。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

运行：

```bash
./build/src/ImageCompare              # Linux
open ./build/src/ImageCompare.app     # macOS
./build/src/Release/ImageCompare.exe  # Windows (MSVC)
```

运行单元测试（CTest，headless 通过 `QT_QPA_PLATFORM=offscreen`）：

```bash
ctest --test-dir build --output-on-failure --timeout 120
```

## 架构

代码分层 `utils → services/models → widgets → app`。四个跨组件服务（`SettingsManager`、`CompareSession`、`ImageLoader`、`ImageMarkManager`）由 `MainWindow` 持有，通过构造函数/setter 注入到各面板——没有全局变量，没有单例。详细架构见面向贡献者的 [CLAUDE.md](CLAUDE.md)。

UI 自左向右三栏：`FolderPanel`（文件夹树） → `BrowsePanel`（每个对比文件夹一列缩略图，必要时横向滚动） → `ComparePanel`（选中图片的网格视图 + 用于交换/容差对比的箭头覆盖层）。

## 贡献

欢迎在 [GitHub](https://github.com/yqwu905/EplayerPlusPlus) 提交 issue 或 PR。新增组件需配套提交 `tests/tst_*.cpp` 单元测试，开发工作流详见 [CLAUDE.md](CLAUDE.md)。

## License

本项目采用 **MIT License**，全文见 [LICENSE](LICENSE)。
