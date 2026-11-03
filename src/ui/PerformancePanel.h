#ifndef PERFORMANCEPANEL_H
#define PERFORMANCEPANEL_H

#include "business/PerformanceStats.h"

#include <QGroupBox>

class QLabel;

namespace wafer {

/**
 * @brief 性能监控面板（UI 层，功能7）。
 *
 * 以标签形式展示：FPS（瞬时/平均）、帧耗时、GPU 耗时、三角面片数、
 * 点数量、DrawCall、当前缺陷数、GPU 资源占用、热力图 CPU 构建耗时。
 */
class PerformancePanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit PerformancePanel(QWidget *parent = nullptr);

public slots:
    /** 接收业务层聚合后的统计文本 */
    void updateSummary(const QString &summary);

    /** 更新 GPU 资源统计 */
    void updateGpuResources(const QString &text);

    /** 更新热力图构建耗时 */
    void updateHeatmapBuild(double ms);

    /** 更新离屏渲染分辨率 */
    void updateOffscreenSize(const QString &sizeText);

private:
    QLabel *m_fpsValue = nullptr;
    QLabel *m_frameTimeValue = nullptr;
    QLabel *m_gpuTimeValue = nullptr;
    QLabel *m_primitiveValue = nullptr;
    QLabel *m_drawCallValue = nullptr;
    QLabel *m_defectValue = nullptr;
    QLabel *m_gpuResourceValue = nullptr;
    QLabel *m_heatmapBuildValue = nullptr;
    QLabel *m_offscreenValue = nullptr;
};

} // namespace wafer

#endif // PERFORMANCEPANEL_H
