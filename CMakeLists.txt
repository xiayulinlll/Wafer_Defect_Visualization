cmake_minimum_required(VERSION 3.16)

project(Wafer_Defect_Visualization
    VERSION 1.0.0
    DESCRIPTION "轻量化3D晶圆缺陷可视化与检测数据叠加系统"
    LANGUAGES CXX)

# =====================================================================
#  全局设置
# =====================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 输出目录统一：可执行文件与 DLL 都落到 bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# 默认开启测试与翻译；如不需要可 -DWAFER_BUILD_TESTS=OFF
option(WAFER_BUILD_TESTS "Build unit tests" ON)
option(WAFER_ENABLE_TRANSLATIONS "Build translations" ON)

# =====================================================================
#  依赖
# =====================================================================
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(Qt6 QUIET COMPONENTS Test)

# =====================================================================
#  静态库：可复用核心逻辑（便于单元测试直接链接，不拉 UI）
# =====================================================================
set(WAFER_CORE_SOURCES
    src/core/Logger.cpp

    src/data/CsvDatasetParser.cpp
    src/data/CsvDatasetExporter.cpp
    src/data/DataBufferManager.cpp
    src/data/DatasetLoadWorker.cpp
    src/data/DefectStatistics.cpp
    src/data/Neighborhood.cpp

    src/business/SceneStateManager.cpp
    src/business/GeometryBuilder.cpp
    src/business/RayPicker.cpp
    src/business/PerformanceStats.cpp

    src/render/ShaderManager.cpp
    src/render/GpuResourceManager.cpp
    src/render/HeatmapRenderer.cpp
    src/render/FboRenderer.cpp
    src/render/SceneRenderer.cpp
)

set(WAFER_CORE_HEADERS
    src/core/Constants.h
    src/core/DefectTypes.h
    src/core/MathUtils.h
    src/core/Logger.h

    src/data/WaferDefect.h
    src/data/DefectFilter.h
    src/data/CsvDatasetParser.h
    src/data/CsvDatasetExporter.h
    src/data/DataBufferManager.h
    src/data/DatasetLoadWorker.h
    src/data/DefectStatistics.h
    src/data/Neighborhood.h

    src/business/SceneStateManager.h
    src/business/GeometryBuilder.h
    src/business/RayPicker.h
    src/business/PerformanceStats.h

    src/render/ShaderManager.h
    src/render/GpuResourceManager.h
    src/render/HeatmapRenderer.h
    src/render/FboRenderer.h
    src/render/SceneRenderer.h
)

add_library(wafer_core STATIC
    ${WAFER_CORE_SOURCES}
    ${WAFER_CORE_HEADERS}
)
add_library(wafer::core ALIAS wafer_core)

target_include_directories(wafer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wafer_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

set_target_properties(wafer_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Windows/MinGW：渲染层直接调用原生 GL 入口（glGenVertexArrays 等 FBO/VAO API），
# 必须显式链接 opengl32，否则在链接可执行文件时报 __imp_gl* 未定义。
if(WIN32)
    target_link_libraries(wafer_core PUBLIC opengl32)
endif()

# =====================================================================
#  可执行文件
# =====================================================================
set(WAFER_APP_SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/AppSettings.cpp
    src/ui/DatasetController.cpp
    src/ui/CaptureController.cpp
    src/ui/WaferViewport.cpp
    src/ui/DefectInfoDialog.cpp
    src/ui/PerformancePanel.cpp
    src/ui/OverlayPanel.cpp
    src/ui/SeverityOverlayPanel.cpp
    src/ui/LogOverlayPanel.cpp
    src/ui/StatisticsDialog.cpp
)

set(WAFER_APP_HEADERS
    src/ui/MainWindow.h
    src/ui/AppSettings.h
    src/ui/DatasetController.h
    src/ui/CaptureController.h
    src/ui/WaferViewport.h
    src/ui/DefectInfoDialog.h
    src/ui/PerformancePanel.h
    src/ui/OverlayPanel.h
    src/ui/SeverityOverlayPanel.h
    src/ui/LogOverlayPanel.h
    src/ui/StatisticsDialog.h
)

qt_add_executable(Wafer_Defect_Visualization
    MANUAL_FINALIZATION
    ${WAFER_APP_SOURCES}
    ${WAFER_APP_HEADERS}
    resources.qrc
)

target_link_libraries(Wafer_Defect_Visualization PRIVATE
    wafer::core
    Qt6::Widgets
    Qt6::OpenGLWidgets
)

# Windows/MinGW 下 Qt6::OpenGL 只提供 QOpenGLFunctions 封装，
# 直接调用 glGetString / glClear 等原生 GL 入口仍需链接 opengl32
# （否则报 undefined reference to `__imp_glGetString'）。
if(WIN32)
    target_link_libraries(Wafer_Defect_Visualization PRIVATE opengl32)
endif()

# ---- 编译告警 ----
if(MSVC)
    target_compile_options(wafer_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(Wafer_Defect_Visualization PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(wafer_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(Wafer_Defect_Visualization PRIVATE
        -Wall -Wextra -Wno-unused-parameter)
endif()

# =====================================================================
#  国际化（保留可翻译骨架）
# =====================================================================
if(WAFER_ENABLE_TRANSLATIONS)
    if(COMMAND qt_add_translations)
        qt_add_translations(Wafer_Defect_Visualization
            TS_FILES translations/wafer_zh_CN.ts
            RESOURCE_PREFIX "/i18n"
        )
    else()
        message(STATUS "qt_add_translations 不可用，跳过翻译目标（不影响构建）")
    endif()
endif()

# =====================================================================
#  运行时资源：示例数据集
# =====================================================================
add_custom_command(TARGET Wafer_Defect_Visualization POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:Wafer_Defect_Visualization>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/wafer_defects.csv"
            "$<TARGET_FILE_DIR:Wafer_Defect_Visualization>/data/wafer_defects.csv"
)

qt_finalize_executable(Wafer_Defect_Visualization)

# Qt 运行时所在目录（供测试设置 PATH 使用）
if(TARGET Qt6::Core)
    get_target_property(_qt_core_loc Qt6::Core IMPORTED_LOCATION)
    if(_qt_core_loc)
        get_filename_component(QT_BIN_DIR "${_qt_core_loc}" DIRECTORY)
    endif()
endif()
if(NOT QT_BIN_DIR)
    set(QT_BIN_DIR "${QT6_DIR}/../../../bin")
    get_filename_component(QT_BIN_DIR "${QT_BIN_DIR}" ABSOLUTE)
endif()

# =====================================================================
#  测试
# =====================================================================
if(WAFER_BUILD_TESTS)
    if(Qt6Test_FOUND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(WARNING "未找到 Qt6::Test，跳过单元测试。")
    endif()
endif()

# =====================================================================
#  安装规则
# =====================================================================
install(TARGETS Wafer_Defect_Visualization
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)
install(FILES data/wafer_defects.csv DESTINATION bin/data)
