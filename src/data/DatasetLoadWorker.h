#ifndef DATASETLOADWORKER_H
#define DATASETLOADWORKER_H

#include "core/DefectTypes.h"
#include "data/DataBufferManager.h"

#include <QObject>
#include <QString>

#include <atomic>

namespace wafer {

/**
 * @brief 后台数据加载 Worker（流程1：数据集异步加载）。
 *
 * 生命周期：由 UI 层创建一个 QThread，将本对象 moveToThread(thread)，
 * 通过 startLoad() 槽触发解析。解析完成/失败/进度均以信号回传 UI 层，
 * 再由 UI 层转发给业务层 publish 数据，绝不跨线程直接调用对象方法。
 *
 * 取消机制：requestCancel() 可在任意线程调用，仅置位原子标志；
 * 解析循环周期性检查该标志并安全提前返回，避免使用 QThread::terminate()
 * 导致的资源泄漏与未定义行为（企业级线程安全要求）。
 *
 * 本类不持有任何 OpenGL 资源，也不直接访问 UI。
 */
class DatasetLoadWorker : public QObject
{
    Q_OBJECT

public:
    explicit DatasetLoadWorker(DataBufferManager *bufferManager, QObject *parent = nullptr);
    ~DatasetLoadWorker() override;

    /** 请求取消（线程安全，可从 UI 线程直接调用） */
    void requestCancel() { m_cancelRequested.store(true, std::memory_order_release); }

    /** 是否已被请求取消 */
    bool isCancelRequested() const { return m_cancelRequested.load(std::memory_order_acquire); }

public slots:
    /** 执行加载（在子线程事件循环中被调用） */
    void loadDataset(const QString &filePath, float waferRadiusMm = 0.0f);

signals:
    /** 加载进度：已完成字节 / 总字节 */
    void progressChanged(int percent, const QString &stage);

    /** 解析成功：携带数据版本号；数据集本体请通过 DataBufferManager::snapshot() 获取 */
    void datasetReady(int version);

    /** 加载失败 */
    void loadFailed(const QString &errorMessage);

private:
    DataBufferManager *m_bufferManager = nullptr; // 不持有所有权
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace wafer

#endif // DATASETLOADWORKER_H
