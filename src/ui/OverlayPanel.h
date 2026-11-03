#ifndef OVERLAYPANEL_H
#define OVERLAYPANEL_H

#include <QFrame>
#include <QPoint>
#include <QRect>
#include <Qt>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;

namespace wafer {

/**
 * @brief 悬浮面板基类（UI 层的通用"HUD 卡片"）。
 *
 * 设计要点：
 *  - 构造时 parent 传主窗口（或任意"定位容器"），随后由 attach() 把自身
 *    挂到容器上并保持固定在某个角；
 *  - 面板不参与父控件布局，而是以绝对位置定位，因此可以悬浮在 3D 视口之上；
 *  - 半透明深色卡片 + 圆角 + 细边框，与深色渲染背景协调；
 *  - 标题栏可拖动（可选），并可提供收起/展开按钮，收起后只剩标题条。
 *
 * 用法：
 * @code
 *   auto *panel = new OverlayPanel("缺陷等级分布", container);
 *   panel->setContentLayout(layout);
 *   panel->attach(container, OverlayPanel::Anchor::TopLeft);
 * @endcode
 */
class OverlayPanel : public QFrame
{
    Q_OBJECT

public:
    /** 悬浮停靠位置（距离容器对应角落的固定边距） */
    enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight };

    explicit OverlayPanel(const QString &title, QWidget *parent = nullptr);

    /** 装载内容布局（面板内部标题栏之下、可折叠区域之上的部分） */
    void setContentLayout(QLayout *layout);

    /** 把面板挂到 container 上并锚定到指定角落；会同步更新跟随的几何尺寸 */
    void attach(QWidget *container, Anchor anchor, const QPoint &margin = QPoint(12, 12));

    /** 重新计算自身位置（容器尺寸变化时调用） */
    void reposition();

    /** 该面板当前所处的定位容器 */
    QWidget *container() const { return m_container; }

    /** 折叠/展开内容区，仅保留标题栏 */
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    /** 面板内容区（标题栏下方的容器，供子类继续追加控件） */
    QVBoxLayout *contentLayout() const { return m_contentLayout; }

    /**
     * 设定初始尺寸（应在 attach() 之前调用）。
     * 传入的比例会乘在面板"内容自然尺寸"上，便于子类把面板整体调大。
     */
    void setInitialSizeScale(float widthScale, float heightScale);

    /** 允许用户拖拽边缘/四角缩放（默认开启） */
    void setResizable(bool on) { m_resizable = on; }
    bool isResizable() const { return m_resizable; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void setupUi(const QString &title);
    void applyOverlayStyle();

    /** 可拖拽缩放的"热区"（边缘/四角的判定宽度，像素） */
    static constexpr int kResizeGrip = 6;

    /** 当前鼠标位置对应的缩放方向（不在边缘时返回 0） */
    Qt::Edges resizeEdgesAt(const QPoint &pos) const;

    /** 根据缩放方向返回对应鼠标指针 */
    static Qt::CursorShape cursorForEdges(Qt::Edges edges);

    /** 由内容计算"自然尺寸"，再乘上初始缩放比例得到建议尺寸 */
    QSize preferredSize() const;

    QWidget *m_container = nullptr;
    Anchor m_anchor = Anchor::TopLeft;
    QPoint m_margin{12, 12};

    QVBoxLayout *m_rootLayout = nullptr;
    QHBoxLayout *m_titleLayout = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QWidget *m_contentHost = nullptr;
    QWidget *m_titleBar = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_collapseButton = nullptr;

    bool m_collapsed = false;
    QSize m_expandedSize;

    // 拖动状态（按住标题栏移动面板，可选择脱离固定锚点）
    bool m_dragging = false;
    bool m_dragDetached = false;
    QPoint m_dragOffset;

    // 缩放状态（拖拽边缘/四角改变面板大小）
    bool m_resizable = true;
    bool m_resizing = false;
    Qt::Edges m_resizeEdges;
    QRect m_resizeStartGeometry;
    QPoint m_resizeStartGlobal;

    // 初始尺寸放大比例（相对内容自然尺寸）
    float m_widthScale = 1.0f;
    float m_heightScale = 1.0f;
};

} // namespace wafer

#endif // OVERLAYPANEL_H
