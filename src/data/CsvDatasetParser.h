#ifndef CSVDATASETPARSER_H
#define CSVDATASETPARSER_H

#include "core/DefectTypes.h"

#include <QString>

#include <functional>

namespace wafer {

/**
 * @brief 模拟晶圆缺陷 CSV 解析器（纯 CPU 逻辑，无任何 Qt UI / OpenGL 依赖）。
 *
 * 期望的 CSV 列顺序（首行为表头，可自动识别/跳过）：
 *   DefectId,X,Y,Z,Area,Severity,Type,Layer
 * 兼容中文表头与大小写差异；无法识别的行计入 skippedLines。
 *
 * 该类被设计为可在后台线程中安全执行（无共享状态、无 Qt 对象依赖）。
 */
class CsvDatasetParser
{
public:
    struct Result {
        bool ok = false;
        DefectDataset dataset;
        QString errorMessage;
    };

    /** 取消回调：返回 true 表示请求中止解析（协作式取消，替代 QThread::terminate） */
    using CancelCallback = std::function<bool()>;

    /**
     * @brief 解析文件。
     * @param filePath    CSV 文件绝对路径
     * @param radiusMm    晶圆半径（mm），<=0 时从数据包围盒自动推导
     * @param shouldCancel 可选取消回调，每处理一批行检查一次
     * @return 解析结果；被取消时 ok=false 且 errorMessage 标注“已取消”
     */
    static Result parseFile(const QString &filePath, float radiusMm = 0.0f,
                            const CancelCallback &shouldCancel = {});

    /** 单行解析（供单元测试与流式加载复用） */
    static bool parseLine(const QString &line, DefectPoint *out);

    /** CSV 字段名（表头） */
    static QString headerLine();
};

} // namespace wafer

#endif // CSVDATASETPARSER_H
