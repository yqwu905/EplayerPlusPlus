# ImageCompare

[简体中文](README.zh-CN.md)

Cross-platform desktop application for browsing and **visually diffing image folders side-by-side**. Built with Qt 6 and C++17.

[![Build & Release](https://github.com/yqwu905/EplayerPlusPlus/actions/workflows/release.yml/badge.svg)](https://github.com/yqwu905/EplayerPlusPlus/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/yqwu905/EplayerPlusPlus)](https://github.com/yqwu905/EplayerPlusPlus/releases/latest)

## Features

- **Side-by-side browse of up to 4 folders** — each folder becomes a scrollable column of thumbnails, all aligned for visual comparison.
- **Three selection modes** for picking what to compare:
  - **Click** — select a single image in one column.
  - **Ctrl + click** — select the image and the same-index image in every other column.
  - **Alt + click** — select the image and the same-filename image in every other column.
- **Per-pixel tolerance diff overlay** — for any pair of selected images, replace one with a colorized diff: red where the channel delta exceeds the threshold, blue where it is below, desaturated where they match exactly. The threshold is adjustable from a slider in the toolbar.
- **A / B / C / D marks** persisted **outside** the source folders so read-only or shared folders work, with an automatic journal + snapshot so marks survive crashes.
- **Folder list persistence** — the folders you added are restored on next launch via `QSettings`.
- **Fluent 2 styling** — a custom stylesheet applied globally for a consistent look across Windows, macOS, and Linux.

## Download

Pre-built installers and portable archives for Windows, macOS, and Linux are published on the [Releases page](https://github.com/yqwu905/EplayerPlusPlus/releases/latest):

| Platform | Artifact |
|---|---|
| Windows 10 / 11 (x64) | `ImageCompare-windows-x64-installer.exe` (Inno Setup) or `ImageCompare-windows-x64.zip` (portable) |
| macOS 12+ (x64) | `ImageCompare-macos-x64.dmg` |
| Linux (x64) | `ImageCompare-linux-x64.AppImage` |

## Build from source

Requirements: **Qt 6.7+** (with the `qtimageformats` module) and **CMake 3.20+**. C++17 compiler — MSVC 2019+, AppleClang 14+, or GCC 11+ / Clang 14+.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run the app:

```bash
./build/src/ImageCompare              # Linux
open ./build/src/ImageCompare.app     # macOS .app bundle
./build/src/Release/ImageCompare.exe  # Windows MSVC
```

Run the test suite (CTest, headless via `QT_QPA_PLATFORM=offscreen`):

```bash
ctest --test-dir build --output-on-failure --timeout 120
```

## Architecture

The codebase is layered as `utils → services/models → widgets → app`. Cross-cutting services (`SettingsManager`, `CompareSession`, `ImageLoader`, `ImageMarkManager`) are owned by `MainWindow` and injected into panels — no globals, no singletons. See [CLAUDE.md](CLAUDE.md) for a fuller architectural tour intended for contributors.

The UI is three panels (left to right): `FolderPanel` (tree of added folders) → `BrowsePanel` (one scrollable column of thumbnails per folder, up to 4) → `ComparePanel` (grid of selected images with arrow overlays for swap / tolerance diff).

## Contributing

Issues and pull requests are welcome on [GitHub](https://github.com/yqwu905/EplayerPlusPlus). Every new component is expected to ship with a `tst_*.cpp` under `tests/`; see [CLAUDE.md](CLAUDE.md) for the development workflow.

## License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE) for the full text.
