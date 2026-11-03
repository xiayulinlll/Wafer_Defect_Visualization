#ifndef GPURESOURCEMANAGER_H
#define GPURESOURCEMANAGER_H

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QOpenGLTexture;

namespace wafer {

/**
 * @brief GPU 资源管理器（渲染层，难点5：资源生命周期统一管理）。
 *
 * 所有 VAO / VBO / EBO / 纹理统一在本类登记，窗口销毁或数据集切换时
 * 按相反顺序集中释放，并输出释放日志供长期运行排查内存泄漏。
 *
 * 约束：所有 OpenGL 调用必须发生在渲染线程且上下文 current。
 */
class GpuResourceManager : protected QOpenGLFunctions_3_3_Core
{
public:
    /** 资源分类，用于统计与日志 */
    enum class Category {
        VertexArray = 0,
        ArrayBuffer,
        IndexBuffer,
        Texture,
        Framebuffer,
        Renderbuffer
    };

    GpuResourceManager();
    ~GpuResourceManager();

    GpuResourceManager(const GpuResourceManager &) = delete;
    GpuResourceManager &operator=(const GpuResourceManager &) = delete;

    /** 初始化 GL 函数指针；失败返回 false */
    bool initialize(QString *errorLog = nullptr);

    // ---------------- 创建 ----------------

    /** 创建一个已配置好的 VAO（attr 布局由调用方设置） */
    QOpenGLVertexArrayObject *createVertexArray(const QString &name);

    /** 创建一个已绑定 VBO 的 QOpenGLBuffer（VertexBuffer） */
    QOpenGLBuffer *createArrayBuffer(const QString &name);

    /** 创建一个索引缓冲（IndexBuffer） */
    QOpenGLBuffer *createIndexBuffer(const QString &name);

    /** 创建 2D 纹理；internalFormat/format 使用 GL 枚举值 */
    QOpenGLTexture *createTexture2D(const QString &name, int width, int height,
                                    unsigned int internalFormat, unsigned int format,
                                    unsigned int pixelType, const void *data);

    /**
     * @brief 创建一个 FBO 及其颜色附件纹理与深度 renderbuffer。
     * @param width/height  尺寸（像素）
     * @param outColorTex   输出颜色纹理（不持有所有权）
     * @param outFboId      输出 FBO 句柄
     * @param outDepthRbo   输出深度 renderbuffer 句柄
     */
    bool createColorFramebuffer(const QString &name, int width, int height,
                                unsigned int *outColorTex, unsigned int *outFboId,
                                unsigned int *outDepthRbo, QString *errorLog = nullptr);

    // ---------------- 释放 ----------------

    /** 释放并注销指定名称的 VAO */
    void destroyVertexArray(const QString &name);

    /** 释放并注销指定名称的 Buffer */
    void destroyBuffer(const QString &name);

    /** 释放并注销指定名称的纹理 */
    void destroyTexture(const QString &name);

    /**
     * @brief 按名称查找已登记的纹理（不转移所有权）。
     *
     * 供热力图这类「需要原地更新」的子模块使用：找到就 setData()，找不到再创建，
     * 避免每次数据变化都销毁/新建纹理对象。
     */
    QOpenGLTexture *findTexture(const QString &name) const;

    /** 释放并注销指定名称的 FBO（含附件） */
    void destroyFramebuffer(const QString &name);

    /** 释放全部 GPU 资源（窗口析构前调用） */
    void destroyAll(bool verbose = true);

    // ---------------- 查询 ----------------

    /** 当前登记的资源数量 */
    int resourceCount() const;

    /** 各分类数量统计文本（供 UI 与日志展示） */
    QString statisticsText() const;

    /** 最近一次释放的日志（含释放顺序），便于排查泄漏 */
    QString lastReleaseLog() const { return m_lastReleaseLog; }

    /** 显式注册一个纹理句柄（外部创建但希望纳入管理，如 QOpenGLTexture 已接管） */
    void registerExternalTexture(const QString &name, QOpenGLTexture *texture, bool takeOwnership);

private:
    struct Entry {
        QString name;
        Category category;
        QOpenGLVertexArrayObject *vao = nullptr;   // 拥有
        QOpenGLBuffer *buffer = nullptr;           // 拥有
        QOpenGLTexture *texture = nullptr;         // 拥有
        unsigned int fboId = 0;
        unsigned int depthRbo = 0;
        unsigned int colorTex = 0;
    };

    std::vector<Entry> m_entries;
    std::vector<QString> m_creationOrder;
    QString m_lastReleaseLog;
    bool m_initialized = false;
    int m_totalCreated = 0;
    int m_totalDestroyed = 0;
};

} // namespace wafer

#endif // GPURESOURCEMANAGER_H
