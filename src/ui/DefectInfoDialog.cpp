#include "ui/DefectInfoDialog.h"
#include "data/WaferDefect.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace wafer {

DefectInfoDialog::DefectInfoDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
}

void DefectInfoDialog::setupUi()
{
    setWindowTitle(tr("缺陷详细信息"));
    setMinimumWidth(380);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // 顶部等级徽标
    m_severityBadge = new QLabel(tr("LEVEL -"), this);
    m_severityBadge->setAlignment(Qt::AlignCenter);
    m_severityBadge->setMinimumHeight(44);
    root->addWidget(m_severityBadge);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);

    auto makeValue = [this]() {
        auto *label = new QLabel(tr("--"), this);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    m_idValue = makeValue();
    m_posValue = makeValue();
    m_areaValue = makeValue();
    m_severityValue = makeValue();
    m_typeValue = makeValue();
    m_layerValue = makeValue();
    m_neighborValue = makeValue();
    m_neighborValue->setWordWrap(true);

    form->addRow(tr("缺陷 ID："), m_idValue);
    form->addRow(tr("缺陷坐标："), m_posValue);
    form->addRow(tr("缺陷面积："), m_areaValue);
    form->addRow(tr("缺陷等级："), m_severityValue);
    form->addRow(tr("缺陷类型："), m_typeValue);
    form->addRow(tr("掩膜层号："), m_layerValue);
    form->addRow(tr("邻域信息："), m_neighborValue);

    root->addLayout(form);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    root->addWidget(line);

    m_conclusionValue = new QLabel(tr("请点击视口中的缺陷标记点。"), this);
    m_conclusionValue->setWordWrap(true);
    root->addWidget(m_conclusionValue);

    root->addStretch(1);

    auto *buttons = new QHBoxLayout();
    auto *focusButton = new QPushButton(tr("定位到此缺陷"), this);
    auto *closeButton = new QPushButton(tr("关闭"), this);
    buttons->addStretch(1);
    buttons->addWidget(focusButton);
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(focusButton, &QPushButton::clicked, this, [this]() {
        if (m_current.id >= 0)
            emit focusRequested(m_current.id);
    });
}

void DefectInfoDialog::setField(int row, const QString &text)
{
    if (row == 0)
        m_idValue->setText(text);
    else if (row == 1)
        m_posValue->setText(text);
    else if (row == 2)
        m_areaValue->setText(text);
    else if (row == 3)
        m_severityValue->setText(text);
    else if (row == 4)
        m_typeValue->setText(text);
    else if (row == 5)
        m_layerValue->setText(text);
    else if (row == 6)
        m_neighborValue->setText(text);
}

void DefectInfoDialog::showDefect(const DefectPoint &point)
{
    // 无邻域上下文时（例如由外部直接调用）显示占位说明
    NeighborInfo unknown;
    showDefectWithNeighbors(point, unknown);
}

void DefectInfoDialog::showDefectWithNeighbors(const DefectPoint &point,
                                               const NeighborInfo &neighbor)
{
    m_current = point;

    setField(0, QString::number(point.id));
    setField(1, tr("X=%1 mm, Y=%2 mm, Z=%3 um")
                    .arg(point.x, 0, 'f', 3)
                    .arg(point.y, 0, 'f', 3)
                    .arg(point.z, 0, 'f', 2));
    setField(2, tr("%1 um²").arg(point.area, 0, 'f', 2));
    setField(3, DefectPresentation::severityText(point.severity));
    setField(4, defectTypeName(point.type));
    setField(5, tr("Layer %1").arg(point.layer));

    // 邻域信息（§3-8）：半径内数量 / 最近邻 / 簇规模
    if (neighbor.clusterSize > 0) {
        const QString nearest = neighbor.nearestDistanceMm >= 0.0f
                                    ? tr("%1 mm（#%2）")
                                          .arg(neighbor.nearestDistanceMm, 0, 'f', 2)
                                          .arg(neighbor.nearestId)
                                    : tr("无其它缺陷");
        setField(6, tr("半径 %1 mm 内 %2 个（同类型 %3）；最近邻 %4；同簇 %5 个")
                        .arg(Neighborhood::kDefaultRadiusMm, 0, 'f', 1)
                        .arg(neighbor.neighborsInRadius)
                        .arg(neighbor.typeMatesInRadius)
                        .arg(nearest)
                        .arg(neighbor.clusterSize));
    } else {
        setField(6, tr("暂无邻域统计（未加载数据）"));
    }

    // 徽标颜色与热力图色带保持一致
    const QColor color = DefectPresentation::severityColor(point.severity);
    m_severityBadge->setText(tr("LEVEL %1  |  %2")
                                 .arg(static_cast<int>(point.severity))
                                 .arg(DefectPresentation::severityText(point.severity)));
    m_severityBadge->setStyleSheet(
        QStringLiteral("QLabel { background-color: %1; color: #10151c; font-weight: bold; "
                       "font-size: 15px; border-radius: 6px; padding: 6px; }")
            .arg(color.name()));

    // 简单结论：把等级映射为工程师可读的判断
    const int level = static_cast<int>(point.severity);
    QString conclusion;
    if (level >= 5)
        conclusion = tr("判定：致命缺陷，建议立即拦截该晶圆并复检。");
    else if (level == 4)
        conclusion = tr("判定：严重缺陷，建议人工复核该区域。");
    else if (level == 3)
        conclusion = tr("判定：中等缺陷，建议纳入趋势统计观察。");
    else
        conclusion = tr("判定：轻微缺陷，通常不影响良率。");
    m_conclusionValue->setText(conclusion);

    if (!isVisible())
        show();
    raise();
    activateWindow();
}

} // namespace wafer
