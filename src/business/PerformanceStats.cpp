#include "business/PerformanceStats.h"

#include <algorithm>

namespace wafer {

PerformanceStats::PerformanceStats(QObject *parent)
    : QObject(parent)
{
    m_frameTimesMs.fill(0.0);
}

void PerformanceStats::reset()
{
    m_frameTimesMs.fill(0.0);
    m_writeIndex = 0;
    m_filled = 0;
    m_avgFps = 0.0;
    m_instantFps = 0.0;
    m_lastSample = FrameSample();
    emit statsUpdated(summaryText());
}

void PerformanceStats::submitFrame(const FrameSample &sample)
{
    m_lastSample = sample;

    // GPU 耗时优先（更真实反映渲染负载），不可用时退化为 CPU 帧耗时
    const double ms = sample.gpuTimeMs > 0.0 ? sample.gpuTimeMs : sample.cpuFrameMs;
    if (ms > 0.0) {
        m_frameTimesMs[m_writeIndex] = ms;
        m_writeIndex = (m_writeIndex + 1) % kWindow;
        m_filled = std::min(m_filled + 1, kWindow);
    }

    if (sample.cpuFrameMs > 0.0)
        m_instantFps = 1000.0 / sample.cpuFrameMs;

    recompute();

    // 约 4Hz 上报 UI，避免高频刷新导致面板重绘成为新的瓶颈
    ++m_framesSinceEmit;
    if (m_framesSinceEmit >= 15) {
        m_framesSinceEmit = 0;
        emit statsUpdated(summaryText());
    }
}

void PerformanceStats::recompute()
{
    if (m_filled <= 0) {
        m_avgFps = 0.0;
        return;
    }

    // 平均帧耗时 -> 平均帧率。使用"平均 FPS"而非"FPS 平均"，
    // 后者会被个别极快/极慢帧带偏。
    double sum = 0.0;
    for (int i = 0; i < m_filled; ++i)
        sum += m_frameTimesMs[i];

    const double avgMs = sum / static_cast<double>(m_filled);
    m_avgFps = avgMs > 1e-6 ? 1000.0 / avgMs : 0.0;
}

QString PerformanceStats::summaryText() const
{
    const double gpu = m_lastSample.gpuTimeMs;
    const QString gpuText =
        gpu > 0.0 ? QStringLiteral("%1 ms").arg(gpu, 0, 'f', 2) : QStringLiteral("N/A");

    return QStringLiteral("FPS %1 (瞬时) / %2 (平均) | 帧 %3 ms | GPU %4 | 三角面 %5 | 点 %6 | "
                          "DrawCall %7 | 缺陷 %8")
        .arg(m_instantFps, 0, 'f', 1)
        .arg(m_avgFps, 0, 'f', 1)
        .arg(m_lastSample.cpuFrameMs, 0, 'f', 2)
        .arg(gpuText)
        .arg(m_lastSample.triangles)
        .arg(m_lastSample.points)
        .arg(m_lastSample.drawCalls)
        .arg(m_lastSample.defectCount);
}

} // namespace wafer
