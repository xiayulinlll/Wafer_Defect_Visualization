#include "ui/AppSettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <memory>

namespace wafer {

namespace {
/** QSet<int> <-> 逗号分隔字符串（QSettings 原生不支持容器，避免 QVariant 兼容问题） */
QString setToString(const QSet<int> &values)
{
    QList<int> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    parts.reserve(sorted.size());
    for (int v : sorted)
        parts << QString::number(v);
    return parts.join(QLatin1Char(','));
}

QSet<int> stringToSet(const QString &text)
{
    QSet<int> values;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        bool ok = false;
        const int v = p.trimmed().toInt(&ok);
        if (ok)
            values.insert(v);
    }
    return values;
}
} // namespace

class AppSettings::Impl
{
public:
    /** 读取当前 QCoreApplication 的应用/组织名（空则用本程序内置名兜底） */
    static void resolveNames(QString *org, QString *appName)
    {
        *org = QCoreApplication::organizationName().isEmpty()
                   ? QStringLiteral("SemiconductorInspection")
                   : QCoreApplication::organizationName();
        *appName = QCoreApplication::applicationName().isEmpty()
                       ? QStringLiteral("WaferDefectVisualization")
                       : QCoreApplication::applicationName();
    }

    void openSettings()
    {
        QString org;
        QString appName;
        resolveNames(&org, &appName);

        // 已经指向同一份配置：直接复用（避免每次访问都新建 QSettings）
        if (settings && org == m_org && appName == m_app)
            return;

        settings = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, org,
                                               appName);
        settings->setFallbacksEnabled(false);
        m_org = org;
        m_app = appName;
    }

    QSettings &get()
    {
        openSettings();
        return *settings;
    }

    std::unique_ptr<QSettings> settings;
    QString m_org;
    QString m_app;
};

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_impl(new Impl)
{
    m_impl->openSettings();
}

AppSettings::~AppSettings()
{
    sync();
}

void AppSettings::bindApplicationIdentity()
{
    // 正常情况下只做一次「对齐」；若应用名在构造之后才设置，这里会切换到正确文件名。
    m_impl->openSettings();
}

void AppSettings::sync()
{
    if (m_impl && m_impl->settings)
        m_impl->settings->sync();
}

QString AppSettings::lastDatasetPath() const
{
    return m_impl->get().value(QStringLiteral("dataset/lastPath")).toString();
}


void AppSettings::setLastDatasetPath(const QString &path)
{
    m_impl->get().setValue(QStringLiteral("dataset/lastPath"), path);
}

AppSettings::CameraState AppSettings::loadCamera() const
{
    CameraState s;
    const QSettings &st = m_impl->get();
    s.valid = st.contains(QStringLiteral("camera/distance"));
    if (!s.valid)
        return s;

    s.yawDeg = st.value(QStringLiteral("camera/yaw"), s.yawDeg).toFloat();
    s.pitchDeg = st.value(QStringLiteral("camera/pitch"), s.pitchDeg).toFloat();
    s.distance = st.value(QStringLiteral("camera/distance"), s.distance).toFloat();
    s.targetX = st.value(QStringLiteral("camera/targetX"), 0.0f).toFloat();
    s.targetY = st.value(QStringLiteral("camera/targetY"), 0.0f).toFloat();
    s.targetZ = st.value(QStringLiteral("camera/targetZ"), 0.0f).toFloat();
    return s;
}

void AppSettings::saveCamera(const CameraState &state)
{
    QSettings &st = m_impl->get();
    st.setValue(QStringLiteral("camera/yaw"), state.yawDeg);
    st.setValue(QStringLiteral("camera/pitch"), state.pitchDeg);
    st.setValue(QStringLiteral("camera/distance"), state.distance);
    st.setValue(QStringLiteral("camera/targetX"), state.targetX);
    st.setValue(QStringLiteral("camera/targetY"), state.targetY);
    st.setValue(QStringLiteral("camera/targetZ"), state.targetZ);
}

AppSettings::FilterState AppSettings::loadFilter() const
{
    FilterState s;
    const QSettings &st = m_impl->get();
    s.minSeverity = st.value(QStringLiteral("filter/minSeverity"), s.minSeverity).toInt();
    s.maxSeverity = st.value(QStringLiteral("filter/maxSeverity"), s.maxSeverity).toInt();
    s.types = stringToSet(st.value(QStringLiteral("filter/types")).toString());
    s.layers = stringToSet(st.value(QStringLiteral("filter/layers")).toString());
    s.minArea = st.value(QStringLiteral("filter/minArea"), s.minArea).toFloat();
    s.maxArea = st.value(QStringLiteral("filter/maxArea"), s.maxArea).toFloat();
    s.minHeight = st.value(QStringLiteral("filter/minHeight"), s.minHeight).toFloat();
    s.maxHeight = st.value(QStringLiteral("filter/maxHeight"), s.maxHeight).toFloat();
    s.layerOffset = st.value(QStringLiteral("filter/layerOffset"), s.layerOffset).toBool();
    return s;
}

void AppSettings::saveFilter(const FilterState &state)
{
    QSettings &st = m_impl->get();
    st.setValue(QStringLiteral("filter/minSeverity"), state.minSeverity);
    st.setValue(QStringLiteral("filter/maxSeverity"), state.maxSeverity);
    st.setValue(QStringLiteral("filter/types"), setToString(state.types));
    st.setValue(QStringLiteral("filter/layers"), setToString(state.layers));
    st.setValue(QStringLiteral("filter/minArea"), state.minArea);
    st.setValue(QStringLiteral("filter/maxArea"), state.maxArea);
    st.setValue(QStringLiteral("filter/minHeight"), state.minHeight);
    st.setValue(QStringLiteral("filter/maxHeight"), state.maxHeight);
    st.setValue(QStringLiteral("filter/layerOffset"), state.layerOffset);
}

AppSettings::RenderState AppSettings::loadRender() const
{
    RenderState s;
    const QSettings &st = m_impl->get();
    s.heatmap = st.value(QStringLiteral("render/heatmap"), s.heatmap).toBool();
    s.markers = st.value(QStringLiteral("render/markers"), s.markers).toBool();
    s.wafer = st.value(QStringLiteral("render/wafer"), s.wafer).toBool();
    s.zScale = st.value(QStringLiteral("render/zScale"), s.zScale).toFloat();
    s.captureMode = st.value(QStringLiteral("render/captureMode"), s.captureMode).toBool();
    s.autoCapture = st.value(QStringLiteral("render/autoCapture"), s.autoCapture).toBool();
    // 防御：配置文件可能被手工改坏
    if (!(s.zScale >= 0.1f && s.zScale <= 10.0f))
        s.zScale = 1.0f;
    return s;
}

void AppSettings::saveRender(const RenderState &state)
{
    QSettings &st = m_impl->get();
    st.setValue(QStringLiteral("render/heatmap"), state.heatmap);
    st.setValue(QStringLiteral("render/markers"), state.markers);
    st.setValue(QStringLiteral("render/wafer"), state.wafer);
    st.setValue(QStringLiteral("render/zScale"), state.zScale);
    st.setValue(QStringLiteral("render/captureMode"), state.captureMode);
    st.setValue(QStringLiteral("render/autoCapture"), state.autoCapture);
}

} // namespace wafer
