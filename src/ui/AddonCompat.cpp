#include "ui/AddonCompat.hpp"

#include <QFile>
#include <QRegularExpression>

namespace ui
{

QString DetectMinecraftVersion(const core::ServerConfig& cfg)
{
    if (!cfg.minecraftVersion.empty())
    {
        return QString::fromStdWString(cfg.minecraftVersion).trimmed();
    }

    QFile log(QString::fromStdWString((cfg.directory / L"logs" / L"latest.log").wstring()));
    if (log.open(QIODevice::ReadOnly))
    {
        const QString head = QString::fromUtf8(log.read(96 * 1024));
        static const QRegularExpression logPattern(QStringLiteral("Starting minecraft server version (\\d+(?:\\.\\d+){1,2})"));
        const auto m = logPattern.match(head);
        if (m.hasMatch())
        {
            return m.captured(1);
        }
    }

    static const QRegularExpression jarPattern(QStringLiteral("(\\d+\\.\\d+(?:\\.\\d+)?)"));
    const auto m = jarPattern.match(QString::fromStdWString(cfg.serverJar.filename().wstring()));
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList LoadersForServer(const core::ServerConfig& cfg, const QString& projectType)
{
    const QString st = QString::fromStdWString(cfg.serverType).trimmed().toLower();
    if (projectType == QLatin1String("datapack"))
    {
        return {QStringLiteral("datapack")};
    }
    if (projectType == QLatin1String("plugin"))
    {
        if (st == QLatin1String("paper")) return {QStringLiteral("paper"), QStringLiteral("spigot"), QStringLiteral("bukkit")};
        if (st == QLatin1String("purpur")) return {QStringLiteral("purpur"), QStringLiteral("paper"), QStringLiteral("spigot"), QStringLiteral("bukkit")};
        if (st == QLatin1String("folia")) return {QStringLiteral("folia")};
        if (st == QLatin1String("spigot")) return {QStringLiteral("spigot"), QStringLiteral("bukkit")};
        if (st == QLatin1String("bukkit")) return {QStringLiteral("bukkit")};
        return {};
    }
    if (projectType == QLatin1String("mod") || projectType == QLatin1String("modpack"))
    {
        if (st == QLatin1String("fabric")) return {QStringLiteral("fabric")};
        if (st == QLatin1String("quilt")) return {QStringLiteral("quilt"), QStringLiteral("fabric")};
        if (st == QLatin1String("forge")) return {QStringLiteral("forge")};
        if (st == QLatin1String("neoforge")) return {QStringLiteral("neoforge")};
        return {};
    }
    return {};
}

QString DefaultProjectTypeFor(const core::ServerConfig& cfg)
{
    const QString st = QString::fromStdWString(cfg.serverType).trimmed().toLower();
    static const QStringList pluginPlatforms = {
        QStringLiteral("paper"), QStringLiteral("spigot"), QStringLiteral("purpur"), QStringLiteral("folia"), QStringLiteral("bukkit")};
    return pluginPlatforms.contains(st) ? QStringLiteral("plugin") : QStringLiteral("mod");
}

} // namespace ui
