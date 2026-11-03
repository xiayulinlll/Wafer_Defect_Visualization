# 轻量化3D晶圆缺陷可视化与检测数据叠加系统

> Demo项目：半导体晶圆检测设备上位机3D可视化模块，Qt6 + OpenGL3.3 + GLSL，C++17

## 项目简介

本项目模拟半导体晶圆量测设备的可视化子系统，用于展示晶圆三维模型与缺陷检测数据。
系统支持交互式3D视口、缺陷热力云图渲染、缺陷点拾取查看详情、局部区域截图导出。
采用多线程架构分离数据加载、UI交互与GPU渲染，解决工业上位机大数据场景UI卡顿问题。
项目不依赖VTK/Open3D等重型图形库，原生使用现代OpenGL实现渲染逻辑。

## 技术栈

C++17 | Qt6 | OpenGL 3.3 Core | GLSL 330 | QOpenGLWidget | FBO | VAO/VBO | 多线程 | 射线拾取

## 架构概述

项目分为四层：数据层、业务逻辑层、图形渲染层、UI交互层。
采用双缓冲数据结构实现跨线程安全数据访问，所有OpenGL资源在渲染线程管理。

```
+-------------------------------------------------------------+
|  UI交互层 (Qt主线程)                                          |
|  MainWindow / WaferViewport / DefectInfoDialog / LogPanel    |
+-------------------------------------------------------------+
              |  信号槽 (QueuedConnection, 跨线程安全)
+-------------------------------------------------------------+
|  业务逻辑层                                                   |
|  SceneStateManager / GeometryBuilder / RayPicker / PerfStats |
+-------------------------------------------------------------+
              |  CPU侧只读快照 (双缓冲原子切换)
+-------------------------------------------------------------+
|  数据层 (后台子线程)                                          |
|  CsvParser / CsvDatasetExporter / DataBufferManager /        |
|  DefectFilter / DefectAnalyzer / Neighborhood                 |
+-------------------------------------------------------------+
              |  顶点/纹理数据
+-------------------------------------------------------------+
|  图形渲染层 (OpenGL渲染线程, 上下文内)                         |
|  ShaderManager / SceneRenderer / HeatmapRenderer /           |
|  FboRenderer / GpuResourceManager                            |
+-------------------------------------------------------------+
```

## 目录结构

```
Wafer_Defect_Visualization/
├── CMakeLists.txt                      # ★ 主构建（wafer_core 静态库 + 可执行文件 + 测试 + 安装）
├── CMakePresets.json                   # CMake 预设（见下）
├── run.bat / package.bat               # 运行 / windeployqt 打包
├── README.md                           # ★ 含「打开与运行完整流程」与「运行阶段踩坑记录」
├── .gitignore / .clang-format
├── .github/workflows/ci.yml            # CI：Windows(MSVC+Ninja) / Linux(GCC) / clang-format
├── Wafer_Defect_Visualization.pro      # ⚠️ 已弃用（保留仅供历史参考，请改用 CMake）
├── docs/
│   └── BUILD_TROUBLESHOOTING.md        # 编译问题排查记录（工具链/moc/链接）
├── tools/
│   ├── env.bat                         # qmake 版构建环境（正确工具链 + PATH）
│   ├── build_cmake.bat                 # CMake 版一键构建 + ctest
│   ├── smoke_test.bat                  # CMake 版冒烟测试（tasklist 健康检查）
│   └── generate_sample_data.cpp        # 示例数据生成器
├── translations/
│   └── wafer_zh_CN.ts                  # i18n 翻译骨架（lupdate/release 由 CMake 目标驱动）
├── tests/                              # ★ 单元测试（Qt Test + CTest）
│   ├── CMakeLists.txt
│   ├── tst_csvparser.cpp               # CSV 解析（含协作式取消）
│   ├── tst_defectfilter.cpp            # 缺陷筛选
│   ├── tst_mathutils.cpp               # 射线/AABB/球体相交、屏幕->世界射线
│   ├── tst_geometry.cpp                # 缺陷->世界映射、标记点半径、晶圆几何绕序校验
│   ├── tst_raypicker.cpp               # 射线拾取、包围盒、空间索引等价性
│   ├── tst_databuffer.cpp              # 双缓冲发布/快照
│   └── tst_datasetexport.cpp           # 筛选导出/回读、统计、邻域
├── data/
│   └── wafer_defects.csv               # 模拟检测数据集
├── shaders/
│   ├── wafer.vert / wafer.frag         # 晶圆主体 + 热力图着色器
│   ├── marker.vert / marker.frag       # 缺陷标记点着色器
│   ├── grid.vert / grid.frag           # 坐标网格/坐标轴
│   └── fbo_blit.vert / fbo_blit.frag   # FBO离屏渲染全屏拷贝着色器
├── resources.qrc                       # 着色器资源打包（含 /i18n 前缀）
└── src/
    ├── main.cpp                        # 入口（日志 + i18n + OpenGL 3.3 Core 上下文）
    ├── core/           # 通用类型、常量、数学库、日志
    │   ├── DefectTypes.h
    │   ├── Constants.h                 # ★ 全部魔法数字 + markerRadiusFor() 单一实现
    │   ├── MathUtils.h
    │   └── Logger.h/.cpp               # ★ 落盘滚动日志 + QLoggingCategory + UI 订阅
    ├── data/           # 数据层
    │   ├── WaferDefect.h
    │   ├── CsvDatasetParser.h/.cpp     # ★ 支持 CancelCallback 协作式取消 + 单遍扫描
    │   ├── CsvDatasetExporter.h/.cpp   # ★ 筛选结果导出（表头复用解析器，可回读）
    │   ├── DataBufferManager.h/.cpp
    │   ├── DefectFilter.h              # ★ 等级/类型/层/面积/高度 多条件
    │   ├── DefectAnalyzer.h/.cpp       # ★ 类型×等级交叉表、半径分区、良率估算
    │   ├── Neighborhood.h/.cpp         # ★ 邻域/簇统计（详情弹窗）
    │   └── DatasetLoadWorker.h/.cpp    # ★ std::atomic 取消标志（替代 QThread::terminate）
    ├── business/       # 业务逻辑层
    │   ├── SceneStateManager.h/.cpp
    │   ├── GeometryBuilder.h/.cpp
    │   ├── RayPicker.h/.cpp
    │   └── PerformanceStats.h/.cpp
    ├── render/         # 图形渲染层
    │   ├── ShaderManager.h/.cpp
    │   ├── GpuResourceManager.h/.cpp
    │   ├── HeatmapRenderer.h/.cpp
    │   ├── FboRenderer.h/.cpp
    │   └── SceneRenderer.h/.cpp
    └── ui/             # UI交互层
        ├── MainWindow.h/.cpp
        ├── AppSettings.h/.cpp          # ★ QSettings 持久化（相机/筛选/开关/数据集）
        ├── DatasetController.h/.cpp    # 加载线程托管（协作式取消）
        ├── CaptureController.h/.cpp    # 框选截图导出
        ├── StatisticsDialog.h/.cpp     # ★ 统计视图（交叉表 / 分区 / 良率）
        ├── WaferViewport.h/.cpp
        ├── DefectInfoDialog.h/.cpp     # ★ 含邻域信息
        ├── PerformancePanel.h/.cpp
        ├── OverlayPanel.h/.cpp         # 悬浮面板基类
        ├── SeverityOverlayPanel.h/.cpp # 左上角：缺陷等级分布
        └── LogOverlayPanel.h/.cpp      # 右上角：运行日志（订阅 core::Logger）
```

## 核心功能

- 3D晶圆交互式视口，旋转、缩放、平移
- 后台异步加载缺陷数据集，UI不阻塞
- GPU着色器实现缺陷热力云图伪彩色渲染
- 三维缺陷标记点批量渲染，鼠标拾取查看缺陷信息（含邻域/簇统计）
- 缺陷数据筛选：等级区间、多选类型、多选掩膜层、**面积/高度区间**、按层高度错开
- 按缺陷 ID 定位高亮（ID 被筛选隐藏时自动放宽条件）
- **筛选结果导出 CSV**（含统计注释，可被本程序直接重新加载）
- **统计视图**：类型×等级交叉表、半径分区密度、泊松良率估算
- **界面状态持久化**：数据集路径、相机、筛选条件、渲染开关在下次启动自动恢复
- FBO离屏渲染，局部区域截图导出（带标注水印）
- 渲染性能监控面板，帧率（瞬时/平均）、图元统计、GPU耗时、GPU 资源累计创建

## 操作说明

| 操作 | 功能 |
| --- | --- |
| 鼠标左键拖拽 | 旋转晶圆模型 |
| 鼠标右键拖拽 / 中键拖拽 | 场景平移 |
| 鼠标滚轮 | 缩放视口 |
| 鼠标左键单击（无拖拽） | 射线拾取缺陷点，弹出详情 |
| 鼠标左键框选（Ctrl+拖拽） | 框选局部区域，松开后离屏渲染并保存截图 |
| 双击视口 | 恢复默认视角 |

### 筛选面板

| 控件 | 行为 |
| --- | --- |
| 等级下限 / 等级上限 | 区间筛选；上限低于下限时自动抬升上限，避免出现空结果 |
| 缺陷类型 | 可勾选多项；**全部勾选表示不过滤** |
| 掩膜层 | 加载数据集后按真实层号自动填充；全部勾选表示不过滤 |
| 按层高度错开 | 不同层的缺陷在 Z 方向按层号错开，便于区分层次（关闭即恢复真实高度） |
| 缺陷面积 / 缺陷高度 Z | 区间筛选（含端点）；「不限」= -1；上限低于下限时自动抬升；高度比较的是**原始 Z**，不受「层高度错开」影响 |
| 按 ID 定位 | 输入缺陷 ID 后回车或点「定位」；命中缺陷若被当前筛选隐藏，会自动重置筛选条件后高亮 |
| 重置筛选条件 | 一次性恢复全部控件并重新渲染（内部阻塞信号，只重建一次热力图） |
| 导出筛选结果 | 把当前可见缺陷导出为 CSV：前置 `#` 统计注释行（数据集/筛选条件/统计摘要），UTF-8 BOM，可直接被本程序重新加载 |
| 统计视图 | 类型 × 等级交叉表、半径分区（Zone）数量与密度、泊松模型良率估算 |

## 项目亮点

1. 多线程隔离IO、UI、渲染，适配工业上位机实时性需求
2. GPU完成热力图颜色映射，减少CPU计算压力
3. 射线包围盒拾取实现三维场景缺陷点选中
4. GPU资源统一生命周期管理，规避长时间运行内存泄漏
5. 无重型第三方图形引擎，原生OpenGL渲染，方便面试官考察图形基础

## 编译与运行

环境要求：Qt6 + **与 Qt 匹配的 MinGW**（Qt 6.2.4 mingw_64 由 MinGW 11.2.0 构建），支持 OpenGL3.3 的显卡。

> ⚠️ 本项目编译期踩过较多环境坑（工具链版本不匹配、系统 PATH 被其他 MinGW 污染导致 moc 解析失败、链接缺 `opengl32` 等），
> 完整的根因分析与排查方法记录在 **[docs/BUILD_TROUBLESHOOTING.md](docs/BUILD_TROUBLESHOOTING.md)**，遇到编译问题请先查阅。
>
> 🚀 **只想把程序跑起来**：直接看后文 **[打开与运行完整流程](#打开与运行完整流程本机实测)**（5 步）与
> **[本次打开程序踩的坑](#本次打开程序踩的坑运行阶段)**（运行阶段的 8 个坑 + 速查顺序）。
>
> 🔧 **想看后续要改什么**：见 **[遗留问题与可见的功能优化空间](#遗留问题与可见的功能优化空间)**（正确性 11 项 / 性能 9 项 / 功能缺口 10 项 / 健壮性若干，**已全部处理**，修复方式与新增回归测试见该节「五、本轮完成情况」）。

### 方式零：一键脚本（推荐）

```bat
build.bat          REM 配置 + 构建（自动探测 Qt / CMake / Ninja）
build.bat test     REM 构建并运行单元测试（CTest）
build.bat clean    REM 清理后重新构建
run.bat            REM 运行程序
package.bat        REM windeployqt 打包成可分发的 dist 目录
```

CMake 版可用的辅助脚本（本机已验证）：

```bat
tools\build_cmake.bat    REM 配置 + 构建 + ctest，日志写入 build\configure.log / build.log / ctest.log
tools\smoke_test.bat     REM 拉起程序做健康检查（tasklist），产物见 build\smoke_status.txt
```

### 方式一：CMake（主构建，推荐）

```bat
REM 用 CMake Presets（需先按本机路径调整 CMakePresets.json 里的 CMAKE_PREFIX_PATH）
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release --output-on-failure

REM 或显式指定工具链（关键：编译器必须用 mingw1120_64，不是 mingw810_64）
cmake -S . -B build\cmake -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=D:/qt6/6.2.4/mingw_64 ^
      -DCMAKE_C_COMPILER=D:/qt6/Tools/mingw1120_64/bin/gcc.exe ^
      -DCMAKE_CXX_COMPILER=D:/qt6/Tools/mingw1120_64/bin/g++.exe ^
      -DWAFER_BUILD_TESTS=ON -DWAFER_ENABLE_TRANSLATIONS=ON
cmake --build build\cmake --parallel
ctest --test-dir build\cmake --output-on-failure
```

CMake 构建产物：

| 路径 | 说明 |
| --- | --- |
| `build\cmake\bin\Wafer_Defect_Visualization.exe` | 主程序 |
| `build\cmake\bin\data\wafer_defects.csv` | 示例数据集（POST_BUILD 自动拷贝） |
| `build\cmake\bin\logs\wafer_<时间戳>.log` | 运行日志（Logger 自动生成） |
| `build\cmake\bin\tst_*.exe` | 7 个单元测试可执行文件 |

可选 CMake 开关：

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `WAFER_BUILD_TESTS` | ON | 构建单元测试（缺 Qt6::Test 时自动降级为警告） |
| `WAFER_ENABLE_TRANSLATIONS` | ON | 构建翻译目标（Qt 6.2.4 未带 LinguistTools 时自动跳过） |

### 方式二：qmake（⚠️ 已弃用，仅历史参考）

```bat
mkdir build\app && cd build\app
D:\qt6\6.2.4\mingw_64\bin\qmake.exe ..\..\Wafer_Defect_Visualization.pro
D:\qt6\Tools\mingw1120_64\bin\mingw32-make.exe -j8
```

> 注意：必须使用 `mingw1120_64`（gcc 11.2.0），不能用 `mingw810_64`。
> 可用 `findstr QT_GCC_MAJOR_VERSION D:\qt6\6.2.4\mingw_64\mkspecs\qconfig.pri` 自行核对版本。

## 单元测试

测试基于 **Qt Test + CTest**，链接 `wafer_core` 静态库，无需 GUI / OpenGL 上下文即可运行：

```bat
build.bat test
REM 或
ctest --test-dir build\cmake --output-on-failure
```

| 测试 | 覆盖内容 |
| --- | --- |
| `tst_csvparser` | 行解析、非法行拒绝、表头跳过、列重排/中文别名、`#` 注释行跳过、短行与非法行分别统计、半径推导、缺失文件、**协作式取消** |
| `tst_defectfilter` | 按等级区间 / 类型集合 / 掩膜层筛选；**面积与高度区间**（闭区间、-1 = 不限、与层错开解耦） |
| `tst_mathutils` | `degToRad`/`clampf`、AABB 展开与有效性、射线-AABB / 射线-球体相交、屏幕点转世界射线 |
| `tst_geometry` | 缺陷→世界映射（含 Y→-Z、Z→Y 轴向）、标记点半径增长与上限、晶圆圆盘索引合法性与绕序校验、网格/坐标轴 |
| `tst_raypicker` | 最近缺陷拾取、背向未命中、世界坐标拾取、包围盒覆盖与空集回退、**空间索引与线性扫描逐条等价**（含「拾取半径远大于格子」的场景） |
| `tst_databuffer` | 双缓冲发布版本递增、快照隔离、空数据集处理 |
| `tst_datasetexport` | 筛选结果 CSV 导出/回读往返、`#` 注释行不影响解析、类型×等级交叉表、半径分区密度、泊松良率、邻域统计与簇规模 |

实测结果（7 个套件 / 全部通过）：

```
1/7 tst_csvparser      Passed    5/7 tst_geometry       Passed
2/7 tst_datasetexport  Passed    6/7 tst_raypicker      Passed
3/7 tst_defectfilter   Passed    7/7 tst_databuffer     Passed
4/7 tst_mathutils      Passed
100% tests passed, 0 tests failed out of 7
```

运行 exe 后，点击「加载数据集」选择 `data/wafer_defects.csv` 即可演示（约 3600 个缺陷点）。

## 日志与国际化

- **日志**：`core::Logger` 安装 Qt 消息处理器，日志同时输出到控制台与 `logs/wafer_<时间戳>.log`；
  并按 `wafer.app / wafer.data / wafer.render / wafer.business` 分类（`QLoggingCategory`）。
  同一份日志还通过 `Logger::messageLogged` 信号转发到右上角的「运行日志」悬浮面板，实现面板与文件同源。
- **国际化**：`main.cpp` 加载 Qt 基础翻译与项目翻译；`translations/wafer_zh_CN.ts` 为翻译骨架，
  由 CMake 的 `qt_add_translations` 目标驱动 `lupdate` / `lrelease`（Qt 6.2.4 未附 LinguistTools 时自动跳过，不影响构建）。

## 演示

- 动图：视口交互、热力图切换、缺陷拾取演示
- 截图：主界面、性能面板、局部截图效果

## 可扩展方向

掩膜层半透明叠加、实例化渲染优化、预设视角切换等（具体到条目的改造清单见下一节「遗留问题」）。

## 遗留问题与可见的功能优化空间

本节是**逐文件通读 `src/`、`shaders/` 后整理的功能级问题清单**（只列“行为上能被用户感知”的缺陷与可优化点，
不含纯代码风格问题）。每条都标注了位置、现象、成因与建议动作，可直接作为后续迭代清单。

### 一、正确性问题（会导致“操作与结果不符”，建议优先修）

| # | 位置 | 现象 | 成因 | 建议 |
|---|---|---|---|---|
| 1 | `SceneRenderer::captureRegionToFile` | ~~**框选截图里的内容不是框选区域，而是整个视口被重新适配后渲染的图像**；输出比例也被拉伸~~ ✅ 已修 | 离屏渲染时用 `state->viewMatrix()` + `projectionMatrix(regionW, regionH)` 重新算了一套投影，只是把“整个场景”按选区宽高比再画一遍，没有做视锥裁剪（`glScissor` / 子视锥） | 改为「按**视口原尺寸与宽高比**离屏渲染整幅画面 + 从读回图像里裁剪框选区域」，投影不变 ⇒ 裁剪结果就是屏幕上看到的画面 |
| 2 | `HeatmapRenderer::rebuild`、`SceneRenderer::captureRegionToFile` | ~~截图**看不到拖动中的框选矩形和“W x H”提示**~~ ✅ 已修 | 框选矩形是用 `QPainter` 画在 `paintGL()` 末尾（屏幕帧缓冲），离屏 FBO 链路里没有它 | 读回 `QImage` 后用 `QPainter` 叠加强调边框、尺寸标签与标题水印（`SceneRenderer::annotateCapture`） |
| 3 | `CaptureController::captureRegion` | ~~自动命名只含尺寸，**同一区域连点两次会静默覆盖上一张**~~ ✅ 已修 | 文件名 `%1_capture_%2x%3.png` 无时间戳/序号 | 追加 `yyyyMMdd_HHmmss_zzz`，写前 `QFileInfo::exists` 检查，撞名再加序号 |
| 4 | `GeometryBuilder::buildWaferDisk` | ~~晶圆**底面不可见**：从上往下看正常，从下往上看圆盘消失/出现空洞~~ ✅ 已修 | 底面扇形索引 `(bottomCenter, b0, b1)` 在 Y 轴向上的右手系里是**逆时针（朝 +Y）**，被 `glCullFace(GL_BACK)` 剔除 | 改为 `(bottomCenter, b1, b0)`，并复核顶面绕序 |
| 4b | `GeometryBuilder::buildWaferDisk` + `validateWaferDisk` | ~~修复 4 之后仍被 `validateWaferDisk` 判为“顶面绕序不是逆时针”~~ ✅ 已修 | ① 环顶点用 `(cos a, sin a)` 映射到 XZ，在 XZ 平面（“上”是 -Z）里其实是**顺时针**；② 校验器把 XZ 直接套用 2D 有符号面积公式，符号与 3D 右手法线相反；③ 校验器用“顶点法线和”的 Y 分量区分顶/底面，而侧壁三个法线求和后 Y 分量同样非零（实测 ±1.5），会被误判 | 参数化改为 `(sin a, cos a)`；`signedAreaXZ` 改用 `(bz-az)(cx-ax) - (bx-ax)(cz-az)`；分类改为“索引中是否出现中心点 0/1”。已由 `tst_geometry` 显式回归 |
| 5 | `HeatmapRenderer::rebuild` | ~~**边缘簇的热力在晶圆边界被“切一刀”**，贴边的高等级缺陷热区突然中断~~ ✅ 已修 | 只保留圆心在 `[0,1]` 内的点，而 `splat()` 本身已把越界像素 clamp 掉 | 去掉圆心判断，交给 `splat()` 裁剪 |
| 6 | `RayPicker` + `SceneRenderer::worldPositions()` | ~~把晶圆平移到画面边缘后再点，**明明看到点却点不中**~~ ✅ 已修 | 容差 `kDefaultPickTol = 0.012` 是**世界单位常量**，标记点的像素尺寸却是固定的；离屏中心越远，透视后同样的屏幕覆盖换算到世界空间越大 | 容差改为按屏幕像素换算（`constants::pixelSizeToWorldRadius`），`tst_raypicker` 有深度不变性回归 |
| 7 | `RayPicker` 与 `marker.vert` | ~~拾取半径与**实际绘制半径不一致**~~ ✅ 已修 | 着色器点尺寸 = `uPointSize * (0.65+0.9*i) * (0.7+14*r)`，拾取只用 `markerRadiusFor(area)+tol`，等级项没参与 | 尺寸公式收敛到 `constants::markerPixelSize()`，渲染（`uPixelScale`）与拾取共用 |
| 8 | `SceneRenderer::rebuildDefectGeometry` | ~~选中高亮点**刚点中时不出现**，要等下一次筛选/改缩放才显示~~ ✅ 已修 | `m_selectedVbo` 只在 `rebuildDefectGeometry()` 里写入，而 `setSelectedIndex()` 只改下标 | 在 `setSelectedIndex()` 里同步 `rebuildSelectedMarker()` |
| 9 | `WaferViewport::mouseReleaseEvent` | ~~快速拖动旋转时偶尔被判定成“点击”，**弹出未命中提示并打断交互**~~ ✅ 已修 | 是否拾取依赖 `mouseMoveEvent` 里把 `m_pendingPick` 清掉；移动事件被合并/跳过时会漏判 | 用 `pressPos` 与 `releasePos` 的距离对比 `QApplication::startDragDistance()` 判定点击 |
| 10 | `SceneRenderer::captureRegionToFile` 末尾 | ~~截图后手动 `glViewport` 恢复、`m_viewportSize` 先存后改，逻辑分散在两处~~ ✅ 已修 | 状态恢复与 FBO 生命周期耦合 | `FboRenderer::beginOffscreen/endOffscreen` 负责帧缓冲切换，`captureRegionToFile` 只保存/恢复 DPR 与 viewport |
| 11 | `LogOverlayPanel` | ~~`m_entries` **只增不删**，长时间运行内存缓慢增长~~ ✅ 已修 | 没有与 `setMaximumBlockCount` 对齐的裁剪 | `m_entries` 超过 5000 时 `removeFirst()`，与视图上限对齐 |

### 二、性能优化（1 千～10 万点量级下收益明显）

| # | 位置 | 现状 | 建议 |
|---|---|---|---|
| 1 | `HeatmapRenderer::rebuild` | ~~每次筛选变化都：**1024×1024 float 缓冲清零（4 MB）→ 逐点高斯 splat → 转 8bit → 销毁旧纹理 → 新建纹理**~~ ✅ 已修 | ① 只清上一轮写过的脏矩形；② 纹理**只创建一次**，后续 `setData()` 原地更新；③ 衰减核改为二次核 `(1-d²/r²)²` 近似高斯，避免逐像素 `std::exp` |
| 2 | `MainWindow::onZScaleChanged` | ~~滑块每动一格 → `setZScale()` → `rebuildDefectGeometry()`，且连带重建热力纹理~~ ✅ 已修 | `SceneRenderer::setZScale()` 只重建标记点几何、不碰热力纹理；`MainWindow` 用 30ms 单次定时器合并拖动事件 |
| 3 | `MainWindow::applyFilterAndPushToRenderer` | ~~一次筛选要**拷贝 4 份**~~ ✅ 已修 | 删除无读者的 `m_lastFiltered`；筛选结果直接交给 `setDefectData`（内部 `QVector` 隐式共享，写入时才分离） |
| 4 | `DataBufferManager::publish` | ~~worker 已构造好 `res.dataset`，`publish()` 又 `new DefectDataset(dataset)` **整份拷贝**~~ ✅ 已修 | 新增 `publish(DefectDataset &&)`，worker 用 `std::move` 移交所有权 |
| 5 | `CsvDatasetParser::parseFile` | ~~每行 `trimmed()` + `split()` 产生 8 个临时 `QString`~~ ✅ 已修 | 改为一遍扫描逗号、只切出**用得到的列**（`parseLineScanned`），去掉每行的 `split()` 分配；`QStringTokenizer` 在 Qt 6.0 才有且收益不明显，故未采用 |
| 6 | `RayPicker::pickFromWorldPositions` | ~~每次点击**线性遍历全部可见点**，无空间索引~~ ✅ 已修 | 新增 `PickGrid`（XZ 均匀网格 + CSR 索引 + 2D DDA 遍历），`pickWithGrid()` 结果与线性扫描**逐字段一致**（`tst_raypicker` 逐条比对回归），索引为空/点数不匹配时自动退化 |
| 7 | `SceneRenderer::renderFrame` | ~~无论场景是否变化都出帧~~ ✅ 已修 | `WaferViewport` 区分「交互帧」与「程序性刷新」：交互立即出帧，连续 `update()` 合并到 30fps（`kIdleFrameIntervalMs`），最小化时不出帧 |
| 8 | `FboRenderer::ensureSize` | ~~窗口拖动 resize 时**每收到一次 resize 就销毁重建** FBO~~ ✅ 已修 | `WaferViewport` 对 resize 做 200ms debounce，尺寸稳定后才重建 |
| 9 | `GpuResourceManager::createTexture2D` | ~~每个热力重建都 `new/create/destroy` 一个新纹理对象~~ ✅ 已修 | 热力纹理单例化（`findTexture` 命中即原地更新）；性能面板的「GPU 资源」行显示 `累计创建 / 释放`，便于发现泄漏或抖动 |

### 三、功能缺口（对“检测数据叠加”课题比较关键）

| # | 缺口 | 说明 | 建议 |
|---|---|---|---|
| 1 | ~~**缺陷类型只能单选**~~ ✅ 已修 | 下拉框一次只能选 1 种，但 `DefectFilter` 本身支持 `QSet<int>` 多类型 | 已改为可勾选 `QListWidget`，全部勾选 = 不过滤 |
| 2 | ~~**等级只有下限**~~ ✅ 已修 | UI 只提供 `≥ N`，`DefectFilter` 已有 `min/max` 双端能力 | 已增加“等级上限”下拉，并在上限 < 下限时自动抬升 |
| 3 | ~~**掩膜层号完全没用上**~~ ✅ 已修 | `DefectPoint::layer` 解析了、详情里显示了，但筛选与渲染都不区分 | 已加“掩膜层”多选列表（按数据集真实层号填充）+ “按层高度错开”开关 |
| 4 | ~~**面积 / 高度无区间筛选**~~ ✅ 已修 | 只有等级、类型、掩膜层 | `DefectFilter::setAreaRange/setHeightRange`（闭区间，-1 = 不限）+ UI 四个 `QDoubleSpinBox`；高度比较**原始 Z**（与「层高度错开」解耦），`tst_defectfilter` 有回归 |
| 5 | ~~**无 ID 搜索定位**~~ ✅ 已修 | 想找 `#12345` 只能肉眼找 | 已加“按 ID 定位”输入框，命中即高亮；被筛选隐藏时自动放宽条件。**并修复**：高亮下标此前误用「数据集下标」而非「过滤后下标」，筛选非空时会高亮错点 |
| 6 | ~~**无数据导出**~~ ✅ 已修 | 只能导出截图 | 「导出筛选结果」按钮 → `CsvDatasetExporter`：表头复用 `CsvDatasetParser::headerLine()`，前置 `#` 统计注释行（含统计摘要与筛选描述），UTF-8 BOM 便于 Excel；导出文件可被本程序**直接重新加载**（解析器已跳过 `#` 注释行） |
| 7 | ~~**无统计视图**~~ ✅ 已修 | 只有等级分布条 | 「统计视图」→ `StatisticsDialog`：类型 × 等级交叉表、半径分区（Zone）数量/严重数/密度、泊松模型良率估算（`DefectAnalyzer`） |
| 8 | ~~**详情缺少邻域信息**~~ ✅ 已修 | 只显示选中点本身 | 详情弹窗新增「邻域信息」：半径 10mm 内缺陷数（含同类型数）、最近邻距离与 ID、同簇规模（`Neighborhood::analyze`，连接距离 5mm） |
| 9 | ~~**无状态持久化**~~ ✅ 已修 | 相机、筛选、开关退出即丢 | `AppSettings`（`QSettings::IniFormat`，写入用户配置目录）记住：上次数据集路径、相机姿态、等级/类型/层/面积/高度筛选、错开开关、渲染开关、高度放缩、框选与自动保存选项；视口与主窗口共享同一份设置，避免互相覆盖 |
| 10 | ~~**`uColorBySeverity` 等 uniform 未接入**~~ ✅ 已修 | 着色器声明了但 C++ 从不设置，默认 `false`，所以标记点是单一青色而不是等级色；`uSelectedId`、`uWaferPattern` 同样无人使用 | 已删除无用 uniform；`marker.frag` 直接用顶点属性里的等级做伪彩色，与 `wafer.frag` / 左侧图例同一条色带 |

### 四、健壮性与一致性小问题

- ~~`CsvDatasetParser` **列顺序写死为 `DefectId,X,Y,Z,Area,Severity,Type,Layer`**~~ ✅ 已修：改为按表头建立“列名 → 索引”映射（支持重排列序、中英文别名），缺失列取默认值，未识别列在加载日志中提示。
- ~~`looksLikeHeader()` 只看“是否存在非数字字段”，`parseLine()` 字段数 < 3 直接丢弃~~ ✅ 已修：已分别统计 `shortLines`（列数不足）与 `invalidValueLines`（数值非法），并在日志中列出。
  - 额外踩坑：别名表里的中文是 UTF-8 字节串，而 GCC 默认 `-fexec-charset` 不是 UTF-8，`QLatin1String(a.name)` 比较永远失败（中文表头被当成“缺少必需列”）。改用 `QString::fromUtf8()` 后正确。
- ~~`SceneStateManager::panByScreenDelta` 用 `worldPerPixel = distance * 0.25f`，与 `fov`、视口高度无关~~ ✅ 已修：改为 `2 * distance * tan(fov/2) / viewportH`（真正的 1:1 跟手），`resizeGL` 时同步视口高度。
- ~~`WaferViewport::initializeGL` 失败后没有重试入口，`onRendererError` 只弹一次框~~ ✅ 已修：失败时禁用依赖 3D 的控件，错误框提供「重新初始化」按钮 → `WaferViewport::retryInitialize()` 重建上下文；成功后 `onRendererReady` 恢复控件。
- ~~`SceneRenderer::shutdown()` 被 `MainWindow::~MainWindow` 与 `WaferViewport::~WaferViewport` 各调一次~~ ✅ 已修：只保留视口析构这一处（上下文有效时释放），`MainWindow` 只停后台线程并清空数据缓冲。
- ~~**常量重复定义**：`SceneRenderer.cpp` 匿名命名空间里的 `kWaferSegments / kWaferThickness / kGridHalfExtent / kGridDivisions`，`HeatmapRenderer` 里的 `1024`、`60.0f`、`0.85f` 魔数~~ ✅ 已修：全部改为引用 `constants::`（`kHeatmapResolution` / `kHeatmapSaturation` / `kAreaNormalizeRef` 等）。
- ~~`GeometryBuilder::buildDefectMarkers` 里 `radius <= 0 ? 150.0f`~~ ✅ 已修：统一用 `constants::kDefaultWaferRadiusMm`。
- ~~`PerformanceStats::summaryText()` 打印 “FPS ... (avg)” 但取的是 `m_instantFps`~~ ✅ 已修：同时显示「瞬时 / 平均」，并修正 `PerformancePanel` 的分列解析。
- ~~日志双写：`LogOverlayPanel` 既订阅 `Logger::messageLogged`，`MainWindow` 又直接 `appendInfo()`~~ ✅ 已修：`MainWindow` 只对**尚未安装消息处理器前**的启动提示直接写面板，其余统一走 `qCInfo(lcApp)` / `qCInfo(lcData)`，由 `Logger` 单一路径分发。
- **启动崩溃（本轮回归发现）**：`SceneRenderer::rebuildHeatmap()` 无条件解引用 `m_bufferManager`，但 `setDataBufferManager()` 从未被调用 ⇒ 首帧 SIGSEGV（gdb 栈：`rebuildHeatmap → SceneRenderer::initialize → initializeGL`）。已修：`MainWindow` 显式注入数据缓冲，且渲染器内**判空**兜底。`build/verify.bat` + 实际启动冒烟检查已纳入验收。

### 五、本轮完成情况

原清单中的 **正确性 11 条 / 性能 9 条 / 功能缺口 10 条 / 健壮性若干** 已全部处理（上文以 ~~删除线~~ + ✅ 标注）。
实施顺序与验收方式：

1. **正确性**：框选截图（原一·1/2/3）、晶圆绕序（一·4/4b）、选中高亮（一·8）、拾取尺度（一·6/7）、点击判定（一·9）、日志裁剪（一·11）——由 `tst_geometry` / `tst_raypicker` 回归，并用实际启动 + 框选截图人工确认。
2. **交互补齐**：面积/高度区间（三·4）、筛选结果导出（三·6）、统计视图（三·7）、邻域信息（三·8）、状态持久化（三·9）——新增 `tst_datasetexport` 覆盖纯逻辑部分；持久化通过「启动 → 正常关闭 → 检查 ini」实测。
3. **性能**：热力纹理复用 + 脏矩形（二·1/9）、Z 滑块节流（二·2）、去掉无读者拷贝（二·3/4）、CSV 单遍扫描（二·5）、拾取空间索引（二·6）、空闲降频与 resize debounce（二·7/8）——性能面板可对比「热力图构建」「DrawCall」「GPU 资源累计创建」三项。

**本轮新发现并修复的缺陷**（清单里原本没有）：

| 缺陷 | 现象 | 根因 | 修复 |
|---|---|---|---|
| 启动即崩溃 | 双击后窗口一闪而退，日志只停在「请求的 OpenGL 上下文」 | `SceneRenderer::rebuildHeatmap()` 无条件解引用 `m_bufferManager`，而 `setDataBufferManager()` 从未被调用 | `MainWindow` 显式注入数据缓冲 + 渲染器内判空 |
| ID 定位高亮错点 | 有筛选时「按 ID 定位」高亮的是别的点 | 把**数据集下标**当成**过滤后下标**传给 `setSelectedIndex` | 新增 `filteredIndexForId()`，并在定位前同步渲染层数据 |
| 导出文件无法回读 | 导出的 CSV 重新加载报「表头缺少必需列」 | 解析器把第一条 `#` 统计注释当成表头 | 解析器跳过 `#` 注释行（`tst_csvparser` 回归） |
| 配置写到错误位置 | 关闭后 `%APPDATA%\SemiconductorInspection\...ini` 不存在 | `main.cpp` 在创建 `MainWindow` **之后**才 `setApplicationName`，`AppSettings` 构造时拿到的是 exe 默认名 | 调整 `main.cpp` 顺序 + `AppSettings::bindApplicationIdentity()` 延迟绑定 |
| 关闭时可能重复释放 | —（潜在） | `MainWindow` 与 `WaferViewport` 两处 `shutdown()` | 收敛为视口析构单点 |

### 六、仍可继续扩展的方向（不属于「遗留问题」）

- 掩膜层半透明叠加与逐层爆炸视图；
- 实例化渲染（`glDrawArraysInstanced`）支持百万级缺陷；
- 预设视角与截图标注模板；
- 与真实设备 MES/SPC 接口对接（当前为纯离线 Demo）。

## 项目说明

本项目为原型Demo，用于学习与求职展示，不对接真实半导体硬件设备。

---

## 打开与运行完整流程（本机实测）

> 本节记录的是**拿到一个已有构建目录的工程，从「确认环境」到「界面出现」的真实操作顺序**，
> 以及每一步踩到的坑。约定：`<Qt>` = `D:\qt6\6.2.4\mingw_64`，`<MinGW>` = `D:\qt6\Tools\mingw1120_64`，`<CMakeBin>` = `D:\qt6\Tools\CMake_64\bin`。

### 流程总览（5 步）

```
① 识别构建体系 ──► ② 确认工具链/构建目录 ──► ③ 增量构建(ninja) ──► ④ 校验依赖(PATH) ──► ⑤ 启动 + 健康检查
   CMake 为主         CMakeCache 是否指向              no work to do           objdump -p           进程存活/内存/日志
   .pro 已弃用        本工程(Ninja/mingw)              即产物最新               DLL 全部可解析        (tasklist / Get-Process)
```

### 第 1 步：先确认「用哪套构建系统」

- 主构建是 **CMake**（`CMakeLists.txt`）；根目录 `Wafer_Defect_Visualization.pro` 顶部已明确标注 **DEPRECATED**，
  且不含 `src/core/Logger.*`、`src/core/Constants.h`、`tests/` 等新增文件，**直接拿 .pro 编译会缺文件**。
- 因此一律走 CMake，`.pro` 只做历史参考。

```bat
dir /b /s *.pro *.vcxproj CMakeLists.txt
```

### 第 2 步：确认工具链与已有构建目录（别急着重配）

先看 `build\cmake\CMakeCache.txt` 里的三个关键项，确认它确实是「本工程 + Ninja + MinGW」的产物，
避免误用别的工程遗留的缓存：

```bat
findstr /c:"CMAKE_HOME_DIRECTORY" /c:"CMAKE_MAKE_PROGRAM" /c:"CMAKE_BUILD_TYPE" build\cmake\CMakeCache.txt
```

本机实测输出：

```
CMAKE_BUILD_TYPE:STRING=Release
CMAKE_MAKE_PROGRAM:UNINITIALIZED=d:\qt6\Tools\Ninja\ninja.exe
CMAKE_HOME_DIRECTORY:INTERNAL=D:/qt6.2.4_project/Wafer_Defect_Visualization
```

> `CMAKE_MAKE_PROGRAM` 显示为 `UNINITIALIZED` 是正常现象（显式 `-D` 传入、未被 CMake 内部改写的变量会这样显示），
> **不是错误**，不要因此删缓存重配。

### 第 3 步：增量构建（有产物就不要 clean）

```bat
set PATH=<MinGW>\bin;<Qt>\bin;C:\Windows\System32;C:\Windows
<CMakeBin>\cmake.exe -S . -B build\cmake -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH=D:/qt6/6.2.4/mingw_64 ^
    -DWAFER_BUILD_TESTS=ON -DWAFER_ENABLE_TRANSLATIONS=ON
<CMakeBin>\cmake.exe --build build\cmake --parallel
REM 或直接调 ninja，只看增量结果
D:\qt6\Tools\Ninja\ninja.exe -C build\cmake
```

期望看到 `ninja: no work to do.` —— 说明源码没有比 exe 更新，当前产物就是最新源码编出来的，可以直接运行。
**首次配置或换了工具链才需要 `build.bat clean`**，日常打开工程不要随手 clean（本工程全量重编译很慢）。

### 第 4 步：校验运行时依赖（启动前必做，能省掉 90% 的「双击没反应」）

用 `objdump` 看 exe 到底要哪些 DLL：

```bat
<MinGW>\bin\objdump.exe -p build\cmake\bin\Wafer_Defect_Visualization.exe | findstr "DLL Name"
```

本机实测依赖清单：

```
Qt6Core.dll / Qt6Gui.dll / Qt6OpenGL.dll / Qt6OpenGLWidgets.dll / Qt6Widgets.dll
libgcc_s_seh-1.dll / libstdc++-6.dll        <- MinGW 运行时
KERNEL32.dll / msvcrt.dll / OPENGL32.dll
```

结论：启动时的 PATH **必须同时包含 `<Qt>\bin`（5 个 Qt6 DLL）和 `<MinGW>\bin`（2 个 MinGW DLL）**，缺一不可。
注意 exe 旁边没有部署 DLL，工程也没有 `qt.conf`，所以必须靠 PATH 找（或先用 `package.bat` 做 windeployqt 打包）。

### 第 5 步：启动 + 健康检查

```bat
cd /d build\cmake\bin
set PATH=<MinGW>\bin;<Qt>\bin;C:\Windows\System32;C:\Windows
start "" "Wafer_Defect_Visualization.exe"

REM 等约 5~10 秒后，确认「进程活着 + 界面在响应 + 内存 >100MB」
tasklist /fi "imagename eq Wafer_Defect_Visualization.exe" /fo csv
REM 更精确的判定（推荐）
powershell -NoProfile -Command "$p = Get-Process Wafer_Defect_Visualization; 'WS_MB=' + [int]($p.WorkingSet64/1MB) + ' RESPONDING=' + $p.Responding"
```

本机实测（健康）：

```
ALIVE PID=2640 / WS_MB=180 / RESPONDING=True / THREADS=26
主窗口标题：轻量化3D晶圆缺陷可视化与检测数据叠加系统
```

判定标准（与 `docs/BUILD_TROUBLESHOOTING.md` 第 7 节一致）：

| 观察项 | 正常 | 异常含义 |
| --- | --- | --- |
| `tasklist` 能否列出进程 | 列出 | 不在列表 → 启动即崩溃，多半缺 DLL |
| 内存 | > 100 MB（实测 180 MB） | 几 MB 就退出 → 缺 DLL / 驱动不支持 GL3.3 |
| `Responding` | True | False 且弹窗 → 看是否是 OpenGL 初始化失败框 |
| `logs\wafer_<时间戳>.log` | 有新文件且无 WARN/ERROR | 无日志 → 还没进 `main()`，属加载期问题 |

### 第 6 步：用日志确认「确实跑起来了」（比看进程更硬的证据）

日志位置：`build\cmake\bin\logs\wafer_<yyyyMMdd_HHmmss>.log`（Logger 在 `main()` 第一行初始化，落盘到 exe 同级的 `logs/`）。

本次成功启动的完整日志：

```
[INFO ] [wafer.app]  日志系统已启动，日志文件： "...\build\cmake\bin\logs\wafer_20261018_192402.log"
[INFO ] [wafer.app]  "请求的 OpenGL 上下文: 3.3 Core"
[INFO ] [wafer.data] 开始解析数据集 "...\build\cmake\bin\data\wafer_defects.csv"
[INFO ] [wafer.data] 数据集解析完成，缺陷点数量： 3619 版本： 1
```

**关键判据**：出现「数据集解析完成，缺陷点数量： 3619」= 渲染器已就绪并自动加载了默认数据集。
原因在代码里：`onRendererReady()` 只有在 `SceneRenderer::initialize()` 全程成功（GL 函数指针 → GPU 资源 → 着色器 → 热力图 → FBO）
之后才会被触发，而它反过来才会调用 `loadDatasetFile()`。**所以「数据集解析完成」这一行本身就是「渲染全链路 OK」的证明。**

关闭程序：任务管理器结束 `Wafer_Defect_Visualization.exe`，或 `taskkill /f /im Wafer_Defect_Visualization.exe`。

---

## 本次打开程序踩的坑（运行阶段）

> 编译期的坑（工具链版本、PATH 污染 moc、缺 openglwidgets、缺 `-lopengl32`、`near/far` 宏等）已在
> [docs/BUILD_TROUBLESHOOTING.md](docs/BUILD_TROUBLESHOOTING.md) 中详述，本节只记录**「打开/运行」阶段**新踩到的坑。

### 坑 1：`.pro` 是弃用的，`run_app.bat` 也是弃用的 —— 别顺着旧脚本走

- **现象**：`run_app.bat` 指向 `build\app\bin`，而当前产物在 `build\cmake\bin`，直接跑会提示「未找到可执行文件」。
- **根因**：工程已从 qmake 迁移到 CMake，`run_app.bat` / `build_all.bat` 属 qmake 时代遗留，文件头已标注 DEPRECATED。
- **解决**：用 `run.bat`；它默认找 `build\ninja`，本机产物在 `build\cmake`，所以要显式传目录：

```bat
run.bat build\cmake
```

### 坑 2：构建目录在 `build\cmake`，而不是 `run.bat` / `build.bat` 默认的 `build\ninja`

- **现象**：直接 `run.bat` 报「未找到可执行文件，请先执行: build.bat」。
- **根因**：默认目录是 `build\ninja`（对应 CMakePresets 的 `ninja` 预设风格），而当前用的是按 README「方式一」显式配置出来的 `build\cmake`。
- **解决**：要么 `run.bat build\cmake`，要么用 `build.bat` 按默认目录重新构建一遍。**判断依据永远是「exe 实际在哪」**：

```bat
dir /b /s build\*.exe
```

### 坑 3：`where` / `findstr` / `more` / `forfiles` 突然「不是内部或外部命令」

- **现象**：在精简 PATH 的 cmd 会话里执行 `where cmake`、`findstr ...`、`forfiles ...`，报「不是内部或外部命令」。
- **根因**：为了隔离 PATH 污染，命令里用 `set PATH=<MinGW>\bin;<Qt>\bin;...` **替换**了 PATH（这是正确做法），
  但替换时漏掉了 `C:\Windows\System32`，于是 System32 里的 `where.exe`、`findstr.exe` 也找不到了。
- **解决**：PATH 里**始终保留** `C:\Windows\System32;C:\Windows`；已进入精简会话时，用绝对路径调用系统工具：

```bat
C:\Windows\System32\findstr.exe /c:"CMAKE_HOME_DIRECTORY" build\cmake\CMakeCache.txt
C:\Windows\System32\tasklist.exe /fi "imagename eq Wafer_Defect_Visualization.exe"
```

> 注意：`where.exe` 在 `C:\Windows\System32\where.exe`；而 `type`、`dir`、`set` 是 **cmd 内建命令**，没有 `.exe`，
> 不要写 `C:\Windows\System32\type.exe`（会报「不是可运行程序」）。

### 坑 4：长时间链式命令会被打断，`&` 串太长时后面的步骤根本没执行

- **现象**：把「启动 → ping 等待 → tasklist → 写文件 → type」串成一条很长的 `&` 命令，结果什么都没落盘，
  甚至终端被重置（重新出现「Microsoft Windows [版本 ...]」横幅）。
- **根因**：非交互终端里，长链路中的启动/等待类命令会阻塞或被回收，链式 `&` 一旦中断，后续步骤全部丢失，
  而且拿不到「命令完成」信号，于是误以为程序没起来。
- **解决**：
  1. **一条命令只做一件事**，「start 启动 GUI」与「检查状态」拆成两次调用；
  2. 中间用**独立进程**等待，别依赖终端会话：

     ```bat
     start /min "" cmd /c "ping -n 11 127.0.0.1 >nul"
     ```

  3. 需要看输出时，**重定向到文件再读文件**，不要依赖终端回显（本环境终端回显不可靠）。

### 坑 5：启动后「日志只有 4 行、没有渲染器信息」——这是正常的，不要误判为卡住

- **现象**：日志停在「数据集解析完成」，看不到「渲染器就绪 / OpenGL 版本」之类的行，容易以为渲染器初始化失败。
- **根因**：`WaferViewport::initializeGL()` 里 `rendererReady` / `rendererError` 的结果**只进 UI 日志面板**
  （`MainWindow::onRendererReady` → `m_logPanel->appendSuccess`），**没有 `qCInfo(lcRender)` 写文件日志**；
  `SceneRenderer::initialize()` 成功路径同样不打日志。所以**文件日志天生就只剩这几行**。
- **正确判法**：
  - 成功 → 日志里一定会出现「数据集解析完成，缺陷点数量： 3619」（由 `onRendererReady` → 自动加载数据触发）；
  - 失败 → 会弹出模态框「OpenGL 初始化失败」，且**永远不会**出现「数据集解析完成」这一行；
  - 进程判定：`tasklist` 有进程 + 内存 >100 MB + `Responding=True`。

### 坑 6：进程「活着」不等于「渲染正常」，只看 tasklist 会漏判

- **现象**：进程在列表里、内存也不低，但其实界面被错误 `QMessageBox` 堵着，或者 GL 上下文创建失败。
- **根因**：错误框是模态的，主进程照样存活；而 `QMessageBox::critical` 不改日志文件。
- **解决**：健康检查做「三重确认」：`Responding=True` + 日志出现「数据集解析完成」+ 内存 >100 MB。
  必要时用 PowerShell 取 `MainWindowTitle`，确认是正常的「轻量化3D晶圆缺陷可视化与检测数据叠加系统」，
  而不是「OpenGL 初始化失败」。

### 坑 7：忘了带 MinGW 的 bin，`libstdc++-6.dll` / `libgcc_s_seh-1.dll` 找不到

- **现象**：双击 exe 后窗口一闪而过，或弹出「找不到 libstdc++-6.dll」。
- **根因**：只加了 `<Qt>\bin`。Qt 是用 MinGW 11.2.0 构建的，运行时还需要同版本 MinGW 的 `libstdc++-6.dll`、`libgcc_s_seh-1.dll`，
  它们不在 Qt 目录里；系统 PATH 里那套 32 位 gcc 9.2.0 的 DLL **不能**替代（架构都不同）。
- **解决**：PATH 必须同时有 `<MinGW>\bin` 和 `<Qt>\bin`，且 `<MinGW>` 必须是 `mingw1120_64`：

```bat
set PATH=D:\qt6\Tools\mingw1120_64\bin;D:\qt6\6.2.4\mingw_64\bin;C:\Windows\System32;C:\Windows
```

> 想彻底摆脱 PATH 依赖就用 `package.bat`（windeployqt）生成 `dist\`，DLL 与 exe 同目录，可直接分发。

### 坑 8：数据集不在 exe 旁边时，界面起来了但没有缺陷点

- **现象**：程序正常启动，但视口空白 / 缺陷数为 0。
- **根因**：默认数据集路径按 `../../data`、`../data`、`data`（相对**当前工作目录**）依次探测；
  不在 `build\cmake\bin` 下启动、或 POST_BUILD 拷贝没生效时，就找不到 CSV。
- **解决**：

```bat
REM 1) 先确认数据在不在 exe 旁边
dir build\cmake\bin\data
REM 2) 启动前必须切到 exe 目录（不要在其他目录里用相对路径启动）
cd /d build\cmake\bin
```

  数据集缺失时，也可以在界面里点「加载数据集」手动选择 `data\wafer_defects.csv`。

### 打开程序的排查顺序（速查）

1. **exe 在哪**：`dir /b /s build\*.exe` → 决定 `run.bat <目录>` 的参数。
2. **缓存对不对**：`findstr CMAKE_HOME_DIRECTORY CMAKE_MAKE_PROGRAM build\cmake\CMakeCache.txt`。
3. **是不是最新**：`ninja -C build\cmake` 是否 `no work to do`。
4. **依赖齐不齐**：`objdump -p ...exe | findstr "DLL Name"`，逐个确认在 PATH 上。
5. **起没起来**：`tasklist` / PowerShell `Responding` / 内存 >100 MB。
6. **渲染 OK 没**：日志是否出现「数据集解析完成，缺陷点数量： 3619」。
7. **有没有错误框**：弹了「OpenGL 初始化失败」= 显卡驱动不支持 GL 3.3 Core，换驱动或换机器。

---

## 实测记录（2026-10-18）

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| 工具链探测 | `dir D:\qt6\...` | Qt 6.2.4 mingw_64 / MinGW 11.2.0 / Ninja / CMake_64 全部就位 |
| 缓存确认 | `findstr ... build\cmake\CMakeCache.txt` | Release + Ninja + 本工程目录 ✅ |
| 增量构建 | `ninja -C build\cmake` | `ninja: no work to do.` ✅ |
| 依赖检查 | `objdump -p ...exe` | 10 个 DLL 均可解析 ✅ |
| 启动 | `start "" Wafer_Defect_Visualization.exe` | PID 2640，窗口标题正确 ✅ |
| 健康检查 | `Get-Process` | WS_MB=180，Responding=True，线程 26 ✅ |
| 数据加载 | 日志 `wafer_20261018_192402.log` | 缺陷点 3619，版本 1 ✅ |
| 退出清理 | `taskkill /f /im ...` | 无残留进程，GPU 资源随上下文释放 ✅ |
