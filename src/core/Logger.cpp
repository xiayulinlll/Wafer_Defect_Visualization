#include "core/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <atomic>
#include <cstdio>

namespace wafer {

Q_LOGGING_CATEGORY(lcApp, "wafer.app")
Q_LOGGING_CATEGORY(lcData, "wafer.data")
Q_LOGGING_CATEGORY(lcRender, "wafer.render")
Q_LOGGING_CATEGORY(lcBusiness, "wafer.business")

namespace {
/** "成功" 级日志的固定前缀：UI 侧用 qCInfo + 该前缀表达成功事件（Qt 无 Success 级别） */
const char kSuccessPrefix[] = "[OK] ";

/**
 * 日志级别枚举（与 UI 面板的 Level 对齐：Info=0/Success=1/Warning=2/Error=3）
 *
 * UI 层统一走 qCInfo/qCWarning/qCCritical 单一路径（避免「直接写面板 + 订阅」双写），
 * 而 Qt 没有 Success 级别，因此用 `[OK] ` 前缀约定把 Info 提升为 Success。
 */
int uiLevelFor(QtMsgType type, const QString &message)
{
    switch (type) {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return message.startsWith(QLatin1String(kSuccessPrefix)) ? 1 : 0;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
    case QtFatalMsg:
        return 3;
    }
    return 0;
}

QString levelTag(QtMsgType type, const QString &message)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return message.startsWith(QLatin1String(kSuccessPrefix)) ? QStringLiteral("OK   ")
                                                                 : QStringLiteral("INFO ");
    case QtWarningMsg: return QStringLiteral("WARN ");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("INFO ");
}
} // namespace

struct Logger::Private {
    QFile file;
    QTextStream stream;
    QMutex mutex;
    bool toConsole = true;
    Logger *self = nullptr;
};

Logger::Logger()
    : d(new Private)
{
    d->self = this;
}

Logger::~Logger()
{
    shutdown();
    delete d;
    d = nullptr;
}

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext &context,
                            const QString &message)
{
    Logger &self = instance();

    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString cat = QString::fromLatin1(context.category ? context.category : "default");
    const QString line = QStringLiteral("[%1] [%2] [%3] %4")
                             .arg(ts, levelTag(type, message), cat, message);

    {
        QMutexLocker locker(&self.d->mutex);
        if (self.d->file.isOpen()) {
            self.d->stream << line << '\n';
            self.d->stream.flush();
        }
        if (self.d->toConsole)
            std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    }

    // 转发给 UI（跨线程安全：信号槽自动排队）
    emit self.messageLogged(uiLevelFor(type, message), message);

    if (type == QtFatalMsg) {
        self.d->stream.flush();
        std::abort();
    }
}

QString Logger::initialize(const QString &logDir, bool toConsole)
{
    if (m_active)
        return m_logFilePath;

    QString dir = logDir;
    if (dir.isEmpty()) {
        const QString base = QCoreApplication::applicationDirPath();
        dir = QDir(base).absoluteFilePath(QStringLiteral("logs"));
    }
    QDir().mkpath(dir);

    const QString fileName = QStringLiteral("wafer_%1.log")
                                 .arg(QDateTime::currentDateTime().toString(
                                     QStringLiteral("yyyyMMdd_HHmmss")));
    m_logFilePath = QDir(dir).absoluteFilePath(fileName);

    d->file.setFileName(m_logFilePath);
    if (!d->file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        m_logFilePath.clear();
        m_active = false;
        return QString();
    }
    d->stream.setDevice(&d->file);
    d->toConsole = toConsole;

    qInstallMessageHandler(&Logger::messageHandler);
    m_active = true;

    qCInfo(lcApp) << "日志系统已启动，日志文件：" << m_logFilePath;
    return m_logFilePath;
}

void Logger::shutdown()
{
    if (!m_active)
        return;

    qInstallMessageHandler(nullptr);

    {
        // d 可能已析构（静态析构顺序），需判空
        if (d) {
            QMutexLocker locker(&d->mutex);
            if (d->stream.device())
                d->stream.flush();
            if (d->file.isOpen())
                d->file.close();
        }
    }
    m_active = false;
}

} // namespace wafer
