#include "data/DataBufferManager.h"

#include <memory>
#include <utility>

namespace wafer {

int DataBufferManager::publish(const DefectDataset &dataset)
{
    // 构造一个全新的不可变对象，再原子替换指针。
    // 使用 shared_ptr 而非裸指针 + delete：旧数据可能仍被渲染线程持有，
    // 只有当所有 snapshot() 引用释放后才会析构，天然避免悬垂。
    return publish(std::make_shared<const DefectDataset>(dataset));
}

int DataBufferManager::publish(DefectDataset &&dataset)
{
    // 移交所有权：worker 线程解析完成后数据只属于它自己，直接 move 进
    // 共享对象即可，省掉一次 O(N) 的深拷贝（10 万点约 4 MB+）。
    auto next = std::make_shared<DefectDataset>(std::move(dataset));
    return publish(std::shared_ptr<const DefectDataset>(next));
}

int DataBufferManager::publish(std::shared_ptr<const DefectDataset> dataset)
{
    std::atomic_store_explicit(&m_buffer, dataset, std::memory_order_release);

    m_pointCount.store(dataset ? dataset->points.size() : 0, std::memory_order_release);
    return m_version.fetch_add(1, std::memory_order_acq_rel) + 1;
}

DataBufferManager::DatasetPtr DataBufferManager::snapshot() const
{
    // atomic_load 与 publish 的 atomic_store 配对，形成 release/acquire 关系，
    // 保证读到的 Dataset 内容完整可见（happens-before）。
    std::shared_ptr<const DefectDataset> cur =
        std::atomic_load_explicit(&m_buffer, std::memory_order_acquire);

    if (!cur)
        return DatasetPtr();

    // QSharedPointer 不支持别名构造，这里用自定义删除器捕获 std::shared_ptr，
    // 使返回的 QSharedPointer 与 cur 共用同一份所有权：
    // 只要任一方仍持有引用，底层 DefectDataset 就不会被析构。
    return DatasetPtr(cur.get(),
                      [keep = cur](const DefectDataset *) mutable { keep.reset(); });
}

int DataBufferManager::pointCount() const
{
    return m_pointCount.load(std::memory_order_acquire);
}

void DataBufferManager::clear()
{
    std::shared_ptr<const DefectDataset> empty;
    std::atomic_store_explicit(&m_buffer, empty, std::memory_order_release);
    m_pointCount.store(0, std::memory_order_release);
}

} // namespace wafer
