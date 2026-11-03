#ifndef DATASETCONTROLLER_H
#define DATASETCONTROLLER_H

#include "core/DefectTypes.h"
#include "data/DataBufferManager.h"

#include <QObject>
#include <QString>

class QThread;

namespace wafer {

class DatasetLoadWorker;

/**
 * @brief 数据集加载控制器（从 MainWindow 抽取的「加载线程 + 数据缓冲」职责）。
 *
 * 设计目标（对应企业级重构项：#12 拆分上帝类）：
 *  - 把「后台解析线程的创建 / 取消 / 回收」与「数据缓冲发布」从 UI 类中剥离，
 *    MainWindow 只负责按钮点击与状态展示；
 *  - 线程安全：所有对外的状态变更都通过信号投递回 UI 线程
 *    （loadProgress / datasetReady / loadFailed 均为队列连接）；
 *  - 取消策略：协作式取消（requestCancel），绝不使用 QThread::terminate()。
 *
 * 所有权：本对象由 MainWindow 以 unique_ptr 持有；QThread 以本对象为 parent，
 * 保证析构时线程对象随控制器一起回收。
 */
class DatasetController : public QObject
{
    Q_OBJECT

public:
    explicit DatasetController(DataBufferManager *bufferManager, QObject *parent = nullptr);
    ~DatasetController() override;

    /**
     * @brief 启动一次后台加载。
     *
     * 会先停止上一次未结束的加载（若有），再创建新的线程与 worker。
     * @param filePath 要解析的 CSV 文件路径
     * @return 是否成功启动（路径为空返回 false）
     */
    bool load(const QString &filePath);

    /**
     * @brief 停止后台加载线程并回收资源。
     *
     * 协作式取消：请求 worker 中止解析 -> quit() -> 等待自然退出；
     * 超时仅记录告警，绝不做强制终止，以免留下未释放的文件句柄。
     * @return 线程是否在等待窗口内正常退出
     */
    bool shutdown();

    /** 后台线程当前是否在运行 */
    bool isRunning() const;

    /** 数据缓冲的当前版本号（0 表示尚未加载过任何数据集） */
    int bufferVersion() const;

signals:
    /** 解析进度（0..100）与阶段文案 */
    void loadProgress(int percent, const QString &stage);
    /** 解析成功：携带新的数据版本号；数据集通过 DataBufferManager 快照读取 */
    void datasetReady(int version);
    /** 解析失败：携带错误信息 */
    void loadFailed(const QString &errorMessage);

private:
    DataBufferManager *m_bufferManager = nullptr; // 不持有所有权

    QThread *m_thread = nullptr;
    DatasetLoadWorker *m_worker = nullptr;
};

} // namespace wafer

#endif // DATASETCONTROLLER_H
