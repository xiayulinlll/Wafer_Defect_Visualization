# =====================================================================
#  ⚠️ 已弃用（DEPRECATED）—— 请使用 CMake 构建
#
#  本项目的主构建系统已迁移到 CMakeLists.txt / CMakePresets.json，
#  它额外提供了：wafer_core 静态库、单元测试(CTest)、install 规则、
#  翻译目标、POST_BUILD 资源拷贝，以及 CI。
#
#  本 .pro 文件保留仅供历史参考，不再维护（不包含 src/core/Logger.*、
#  src/core/Constants.h、tests/ 等新增文件）。请改用：
#
#      build.bat test                    # 推荐：一键构建 + 测试
#      cmake --preset mingw-release      # 或使用 CMake Presets
#
#  构建命令：
#      cmake -S . -B build/cmake -G Ninja ^
#            -DCMAKE_PREFIX_PATH=D:/qt6/6.2.4/mingw_64 ^
#            -DCMAKE_CXX_COMPILER=D:/qt6/Tools/mingw1120_64/bin/g++.exe
#      cmake --build build/cmake
# =====================================================================

QT += core gui widgets opengl openglwidgets

CONFIG += c++17
CONFIG -= console

TARGET = Wafer_Defect_Visualization
TEMPLATE = app

# ---------------- 源文件 ----------------
SOURCES += \
    src/main.cpp \
    src/ui/MainWindow.cpp \
    src/ui/WaferViewport.cpp \
    src/ui/DefectInfoDialog.cpp \
    src/ui/PerformancePanel.cpp \
    src/ui/OverlayPanel.cpp \
    src/ui/SeverityOverlayPanel.cpp \
    src/ui/LogOverlayPanel.cpp \
    src/business/SceneStateManager.cpp \
    src/business/GeometryBuilder.cpp \
    src/business/RayPicker.cpp \
    src/business/PerformanceStats.cpp \
    src/data/CsvDatasetParser.cpp \
    src/data/DataBufferManager.cpp \
    src/data/DatasetLoadWorker.cpp \
    src/render/ShaderManager.cpp \
    src/render/GpuResourceManager.cpp \
    src/render/HeatmapRenderer.cpp \
    src/render/FboRenderer.cpp \
    src/render/SceneRenderer.cpp

HEADERS += \
    src/core/DefectTypes.h \
    src/core/MathUtils.h \
    src/ui/MainWindow.h \
    src/ui/WaferViewport.h \
    src/ui/DefectInfoDialog.h \
    src/ui/PerformancePanel.h \
    src/ui/OverlayPanel.h \
    src/ui/SeverityOverlayPanel.h \
    src/ui/LogOverlayPanel.h \
    src/business/SceneStateManager.h \
    src/business/GeometryBuilder.h \
    src/business/RayPicker.h \
    src/business/PerformanceStats.h \
    src/data/WaferDefect.h \
    src/data/CsvDatasetParser.h \
    src/data/DataBufferManager.h \
    src/data/DefectFilter.h \
    src/data/DatasetLoadWorker.h \
    src/render/ShaderManager.h \
    src/render/GpuResourceManager.h \
    src/render/HeatmapRenderer.h \
    src/render/FboRenderer.h \
    src/render/SceneRenderer.h

RESOURCES += resources.qrc

INCLUDEPATH += src

# ---------------- moc 的 include 路径修正 ----------------
# 本机系统 PATH 里残留另一个 32 位 MinGW（d:\opengl\mingw，gcc 9.2.0）。
# Qt 的 mkspecs/features/moc.prf 第 13 行会执行：
#     MOC_INCLUDEPATH = $$QMAKESPEC $$_PRO_FILE_PWD_ $$MOC_INCLUDEPATH $$QMAKE_DEFAULT_INCDIRS
# 其中 $$QMAKE_DEFAULT_INCDIRS 是 qmake 探测到的编译器默认 include 目录，
# 于是那套 32 位 gcc 9.2.0 的标准库头路径被塞进 moc 的 -I 列表，
# moc 便用错误的 libstdc++ 头解析 Qt 头，报出：
#   :/opengl/mingw/mingw/lib/gcc/mingw32/9.2.0/include/c++/type_trait:40:
#       Parse error at "std"
# 解决：把 QMAKE_DEFAULT_INCDIRS 显式重定向到当前工具链（mingw1120_64）的
# 真实默认 include 目录，确保 moc 与 g++ 使用同一套标准库头。
win32-g++ {
    QMAKE_DEFAULT_INCDIRS = \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include/c++)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include/c++/x86_64-w64-mingw32)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include/c++/backward)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include-fixed)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/x86_64-w64-mingw32/include))
}

# ---------------- 编译选项 ----------------
win32-g++ {
    QMAKE_CXXFLAGS += -Wall -Wextra
    QMAKE_CXXFLAGS_RELEASE += -O2
    # 未使用参数的告警在第三方/框架回调中噪音较大，这里关闭
    QMAKE_CXXFLAGS += -Wno-unused-parameter
}

# ---------------- Windows OpenGL 系统库 ----------------
# 代码里直接调用了 glGetString 等 OpenGL 1.1 符号（QOpenGLFunctions_3_3_Core
# 在 Windows 上对 1.1 符号走 opengl32.dll 导入），但 QT += opengl 生成的链接
# 命令行里并没有带上系统 OpenGL 库，链接期会报：
#   undefined reference to `__imp_glGetString'
# 因此显式链接 opengl32 / glu32（GlU 供后续扩展使用）。
win32: LIBS += -lopengl32 -lglu32

DESTDIR = bin
OBJECTS_DIR = build/obj
MOC_DIR = build/moc
RCC_DIR = build/rcc

# ---------------- 部署 ----------------
# 运行前把 sample 数据集复制到输出目录旁
win32 {
    DATASET_FILES = $$PWD/data/wafer_defects.csv
    dataset_copy.files = $$DATASET_FILES
    dataset_copy.path = $$OUT_PWD/bin/data
    COPIES += dataset_copy
}
