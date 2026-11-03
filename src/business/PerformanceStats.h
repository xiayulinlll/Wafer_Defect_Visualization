#ifndef PERFORMANCESTATS_H
#define PERFORMANCESTATS_H

#include <QObject>
#include <QString>

#include <array>

namespace wafer {

/**
 * @brief 性能统计模块（业务层）。
 *
 * 采集每帧：
 *  - CPU 帧耗时（上一帧渲染耗时，含 GPU 提交）
 *  - GPU 侧耗时（通过 GL_TIME_ELAPSED 查询对象异步回读，渲染层提供）
 *  - 绘制图元数（三角面片、点、线段）
 *  - 缺陷点数量、DrawCall 数
 * 并按滑动窗口统计平均帧率与 1% 低帧。
 *
 * 本类不做任何渲染，只接收渲染层每帧上报的原始数据。
 */
class PerformanceStats : public QObject
{
    Q_OBJECT

public:
    struct FrameSample {
        double cpuFrameMs = 0.0;   // 帧总耗时
        double gpuTimeMs = -1.0;   // GPU 计时；<0 表示不可用
        int triangles = 0;
        int points = 0;
        int lines = 0;
        int drawCalls = 0;
        int defectCount = 0;
    };

    explicit PerformanceStats(QObject *parent = nullptr);

    /** 提交一帧采样（由渲染层每帧调用；只能在其所属线程调用） */
    void submitFrame(const FrameSample &sample);

    /** 重置统计窗口 */
    void reset();

    double averageFps() const { return m_avgFps; }
    double instantaneousFps() const { return m_instantFps; }
    double lastCpuFrameMs() const { return m_lastSample.cpuFrameMs; }
    double lastGpuTimeMs() const { return m_lastSample.gpuTimeMs; }
    int lastTriangles() const { return m_lastSample.triangles; }
    int lastPoints() const { return m_lastSample.points; }
    int lastDrawCalls() const { return m_lastSample.drawCalls; }
    int lastDefectCount() const { return m_lastSample.defectCount; }

    /** 一行摘要文本，供状态栏/面板显示 */
    QString summaryText() const;

signals:
    /** 统计结果更新（默认约 4Hz 节流，避免 UI 高频重绘） */
    void statsUpdated(const QString &summary);

private:
    void recompute();

    static constexpr int kWindow = 120; // 约 2 秒 @60fps

    std::array<double, kWindow> m_frameTimesMs{};
    int m_writeIndex = 0;
    int m_filled = 0;
    bool m_useCpuTime = true;

    FrameSample m_lastSample;
    double m_avgFps = 0.0;
    double m_instantFps = 0.0;
    double m_lastEmitMs = 0.0; // 节流用（基于累计帧数近似）
    int m_framesSinceEmit = 0;
};

} // namespace wafer

#endif // PERFORMANCESTATS_H
