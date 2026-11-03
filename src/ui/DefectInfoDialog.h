#ifndef DEFECTINFODIALOG_H
#define DEFECTINFODIALOG_H

#include "core/DefectTypes.h"
#include "data/Neighborhood.h"

#include <QDialog>

class QLabel;

namespace wafer {

/**
 * @brief 缺陷信息弹窗（UI 层，流程2 第4步）。
 *
 * 展示被拾取缺陷的详细参数：ID、坐标、面积、等级、类型、掩膜层，
 * 以及邻域信息（§3-8）：半径内缺陷数、最近邻距离、所属簇规模、同类型邻点。
 * 使用非模态方式展示，方便工程师连续点击多个缺陷对比。
 */
class DefectInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DefectInfoDialog(QWidget *parent = nullptr);

    /** 显示出指定缺陷；若对话框未显示则 show() */
    void showDefect(const DefectPoint &point);

    /** 显示缺陷并附带邻域统计（由主窗口用当前筛选结果计算） */
    void showDefectWithNeighbors(const DefectPoint &point, const NeighborInfo &neighbor);

signals:
    /** 用户点击"定位到此缺陷"（可用于相机聚焦，扩展接口） */
    void focusRequested(int defectId);

private:
    void setupUi();
    void setField(int row, const QString &text);

    QLabel *m_severityBadge = nullptr;
    QLabel *m_idValue = nullptr;
    QLabel *m_posValue = nullptr;
    QLabel *m_areaValue = nullptr;
    QLabel *m_severityValue = nullptr;
    QLabel *m_typeValue = nullptr;
    QLabel *m_layerValue = nullptr;
    QLabel *m_neighborValue = nullptr;
    QLabel *m_conclusionValue = nullptr;

    DefectPoint m_current;
};

} // namespace wafer

#endif // DEFECTINFODIALOG_H
