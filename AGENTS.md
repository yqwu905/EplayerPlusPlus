## 项目描述
1. 这个项目的目标是开发一个全平台的图像管理程序。
2. 项目使用C++和Qt开发

## 开发规范
1. 开发时必须考虑全平台兼容性
2. 每个功能必须编写单元测试
3. 开发完成后，必须运行单元测试，保证全部测试通过

## 功能描述

### 文件管理
1. 支持添加文件夹，可以展开浏览子文件夹，并将文件夹添加到对比列表中
2. 添加的文件夹会记录到本地，重启后会自动恢复文件夹列表
3. 文件夹右键支持删除，刷新和添加到对比。
4. 支持全局刷新和清空文件夹列表

### 图片浏览
添加对比的文件夹，程序会自动扫描文件夹下的所有图片，并显示在界面上。ß
最多支持添加四个文件夹进行对比。
添加对比的文件夹会显示在图片浏览界面中，显示其中所有图片的缩略图。
每个添加的文件夹会在图片浏览界面中显示为一列，支持上下滚动
支持按照多种逻辑选择对比图片，具体包括：
1. 点击图片，则选中单张图片
2. ctrl+点击图片，选中这张图片，以及其他对比文件夹中的相同顺序的图片
3. alt+点击图片，选中这张图片，以及其他对比文件夹中的相同文件名的图片

### 对比图片
被选中的图片会以网格形式展示在对比窗口中
每个图片上方会显示该图片的名称和该图片所属的文件夹名称
每个图片上会有指向其他图片的箭头，点击箭头可以触发对比。下面将点击箭头所在的图片称为图片A，箭头指向的图片称为图片B。对比方式包括：
1. 按住箭头时，图片B会被替换为图片A
2. 按下箭头后，图片B会被替换为图片A和图片B的容差图。具体来说，计算图片B和图片A的逐像素差值，如果差值大于阈值，则将该像素红色通道设置为255，否则将蓝色通道设置为255. 如果差值为0，则将该像素转为灰度。再次按下箭头后，将图片B恢复为原图

---

## 技术栈

| 类别 | 技术选型 | 说明 |
|------|----------|------|
| 语言 | C++17 | 现代 C++ 标准，兼容主流编译器 |
| GUI 框架 | Qt 6 (Widgets) | 跨平台 UI，使用 QTreeView / QListView / QGraphicsView 等组件 |
| 构建系统 | CMake 3.20+ | 跨平台构建，配合 Qt6 的 CMake 集成 |
| 单元测试 | QTest | Qt 自带测试框架，与 CTest 集成 |
| 图像处理 | QImage / QPixmap | Qt 内置图像处理，无需额外依赖 |
| 持久化存储 | QSettings | 跨平台本地配置存储（文件夹列表持久化） |
| 包管理 | vcpkg（可选） | 如需引入第三方库时使用 |

### 支持平台
- Windows 10/11 (MSVC 2019+)
- macOS 12+ (Clang 14+)
- Linux (GCC 11+ / Clang 14+)

### 关键 Qt 模块
- `Qt::Widgets` — 主要 UI 组件
- `Qt::Gui` — 图像处理（QImage, QPixmap）
- `Qt::Concurrent` — 缩略图异步加载
- `Qt::Test` — 单元测试

## 项目目录结构

```
ImageCompare/
├── CMakeLists.txt                  # 顶层 CMake
├── AGENTS.md
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                    # 程序入口
│   ├── app/
│   │   ├── MainWindow.h/cpp        # 主窗口，整合所有面板
│   │   └── Application.h/cpp       # 应用初始化、全局状态
│   ├── models/
│   │   ├── FolderModel.h/cpp       # 文件夹树模型（QAbstractItemModel）
│   │   ├── ImageListModel.h/cpp    # 图片列表模型
│   │   └── CompareSession.h/cpp    # 对比会话，管理选中的对比文件夹（最多4个）
│   ├── widgets/
│   │   ├── FolderPanel.h/cpp       # 左侧文件夹管理面板（QTreeView + 工具栏）
│   │   ├── BrowsePanel.h/cpp       # 图片浏览面板（多列缩略图网格）
│   │   ├── ComparePanel.h/cpp      # 对比面板（网格展示选中图片 + 箭头交互）
│   │   ├── ThumbnailWidget.h/cpp   # 单个缩略图组件
│   │   └── ArrowOverlay.h/cpp      # 对比箭头覆盖层
│   ├── services/
│   │   ├── ImageLoader.h/cpp       # 异步图片/缩略图加载服务
│   │   ├── ImageComparer.h/cpp     # 图片对比算法（容差图生成）
│   │   └── SettingsManager.h/cpp   # QSettings 封装，持久化管理
│   └── utils/
│       ├── ImageUtils.h/cpp        # 图像工具函数（缩放、格式转换等）
│       └── FileUtils.h/cpp         # 文件扫描、过滤工具函数
├── tests/
│   ├── CMakeLists.txt
│   ├── tst_FolderModel.cpp         # FolderModel 单元测试
│   ├── tst_ImageListModel.cpp      # ImageListModel 单元测试
│   ├── tst_CompareSession.cpp      # CompareSession 单元测试
│   ├── tst_ImageComparer.cpp       # ImageComparer 单元测试（容差图算法）
│   ├── tst_SettingsManager.cpp     # SettingsManager 单元测试
│   ├── tst_ImageUtils.cpp          # ImageUtils 单元测试
│   ├── tst_FileUtils.cpp           # FileUtils 单元测试
│   └── resources/                  # 测试用图片资源
│       ├── test_image_a.png
│       └── test_image_b.png
└── resources/
    ├── resources.qrc               # Qt 资源文件
    └── icons/                      # 应用图标
```

## 功能开发 TODO

### 阶段一：项目基础设施

- [x] **T-001** 创建顶层 CMakeLists.txt，配置 Qt6 查找、C++17 标准、编译选项
- [x] **T-002** 创建 src/CMakeLists.txt，定义可执行目标和源文件
- [x] **T-003** 创建 tests/CMakeLists.txt，配置 QTest + CTest 集成
- [x] **T-004** 实现 main.cpp 程序入口，创建 QApplication 和 MainWindow
- [x] **T-005** 实现 MainWindow 基本框架（QMainWindow + QSplitter 布局: 左侧文件夹面板 | 中间图片浏览面板 | 右侧对比面板）

---

### 阶段二：工具层与服务层

- [x] **T-006** 实现 FileUtils — 递归扫描文件夹中的图片文件（支持 png/jpg/bmp/tiff 等格式过滤）
- [x] **T-007** 编写 tst_FileUtils 单元测试
- [x] **T-008** 实现 ImageUtils — 缩略图生成、图像缩放、格式判断
- [x] **T-009** 编写 tst_ImageUtils 单元测试
- [x] **T-010** 实现 SettingsManager — 基于 QSettings 的文件夹列表持久化（保存/加载/清空）
- [x] **T-011** 编写 tst_SettingsManager 单元测试
- [x] **T-012** 实现 ImageLoader — 基于 QtConcurrent 的异步缩略图加载服务，支持缓存
- [x] **T-013** 实现 ImageComparer — 容差图生成算法（逐像素差值计算、阈值判断、红/蓝/灰度着色）
- [x] **T-014** 编写 tst_ImageComparer 单元测试（验证容差图输出的像素正确性）

---

### 阶段三：文件管理模块

- [x] **T-015** 实现 FolderModel — 继承 QAbstractItemModel，支持树形展示文件夹及子文件夹
- [x] **T-016** 编写 tst_FolderModel 单元测试
- [x] **T-017** 实现 FolderPanel — QTreeView + 工具栏（添加文件夹按钮、全局刷新按钮、清空按钮）
- [x] **T-018** 实现 FolderPanel 中文件夹的添加功能（弹出 QFileDialog 选择文件夹）
- [x] **T-019** 实现文件夹右键上下文菜单（删除、刷新、添加到对比）
- [x] **T-020** 集成 SettingsManager，实现文件夹列表的持久化和启动恢复
- [x] **T-021** 实现全局刷新（重新扫描所有已添加文件夹）和清空文件夹列表功能

---

### 阶段四：图片浏览模块

- [x] **T-022** 实现 CompareSession — 管理当前对比的文件夹列表（最多4个），提供增删接口和信号通知
- [x] **T-023** 编写 tst_CompareSession 单元测试
- [x] **T-024** 实现 ImageListModel — 管理单个文件夹内的图片列表，提供缩略图数据
- [x] **T-025** 编写 tst_ImageListModel 单元测试
- [x] **T-026** 实现 ThumbnailWidget — 单个缩略图显示组件（显示缩略图 + 文件名）
- [x] **T-027** 实现 BrowsePanel — 多列布局，每个对比文件夹显示为一列，列内垂直滚动显示缩略图
- [x] **T-028** 实现图片选中逻辑 — 单击选中单张图片（仅影响当前列，不影响其他文件夹）
- [x] **T-029** 实现 Ctrl+点击选中逻辑 — 选中当前图片及其他文件夹中相同文件名的图片
- [x] **T-030** 实现 Alt+点击选中逻辑 — 选中当前图片及其他文件夹中相同顺序（索引）的图片
- [x] **T-031** 集成 ImageLoader 异步加载缩略图，支持懒加载和缓存

---

### 阶段五：对比图片模块

- [x] **T-032** 实现 ComparePanel 基本布局 — 网格展示选中的图片，每张图片上方显示图片名称和所属文件夹名称
- [x] **T-033** 实现 ArrowOverlay — 在每张图片上绘制指向其他图片的方向箭头
- [x] **T-034** 实现箭头按住交互 — 按住箭头时，目标图片（图片B）临时替换显示为源图片（图片A），松开后恢复
- [x] **T-035** 实现箭头点击交互 — 点击箭头后，目标图片替换为容差图（调用 ImageComparer），再次点击恢复原图
- [x] **T-036** 实现容差图阈值设置 UI（工具栏或设置面板中提供阈值滑动条）

---

### 阶段六：集成与优化

- [x] **T-037** 集成所有模块到 MainWindow，确保信号/槽连接正确
- [x] **T-038** 优化大量图片场景下的性能（虚拟滚动、缩略图缓存策略）
- [x] **T-039** 全平台编译测试（Windows / macOS / Linux）
- [x] **T-040** 运行全部单元测试，确保通过
- [x] **T-041** 添加应用图标和基本的菜单栏（文件/帮助）