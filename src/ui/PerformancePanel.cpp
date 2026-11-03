#include "ui/PerformancePanel.h"

#include <QFormLayout>
#include <QLabel>

namespace wafer {

PerformancePanel::PerformancePanel(QWidget *parent)
    : QGroupBox(tr("性能监控"), parent)
{
    auto *form = new QFormLayout(this);
    form->setContentsMargins(12, 16, 12, 12);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(6);

    auto makeValue = [this]() {
        auto *label = new QLabel(tr("--"), this);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setStyleSheet(QStringLiteral("QLabel { font-family: Consolas, monospace; }"));
        return label;
    };

    m_fpsValue = makeValue();
    m_frameTimeValue = makeValue();
    m_gpuTimeValue = makeValue();
    m_primitiveValue = makeValue();
    m_drawCallValue = makeValue();
    m_defectValue = makeValue();
    m_heatmapBuildValue = makeValue();
    m_offscreenValue = makeValue();
    m_gpuResourceValue = makeValue();

    form->addRow(tr("帧率："), m_fpsValue);
    form->addRow(tr("CPU 帧耗时："), m_frameTimeValue);
    form->addRow(tr("GPU 耗时："), m_gpuTimeValue);
    form->addRow(tr("图元数量："), m_primitiveValue);
    form->addRow(tr("DrawCall："), m_drawCallValue);
    form->addRow(tr("渲染缺陷数："), m_defectValue);
    form->addRow(tr("热力图构建："), m_heatmapBuildValue);
    form->addRow(tr("离屏分辨率："), m_offscreenValue);
    form->addRow(tr("GPU 资源："), m_gpuResourceValue);
}

void PerformancePanel::updateSummary(const QString &summary)
{
    // summary 由 PerformanceStats::summaryText() 生成，格式：
    // FPS x (avg) | 帧 x ms | GPU x | 三角面 x | 点 x | DrawCall x | 缺陷 x
    const QStringList parts = summary.split(QLatin1Char('|'));
    if (parts.size() < 7)
        return;

    m_fpsValue->setText(parts.value(0).trimmed());
    m_frameTimeValue->setText(parts.value(1).trimmed());
    m_gpuTimeValue->setText(parts.value(2).trimmed());
    m_primitiveValue->setText(QStringLiteral("%1 / %2")
                                  .arg(parts.value(3).trimmed(), parts.value(4).trimmed()));
    m_drawCallValue->setText(parts.value(5).trimmed());
    m_defectValue->setText(parts.value(6).trimmed());
}

void PerformancePanel::updateGpuResources(const QString &text)
{
    m_gpuResourceValue->setText(text);
}

void PerformancePanel::updateHeatmapBuild(double ms)
{
    m_heatmapBuildValue->setText(QStringLiteral("%1 ms").arg(ms, 0, 'f', 2));
}

void PerformancePanel::updateOffscreenSize(const QString &sizeText)
{
    m_offscreenValue->setText(sizeText);
}

} // namespace wafer
