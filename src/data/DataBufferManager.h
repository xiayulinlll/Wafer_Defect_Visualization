#ifndef DATABUFFERMANAGER_H
#define DATABUFFERMANAGER_H

#include "core/DefectTypes.h"

#include <QSharedPointer>

#include <atomic>
#include <memory>

namespace wafer {

/**
 * @brief 双缓冲数据缓存管理器（难点1：跨线程无锁读写分离）。
 *
 * 设计：
 *  - 后台线程（生产者）构造一个全新的 DefectDataset，交给 publish()；
 *  - publish() 使用 std::atomic_store 原子替换共享指针，渲染/业务线程
 *    通过 snapshot() 使用 std::atomic_load 获取当前指针；
 *  - 两个线程各自持有 shared_ptr，旧数据在最后一个引用释放后自动析构；
 *  - 由于 DefectDataset 一旦 publish 就视为不可变，因此整个读路径无需加锁，
 *    不会出现"渲染中数组被修改"的竞态。
 *
 * 注意：本类只管理 CPU 侧数据；GPU 资源由渲染层的 GpuResourceManager 管理。
 */
class DataBufferManager
{
public:
    using DatasetPtr = QSharedPointer<const DefectDataset>;

    DataBufferManager() = default;
    ~DataBufferManager() = default;

    DataBufferManager(const DataBufferManager &) = delete;
    DataBufferManager &operator=(const DataBufferManager &) = delete;

    /**
     * @brief 发布新数据集（生产者线程调用），原子切换当前缓冲区。
     * @param dataset 新数据集（内部转为 const 共享指针）
     * @return 版本号（自增）
     */
    int publish(const DefectDataset &dataset);

    /**
     * @brief 移交式发布：直接接管调用方构造好的数据集，**不做任何深拷贝**。
     *
     * 后台解析完成后数据只属于 worker 线程，move 进来即可避免 10 万点级别
     * 数十 MB 的整份复制。发布后 dataset 处于有效但未指定状态。
     * @return 版本号（自增）
     */
    int publish(DefectDataset &&dataset);

    /** 直接发布一个只读共享指针（调用方已构造好；为空表示清空） */
    int publish(std::shared_ptr<const DefectDataset> dataset);

    /**
     * @brief 获取当前数据集只读快照（消费者线程调用）。
     * 返回的 shared_ptr 保证在持有期间数据不会被析构。
     */
    DatasetPtr snapshot() const;

    /** 当前版本号；每次 publish +1，可用于 UI 判断是否需要重建几何 */
    int version() const { return m_version.load(std::memory_order_acquire); }

    /** 当前数据点数量（无锁查询，供 UI 状态栏使用） */
    int pointCount() const;

    /** 清空（窗口析构或重新加载前调用） */
    void clear();

private:
    mutable std::shared_ptr<const DefectDataset> m_buffer; // 由 atomic_* 系列函数访问
    std::atomic<int> m_version{0};
    std::atomic<int> m_pointCount{0};
};

} // namespace wafer

#endif // DATABUFFERMANAGER_H
