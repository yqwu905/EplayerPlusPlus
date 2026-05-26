# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Cross-platform image comparison tool (`ImageCompare`) — a Qt 6 / C++17 desktop app for browsing up to four folders side-by-side and inspecting per-pixel diffs between selected images. See [README.md](README.md) (or [README.zh-CN.md](README.zh-CN.md) for Chinese) for the user-facing overview; user-facing strings in the app are in Chinese.

## Build & test commands

The project uses CMake + Qt 6 (`Widgets`, `Gui`, `Concurrent`, `Test`). Out-of-tree build into `build/`:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Run the full test suite (registered with CTest, all use `QT_QPA_PLATFORM=offscreen`):

```bash
ctest --test-dir build --output-on-failure --timeout 120
```

Run a single test executable directly — useful when iterating on one component and you want QTest's verbose output:

```bash
QT_QPA_PLATFORM=offscreen ./build/tests/tst_CompareSession          # all cases in one binary
QT_QPA_PLATFORM=offscreen ./build/tests/tst_CompareSession testName # one case
ctest --test-dir build -R tst_CompareSession --output-on-failure    # via ctest
```

Run the GUI app:

```bash
./build/src/ImageCompare        # Linux
open ./build/src/ImageCompare.app  # macOS (if built as .app bundle)
```

There is also a non-CTest benchmark binary `bench_ThumbnailPerf` (run manually).

CI (`.github/workflows/release.yml`) builds on Windows/macOS/Linux against Qt 6.7.2 with the `qtimageformats` module, runs CTest, and packages installers (Inno Setup `.exe` / `windeployqt` zip, `macdeployqt` dmg, `linuxdeployqt` AppImage) on `v*` tags.

## Architecture

The codebase is layered (`utils → services/models → widgets → app`) and Qt-conventional (`Q_OBJECT`, signals/slots, `QAbstractItemModel`). Two key wiring facts a new contributor needs:

**Static library split for testability.** `src/CMakeLists.txt` builds everything except `main.cpp` into a static `ImageCompareLib`, which the executable and every `tst_*` target link against. When adding a new source file, add it to **both** `SOURCES` and `LIB_SOURCES` lists in `src/CMakeLists.txt`, then register any new test in `tests/CMakeLists.txt` via `add_unit_test(...)`.

**Shared service ownership lives in `MainWindow`.** `MainWindow` owns the four cross-cutting services and injects them into panels via constructors / setters — no globals, no singletons:

- `SettingsManager` — `QSettings` wrapper, persists folder list and UI state.
- `CompareSession` — holds the ≤4 folders currently being compared, emits add/remove signals that both `BrowsePanel` and `ComparePanel` listen to.
- `ImageLoader` — async thumbnail/full-image loader on `QtConcurrent` with a cache; shared between panels so cached thumbnails are reused in the compare view.
- `ImageMarkManager` — persists per-image A/B/C/D marks **outside** the image folders (so read-only folders work). It writes a journal + snapshot via background `QFuture`s; on load it replays the journal on top of the snapshot.

Three-panel split (left to right): `FolderPanel` (tree of added folders) → `BrowsePanel` (one scrollable column of thumbnails per folder in the compare session, up to 4) → `ComparePanel` (grid of selected images with arrow overlays for swap/tolerance diff). Selection logic lives in `BrowsePanel`: plain click is single-image, Ctrl+click matches by index across folders, Alt+click matches by filename.

Tolerance diff math is `ImageComparer::makeToleranceImage` (per-pixel diff → red if above threshold, blue if below, grayscale if zero). The threshold slider in the `MainWindow` command bar feeds into it.

Styling is centralized in `FluentStyle::applyGlobalStyle` (called from `main.cpp`) — a custom Fluent 2 stylesheet applied to the `QApplication`. Don't sprinkle stylesheets on individual widgets; extend `FluentStyle` instead.

## Conventions

- C++17, no extensions (`-Wall -Wextra -Wpedantic` on GCC/Clang, `/W4 /utf-8` on MSVC). Keep the build warning-clean.
- Every new component gets a `tst_*.cpp` under `tests/` and a line in `tests/CMakeLists.txt`. Widget tests run headless via `QT_QPA_PLATFORM=offscreen`.
- Cross-platform is a hard requirement — no platform-specific APIs without Qt abstractions. Path handling goes through `FileUtils` / `QDir`, persisted paths are normalized via `ImageMarkManager::normalizePath` (forward slashes, no trailing separator).
- Resources are compiled in via `resources/resources.qrc` (`qt_add_resources` in `src/CMakeLists.txt`); reference them with `:/` paths (e.g. `:/icons/app_icon.png`).
