#include "render/ShaderManager.h"

#include <QFile>
#include <QOpenGLShaderProgram>
#include <QStringList>

namespace wafer {

ShaderManager::ShaderManager() = default;

ShaderManager::~ShaderManager()
{
    // 所有权已由 unique_ptr 释放；destroy() 需显式在上下文有效时调用，
    // 这里不再重复调用 OpenGL API，避免上下文失效后崩溃。
}

QString ShaderManager::loadShaderSource(const QString &path, QString *errorLog)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorLog)
            *errorLog += QStringLiteral("[ShaderManager] 无法打开着色器文件: %1\n").arg(path);
        return QString();
    }
    const QByteArray data = file.readAll();
    file.close();
    return QString::fromUtf8(data);
}

bool ShaderManager::checkLinkStatus(QOpenGLShaderProgram *prog, const QString &tag,
                                    QString *errorLog)
{
    if (!prog)
        return false;

    if (!prog->isLinked()) {
        if (errorLog) {
            *errorLog += QStringLiteral("[ShaderManager] 着色器链接失败: %1\n%2\n")
                             .arg(tag, prog->log());
        }
        return false;
    }
    return true;
}

bool ShaderManager::compileProgram(Program which, const QString &vertPath, const QString &fragPath,
                                   QString *errorLog)
{
    const QString vertSrc = loadShaderSource(vertPath, errorLog);
    const QString fragSrc = loadShaderSource(fragPath, errorLog);
    if (vertSrc.isEmpty() || fragSrc.isEmpty())
        return false;

    auto prog = std::make_unique<QOpenGLShaderProgram>();

    if (!prog->addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc)) {
        if (errorLog)
            *errorLog += QStringLiteral("[ShaderManager] 顶点着色器编译失败: %1\n%2\n")
                             .arg(vertPath, prog->log());
        return false;
    }

    if (!prog->addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc)) {
        if (errorLog)
            *errorLog += QStringLiteral("[ShaderManager] 片元着色器编译失败: %1\n%2\n")
                             .arg(fragPath, prog->log());
        return false;
    }

    if (!prog->link()) {
        if (errorLog)
            *errorLog +=
                QStringLiteral("[ShaderManager] 链接失败: %1 + %2\n%3\n")
                    .arg(vertPath, fragPath, prog->log());
        return false;
    }

    if (!checkLinkStatus(prog.get(), vertPath, errorLog))
        return false;

    m_programs[static_cast<int>(which)] = std::move(prog);
    return true;
}

bool ShaderManager::initialize(const QString &shaderDir, QString *errorLog)
{
    QString log;

    // 先确保 GL 函数指针可用（QOpenGLShaderProgram 内部会处理 ES/桌面差异，
    // 这里主要用于后续显式调用 glGetError）。
    if (!initializeOpenGLFunctions()) {
        if (errorLog)
            *errorLog += QStringLiteral("[ShaderManager] 无法初始化 OpenGL 3.3 函数指针\n");
        return false;
    }

    const QString base = shaderDir + QLatin1Char('/');

    const struct {
        Program which;
        const char *vert;
        const char *frag;
    } defs[] = {
        {Program::Wafer, "wafer.vert", "wafer.frag"},
        {Program::Marker, "marker.vert", "marker.frag"},
        {Program::Line, "grid.vert", "grid.frag"},
        {Program::FboBlit, "fbo_blit.vert", "fbo_blit.frag"},
    };

    bool ok = true;
    for (const auto &def : defs) {
        if (!compileProgram(def.which, base + QString::fromLatin1(def.vert),
                            base + QString::fromLatin1(def.frag), &log)) {
            ok = false;
            break;
        }
    }

    m_valid = ok;

    if (!ok && errorLog)
        *errorLog += log;

    if (ok) {
        // 清除残留 GL 错误，避免影响后续错误检测
        while (glGetError() != GL_NO_ERROR) {
        }
    }

    return ok;
}

QOpenGLShaderProgram *ShaderManager::program(Program which) const
{
    const int idx = static_cast<int>(which);
    if (idx < 0 || idx >= static_cast<int>(Program::Count))
        return nullptr;
    return m_programs[idx].get();
}

bool ShaderManager::bind(Program which) const
{
    QOpenGLShaderProgram *p = program(which);
    if (!p)
        return false;
    return p->bind();
}

void ShaderManager::releaseAll() const
{
    for (int i = 0; i < static_cast<int>(Program::Count); ++i) {
        if (m_programs[i])
            m_programs[i]->release();
    }
}

void ShaderManager::destroy()
{
    // unique_ptr 析构会调用 QOpenGLShaderProgram 析构函数 -> glDeleteProgram。
    // 因此必须在上下文有效（makeCurrent）时调用本函数。
    for (int i = 0; i < static_cast<int>(Program::Count); ++i)
        m_programs[i].reset();

    m_valid = false;
}

} // namespace wafer
