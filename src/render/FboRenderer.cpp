#include "render/FboRenderer.h"
#include "core/Constants.h"
#include "render/GpuResourceManager.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace wafer {

namespace {
const char *const kFboName = "OffscreenFbo";
const char *const kBlitVaoName = "BlitQuadVAO";
} // namespace

FboRenderer::FboRenderer() = default;

FboRenderer::~FboRenderer()
{
    // 资源由 GpuResourceManager 统一释放
    m_fbo = m_colorTex = m_depthRbo = 0;
    m_blitVao = 0;
}

bool FboRenderer::initialize(GpuResourceManager *resourceManager, const QSize &size,
                             QString *errorLog)
{
    if (!resourceManager) {
        if (errorLog)
            *errorLog += QStringLiteral("[FboRenderer] resourceManager 为空\n");
        return false;
    }

    if (!initializeOpenGLFunctions()) {
        if (errorLog)
            *errorLog += QStringLiteral("[FboRenderer] OpenGL 3.3 函数指针初始化失败\n");
        return false;
    }

    m_resources = resourceManager;
    m_initialized = true;

    // Core Profile 下即使不使用顶点属性，也必须绑定一个 VAO 才能 drawArrays
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    m_blitVao = vao;

    return ensureSize(size, errorLog);
}

bool FboRenderer::ensureSize(const QSize &size, QString *errorLog)
{
    if (!m_initialized || !m_resources)
        return false;

    // 上限保护：超大导出（例如 8K 屏全窗口）会让 RGBA8 + 深度 附件占用数百 MB 显存，
    // 这里按常数上限等比缩小，保证「宁可分辨率低一点，也不要分配失败」。
    QSize wanted(std::max(16, size.width()), std::max(16, size.height()));
    const int longest = std::max(wanted.width(), wanted.height());
    if (longest > constants::kMaxExportDimension) {
        const double s = static_cast<double>(constants::kMaxExportDimension) / longest;
        wanted = QSize(std::max(16, static_cast<int>(std::lround(wanted.width() * s))),
                       std::max(16, static_cast<int>(std::lround(wanted.height() * s))));
    }

    if (m_fbo != 0 && wanted == m_size)
        return true;

    // 尺寸变化：先释放旧的 FBO 资源，再重建（避免附件尺寸不一致）
    if (m_fbo != 0) {
        m_resources->destroyFramebuffer(QString::fromLatin1(kFboName));
        m_fbo = m_colorTex = m_depthRbo = 0;
    }

    QString log;
    const bool ok = m_resources->createColorFramebuffer(QString::fromLatin1(kFboName),
                                                        wanted.width(), wanted.height(), &m_colorTex,
                                                        &m_fbo, &m_depthRbo, &log);
    if (!ok) {
        if (errorLog)
            *errorLog += log;
        m_fbo = m_colorTex = m_depthRbo = 0;
        return false;
    }

    m_size = wanted;
    return true;
}

void FboRenderer::beginOffscreen()
{
    if (m_fbo == 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_size.width(), m_size.height());
}

void FboRenderer::endOffscreen()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

QImage FboRenderer::readBackOffscreen()
{
    if (m_fbo == 0 || m_size.isEmpty())
        return QImage();

    const int w = m_size.width();
    const int h = m_size.height();
    QImage image(w, h, QImage::Format_RGBA8888);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // OpenGL 原点在左下，Qt 在左上，需要翻转
    return image.mirrored(false, true);
}

QImage FboRenderer::grabScreenRegion(const QRect &rect, const QSize &viewportSize, float dpr)
{
    if (viewportSize.isEmpty())
        return QImage();

    // 把 widget 坐标系(左上原点)的矩形裁剪到 viewport 内
    const QRect clipped = rect.intersected(QRect(QPoint(0, 0), viewportSize));
    if (clipped.width() <= 0 || clipped.height() <= 0)
        return QImage();

    const float scale = dpr > 0.0f ? dpr : 1.0f;
    const int px = static_cast<int>(std::lround(clipped.x() * scale));
    const int py = static_cast<int>(std::lround(clipped.y() * scale));
    const int pw = std::max(1, static_cast<int>(std::lround(clipped.width() * scale)));
    const int ph = std::max(1, static_cast<int>(std::lround(clipped.height() * scale)));
    const int vpH = static_cast<int>(std::lround(viewportSize.height() * scale));

    // OpenGL 原点在左下：把「左上原点」的 y 换算成 GL 的行偏移
    const int glY = std::max(0, vpH - (py + ph));

    QImage region(pw, ph, QImage::Format_RGBA8888);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(px, glY, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, region.bits());

    // 帧缓冲自下而上，Qt 图像自上而下，需要垂直翻转
    return region.mirrored(false, true);
}

void FboRenderer::blitToCurrentTarget(unsigned int shaderProgramId, bool flipY)
{
    if (shaderProgramId == 0 || m_colorTex == 0 || m_blitVao == 0)
        return;

    glUseProgram(shaderProgramId);

    // 绑定 FBO 颜色纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glUniform1i(glGetUniformLocation(shaderProgramId, "uSource"), 0);
    glUniform1i(glGetUniformLocation(shaderProgramId, "uFlipY"), flipY ? 1 : 0);
    glUniform1i(glGetUniformLocation(shaderProgramId, "uGrayscale"), 0);
    glUniform1f(glGetUniformLocation(shaderProgramId, "uExposure"), 1.0f);

    // 全屏三角形（shader 用 gl_VertexID 生成顶点，无需 VBO）
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_blitVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void FboRenderer::destroy()
{
    if (m_resources)
        m_resources->destroyFramebuffer(QString::fromLatin1(kFboName));

    if (m_blitVao != 0) {
        glDeleteVertexArrays(1, &m_blitVao);
        m_blitVao = 0;
    }

    m_fbo = m_colorTex = m_depthRbo = 0;
    m_initialized = false;
}

bool FboRenderer::saveImage(const QImage &image, const QString &filePath, QString *errorMessage)
{
    if (image.isNull()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("待保存图像为空");
        return false;
    }

    QString path = filePath;
    // 自动补全扩展名
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
        path += QStringLiteral(".png");

    // 确保目录存在
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建目录：%1").arg(dir.absolutePath());
        return false;
    }

    const QString fmt = QFileInfo(path).suffix().toLower() == QLatin1String("jpg")
                            || QFileInfo(path).suffix().toLower() == QLatin1String("jpeg")
                            ? QStringLiteral("JPEG")
                            : QStringLiteral("PNG");

    if (!image.save(path, fmt.toUtf8().constData())) {
        if (errorMessage)
            *errorMessage = QStringLiteral("图像写入失败：%1").arg(path);
        return false;
    }

    return true;
}

} // namespace wafer
