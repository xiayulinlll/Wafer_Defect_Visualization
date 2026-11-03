#include "ui/SeverityOverlayPanel.h"

#include "data/WaferDefect.h" // DefectPresentation::severityColor()

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace wafer {

SeverityOverlayPanel::SeverityOverlayPanel(QWidget *parent)
    : OverlayPanel(tr("缺陷等级分布"), parent)
{
    setupContent();

    // 初始尺寸贴近 200×170：五行等级 + 合计行完整显示，且占比条有足够宽度；
    // 用户仍可拖拽边缘缩放，或由已保存的布局恢复尺寸。
    setMinimumSize(200, 170);
    setInitialSizeScale(1.05f, 1.0f);
}

void SeverityOverlayPanel::setupContent()
{
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    // 让"占比条"所在列吸收面板的额外宽度
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 0);

    for (int level = 1; level <= 5; ++level) {
        const QColor color =
            DefectPresentation::severityColor(static_cast<DefectSeverity>(level));

        auto *nameLabel = new QLabel(tr("LEVEL %1").arg(level));
        nameLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; "
                                                "font-weight: bold; }")
                                     .arg(color.name()));

        // 占比条放在一个"轨道"里，轨道随面板宽度伸缩，条形按比例填充
        auto *barTrack = new QWidget();
        barTrack->setMinimumHeight(14);
        barTrack->setStyleSheet(QStringLiteral("background: rgba(255, 255, 255, 18); "
                                               "border-radius: 3px;"));

        auto *barLabel = new QLabel(barTrack);
        barLabel->setStyleSheet(QStringLiteral("background: transparent; border-radius: 3px;"));
        // 条形高度随轨道高度自适应
        auto *trackLayout = new QHBoxLayout(barTrack);
        trackLayout->setContentsMargins(0, 0, 0, 0);
        trackLayout->setSpacing(0);
        trackLayout->addWidget(barLabel);
        trackLayout->addStretch(1);

        auto *countLabel = new QLabel(QStringLiteral("0"));
        countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        countLabel->setStyleSheet(QStringLiteral("QLabel { color: #dce6f0; font-size: 11px; "
                                                 "font-family: Consolas, monospace; }"));
        countLabel->setMinimumWidth(42);

        grid->addWidget(nameLabel, level - 1, 0);
        grid->addWidget(barTrack, level - 1, 1);
        grid->addWidget(countLabel, level - 1, 2);

        Row row;
        row.countLabel = countLabel;
        row.barLabel = barLabel;
        row.barTrack = barTrack;
        m_rows.push_back(row);
    }

    m_totalLabel = new QLabel(tr("可见 -- / 全部 --"));
    m_totalLabel->setStyleSheet(
        QStringLiteral("QLabel { color: #8fa8bd; font-size: 11px; }"));

    auto *content = new QVBoxLayout();
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(6);
    content->addLayout(grid);
    content->addWidget(m_totalLabel);
    setContentLayout(content);
}

void SeverityOverlayPanel::setCounts(const int counts[6])
{
    int maxCount = 1;
    for (int level = 1; level <= 5; ++level)
        maxCount = qMax(maxCount, counts[level]);
    m_maxCount = maxCount;

    for (int level = 1; level <= 5; ++level)
        m_rows[level - 1].countLabel->setText(QString::number(counts[level]));

    relayoutBars();
}

void SeverityOverlayPanel::relayoutBars()
{
    for (int level = 1; level <= 5; ++level) {
        Row &row = m_rows[level - 1];
        const int count = row.countLabel->text().toInt();

        // 以最长的一条为满格（占满轨道可用宽度），直观反映各级占比
        const int trackWidth = qMax(1, row.barTrack->width() - 4);
        const int width = count == 0 ? 0 : qMax(4, count * trackWidth / m_maxCount);
        const QColor color =
            DefectPresentation::severityColor(static_cast<DefectSeverity>(level));
        row.barLabel->setFixedWidth(width);
        row.barLabel->setStyleSheet(
            width == 0
                ? QStringLiteral("background: transparent;")
                : QStringLiteral("background-color: %1; border-radius: 3px;")
                      .arg(color.name()));
    }
}

void SeverityOverlayPanel::resizeEvent(QResizeEvent *event)
{
    OverlayPanel::resizeEvent(event);
    // 面板被拉大/拉小时，占比条按新宽度重新分配
    relayoutBars();
}

void SeverityOverlayPanel::setTotals(int visibleCount, int totalCount)
{
    m_totalLabel->setText(tr("可见 %1 / 全部 %2").arg(visibleCount).arg(totalCount));
}

} // namespace wafer
