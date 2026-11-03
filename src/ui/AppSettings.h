#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QSet>
#include <QString>

namespace wafer {

/**
 * @brief 用户界面状态持久化（§3-9：QSettings 记住上次的数据集、相机、筛选与开关）。
 *
 * 为什么不做成静态单例：
 *  - 相机状态分别由 MainWindow（UI 开关/筛选）与 WaferViewport（鼠标交互）修改，
 *    两边必须写进**同一个** QSettings，否则后写的一方会覆盖先写的一方；
 *  - 用一个显式的共享对象注入到两处，生命周期与主窗口一致，析构时统一 flush。
 *
 * 存储位置：`QSettings::IniFormat`，platform 默认路径（Windows 为
 * `%APPDATA%\<组织>\<应用>\<应用>.ini`），因此不污染安装目录，也便于人工查看。
 */
class AppSettings : public QObject
{
    Q_OBJECT

public:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings() override;

    /**
     * @brief 绑定应用名/组织名（`main.cpp` 里的 `setApplicationName` 可能**晚于**
     *        本对象构造，因此不能在构造函数里固化）。
     *
     * 绑定后会迁移已有键，避免「同名程序换了组织名就读不到旧配置」。
     */
    void bindApplicationIdentity();

    /** 相机状态 */
    struct CameraState {
        float yawDeg = -35.0f;
        float pitchDeg = 35.0f;
        float distance = 3.2f;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float targetZ = 0.0f;
        bool valid = false;
    };

    /** 筛选状态（与 DefectFilter 一一对应，全部用简单类型以便 QSettings 存取） */
    struct FilterState {
        int minSeverity = 1;
        int maxSeverity = 5;
        QSet<int> types;   // 空 = 全部
        QSet<int> layers;  // 空 = 全部
        float minArea = -1.0f;
        float maxArea = -1.0f;
        float minHeight = -1.0f;
        float maxHeight = -1.0f;
        bool layerOffset = false;
    };

    /** 渲染开关 */
    struct RenderState {
        bool heatmap = true;
        bool markers = true;
        bool wafer = true;
        float zScale = 1.0f;
        bool captureMode = false;
        bool autoCapture = false;
    };

    // ---- 读写（读取失败/不存在时返回默认值，绝不抛异常） ----
    QString lastDatasetPath() const;
    void setLastDatasetPath(const QString &path);

    CameraState loadCamera() const;
    void saveCamera(const CameraState &state);

    FilterState loadFilter() const;
    void saveFilter(const FilterState &state);

    RenderState loadRender() const;
    void saveRender(const RenderState &state);

    /** 立即落盘（析构时自动调用） */
    void sync();

private:
    class Impl;
    Impl *m_impl = nullptr;
};

} // namespace wafer

#endif // APPSETTINGS_H
