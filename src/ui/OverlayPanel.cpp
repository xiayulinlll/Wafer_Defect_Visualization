#include "ui/OverlayPanel.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace wafer {

OverlayPanel::OverlayPanel(const QString &title, QWidget *parent)
    : QFrame(parent)
{
    setupUi(title);
}

void OverlayPanel::setupUi(const QString &title)
{
    // 悬浮面板自身不接收父控件布局管理（由 attach() 做绝对定位），
    // 但内部仍使用布局承载标题栏与内容区。
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // 需要 mouseMoveEvent 才能感知鼠标是否落在边缘（用于切换缩放指针）
    setMouseTracking(true);

    // ---- 标题栏 ----
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("overlayTitleBar"));
    m_titleLayout = new QHBoxLayout(m_titleBar);
    m_titleLayout->setContentsMargins(10, 6, 6, 6);
    m_titleLayout->setSpacing(6);

    m_titleLabel = new QLabel(title, m_titleBar);
    m_titleLabel->setObjectName(QStringLiteral("overlayTitleLabel"));
    m_titleLayout->addWidget(m_titleLabel, 1);

    m_collapseButton = new QPushButton(QStringLiteral("—"), m_titleBar);
    m_collapseButton->setObjectName(QStringLiteral("overlayCollapseButton"));
    m_collapseButton->setCursor(Qt::PointingHandCursor);
    m_collapseButton->setFixedSize(18, 18);
    m_collapseButton->setToolTip(tr("收起 / 展开"));
    // 按钮自身吞掉鼠标移动，避免被面板边缘的缩放指针覆盖
    m_collapseButton->setMouseTracking(true);
    m_collapseButton->installEventFilter(this);
    m_titleLayout->addWidget(m_collapseButton, 0, Qt::AlignTop);

    m_rootLayout->addWidget(m_titleBar);

    // ---- 内容区 ----
    m_contentHost = new QWidget(this);
    m_contentHost->setObjectName(QStringLiteral("overlayContentHost"));
    m_contentLayout = new QVBoxLayout(m_contentHost);
    m_contentLayout->setContentsMargins(10, 4, 10, 10);
    m_contentLayout->setSpacing(6);
    m_rootLayout->addWidget(m_contentHost, 1);

    connect(m_collapseButton, &QPushButton::clicked, this,
            [this]() { setCollapsed(!m_collapsed); });

    applyOverlayStyle();

    // 注意：这里不能固化 setMinimumSize(minimumSizeHint())——此时子类内容尚未添加，
    // 会把最小尺寸锁成标题栏大小，之后子类的内容最小尺寸（如 setMinimumSize(380,220)）
    // 也就无法生效。最小尺寸统一在 attach() 里、子类内容就绪后再结算。
}

void OverlayPanel::applyOverlayStyle()
{
    // 半透明深色卡片：与深色渲染背景协调，同时保证文字可读。
    setObjectName(QStringLiteral("overlayPanel"));
    setStyleSheet(QStringLiteral(R"(
QFrame#overlayPanel {
    background-color: rgba(18, 24, 33, 205);
    border: 1px solid rgba(96, 148, 196, 130);
    border-radius: 8px;
}
QWidget#overlayTitleBar {
    background-color: rgba(32, 46, 62, 220);
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
}
QLabel#overlayTitleLabel {
    color: #9fc7e8;
    font-size: 12px;
    font-weight: bold;
    background: transparent;
}
QPushButton#overlayCollapseButton {
    color: #9fc7e8;
    background-color: rgba(52, 74, 96, 200);
    border: 1px solid rgba(96, 148, 196, 130);
    border-radius: 4px;
    font-size: 11px;
    padding: 0px;
}
QPushButton#overlayCollapseButton:hover {
    background-color: rgba(78, 110, 140, 230);
    color: #ffffff;
}
QWidget#overlayContentHost {
    background: transparent;
}
)"));

    setAttribute(Qt::WA_StyledBackground, true);
    m_titleBar->setAttribute(Qt::WA_StyledBackground, true);
    m_collapseButton->setAttribute(Qt::WA_StyledBackground, true);
}

void OverlayPanel::setContentLayout(QLayout *layout)
{
    if (!layout)
        return;
    m_contentLayout->addLayout(layout);
}

void OverlayPanel::setInitialSizeScale(float widthScale, float heightScale)
{
    m_widthScale = qMax(0.2f, widthScale);
    m_heightScale = qMax(0.2f, heightScale);
}

QSize OverlayPanel::preferredSize() const
{
    // 目标尺寸 = 内容自然尺寸（不小于布局最小尺寸）× 初始比例。
    // 子类的内容最小尺寸（如 setMinimumSize(380, 220)）会被算进 minimumSizeHint()，
    // 因此放大后的结果同样受它兜底，保证默认尺寸下内容不被压扁。
    const QSize natural = sizeHint().expandedTo(minimumSizeHint());
    return QSize(qRound(natural.width() * m_widthScale),
                 qRound(natural.height() * m_heightScale))
        .expandedTo(minimumSizeHint());
}

void OverlayPanel::attach(QWidget *container, Anchor anchor, const QPoint &margin)
{
    m_container = container;
    m_anchor = anchor;
    m_margin = margin;

    if (m_container) {
        setParent(m_container);
        // 跟随容器尺寸变化重新贴角
        m_container->installEventFilter(this);
        raise();
    }

    // 先按"内容自然尺寸 × 初始比例"定出尺寸，再贴到角落。
    // 注意：attach() 往往发生在容器首次布局之前（此时容器尺寸接近 0），
    // 不能在这时就按容器尺寸收缩，否则面板会被压到最小值，
    // 之后尺寸变化通知才会把布局补上。
    adjustSize();
    // 让内容布局先把（样式表生效后的）自然尺寸/最小尺寸结算出来，
    // 否则 preferredSize() 会取到尚未激活布局的缓存值，导致面板偏小。
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    // 子类内容就绪后再结算最小尺寸：不得小于内容所需尺寸，避免被压扁/裁切。
    const QSize need = minimumSizeHint();
    setMinimumSize(qMax(minimumWidth(), need.width()), qMax(minimumHeight(), need.height()));

    resize(preferredSize());
    m_expandedSize = size();
    reposition();
}

void OverlayPanel::reposition()
{
    if (!m_container)
        return;

    // 容器尚未完成布局（宽/高极小）时只做定位，不做尺寸收缩：
    // 否则会把刚设好的默认尺寸压成最小值，用户看到的就是被裁掉内容的面板。
    const bool containerLaidOut = m_container->width() > 120 && m_container->height() > 120;

    // 面板尺寸超过容器时收缩，避免溢出视口
    const int maxWidth = qMax(80, m_container->width() - 2 * m_margin.x());
    const int maxHeight = qMax(60, m_container->height() - 2 * m_margin.y());
    if (containerLaidOut && (width() > maxWidth || height() > maxHeight)) {
        resize(qMin(width(), maxWidth), qMin(height(), maxHeight));
        if (!m_collapsed)
            m_expandedSize = size();
    }

    if (m_dragging || m_resizing || m_dragDetached) {
        // 用户已手动拖动/缩放：夹取在容器范围内即可
        move(qBound(0, x(), qMax(0, m_container->width() - width())),
             qBound(0, y(), qMax(0, m_container->height() - height())));
        raise();
        return;
    }

    const int leftX = m_margin.x();
    const int rightX = m_container->width() - width() - m_margin.x();
    const int topY = m_margin.y();
    const int bottomY = m_container->height() - height() - m_margin.y();

    const int x = (m_anchor == Anchor::TopLeft || m_anchor == Anchor::BottomLeft)
                      ? leftX
                      : rightX;
    const int y = (m_anchor == Anchor::TopLeft || m_anchor == Anchor::TopRight)
                      ? topY
                      : bottomY;

    move(x, y);
    raise();
}

bool OverlayPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_container && event->type() == QEvent::Resize)
        reposition();

    // 鼠标进入收起按钮区域时恢复箭头/手型指针
    if (watched == m_collapseButton && event->type() == QEvent::Enter)
        unsetCursor();

    return QFrame::eventFilter(watched, event);
}

void OverlayPanel::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;

    if (collapsed) {
        m_expandedSize = size();
        m_contentHost->setVisible(false);
        m_collapseButton->setText(QStringLiteral("+"));
        resize(QSize(m_expandedSize.width(), m_titleBar->sizeHint().height()));
    } else {
        m_contentHost->setVisible(true);
        m_collapseButton->setText(QStringLiteral("—"));
        if (m_expandedSize.isValid())
            resize(m_expandedSize);
    }

    m_collapsed = collapsed;
    reposition();
}

Qt::Edges OverlayPanel::resizeEdgesAt(const QPoint &pos) const
{
    if (!m_resizable || m_collapsed)
        return Qt::Edges();

    Qt::Edges edges;
    if (pos.x() <= kResizeGrip)
        edges |= Qt::LeftEdge;
    else if (pos.x() >= width() - 1 - kResizeGrip)
        edges |= Qt::RightEdge;

    if (pos.y() <= kResizeGrip)
        edges |= Qt::TopEdge;
    else if (pos.y() >= height() - 1 - kResizeGrip)
        edges |= Qt::BottomEdge;

    return edges;
}

Qt::CursorShape OverlayPanel::cursorForEdges(Qt::Edges edges)
{
    const bool l = edges & Qt::LeftEdge;
    const bool r = edges & Qt::RightEdge;
    const bool t = edges & Qt::TopEdge;
    const bool b = edges & Qt::BottomEdge;

    if ((l && t) || (r && b))
        return Qt::SizeFDiagCursor;
    if ((r && t) || (l && b))
        return Qt::SizeBDiagCursor;
    if (l || r)
        return Qt::SizeHorCursor;
    if (t || b)
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

void OverlayPanel::leaveEvent(QEvent *event)
{
    if (!m_resizing)
        unsetCursor();
    QFrame::leaveEvent(event);
}

void OverlayPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QFrame::mousePressEvent(event);
        return;
    }

    // 1) 优先判定边缘/四角缩放（收起按钮所在区域除外）
    const Qt::Edges edges = resizeEdgesAt(event->pos());
    if (edges
        && !(m_collapseButton
             && m_collapseButton->geometry().adjusted(-4, -4, 4, 4).contains(event->pos()))) {
        m_resizing = true;
        m_resizeEdges = edges;
        m_resizeStartGeometry = geometry();
        m_resizeStartGlobal = event->globalPosition().toPoint();
        event->accept();
        return;
    }

    // 2) 按住标题栏可拖动面板（拖动后不再受角落锚点约束）
    if (m_titleBar && m_titleBar->geometry().contains(event->pos())) {
        m_dragging = true;
        m_dragOffset = event->pos();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void OverlayPanel::mouseMoveEvent(QMouseEvent *event)
{
    // ---- 正在缩放：按拖动增量调整几何（左/上边需同步移动原点）----
    if (m_resizing && (event->buttons() & Qt::LeftButton) && m_container) {
        const QPoint delta = event->globalPosition().toPoint() - m_resizeStartGlobal;
        QRect g = m_resizeStartGeometry;

        // 最小尺寸由内容决定，保证缩放后仍可用
        const QSize minSz = minimumSizeHint().expandedTo(QSize(120, 40));
        const int maxW = qMax(minSz.width(), m_container->width());
        const int maxH = qMax(minSz.height(), m_container->height());

        if (m_resizeEdges & Qt::LeftEdge)
            g.setLeft(qBound(0, g.left() + delta.x(),
                             g.right() - minSz.width() + 1));
        else if (m_resizeEdges & Qt::RightEdge)
            g.setRight(qBound(g.left() + minSz.width() - 1,
                              g.right() + delta.x(), maxW - 1));

        if (m_resizeEdges & Qt::TopEdge)
            g.setTop(qBound(0, g.top() + delta.y(),
                            g.bottom() - minSz.height() + 1));
        else if (m_resizeEdges & Qt::BottomEdge)
            g.setBottom(qBound(g.top() + minSz.height() - 1,
                               g.bottom() + delta.y(), maxH - 1));

        // 尺寸变了就说明用户已手动调整过，脱离锚点约束
        m_dragDetached = true;
        setGeometry(g);

        // 右侧/下侧被容器边界顶住时，防止继续拖拽导致尺寸回弹
        m_resizeStartGeometry = geometry();
        m_resizeStartGlobal = event->globalPosition().toPoint();
        m_expandedSize = size();
        raise();
        event->accept();
        return;
    }

    if (m_dragging && (event->buttons() & Qt::LeftButton) && m_container) {
        m_dragDetached = true;
        QPoint target = mapTo(m_container, event->pos() - m_dragOffset);
        target.setX(qBound(0, target.x(), qMax(0, m_container->width() - width())));
        target.setY(qBound(0, target.y(), qMax(0, m_container->height() - height())));
        move(target);
        raise();
        event->accept();
        return;
    }

    // ---- 空闲时根据鼠标所在边缘切换指针形状 ----
    if (m_resizable && !m_collapsed) {
        const Qt::Edges edges = resizeEdgesAt(event->pos());
        const Qt::CursorShape shape = cursorForEdges(edges);
        if (shape == Qt::ArrowCursor)
            unsetCursor();
        else
            setCursor(shape);
    }

    QFrame::mouseMoveEvent(event);
}

void OverlayPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing && event->button() == Qt::LeftButton) {
        m_resizing = false;
        m_resizeEdges = Qt::Edges();
        m_expandedSize = size();
        // 交还指针形状给当前鼠标位置
        if (m_resizable) {
            const Qt::CursorShape shape = cursorForEdges(resizeEdgesAt(event->pos()));
            if (shape == Qt::ArrowCursor)
                unsetCursor();
            else
                setCursor(shape);
        }
        event->accept();
        return;
    }
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

} // namespace wafer
