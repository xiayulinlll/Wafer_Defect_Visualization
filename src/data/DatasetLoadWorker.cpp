#include "data/DatasetLoadWorker.h"
#include "data/CsvDatasetParser.h"

#include "core/Logger.h"

namespace wafer {

DatasetLoadWorker::DatasetLoadWorker(DataBufferManager *bufferManager, QObject *parent)
    : QObject(parent)
    , m_bufferManager(bufferManager)
{
}

DatasetLoadWorker::~DatasetLoadWorker() = default;

void DatasetLoadWorker::loadDataset(const QString &filePath, float waferRadiusMm)
{
    // 以下全部在子线程执行，IO 与解析耗时不会阻塞 UI 与渲染线程
    emit progressChanged(5, QStringLiteral("打开文件..."));

    qCInfo(lcData) << "开始解析数据集" << filePath;

    // 协作式取消：解析循环会周期性回调 isCancelRequested()，避免强制杀线程
    const CsvDatasetParser::Result res =
        CsvDatasetParser::parseFile(filePath, waferRadiusMm,
                                    [this]() { return isCancelRequested(); });

    if (isCancelRequested()) {
        qCWarning(lcData) << "数据集加载已被取消：" << filePath;
        emit loadFailed(QStringLiteral("加载已取消"));
        return;
    }

    emit progressChanged(80, QStringLiteral("解析完成，写入数据缓存..."));

    if (!res.ok) {
        qCWarning(lcData) << "数据集解析失败：" << res.errorMessage;
        emit loadFailed(res.errorMessage);
        return;
    }

    // 双缓冲发布：原子切换，渲染线程下一帧即可读到新数据。
    // 用 move 语义移交数据集，避免 worker 侧数据被再复制一份（10 万点约 4 MB+）。
    if (!m_bufferManager) {
        emit loadFailed(QStringLiteral("数据缓冲未初始化"));
        return;
    }

    const int version = m_bufferManager->publish(std::move(res.dataset));
    const int pointCount = m_bufferManager->pointCount();

    qCInfo(lcData) << "数据集解析完成，缺陷点数量：" << pointCount << "版本：" << version;

    emit progressChanged(100, QStringLiteral("数据就绪"));

    // 注意：不再把整份数据集随信号跨线程投递（双重拷贝），
    // 消费者通过 DataBufferManager::snapshot() 直接拿到同一份共享数据。
    emit datasetReady(version);
}

} // namespace wafer
