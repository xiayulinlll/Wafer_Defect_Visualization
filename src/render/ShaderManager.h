#ifndef SHADERMANAGER_H
#define SHADERMANAGER_H

#include <QOpenGLFunctions_3_3_Core>
#include <QString>
#include <QVector3D>

#include <memory>

class QOpenGLShaderProgram;
class QMatrix4x4;

namespace wafer {

/**
 * @brief 着色器管理器（渲染层）。
 *
 * 统一管理多组 shader program（晶圆主体 / 缺陷标记 / 线段 / FBO 拷贝），
 * 负责编译、链接、错误检查与资源释放。
 *
 * 约束：所有方法必须在持有 OpenGL 上下文的渲染线程调用。
 */
class ShaderManager : protected QOpenGLFunctions_3_3_Core
{
public:
    /** 内建着色器标识 */
    enum class Program {
        Wafer = 0,  // 晶圆主体（含热力图采样）
        Marker,     // 缺陷标记点
        Line,       // 网格 / 坐标轴 / 轮廓
        FboBlit,    // FBO 全屏拷贝
        Count
    };

    ShaderManager();
    ~ShaderManager();

    ShaderManager(const ShaderManager &) = delete;
    ShaderManager &operator=(const ShaderManager &) = delete;

    /**
     * @brief 从磁盘加载全部内建着色器。
     * @param shaderDir 着色器目录（支持 Qt 资源路径 :/shaders 或绝对路径）
     * @param errorLog  输出错误详情（可为空）
     * @return 全部编译链接成功返回 true
     */
    bool initialize(const QString &shaderDir, QString *errorLog = nullptr);

    /** 是否初始化成功 */
    bool isValid() const { return m_valid; }

    /** 取得着色器程序；未初始化时返回 nullptr */
    QOpenGLShaderProgram *program(Program which) const;

    /** 便捷绑定（等价于 program()->bind()） */
    bool bind(Program which) const;

    /** 解绑全部着色器 */
    void releaseAll() const;

    /**
     * @brief 释放所有着色器资源。
     * 必须在 OpenGL 上下文仍有效时调用（通常于 QOpenGLWidget 的
     * aboutToBeDestroyed 槽中）。
     */
    void destroy();

    /** 从文件/资源读取源码；失败时 errorLog 记录原因 */
    static QString loadShaderSource(const QString &path, QString *errorLog);

private:
    bool compileProgram(Program which, const QString &vertPath, const QString &fragPath,
                        QString *errorLog);
    static bool checkLinkStatus(QOpenGLShaderProgram *prog, const QString &tag,
                                QString *errorLog);

    std::unique_ptr<QOpenGLShaderProgram> m_programs[static_cast<int>(Program::Count)];
    bool m_valid = false;
};

} // namespace wafer

#endif // SHADERMANAGER_H
