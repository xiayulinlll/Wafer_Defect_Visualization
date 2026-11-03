#include "data/CsvDatasetParser.h"

#include "core/Constants.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>

namespace wafer {

namespace {

/**
 * @brief 列映射：把「表头列名」映射到结构体字段下标。
 *
 * 真实设备导出的 CSV 列顺序经常不同，甚至缺少某些列。这里按表头建映射，
 * 缺失列走默认值，并在结果里记录实际使用的列顺序与未识别列，便于排查数据问题。
 */
enum class Column : int {
    Id = 0,
    X,
    Y,
    Z,
    Area,
    Severity,
    Type,
    Layer,
    ExtraA,
    ExtraB,
    Count
};

/** 列名归一化：去空白、下划线、连字符与大小写差异 */
QString normalizeHeader(const QString &raw)
{
    QString s = raw.trimmed().toLower();
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('_'));
    s.remove(QLatin1Char('-'));
    s.remove(QLatin1Char('.'));
    s.remove(QLatin1Char('('));
    s.remove(QLatin1Char(')'));
    s.remove(QLatin1Char('['));
    s.remove(QLatin1Char(']'));
    s.remove(QLatin1Char('#'));
    return s;
}

/** 列名 -> 字段；无法识别返回 -1 */
int columnForHeader(const QString &raw)
{
    const QString h = normalizeHeader(raw);
    if (h.isEmpty())
        return -1;

    struct Alias {
        const char *name;
        Column column;
    };
    static const Alias aliases[] = {
        {"defectid", Column::Id},     {"id", Column::Id},
        {"缺陷id", Column::Id},       {"缺陷编号", Column::Id},
        {"x", Column::X},             {"xpos", Column::X},
        {"x坐标", Column::X},         {"坐标x", Column::X},
        {"y", Column::Y},             {"ypos", Column::Y},
        {"y坐标", Column::Y},         {"坐标y", Column::Y},
        {"z", Column::Z},             {"zpos", Column::Z},
        {"高度", Column::Z},          {"z坐标", Column::Z},
        {"z高度", Column::Z},         {"厚度", Column::Z},
        {"area", Column::Area},       {"size", Column::Area},
        {"面积", Column::Area},
        {"severity", Column::Severity}, {"level", Column::Severity},
        {"等级", Column::Severity},     {"严重度", Column::Severity},
        {"严重等级", Column::Severity}, {"缺陷等级", Column::Severity},
        {"type", Column::Type},       {"defecttype", Column::Type},
        {"类型", Column::Type},       {"缺陷类型", Column::Type},
        {"layer", Column::Layer},     {"masklayer", Column::Layer},
        {"层号", Column::Layer},      {"掩膜层", Column::Layer},
        {"层", Column::Layer},        {"层次", Column::Layer},
        {"extraa", Column::ExtraA},   {"lot", Column::ExtraA},
        {"extra", Column::ExtraA},
        {"extrab", Column::ExtraB},   {"wafer", Column::ExtraB},
        {"waferid", Column::ExtraB},
    };

    for (const Alias &a : aliases) {
        if (h == QString::fromUtf8(a.name))
            return static_cast<int>(a.column);
    }
    return -1;
}

/** 将字符串安全转为 float，失败返回 fallback */
float toFloat(const QString &s, float fallback = 0.0f)
{
    bool ok = false;
    const float v = s.trimmed().toFloat(&ok);
    return ok ? v : fallback;
}

/** 将字符串安全转为 int */
int toInt(const QString &s, int fallback = 0)
{
    bool ok = false;
    const int v = s.trimmed().toInt(&ok);
    return ok ? v : fallback;
}

/** 缺陷类型字段归一化：支持 PARTICLE / 颗粒污染 / 0 三种写法 */
DefectType toDefectType(const QString &raw)
{
    const QString s = raw.trimmed().toUpper();

    bool numOk = false;
    const int num = s.toInt(&numOk);
    if (numOk) {
        if (num >= 0 && num <= static_cast<int>(DefectType::Other))
            return static_cast<DefectType>(num);
        return DefectType::Other;
    }

    for (int i = 0; i <= static_cast<int>(DefectType::Other); ++i) {
        const DefectType t = static_cast<DefectType>(i);
        if (s == defectTypeKey(t).toUpper())
            return t;
    }

    if (raw.contains(QStringLiteral("颗粒")) || raw.contains(QStringLiteral("污染")))
        return DefectType::Particle;
    if (raw.contains(QStringLiteral("划伤")) || raw.contains(QStringLiteral("划痕")))
        return DefectType::Scratch;
    if (raw.contains(QStringLiteral("图形")) || raw.contains(QStringLiteral("pattern")))
        return DefectType::Pattern;
    if (raw.contains(QStringLiteral("空洞")) || raw.contains(QStringLiteral("孔洞")))
        return DefectType::Void;
    if (raw.contains(QStringLiteral("残留")) || raw.contains(QStringLiteral("残余")))
        return DefectType::Residue;

    return DefectType::Other;
}

/** 等级归一化到 1~5 */
DefectSeverity toSeverity(int raw)
{
    const int lv = std::max(1, std::min(5, raw));
    return static_cast<DefectSeverity>(lv);
}

/** 判断表头行：存在非数字字段即认为是表头 */
bool looksLikeHeader(const QStringList &fields)
{
    if (fields.isEmpty())
        return false;
    for (const QString &f : fields) {
        const QString t = f.trimmed();
        if (t.isEmpty())
            continue;
        bool ok = false;
        t.toFloat(&ok);
        if (!ok)
            return true;
    }
    return false;
}

/**
 * @brief 一行的解析结果（供 parseFile 分类统计跳过原因）
 */
enum class RowStatus { Ok, Blank, Short, Invalid };

/** 列槽位：position[Column] = 该字段在第几个逗号分隔字段里；-1 表示缺失 */
using ColumnMap = std::array<int, static_cast<int>(Column::Count)>;

const char *const kDefaultColumns[] = {"DefectId", "X", "Y", "Z", "Area",
                                       "Severity", "Type",     "Layer", "ExtraA", "ExtraB"};

/** 默认列顺序（无表头时使用） */
ColumnMap defaultColumnMap()
{
    ColumnMap map{};
    for (int i = 0; i < static_cast<int>(Column::Count); ++i)
        map[static_cast<size_t>(i)] = i;
    return map;
}

/** 按表头建立列映射 */
ColumnMap buildColumnMap(const QStringList &header)
{
    ColumnMap map{};
    map.fill(-1);
    for (int i = 0; i < header.size(); ++i) {
        const int col = columnForHeader(header.at(i));
        if (col < 0)
            continue;
        // 同名列只认第一列，避免后出现的空列覆盖有效映射
        if (map[static_cast<size_t>(col)] < 0)
            map[static_cast<size_t>(col)] = i;
    }
    return map;
}

/** 取指定列字段；缺失列返回空字符串 */
inline QString fieldAt(const QStringList &fields, const ColumnMap &map, Column column)
{
    const int idx = map[static_cast<size_t>(column)];
    if (idx < 0 || idx >= fields.size())
        return QString();
    return fields.at(idx);
}

/**
 * @brief 按列映射解析一行。
 * @param status 输出行状态，用于区分「列数不足」与「数值非法」
 */
bool parseLineWithMap(const QStringList &fields, const ColumnMap &map, DefectPoint *out,
                      RowStatus *status)
{
    if (status)
        *status = RowStatus::Invalid;
    if (!out)
        return false;

    // ID 与 XY 是必需列：缺列或空值视为「列数不足」
    const QString idText = fieldAt(fields, map, Column::Id);
    const QString xText = fieldAt(fields, map, Column::X);
    const QString yText = fieldAt(fields, map, Column::Y);
    if (idText.isEmpty() || xText.isEmpty() || yText.isEmpty()) {
        if (status)
            *status = RowStatus::Short;
        return false;
    }

    bool ok = false;
    DefectPoint p;
    p.id = idText.trimmed().toInt(&ok);
    if (!ok || p.id < 0) {
        if (status)
            *status = RowStatus::Invalid;
        return false;
    }

    p.x = xText.trimmed().toFloat(&ok);
    if (!ok) {
        if (status)
            *status = RowStatus::Invalid;
        return false;
    }
    p.y = yText.trimmed().toFloat(&ok);
    if (!ok) {
        if (status)
            *status = RowStatus::Invalid;
        return false;
    }

    const QString zText = fieldAt(fields, map, Column::Z);
    p.z = zText.isEmpty() ? 0.0f : toFloat(zText);

    const QString areaText = fieldAt(fields, map, Column::Area);
    p.area = areaText.isEmpty() ? 1.0f : toFloat(areaText, 1.0f);

    const QString sevText = fieldAt(fields, map, Column::Severity);
    p.severity = toSeverity(sevText.isEmpty() ? 1 : toInt(sevText, 1));
    p.type = toDefectType(fieldAt(fields, map, Column::Type));

    const QString layerText = fieldAt(fields, map, Column::Layer);
    p.layer = layerText.isEmpty() ? 0 : toInt(layerText, 0);

    if (map[static_cast<size_t>(Column::ExtraA)] >= 0)
        p.extraA = toInt(fieldAt(fields, map, Column::ExtraA), 0);
    if (map[static_cast<size_t>(Column::ExtraB)] >= 0)
        p.extraB = toInt(fieldAt(fields, map, Column::ExtraB), 0);

    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
        || !std::isfinite(p.area)) {
        if (status)
            *status = RowStatus::Invalid;
        return false;
    }

    *out = p;
    if (status)
        *status = RowStatus::Ok;
    return true;
}

/**
 * @brief 单行解析（流式热路径，§2-5）。
 *
 * 与 parseLineWithMap(QStringList) 语义一致，区别只在实现：
 *  - 不构造 QStringList，而是一遍扫描逗号，只把**用得到的列**拷成 QString；
 *  - 10 万行时省掉每行 8 个临时 QString 与一次 split 的堆分配
 *    （旧实现每行 `trimmed()` + `split()` 共 9 次分配）。
 *
 * 为什么不用 QStringTokenizer：Qt 6.0 才引入，且这里一行只扫一次更直接，
 * 也更容易与旧实现逐字段对齐（tst_csvparser 有等价性回归）。
 */
bool parseLineScanned(const QString &line, const ColumnMap &map, DefectPoint *out,
                      RowStatus *status)
{
    if (status)
        *status = RowStatus::Invalid;
    if (!out)
        return false;

    // 需要的列（-1 表示该列缺失，保持空 -> 走默认值分支）
    int maxNeeded = -1;
    for (int c = 0; c < static_cast<int>(Column::Count); ++c)
        maxNeeded = std::max(maxNeeded, map[static_cast<size_t>(c)]);

    // 按列号顺序扫描，命中需要的列才切出子串
    std::array<QString, static_cast<int>(Column::Count)> values;
    int fieldIndex = 0;
    int start = 0;
    const int len = line.size();
    auto flush = [&](int end) {
        for (int c = 0; c < static_cast<int>(Column::Count); ++c) {
            if (map[static_cast<size_t>(c)] == fieldIndex) {
                values[static_cast<size_t>(c)] = line.mid(start, end - start);
                break;
            }
        }
        ++fieldIndex;
    };
    for (int i = 0; i < len && fieldIndex <= maxNeeded; ++i) {
        if (line.at(i) == QLatin1Char(',')) {
            flush(i);
            start = i + 1;
        }
    }
    if (fieldIndex <= maxNeeded)
        flush(len); // 最后一列（没有尾随逗号）

    auto field = [&values](Column c) -> const QString & {
        return values[static_cast<size_t>(c)];
    };

    // ID 与 XY 是必需列：缺列或空值视为「列数不足」
    const QString &idText = field(Column::Id);
    const QString &xText = field(Column::X);
    const QString &yText = field(Column::Y);
    if (idText.isEmpty() || xText.isEmpty() || yText.isEmpty()) {
        if (status)
            *status = RowStatus::Short;
        return false;
    }

    bool ok = false;
    DefectPoint p;
    p.id = idText.trimmed().toInt(&ok);
    if (!ok || p.id < 0)
        return false;

    p.x = xText.trimmed().toFloat(&ok);
    if (!ok)
        return false;
    p.y = yText.trimmed().toFloat(&ok);
    if (!ok)
        return false;

    const QString &zText = field(Column::Z);
    p.z = zText.isEmpty() ? 0.0f : toFloat(zText);

    const QString &areaText = field(Column::Area);
    p.area = areaText.isEmpty() ? 1.0f : toFloat(areaText, 1.0f);

    const QString &sevText = field(Column::Severity);
    p.severity = toSeverity(sevText.isEmpty() ? 1 : toInt(sevText, 1));
    p.type = toDefectType(field(Column::Type));

    const QString &layerText = field(Column::Layer);
    p.layer = layerText.isEmpty() ? 0 : toInt(layerText, 0);

    if (map[static_cast<size_t>(Column::ExtraA)] >= 0)
        p.extraA = toInt(field(Column::ExtraA), 0);
    if (map[static_cast<size_t>(Column::ExtraB)] >= 0)
        p.extraB = toInt(field(Column::ExtraB), 0);

    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
        || !std::isfinite(p.area))
        return false;

    *out = p;
    if (status)
        *status = RowStatus::Ok;
    return true;
}

/** 累积 X/Y 范围，用于自动推导晶圆半径 */
struct ExtentAccumulator {
    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
    bool inited = false;

    void add(float x, float y)
    {
        if (!inited) {
            minX = maxX = x;
            minY = maxY = y;
            inited = true;
            return;
        }
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }

    float derivedRadius() const
    {
        if (!inited)
            return constants::kDefaultWaferRadiusMm;
        const float ex = (maxX - minX) * 0.5f;
        const float ey = (maxY - minY) * 0.5f;
        const float r = std::max(ex, ey);
        return std::max(r * constants::kDerivedRadiusMargin, constants::kMinWaferRadiusMm);
    }
};

} // namespace

QString CsvDatasetParser::headerLine()
{
    return QStringLiteral("DefectId,X,Y,Z,Area,Severity,Type,Layer");
}

bool CsvDatasetParser::parseLine(const QString &line, DefectPoint *out)
{
    if (!out)
        return false;

    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return false;

    // 保持历史语义：默认列顺序为 DefectId,X,Y,Z,Area,Severity,Type,Layer,ExtraA,ExtraB
    static const ColumnMap kDefaultMap = defaultColumnMap();
    const QStringList fields = trimmed.split(QLatin1Char(','));
    return parseLineWithMap(fields, kDefaultMap, out, nullptr);
}

CsvDatasetParser::Result CsvDatasetParser::parseFile(const QString &filePath, float radiusMm,
                                                     const CancelCallback &shouldCancel)
{
    Result result;

    QFile file(filePath);
    if (!file.exists()) {
        result.errorMessage = QStringLiteral("文件不存在：%1").arg(filePath);
        return result;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage =
            QStringLiteral("无法打开文件：%1 (%2)").arg(filePath, file.errorString());
        return result;
    }

    QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#else
    stream.setEncoding(QStringConverter::Utf8);
#endif

    ExtentAccumulator extent;
    QVector<DefectPoint> points;
    points.reserve(4096);

    ColumnMap columnMap = defaultColumnMap();
    bool headerChecked = false;
    int checkedLines = 0;
    while (!stream.atEnd()) {
        // 协作式取消：每 4096 行检查一次，避免频繁调用回调（替代强制 terminate）
        if (shouldCancel && (++checkedLines % 4096 == 0) && shouldCancel()) {
            file.close();
            result.errorMessage = QStringLiteral("加载已取消");
            return result;
        }

        const QString line = stream.readLine();
        if (line.trimmed().isEmpty())
            continue;

        // '#' 开头为注释行：导出结果会带统计摘要（CsvDatasetExporter），
        // 这里统一跳过，保证「导出的 CSV 能被本程序直接重新加载」。
        if (line.trimmed().startsWith(QLatin1Char('#')))
            continue;

        if (!headerChecked) {
            headerChecked = true;
            const QStringList header = line.split(QLatin1Char(','));
            if (looksLikeHeader(header)) {
                // 按表头建立列映射：列顺序/列名大小写/中英文差异都不再影响解析
                columnMap = buildColumnMap(header);
                for (int i = 0; i < header.size(); ++i) {
                    const QString name = header.at(i).trimmed();
                    if (name.isEmpty())
                        continue;
                    const int col = columnForHeader(name);
                    if (col < 0)
                        result.dataset.ignoredColumns << name;
                    else
                        result.dataset.columns << QString::fromLatin1(kDefaultColumns[col]);
                }
                if (columnMap[static_cast<size_t>(Column::Id)] < 0
                    || columnMap[static_cast<size_t>(Column::X)] < 0
                    || columnMap[static_cast<size_t>(Column::Y)] < 0) {
                    result.errorMessage =
                        QStringLiteral("表头缺少必需列（至少需要 DefectId / X / Y），实际表头：%1")
                            .arg(line.trimmed());
                    return result;
                }
                continue;
            }
        }

        // 热路径：一遍扫描逗号、只切出需要的列（不再 split 出整行字段）
        DefectPoint p;
        RowStatus status = RowStatus::Invalid;
        if (parseLineScanned(line, columnMap, &p, &status)) {
            extent.add(p.x, p.y);
            points.push_back(p);
            ++result.dataset.totalLines;
        } else {
            ++result.dataset.skippedLines;
            if (status == RowStatus::Short)
                ++result.dataset.shortLines;
            else
                ++result.dataset.invalidValueLines;
        }
    }

    file.close();

    if (points.isEmpty()) {
        result.errorMessage = QStringLiteral("数据集为空或格式不正确（期望列：%1）")
                                  .arg(headerLine());
        return result;
    }

    result.dataset.points = std::move(points);
    result.dataset.waferRadius = radiusMm > 0.0f ? radiusMm : extent.derivedRadius();
    result.dataset.sourceFile = QFileInfo(filePath).absoluteFilePath();
    result.ok = true;
    return result;
}

} // namespace wafer
