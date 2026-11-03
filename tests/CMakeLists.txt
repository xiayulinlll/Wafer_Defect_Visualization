# =====================================================================
#  单元测试（Qt Test + CTest）
#  只依赖 wafer_core 静态库，不拉入 UI / OpenGL 上下文
# =====================================================================

function(wafer_add_test name)
    set(sources ${ARGN})
    qt_add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE
        wafer::core
        Qt6::Test
    )
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    add_test(NAME ${name} COMMAND ${name})
    # 测试可执行文件依赖 Qt DLL，需在 PATH 上找到 Qt bin
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "PATH=${QT_BIN_DIR};$ENV{PATH}"
    )
endfunction()

wafer_add_test(tst_csvparser tst_csvparser.cpp)
wafer_add_test(tst_datasetexport tst_datasetexport.cpp)
wafer_add_test(tst_defectfilter tst_defectfilter.cpp)
wafer_add_test(tst_mathutils tst_mathutils.cpp)
wafer_add_test(tst_geometry tst_geometry.cpp)
wafer_add_test(tst_raypicker tst_raypicker.cpp)
wafer_add_test(tst_databuffer tst_databuffer.cpp)
