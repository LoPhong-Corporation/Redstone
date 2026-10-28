#include "ui/AppSettings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <algorithm>

namespace ui
{
namespace
{
QString SettingsPath()
{
    // The working directory is pinned to the exe folder in Application::Run,
    // but use the explicit path so this also works from tests/tools.
    QDir dir(QCoreApplication::applicationDirPath());
    dir.mkpath(QStringLiteral("data"));
    return dir.filePath(QStringLiteral("data/app_settings.ini"));
}
} // namespace

int AppSettings::PeekUiScalePercent(const QString& executableDirectory)
{
    QSettings ini(QDir(executableDirectory).filePath(QStringLiteral("data/app_settings.ini")), QSettings::IniFormat);
    const int v = ini.value(QStringLiteral("appearance/uiScale"), 0).toInt();
    return v == 0 ? 0 : std::clamp(v, 100, 250);
}

AppSettings AppSettings::Load()
{
    AppSettings s;
    QSettings ini(SettingsPath(), QSettings::IniFormat);

    s.theme = static_cast<ThemeMode>(std::clamp(ini.value(QStringLiteral("appearance/theme"), 1).toInt(), 0, 2));
    s.accentIndex = std::max(0, ini.value(QStringLiteral("appearance/accent"), 0).toInt());
    s.fontScalePercent = std::clamp(ini.value(QStringLiteral("appearance/fontScale"), 100).toInt(), 80, 200);
    s.uiScalePercent = ini.value(QStringLiteral("appearance/uiScale"), 0).toInt();
    if (s.uiScalePercent != 0) s.uiScalePercent = std::clamp(s.uiScalePercent, 100, 250);
    s.language = ini.value(QStringLiteral("appearance/language"), QStringLiteral("auto")).toString();
    s.windowGeometry = QByteArray::fromBase64(ini.value(QStringLiteral("window/geometry")).toByteArray());

    s.consoleFontFamily = ini.value(QStringLiteral("console/fontFamily"), s.consoleFontFamily).toString();
    s.consoleFontSize = std::clamp(ini.value(QStringLiteral("console/fontSize"), 10).toInt(), 7, 28);
    s.consoleWrap = ini.value(QStringLiteral("console/wrap"), true).toBool();
    s.consoleColorize = ini.value(QStringLiteral("console/colorize"), true).toBool();
    s.consoleTimestamps = ini.value(QStringLiteral("console/timestamps"), false).toBool();
    s.consoleAutoScroll = ini.value(QStringLiteral("console/autoScroll"), true).toBool();
    s.consoleMaxLines = std::clamp(ini.value(QStringLiteral("console/maxLines"), 5000).toInt(), 500, 50000);

    s.minimizeToTrayOnClose = ini.value(QStringLiteral("system/minimizeToTray"), true).toBool();
    s.trayNotifications = ini.value(QStringLiteral("system/trayNotifications"), true).toBool();
    s.confirmStopRestart = ini.value(QStringLiteral("system/confirmStopRestart"), true).toBool();

    s.remoteEnabled = ini.value(QStringLiteral("remote/enabled"), false).toBool();
    s.remotePort = std::clamp(ini.value(QStringLiteral("remote/port"), 25585).toInt(), 1024, 65535);
    s.remoteToken = ini.value(QStringLiteral("remote/token")).toString();
    return s;
}

void AppSettings::Save() const
{
    QSettings ini(SettingsPath(), QSettings::IniFormat);
    ini.setValue(QStringLiteral("appearance/theme"), static_cast<int>(theme));
    ini.setValue(QStringLiteral("appearance/accent"), accentIndex);
    ini.setValue(QStringLiteral("appearance/fontScale"), fontScalePercent);
    ini.setValue(QStringLiteral("appearance/uiScale"), uiScalePercent);
    ini.setValue(QStringLiteral("appearance/language"), language);
    ini.setValue(QStringLiteral("window/geometry"), windowGeometry.toBase64());

    ini.setValue(QStringLiteral("console/fontFamily"), consoleFontFamily);
    ini.setValue(QStringLiteral("console/fontSize"), consoleFontSize);
    ini.setValue(QStringLiteral("console/wrap"), consoleWrap);
    ini.setValue(QStringLiteral("console/colorize"), consoleColorize);
    ini.setValue(QStringLiteral("console/timestamps"), consoleTimestamps);
    ini.setValue(QStringLiteral("console/autoScroll"), consoleAutoScroll);
    ini.setValue(QStringLiteral("console/maxLines"), consoleMaxLines);

    ini.setValue(QStringLiteral("system/minimizeToTray"), minimizeToTrayOnClose);
    ini.setValue(QStringLiteral("system/trayNotifications"), trayNotifications);
    ini.setValue(QStringLiteral("system/confirmStopRestart"), confirmStopRestart);
    ini.setValue(QStringLiteral("remote/enabled"), remoteEnabled);
    ini.setValue(QStringLiteral("remote/port"), remotePort);
    ini.setValue(QStringLiteral("remote/token"), remoteToken);
    ini.sync();
}

std::atomic<bool>& RestartRequested()
{
    static std::atomic<bool> flag{false};
    return flag;
}

AppSettings& CurrentSettings()
{
    static AppSettings settings = AppSettings::Load();
    return settings;
}

} // namespace ui
