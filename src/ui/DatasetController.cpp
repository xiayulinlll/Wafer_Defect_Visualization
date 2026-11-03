#include "ui/DatasetController.h"

#include "core/Logger.h"
#include "data/DatasetLoadWorker.h"

#include <QThread>

namespace wafer {

DatasetController::DatasetController(DataBufferManager *bufferManager, QObject *parent)
    : QObject(parent)
    , m_bufferManager(bufferManager)
{
}

DatasetController::~DatasetController()
{
    shutdown();
}

bool DatasetController::load(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    // 上一次加载若尚未结束，先协作式停止，避免两个 worker 同时写缓冲区
    shutdown();

    m_thread = new QThread(this);
    m_worker = new DatasetLoadWorker(m_bufferManager);

    connect(m_thread, &QThread::started, m_worker, [this, filePath]() {
        m_worker->loadDataset(filePath);
        // 单次任务完成后请求线程退出（事件循环安全退出）
        m_thread->quit();
    });

    // 全部结果信号队列投递，保证 UI 线程接收
    connect(m_worker, &DatasetLoadWorker::progressChanged, this,
            &DatasetController::loadProgress, Qt::QueuedConnection);
    connect(m_worker, &DatasetLoadWorker::datasetReady, this,
            &DatasetController::datasetReady, Qt::QueuedConnection);
    connect(m_worker, &DatasetLoadWorker::loadFailed, this, &DatasetController::loadFailed,
            Qt::QueuedConnection);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_worker->moveToThread(m_thread);
    m_thread->start();
    return true;
}

bool DatasetController::shutdown()
{
    if (!m_thread)
        return true;

    bool cleanExit = true;

    if (m_thread->isRunning()) {
        // 协作式取消：先请求 worker 主动中止解析，再等待线程自然退出。
        // 绝不使用 QThread::terminate()——它会在线程任意点强杀，
        // 可能导致文件句柄泄漏、互斥量处于锁定态等未定义行为。
        if (m_worker)
            m_worker->requestCancel();

        m_thread->quit();
        if (!m_thread->wait(5000)) {
            qCWarning(lcApp) << "加载线程未在 5 秒内退出，继续等待（不做强制终止）";
            // 再给一次较长等待窗口，超时后仍不 terminate：
            // 宁可延迟退出，也不让后台线程留下未释放资源。
            if (!m_thread->wait(15000)) {
                qCCritical(lcApp) << "加载线程长时间未退出，可能在解析超大文件";
                cleanExit = false;
            }
        }
    }

    m_thread->deleteLater();
    m_thread = nullptr;
    m_worker = nullptr;
    return cleanExit;
}

bool DatasetController::isRunning() const
{
    return m_thread && m_thread->isRunning();
}

int DatasetController::bufferVersion() const
{
    return m_bufferManager ? m_bufferManager->version() : 0;
}

} // namespace wafer
