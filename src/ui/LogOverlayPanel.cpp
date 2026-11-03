#include "ui/LogOverlayPanel.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace wafer {

namespace {
/** 日志级别对应的文字颜色（Info / Success / Warning / Error） */
const char *const kLevelColors[] = {"#8fb6d8", "#6ed08a", "#e8c35a", "#ff6b6b"};
} // namespace

LogOverlayPanel::LogOverlayPanel(QWidget *parent)
    : OverlayPanel(tr("运行日志"), parent)
{
    setupContent();

    // OverlayPanel 的初始尺寸按「不小于 minimumSizeHint、等于内容自然尺寸」的规则
    // 贴近下面给出的最小尺寸；用户仍可拖拽边缘缩放，或由已保存的布局恢复尺寸。
    setMinimumSize(380, 220);
    setInitialSizeScale(1.05f, 1.0f);

    // 跨线程安全：append() 只发信号，本对象槽函数在 UI 线程执行
    connect(this, &LogOverlayPanel::logRequested, this, &LogOverlayPanel::onLogRequested,
            Qt::QueuedConnection);
}

void LogOverlayPanel::setupContent()
{
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(6);

    m_filterBox = new QComboBox();
    m_filterBox->addItem(tr("全部"));
    m_filterBox->addItem(tr("仅警告及以上"));
    m_filterBox->addItem(tr("仅错误"));
    toolbar->addWidget(m_filterBox, 1);

    m_clearButton = new QPushButton(tr("清空"));
    toolbar->addWidget(m_clearButton);

    m_view = new QPlainTextEdit();
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(5000); // 防止长时间运行日志无限膨胀
    m_view->setMinimumWidth(160);
    // 日志区最小高度按「约 8 行正文」估算，保证默认尺寸下打开即可看到多条日志，
    // 而不是只露出 1~2 行（QPlainTextEdit 的 sizeHint 按行高推算，再叠加
    // OverlayPanel 的 setInitialSizeScale() 放大后会略高于该最小高度）。
    m_view->setMinimumHeight(qMax(48, m_view->fontMetrics().lineSpacing() * 8 + 12));
    m_view->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: rgba(9, 13, 19, 220); color: #cdd6e0; "
        "border: 1px solid rgba(96, 148, 196, 90); border-radius: 4px; "
        "font-family: Consolas, monospace; font-size: 12px; }"));

    auto *content = new QVBoxLayout();
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(6);
    content->addLayout(toolbar);
    content->addWidget(m_view, 1);
    setContentLayout(content);

    connect(m_clearButton, &QPushButton::clicked, this, &LogOverlayPanel::clearLog);
    connect(m_filterBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LogOverlayPanel::onFilterChanged);
}

void LogOverlayPanel::append(Level level, const QString &message)
{
    // 通过信号槽投递，保证在 UI 线程执行（后台线程调用安全）
    emit logRequested(static_cast<int>(level), message);
}

void LogOverlayPanel::appendInfo(const QString &message)
{
    append(Level::Info, message);
}

void LogOverlayPanel::appendSuccess(const QString &message)
{
    append(Level::Success, message);
}

void LogOverlayPanel::appendWarning(const QString &message)
{
    append(Level::Warning, message);
}

void LogOverlayPanel::appendError(const QString &message)
{
    append(Level::Error, message);
}

void LogOverlayPanel::appendFromLogger(int level, const QString &message)
{
    // core::Logger 只发出整数级别，这里做一次显式映射，保持 UI 侧语义清晰。
    switch (level) {
    case 0:
        append(Level::Info, message);
        break;
    case 1:
        append(Level::Success, message);
        break;
    case 2:
        append(Level::Warning, message);
        break;
    default:
        append(Level::Error, message);
        break;
    }
}

void LogOverlayPanel::clearLog()
{
    m_entries.clear();
    m_view->clear();
}

void LogOverlayPanel::onLogRequested(int level, const QString &message)
{
    Entry entry;
    entry.level = static_cast<Level>(level);
    entry.message = message;
    entry.timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));

    m_entries.push_back(entry);

    // 与 QPlainTextEdit::setMaximumBlockCount(5000) 对齐：
    // 视图有上限但 m_entries 只增不删时，长时间运行（工业上位机常连续开数天）
    // 会造成内存缓慢增长，而且失败状态下的过滤/重建会越来越慢。
    static constexpr int kMaxEntries = 5000;
    while (m_entries.size() > kMaxEntries)
        m_entries.removeFirst();

    const int filter = m_filterBox ? m_filterBox->currentIndex() : 0;
    bool visible = true;
    if (filter == 1)
        visible = static_cast<int>(entry.level) >= static_cast<int>(Level::Warning);
    else if (filter == 2)
        visible = entry.level == Level::Error;

    if (visible) {
        m_view->appendHtml(
            QStringLiteral("<span style='color:#5d6b7a'>[%1]</span> "
                           "<span style='color:%2'>%3</span>")
                .arg(entry.timestamp,
                     QString::fromLatin1(kLevelColors[static_cast<int>(entry.level)]),
                     message.toHtmlEscaped()));
    }
}

void LogOverlayPanel::onFilterChanged()
{
    rebuildView();
}

void LogOverlayPanel::rebuildView()
{
    m_view->clear();

    const int filter = m_filterBox ? m_filterBox->currentIndex() : 0;
    for (const Entry &entry : m_entries) {
        bool visible = true;
        if (filter == 1)
            visible = static_cast<int>(entry.level) >= static_cast<int>(Level::Warning);
        else if (filter == 2)
            visible = entry.level == Level::Error;
        if (!visible)
            continue;

        m_view->appendHtml(
            QStringLiteral("<span style='color:#5d6b7a'>[%1]</span> "
                           "<span style='color:%2'>%3</span>")
                .arg(entry.timestamp,
                     QString::fromLatin1(kLevelColors[static_cast<int>(entry.level)]),
                     entry.message.toHtmlEscaped()));
    }
}

} // namespace wafer
