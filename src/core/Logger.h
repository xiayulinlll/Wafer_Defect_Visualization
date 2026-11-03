#ifndef LOGGER_H
#define LOGGER_H

#include <QLoggingCategory>
#include <QString>

namespace wafer {

// ---- 日志分类（供 qCDebug/qCInfo/qCWarning/qCCritical 使用） ----
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcData)
Q_DECLARE_LOGGING_CATEGORY(lcRender)
Q_DECLARE_LOGGING_CATEGORY(lcBusiness)

/**
 * @brief 应用级日志基础设施（企业级：可落盘、可分级、可被 UI 订阅）。
 *
 * 职责：
 *  - 安装 Qt 消息处理器，把 qDebug/qInfo/qWarning/qCritical 统一格式化；
 *  - 同时写入控制台与滚动日志文件（按启动时间命名，运行期单文件）；
 *  - 通过信号把日志转发给 UI（运行日志悬浮面板），实现「面板 + 文件」同源。
 *
 * 使用：
 * @code
 *   wafer::Logger::instance().initialize();   // main() 早期调用一次
 *   qCInfo(wafer::lcApp) << "系统启动";
 * @endcode
 *
 * 线程安全：Qt 消息处理器可能在任意线程触发，内部用互斥锁保护文件写入。
 */
class Logger : public QObject
{
    Q_OBJECT

public:
    static Logger &instance();

    /**
     * @brief 安装消息处理器并打开日志文件。
     * @param logDir     日志目录（默认为可执行文件旁 logs/）
     * @param toConsole  是否同时输出到 stderr
     * @return 日志文件路径；打开失败返回空
     */
    QString initialize(const QString &logDir = QString(), bool toConsole = true);

    /** 关闭日志文件并卸载消息处理器（进程退出前调用） */
    void shutdown();

    /** 当前日志文件路径 */
    QString logFilePath() const { return m_logFilePath; }

    /** 是否已安装 */
    bool isActive() const { return m_active; }

signals:
    /** 每条日志（throttled by 级别），供 UI 订阅 */
    void messageLogged(int level, const QString &message);

private:
    Logger();
    ~Logger() override;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &message);

    struct Private;
    Private *d = nullptr;
    QString m_logFilePath;
    bool m_active = false;
};

} // namespace wafer

#endif // LOGGER_H
