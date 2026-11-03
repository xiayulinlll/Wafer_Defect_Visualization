#ifndef STATISTICSDIALOG_H
#define STATISTICSDIALOG_H

#include "data/DefectStatistics.h"

#include <QDialog>

class QTableWidget;
class QLabel;

namespace wafer {

/**
 * @brief 统计视图（§3-7：检测数据叠加的核心价值）。
 *
 * 三个标签页：
 *  1. 「类型 × 等级」交叉表（含行/列合计）；
 *  2. 按半径分环（Zone）的缺陷分布、严重缺陷数与密度；
 *  3. 良率估算说明与计算依据（泊松模型）。
 */
class StatisticsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StatisticsDialog(QWidget *parent = nullptr);

    /** 用新的统计数据刷新界面（对话框未显示时自动 show） */
    void setStatistics(const DefectStatistics &stats);

private:
    void buildUi();

    QLabel *m_summaryLabel = nullptr;
    QLabel *m_yieldLabel = nullptr;
    QTableWidget *m_crossTable = nullptr;
    QTableWidget *m_zoneTable = nullptr;
};

} // namespace wafer

#endif // STATISTICSDIALOG_H
