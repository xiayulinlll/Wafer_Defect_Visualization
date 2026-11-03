#include "ui/StatisticsDialog.h"

#include "core/Constants.h"
#include "core/DefectTypes.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace wafer {

StatisticsDialog::StatisticsDialog(QWidget *parent)
    : QDialog(parent)
{
    buildUi();
}

void StatisticsDialog::buildUi()
{
    setWindowTitle(tr("缺陷统计与良率估算"));
    setMinimumSize(640, 520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    m_summaryLabel = new QLabel(tr("尚未加载数据"), this);
    m_summaryLabel->setWordWrap(true);
    root->addWidget(m_summaryLabel);

    auto *tabs = new QTabWidget(this);

    // ---- 页 1：类型 × 等级 ----
    m_crossTable = new QTableWidget(this);
    m_crossTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_crossTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_crossTable->verticalHeader()->setVisible(false);
    tabs->addTab(m_crossTable, tr("类型 × 等级"));

    // ---- 页 2：半径分区 ----
    m_zoneTable = new QTableWidget(this);
    m_zoneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_zoneTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_zoneTable->verticalHeader()->setVisible(false);
    tabs->addTab(m_zoneTable, tr("半径分区 (Zone)"));

    root->addWidget(tabs, 1);

    m_yieldLabel = new QLabel(this);
    m_yieldLabel->setWordWrap(true);
    m_yieldLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_yieldLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(buttons);
}

void StatisticsDialog::setStatistics(const DefectStatistics &stats)
{
    m_summaryLabel->setText(
        tr("共 %1 个缺陷点 | 晶圆半径 %2 mm | 严重缺陷（≥%3）%4 个")
            .arg(stats.total)
            .arg(stats.waferRadiusMm, 0, 'f', 1)
            .arg(constants::kCriticalSeverity)
            .arg(stats.criticalTotal));

    // ---- 交叉表：行 = 类型，列 = 等级 1..5 + 合计 ----
    const int typeCount = static_cast<int>(DefectType::Other) + 1;
    const int columns = 5 + 1; // 5 个等级 + 合计
    m_crossTable->clear();
    m_crossTable->setRowCount(typeCount);
    m_crossTable->setColumnCount(columns);

    QStringList headers;
    for (int lv = 1; lv <= 5; ++lv)
        headers << tr("等级 %1").arg(lv);
    headers << tr("合计");
    m_crossTable->setHorizontalHeaderLabels(headers);
    m_crossTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int t = 0; t < typeCount; ++t) {
        auto *nameItem = new QTableWidgetItem(
            defectTypeName(static_cast<DefectType>(t)));
        nameItem->setFlags(Qt::ItemIsEnabled);
        m_crossTable->setVerticalHeaderItem(t, nameItem);

        int rowTotal = 0;
        for (int lv = 1; lv <= 5; ++lv) {
            const int n = stats.typeCounts[t][lv];
            rowTotal += n;
            auto *item = new QTableWidgetItem(QString::number(n));
            item->setTextAlignment(Qt::AlignCenter);
            if (lv >= constants::kCriticalSeverity)
                item->setForeground(QColor(0xE0, 0x54, 0x54));
            m_crossTable->setItem(t, lv - 1, item);
        }
        auto *totalItem = new QTableWidgetItem(QString::number(rowTotal));
        totalItem->setTextAlignment(Qt::AlignCenter);
        m_crossTable->setItem(t, 5, totalItem);
    }
    m_crossTable->resizeColumnsToContents();

    // ---- 分区表 ----
    const QStringList zoneHeaders{tr("分区"), tr("半径范围 (mm)"), tr("缺陷数"),
                                  tr("严重缺陷"), tr("密度 (个/100mm²)")};
    m_zoneTable->clear();
    m_zoneTable->setRowCount(stats.zones.size());
    m_zoneTable->setColumnCount(zoneHeaders.size());
    m_zoneTable->setHorizontalHeaderLabels(zoneHeaders);
    m_zoneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = 0; i < stats.zones.size(); ++i) {
        const ZoneStats &z = stats.zones.at(i);
        const QString range = QStringLiteral("%1 ~ %2")
                                  .arg(z.innerRadiusMm, 0, 'f', 1)
                                  .arg(z.outerRadiusMm, 0, 'f', 1);
        const QStringList cells{QString::number(i + 1), range, QString::number(z.total),
                                QString::number(z.critical),
                                QString::number(z.densityPer100mm2, 'f', 2)};
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells.at(c));
            item->setTextAlignment(Qt::AlignCenter);
            m_zoneTable->setItem(i, c, item);
        }
    }

    // ---- 良率说明 ----
    m_yieldLabel->setText(
        tr("良率估算：%1%（泊松模型 P = exp(-λ)，λ = 严重缺陷数 = %2）。\n"
           "说明：把等级 ≥ %3 的缺陷视为致死缺陷，每片晶圆出现 0 个致死缺陷的概率即为良率；"
           "这是检测数据叠加里最常用的粗估口径，实际产线需结合工艺与 Die 面积做聚类修正。")
            .arg(stats.estimatedYield, 0, 'f', 1)
            .arg(stats.criticalPerDie, 0, 'f', 2)
            .arg(constants::kCriticalSeverity));

    if (!isVisible())
        show();
    raise();
}

} // namespace wafer
