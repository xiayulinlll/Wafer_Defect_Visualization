#ifndef FBORENDERER_H
#define FBORENDERER_H

#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <QSize>
#include <QString>

namespace wafer {

class GpuResourceManager;

/**
 * @brief FBO 离屏渲染子模块（流程4：局部区域截图导出）。
 *
 * 提供两种能力：
 *  1. 屏幕区域捕获：把默认帧缓冲中指定矩形区域的像素读出并保存为图片
 *     （最稳定，与用户所见完全一致）；
 *  2. 离屏渲染目标：提供一个 FBO（size 可调），把场景渲染进纹理后再
 *     用全屏 blit shader 拷贝到屏幕或读回 —— 用于"放大预览"与
 *     "高分辨率报告导出"（可渲染出比窗口更大的图）。
 */
class FboRenderer : protected QOpenGLFunctions_3_3_Core
{
public:
    FboRenderer();
    ~FboRenderer();

    FboRenderer(const FboRenderer &) = delete;
    FboRenderer &operator=(const FboRenderer &) = delete;

    /** 初始化：创建 GL 函数指针与 FBO 资源（默认尺寸） */
    bool initialize(GpuResourceManager *resourceManager, const QSize &size, QString *errorLog);

    /** 确保 FBO 尺寸；尺寸变化会重建附件 */
    bool ensureSize(const QSize &size, QString *errorLog = nullptr);

    /** 绑定为渲染目标（保存/恢复由调用方通过 begin/end 配对） */
    void beginOffscreen();
    /** 解绑，恢复到默认帧缓冲（屏幕） */
    void endOffscreen();

    /** 当前 FBO 句柄与颜色纹理句柄 */
    unsigned int framebufferId() const { return m_fbo; }
    unsigned int colorTextureId() const { return m_colorTex; }
    QSize size() const { return m_size; }

    /**
     * @brief 从 FBO 颜色附件读回像素。
     * @return RGBA8 格式的 QImage（上下翻转以符合 Qt 坐标）
     *
     * @note 非 const：内部会调用 QOpenGLFunctions 的 GL 接口（其成员函数非 const），
     *       并且确实会改变 GL 绑定状态。
     */
    QImage readBackOffscreen();

    /**
     * @brief 从默认帧缓冲（屏幕）读回指定矩形区域的像素。
     * @param rect         区域（widget 坐标，原点左上）
     * @param viewportSize 当前视口尺寸（widget 坐标）
     * @param dpr          设备像素比；HiDPI 下按物理像素读回，保证不模糊
     *
     * 说明：直接 `glReadPixels` 目标区域（只读选区像素），不再整屏读回后裁剪，
     * 既省带宽也避免大窗口下的 4K 级临时缓冲。
     */
    QImage grabScreenRegion(const QRect &rect, const QSize &viewportSize, float dpr = 1.0f);

    /**
     * @brief 用全屏三角形把 FBO 颜色纹理拷贝到当前绑定的目标。
     * @param shaderProgramId 已链接的 blit program（由 ShaderManager 提供）
     */
    void blitToCurrentTarget(unsigned int shaderProgramId, bool flipY);

    /** 释放 FBO 相关资源（上下文有效时调用） */
    void destroy();

    bool isValid() const { return m_fbo != 0; }

    /** 把 QImage 保存为 PNG/JPG，自动补全扩展名 */
    static bool saveImage(const QImage &image, const QString &filePath, QString *errorMessage);

private:
    GpuResourceManager *m_resources = nullptr; // 不持有所有权
    unsigned int m_fbo = 0;
    unsigned int m_colorTex = 0;
    unsigned int m_depthRbo = 0;
    QSize m_size;
    bool m_initialized = false;
    unsigned int m_blitVao = 0; // 空 VAO，满足 Core Profile 必需绑定 VAO 的要求
};

} // namespace wafer

#endif // FBORENDERER_H
