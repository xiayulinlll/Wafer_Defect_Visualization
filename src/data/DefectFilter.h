#ifndef DEFECTFILTER_H
#define DEFECTFILTER_H

#include "core/Constants.h"
#include "core/DefectTypes.h"

#include <QSet>

#include <limits>

namespace wafer {

/**
 * @brief 缺陷数据过滤器（数据层）。
 *
 * 通过等级下界与类型集合描述过滤条件；渲染前调用 apply() 得到紧凑的
 * 过滤结果，减少上传 GPU 的图元数量（难点2：降低 DrawCall 与带宽压力）。
 * 纯值类型，可在任意线程使用。
 */
class DefectFilter
{
public:
    DefectFilter() = default;

    void setMinSeverity(DefectSeverity s) { m_minSeverity = s; }
    DefectSeverity minSeverity() const { return m_minSeverity; }

    void setMaxSeverity(DefectSeverity s) { m_maxSeverity = s; }
    DefectSeverity maxSeverity() const { return m_maxSeverity; }

    /** 设置纳入的类型集合；集合为空表示"全部类型" */
    void setTypes(const QSet<int> &types) { m_types = types; }
    const QSet<int> &types() const { return m_types; }

    /** 设置纳入的掩膜层集合；集合为空表示"全部层"（0 也是有效层号） */
    void setLayers(const QSet<int> &layers) { m_layers = layers; }
    const QSet<int> &layers() const { return m_layers; }

    /** 是否把各掩膜层在高度上错开（叠加视图） */
    void setLayerOffsetEnabled(bool on) { m_layerOffset = on; }
    bool layerOffsetEnabled() const { return m_layerOffset; }
    /** 每层在 Z（高度，um）上的错开量 */
    void setLayerHeightUm(float um) { m_layerHeightUm = um; }
    float layerHeightUm() const { return m_layerHeightUm; }

    // ---------------- 面积 / 高度区间（§3-4） ----------------
    /**
     * @brief 设置面积区间 (um^2)。边界为闭区间；<= 0 / 非有限值表示该端不限制。
     * @note 面积为负本身无意义，因此「不限制」统一用 -1 或非有限值表达。
     */
    void setAreaRange(float minArea, float maxArea)
    {
        m_minArea = sanitizeLower(minArea);
        m_maxArea = sanitizeUpper(maxArea);
    }
    float minArea() const { return m_minArea; }
    float maxArea() const { return m_maxArea; }

    /**
     * @brief 设置高度区间 (um)。这里比较的是**原始数据 Z**，
     * 而不是「按层错开」后的渲染高度，保证筛选语义与 CSV 一致。
     */
    void setHeightRange(float minHeight, float maxHeight)
    {
        m_minHeight = sanitizeLower(minHeight);
        m_maxHeight = sanitizeUpper(maxHeight);
    }
    float minHeight() const { return m_minHeight; }
    float maxHeight() const { return m_maxHeight; }

    /** 清空所有条件（显示全部） */
    void reset()
    {
        m_minSeverity = DefectSeverity::Level1;
        m_maxSeverity = DefectSeverity::Level5;
        m_types.clear();
        m_layers.clear();
        m_minArea = kNoLimit;
        m_maxArea = kNoLimit;
        m_minHeight = kNoLimit;
        m_maxHeight = kNoLimit;
    }

    /** 单个缺陷是否通过过滤 */
    bool accepts(const DefectPoint &p) const
    {
        const int lv = static_cast<int>(p.severity);
        if (lv < static_cast<int>(m_minSeverity) || lv > static_cast<int>(m_maxSeverity))
            return false;
        if (!m_types.isEmpty() && !m_types.contains(static_cast<int>(p.type)))
            return false;
        if (!m_layers.isEmpty() && !m_layers.contains(p.layer))
            return false;
        if (m_minArea != kNoLimit && p.area < m_minArea)
            return false;
        if (m_maxArea != kNoLimit && p.area > m_maxArea)
            return false;
        if (m_minHeight != kNoLimit && p.z < m_minHeight)
            return false;
        if (m_maxHeight != kNoLimit && p.z > m_maxHeight)
            return false;
        return true;
    }

    /**
     * @brief 对单个缺陷应用「层高度错开」，返回渲染用的高度（um）。
     * 未启用错开时原样返回 p.z；启用时按 layer * layerHeightUm 叠加，
     * 便于在 3D 视图中区分不同掩膜层的缺陷。
     */
    float renderHeightUm(const DefectPoint &p) const
    {
        return m_layerOffset ? p.z + static_cast<float>(p.layer) * m_layerHeightUm : p.z;
    }

    /**
     * @brief 对数据集执行过滤。
     * @param dataset 只读输入
     * @param out     输出过滤后的缺陷点（会先清空）；启用层错开时 Z 已叠加偏移
     * @return 通过过滤的数量
     */
    int apply(const DefectDataset &dataset, QVector<DefectPoint> *out) const
    {
        if (!out)
            return 0;
        out->clear();
        out->reserve(dataset.points.size() / 2 + 16);
        for (const DefectPoint &p : dataset.points) {
            if (!accepts(p))
                continue;
            if (m_layerOffset) {
                DefectPoint shifted = p;
                shifted.z = renderHeightUm(p);
                out->push_back(shifted);
            } else {
                out->push_back(p);
            }
        }
        return out->size();
    }

    /** 是否处于"显示全部"状态 */
    bool isPassThrough() const
    {
        return m_types.isEmpty() && m_layers.isEmpty()
               && m_minSeverity == DefectSeverity::Level1
               && m_maxSeverity == DefectSeverity::Level5 && m_minArea == kNoLimit
               && m_maxArea == kNoLimit && m_minHeight == kNoLimit && m_maxHeight == kNoLimit;
    }

    /** 「该端不限制」的哨兵值 */
    static constexpr float kNoLimit = -1.0f;

private:
    /** 下限：负值或非有限值 -> 不限制 */
    static float sanitizeLower(float v)
    {
        return (v >= 0.0f && std::isfinite(v)) ? v : kNoLimit;
    }
    /** 上限：负值或非有限值 -> 不限制 */
    static float sanitizeUpper(float v)
    {
        return (v >= 0.0f && std::isfinite(v)) ? v : kNoLimit;
    }

    DefectSeverity m_minSeverity = DefectSeverity::Level1;
    DefectSeverity m_maxSeverity = DefectSeverity::Level5;
    QSet<int> m_types;  // 空集合 = 全部
    QSet<int> m_layers; // 空集合 = 全部
    bool m_layerOffset = false;
    float m_layerHeightUm = 8.0f;
    // 面积 (um^2) / 高度 (um) 区间；kNoLimit 表示该端不限制（闭区间）
    float m_minArea = kNoLimit;
    float m_maxArea = kNoLimit;
    float m_minHeight = kNoLimit;
    float m_maxHeight = kNoLimit;
};

} // namespace wafer

#endif // DEFECTFILTER_H
