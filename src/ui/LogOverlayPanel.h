#ifndef LOGOVERLAYPANEL_H
#define LOGOVERLAYPANEL_H

#include "ui/OverlayPanel.h"

#include <QVector>

class QPlainTextEdit;
class QComboBox;
class QPushButton;

namespace wafer {

/**
 * @brief 运行日志悬浮面板（停靠视口右上角）。
 *
 * 分级着色、级别过滤、清空，以及跨线程安全投递（append() 只发信号，
 * 槽函数在 UI 线程执行）。以悬浮卡片形式呈现，并额外提供收起/展开。
 *
 * 与 core::Logger 的关系：Logger 负责「控制台 + 落盘」，
 * 本面板负责「界面呈现」；两者由 MainWindow 通过 Logger::messageLogged
 * 信号连接，保证同一份日志既落盘又可见。
 */
class LogOverlayPanel : public OverlayPanel
{
    Q_OBJECT

public:
    enum class Level { Info = 0, Success, Warning, Error };

    explicit LogOverlayPanel(QWidget *parent = nullptr);

    /** 追加一条日志（线程安全：内部经信号槽转投到 UI 线程） */
    void append(Level level, const QString &message);

    /** 清空日志 */
    void clearLog();

public slots:
    void appendInfo(const QString &message);
    void appendSuccess(const QString &message);
    void appendWarning(const QString &message);
    void appendError(const QString &message);

    /**
     * @brief 接收 core::Logger 转发来的日志（level 见 Logger::messageLogged）。
     * 该槽与 Logger::messageLogged(int, QString) 直接连接，实现「日志面板 + 落盘」同源。
     */
    void appendFromLogger(int level, const QString &message);

signals:
    /** 内部桥接：允许从任意线程安全投递日志到 UI 线程 */
    void logRequested(int level, const QString &message);

private slots:
    void onLogRequested(int level, const QString &message);
    void onFilterChanged();

private:
    void setupContent();
    void rebuildView();

    QPlainTextEdit *m_view = nullptr;
    QComboBox *m_filterBox = nullptr;
    QPushButton *m_clearButton = nullptr;

    struct Entry {
        Level level = Level::Info;
        QString message;
        QString timestamp;
    };
    QVector<Entry> m_entries;
};

} // namespace wafer

#endif // LOGOVERLAYPANEL_H
