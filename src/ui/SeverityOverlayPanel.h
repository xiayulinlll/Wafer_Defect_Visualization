#ifndef SEVERITYOVERLAYPANEL_H
#define SEVERITYOVERLAYPANEL_H

#include "core/DefectTypes.h"
#include "ui/OverlayPanel.h"

#include <QVector>

class QLabel;
class QResizeEvent;
class QWidget;

namespace wafer {

/**
 * @brief 缺陷等级分布悬浮面板（停靠视口左上角）。
 *
 * 展示 1~5 级缺陷的当前数量与占比条，等级配色沿用
 * DefectPresentation::severityColor()，与热力图/标记点保持一致。
 */
class SeverityOverlayPanel : public OverlayPanel
{
    Q_OBJECT

public:
    explicit SeverityOverlayPanel(QWidget *parent = nullptr);

    /** 按等级刷新统计（索引 1..5 对应等级 1..5，索引 0 忽略） */
    void setCounts(const int counts[6]);

    /** 显示当前可见 / 全部缺陷总数 */
    void setTotals(int visibleCount, int totalCount);

protected:
    /** 面板尺寸变化后按新的轨道宽度重算占比条长度 */
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupContent();

    /** 按当前轨道宽度重算所有占比条 */
    void relayoutBars();

    struct Row {
        QLabel *countLabel = nullptr;
        QLabel *barLabel = nullptr;
        QWidget *barTrack = nullptr;
    };
    QVector<Row> m_rows; // 下标 0 对应 LEVEL 1
    QLabel *m_totalLabel = nullptr;
    int m_maxCount = 1;
};

} // namespace wafer

#endif // SEVERITYOVERLAYPANEL_H
