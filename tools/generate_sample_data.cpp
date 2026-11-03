#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cmath>

/**
 * 示例数据生成器：生成模拟晶圆缺陷检测 CSV 数据集。
 *
 * 列定义（与 CsvDatasetParser 期望一致）：
 *   DefectId,X,Y,Z,Area,Severity,Type,Layer
 *   X/Y 单位 mm（晶圆半径 150mm），Z 单位 um，Area 单位 um²。
 *
 * 数据特点（便于演示热力图与统计）：
 *  - 整体密度沿半径向外递增（边缘工艺更易产生缺陷）；
 *  - 叠加若干"簇"（cluster）模拟局部工艺异常；
 *  - 等级按面积加权抽样，使高等级缺陷更稀有。
 *
 * 这是独立的控制台工具，只依赖 Qt6::Core，不参与主程序构建。
 */
namespace {

struct Cluster {
    float cx;          // 簇中心 X (mm)
    float cy;          // 簇中心 Y (mm)
    float sigma;       // 高斯半径 (mm)
    int count;         // 簇内缺陷数量
    float severityBias; // 等级偏移：正值更严重
};

unsigned int nextSeed(unsigned int &state)
{
    // xorshift32：无需 <random>，保证跨平台输出稳定可复现
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/** [0,1) 均匀分布 */
double uniform(unsigned int &state)
{
    return static_cast<double>(nextSeed(state) & 0xFFFFFFu) / 16777216.0;
}

/** 标准正态分布（Box-Muller） */
double gaussian(unsigned int &state)
{
    const double u1 = std::max(1e-12, uniform(state));
    const double u2 = uniform(state);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
}

const char *typeKey(int index)
{
    static const char *const keys[] = {"PARTICLE", "SCRATCH", "PATTERN", "VOID", "RESIDUE",
                                       "OTHER"};
    if (index < 0 || index >= 6)
        return "OTHER";
    return keys[index];
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString outPath =
        argc > 1 ? QString::fromLocal8Bit(argv[1])
                 : QDir::current().absoluteFilePath(QStringLiteral("data/wafer_defects.csv"));

    const float waferRadius = 150.0f; // mm

    // 缺陷簇：模拟真实产线上常见的局部工艺异常分布
    const Cluster clusters[] = {
        {92.0f, 64.0f, 9.0f, 260, 1.2f},    // 边缘重灾区，高等级集中
        {-108.0f, 42.0f, 11.0f, 220, 0.8f},
        {18.0f, -120.0f, 8.0f, 180, 1.5f},
        {-52.0f, -88.0f, 13.0f, 150, 0.3f},
        {126.0f, -36.0f, 7.0f, 120, 1.8f},
        {0.0f, 0.0f, 18.0f, 90, -0.8f},     // 晶圆中心：缺陷少且轻微
    };

    const int backgroundCount = 2600; // 全片随机背景缺陷

    QFile file(outPath);
    QDir().mkpath(QFileInfo(outPath).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(stderr) << "无法写入文件: " << outPath << "\n";
        return 1;
    }

    QTextStream out(&file);
    out << "DefectId,X,Y,Z,Area,Severity,Type,Layer\n";

    unsigned int seed = 0x5EED1234u; // 固定种子，保证数据集可复现
    int id = 1;
    int written = 0;

    // ---- 1) 背景缺陷：密度随半径增大 ----
    while (written < backgroundCount) {
        const double r = waferRadius * std::sqrt(uniform(seed)) * 1.02;
        const double theta = uniform(seed) * 2.0 * M_PI;

        // 半径越靠外，接受概率越高（模拟边缘工艺不均匀）
        const double acceptProb = 0.25 + 0.75 * (r / waferRadius);
        if (uniform(seed) > acceptProb)
            continue;

        const double x = r * std::cos(theta);
        const double y = r * std::sin(theta);
        if (std::sqrt(x * x + y * y) > waferRadius)
            continue;

        const double area = std::max(0.05, std::exp(gaussian(seed) * 1.15 + 1.6));
        int severity = static_cast<int>(area / 9.0) + 1;
        severity = std::max(1, std::min(5, severity));

        const double z = std::abs(gaussian(seed)) * 0.9 * (1.0 + severity * 0.35);
        const int type = static_cast<int>(uniform(seed) * 6.0) % 6;
        const int layer = 1 + (static_cast<int>(uniform(seed) * 6.0) % 6);

        out << id++ << ',' << QString::number(x, 'f', 4) << ',' << QString::number(y, 'f', 4)
            << ',' << QString::number(z, 'f', 3) << ',' << QString::number(area, 'f', 4) << ','
            << severity << ',' << typeKey(type) << ',' << layer << '\n';
        ++written;
    }

    // ---- 2) 缺陷簇 ----
    for (const Cluster &cluster : clusters) {
        for (int i = 0; i < cluster.count; ++i) {
            const double x = cluster.cx + gaussian(seed) * cluster.sigma;
            const double y = cluster.cy + gaussian(seed) * cluster.sigma;
            if (std::sqrt(x * x + y * y) > waferRadius)
                continue;

            const double area = std::max(0.08, std::exp(gaussian(seed) * 1.25 + 2.0));
            int severity = static_cast<int>(area / 8.0 + cluster.severityBias + 1.0);
            severity = std::max(1, std::min(5, severity));

            const double z = std::abs(gaussian(seed)) * 0.8 * (1.0 + severity * 0.45);
            // 簇内类型更集中：以簇索引决定主类型，少量抖动
            const int type = static_cast<int>(uniform(seed) * 2.2) % 6;
            const int layer = 1 + (i % 5);

            out << id++ << ',' << QString::number(x, 'f', 4) << ',' << QString::number(y, 'f', 4)
                << ',' << QString::number(z, 'f', 3) << ',' << QString::number(area, 'f', 4)
                << ',' << severity << ',' << typeKey(type) << ',' << layer << '\n';
            ++written;
        }
    }

    file.close();

    QTextStream(stdout) << "已生成数据集: " << outPath << "\n"
                        << "  缺陷点总数: " << written << "\n"
                        << "  晶圆半径: " << waferRadius << " mm\n";
    return 0;
}
