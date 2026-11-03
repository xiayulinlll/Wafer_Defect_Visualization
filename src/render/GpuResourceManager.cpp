#include "render/GpuResourceManager.h"

#include <QDebug>
#include <QOpenGLTexture>

#include <algorithm>

namespace wafer {

GpuResourceManager::GpuResourceManager() = default;

GpuResourceManager::~GpuResourceManager()
{
    // 注意：析构时通常上下文已失效。资源应通过 destroyAll() 在
    // aboutToBeDestroyed 中已释放；此处仅做兜底断言式清理（避免二次 delete）。
    m_entries.clear();
}

bool GpuResourceManager::initialize(QString *errorLog)
{
    if (m_initialized)
        return true;

    if (!initializeOpenGLFunctions()) {
        if (errorLog)
            *errorLog += QStringLiteral("[GpuResourceManager] OpenGL 3.3 函数指针初始化失败\n");
        return false;
    }

    m_initialized = true;
    return true;
}

QOpenGLVertexArrayObject *GpuResourceManager::createVertexArray(const QString &name)
{
    auto *vao = new QOpenGLVertexArrayObject();
    if (!vao->create()) {
        delete vao;
        return nullptr;
    }

    Entry e;
    e.name = name;
    e.category = Category::VertexArray;
    e.vao = vao;
    m_entries.push_back(e);

    m_creationOrder.push_back(name);
    ++m_totalCreated;
    return vao;
}

QOpenGLBuffer *GpuResourceManager::createArrayBuffer(const QString &name)
{
    auto *buf = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    if (!buf->create()) {
        delete buf;
        return nullptr;
    }

    Entry e;
    e.name = name;
    e.category = Category::ArrayBuffer;
    e.buffer = buf;
    m_entries.push_back(e);

    m_creationOrder.push_back(name);
    ++m_totalCreated;
    return buf;
}

QOpenGLBuffer *GpuResourceManager::createIndexBuffer(const QString &name)
{
    auto *buf = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    if (!buf->create()) {
        delete buf;
        return nullptr;
    }

    Entry e;
    e.name = name;
    e.category = Category::IndexBuffer;
    e.buffer = buf;
    m_entries.push_back(e);

    m_creationOrder.push_back(name);
    ++m_totalCreated;
    return buf;
}

QOpenGLTexture *GpuResourceManager::createTexture2D(const QString &name, int width, int height,
                                                    unsigned int internalFormat, unsigned int format,
                                                    unsigned int pixelType, const void *data)
{
    auto *tex = new QOpenGLTexture(QOpenGLTexture::Target2D);
    tex->setSize(std::max(1, width), std::max(1, height));
    tex->setFormat(static_cast<QOpenGLTexture::TextureFormat>(internalFormat));
    tex->setMipLevels(1);

    // 纹理参数：热力纹理需要 ClampToEdge + Linear，避免边缘出现重复采样接缝
    tex->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::ClampToEdge);
    tex->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
    tex->setMinificationFilter(QOpenGLTexture::Linear);
    tex->setMagnificationFilter(QOpenGLTexture::Linear);

    if (!tex->create()) {
        delete tex;
        return nullptr;
    }

    if (data) {
        tex->setData(static_cast<QOpenGLTexture::PixelFormat>(format),
                     static_cast<QOpenGLTexture::PixelType>(pixelType), data);
    }

    Entry e;
    e.name = name;
    e.category = Category::Texture;
    e.texture = tex;
    m_entries.push_back(e);

    m_creationOrder.push_back(name);
    ++m_totalCreated;
    return tex;
}

bool GpuResourceManager::createColorFramebuffer(const QString &name, int width, int height,
                                                unsigned int *outColorTex, unsigned int *outFboId,
                                                unsigned int *outDepthRbo, QString *errorLog)
{
    if (!m_initialized) {
        if (errorLog)
            *errorLog += QStringLiteral("[GpuResourceManager] 未初始化即创建 FBO\n");
        return false;
    }

    width = std::max(1, width);
    height = std::max(1, height);

    // 颜色附件纹理（RGBA8）
    GLuint colorTex = 0;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 深度 renderbuffer（离屏渲染需要深度测试正确）
    GLuint depthRbo = 0;
    glGenRenderbuffers(1, &depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // FBO 本体
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (errorLog) {
            *errorLog += QStringLiteral("[GpuResourceManager] FBO 不完整: %1 (status=0x%2)\n")
                             .arg(name)
                             .arg(status, 0, 16);
        }
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteRenderbuffers(1, &depthRbo);
        return false;
    }

    Entry e;
    e.name = name;
    e.category = Category::Framebuffer;
    e.fboId = fbo;
    e.colorTex = colorTex;
    e.depthRbo = depthRbo;
    m_entries.push_back(e);

    m_creationOrder.push_back(name);
    ++m_totalCreated;

    if (outColorTex)
        *outColorTex = colorTex;
    if (outFboId)
        *outFboId = fbo;
    if (outDepthRbo)
        *outDepthRbo = depthRbo;
    return true;
}

namespace {
/** 在 vector 中按名称查找（取最后一个匹配，便于同名重建） */
template <typename T>
int findEntryIndex(const std::vector<T> &entries, const QString &name)
{
    for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
        if (entries[static_cast<size_t>(i)].name == name)
            return i;
    }
    return -1;
}
} // namespace

void GpuResourceManager::destroyVertexArray(const QString &name)
{
    const int idx = findEntryIndex(m_entries, name);
    if (idx < 0)
        return;

    Entry &e = m_entries[static_cast<size_t>(idx)];
    if (e.vao) {
        e.vao->destroy();
        delete e.vao;
        e.vao = nullptr;
    }
    m_entries.erase(m_entries.begin() + idx);
    ++m_totalDestroyed;
}

void GpuResourceManager::destroyBuffer(const QString &name)
{
    const int idx = findEntryIndex(m_entries, name);
    if (idx < 0)
        return;

    Entry &e = m_entries[static_cast<size_t>(idx)];
    if (e.buffer) {
        e.buffer->destroy();
        delete e.buffer;
        e.buffer = nullptr;
    }
    m_entries.erase(m_entries.begin() + idx);
    ++m_totalDestroyed;
}

void GpuResourceManager::destroyTexture(const QString &name)
{
    const int idx = findEntryIndex(m_entries, name);
    if (idx < 0)
        return;

    Entry &e = m_entries[static_cast<size_t>(idx)];
    if (e.texture) {
        e.texture->destroy();
        delete e.texture;
        e.texture = nullptr;
    }
    m_entries.erase(m_entries.begin() + idx);
    ++m_totalDestroyed;
}

QOpenGLTexture *GpuResourceManager::findTexture(const QString &name) const
{
    const int idx = findEntryIndex(m_entries, name);
    if (idx < 0)
        return nullptr;
    const Entry &e = m_entries[static_cast<size_t>(idx)];
    return e.category == Category::Texture ? e.texture : nullptr;
}

void GpuResourceManager::destroyFramebuffer(const QString &name)
{
    const int idx = findEntryIndex(m_entries, name);
    if (idx < 0)
        return;

    Entry &e = m_entries[static_cast<size_t>(idx)];
    if (e.fboId) {
        glDeleteFramebuffers(1, &e.fboId);
        e.fboId = 0;
    }
    if (e.colorTex) {
        glDeleteTextures(1, &e.colorTex);
        e.colorTex = 0;
    }
    if (e.depthRbo) {
        glDeleteRenderbuffers(1, &e.depthRbo);
        e.depthRbo = 0;
    }
    m_entries.erase(m_entries.begin() + idx);
    ++m_totalDestroyed;
}

void GpuResourceManager::registerExternalTexture(const QString &name, QOpenGLTexture *texture,
                                                 bool takeOwnership)
{
    Entry e;
    e.name = name;
    e.category = Category::Texture;
    e.texture = takeOwnership ? texture : nullptr;
    m_entries.push_back(e);
    m_creationOrder.push_back(name);
    ++m_totalCreated;
}

void GpuResourceManager::destroyAll(bool verbose)
{
    QStringList log;
    log << QStringLiteral("[GpuResourceManager] 开始释放 GPU 资源，剩余 %1 项")
               .arg(m_entries.size());

    // 按创建顺序的逆序释放：先删依赖者（VAO 引用 buffer），再删被依赖者。
    // 这里直接逆序遍历 m_creationOrder，保证 VAO 先于其 VBO 释放。
    for (auto it = m_creationOrder.rbegin(); it != m_creationOrder.rend(); ++it) {
        const int idx = findEntryIndex(m_entries, *it);
        if (idx < 0)
            continue;

        Entry &e = m_entries[static_cast<size_t>(idx)];
        switch (e.category) {
        case Category::VertexArray:
            if (e.vao) {
                e.vao->destroy();
                delete e.vao;
                e.vao = nullptr;
            }
            break;
        case Category::ArrayBuffer:
        case Category::IndexBuffer:
            if (e.buffer) {
                e.buffer->destroy();
                delete e.buffer;
                e.buffer = nullptr;
            }
            break;
        case Category::Texture:
            if (e.texture) {
                e.texture->destroy();
                delete e.texture;
                e.texture = nullptr;
            }
            break;
        case Category::Framebuffer:
            if (e.fboId) {
                glDeleteFramebuffers(1, &e.fboId);
                e.fboId = 0;
            }
            if (e.colorTex) {
                glDeleteTextures(1, &e.colorTex);
                e.colorTex = 0;
            }
            if (e.depthRbo) {
                glDeleteRenderbuffers(1, &e.depthRbo);
                e.depthRbo = 0;
            }
            break;
        case Category::Renderbuffer:
            if (e.depthRbo) {
                glDeleteRenderbuffers(1, &e.depthRbo);
                e.depthRbo = 0;
            }
            break;
        }

        log << QStringLiteral("  - 已释放 [%1] %2").arg(static_cast<int>(e.category)).arg(e.name);
        ++m_totalDestroyed;
    }

    const int released = static_cast<int>(m_entries.size());
    m_entries.clear();
    m_creationOrder.clear();

    log << QStringLiteral("[GpuResourceManager] 释放完成：本次 %1 项，累计创建 %2 / 释放 %3")
               .arg(released)
               .arg(m_totalCreated)
               .arg(m_totalDestroyed);

    m_lastReleaseLog = log.join(QLatin1Char('\n'));

    if (verbose)
        qInfo().noquote() << m_lastReleaseLog;
}

int GpuResourceManager::resourceCount() const
{
    return static_cast<int>(m_entries.size());
}

QString GpuResourceManager::statisticsText() const
{
    int vao = 0, vbo = 0, ebo = 0, tex = 0, fbo = 0;
    for (const Entry &e : m_entries) {
        switch (e.category) {
        case Category::VertexArray: ++vao; break;
        case Category::ArrayBuffer: ++vbo; break;
        case Category::IndexBuffer: ++ebo; break;
        case Category::Texture: ++tex; break;
        case Category::Framebuffer: ++fbo; break;
        case Category::Renderbuffer: break;
        }
    }

    return QStringLiteral("VAO %1 | VBO %2 | EBO %3 | 纹理 %4 | FBO %5 | 累计创建 %6 / 释放 %7")
        .arg(vao)
        .arg(vbo)
        .arg(ebo)
        .arg(tex)
        .arg(fbo)
        .arg(m_totalCreated)
        .arg(m_totalDestroyed);
}

} // namespace wafer
