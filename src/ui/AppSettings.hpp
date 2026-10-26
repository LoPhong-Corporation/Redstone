#pragma once

// Global, per-user UI preferences (not per-server). Persisted as a small
// INI file next to the executable (data/app_settings.ini) via QSettings,
// so there is no extra dependency and the file is easy to hand-edit.

#include <QString>

namespace ui
{

struct AppSettings
{
    enum class ThemeMode { System = 0, Dark = 1, Light = 2 };

    // --- Appearance ---
    ThemeMode theme = ThemeMode::Dark;
    int accentIndex = 0;        // index into FluentTheme's accent presets
    int fontScalePercent = 100; // 90 / 100 / 115 ...

    // --- Console ---
    QString consoleFontFamily = QStringLiteral("Cascadia Mono");
    int consoleFontSize = 10;
    bool consoleWrap = true;
    bool consoleColorize = true;     // colour lines by level (WARN/ERROR/manager/tunnel)
    bool consoleTimestamps = false;  // prefix each received line with the local time
    bool consoleAutoScroll = true;
    int consoleMaxLines = 5000;

    // --- System ---
    bool minimizeToTrayOnClose = true;
    bool trayNotifications = true;   // balloon on server crash / start / stop
    bool confirmStopRestart = true;  // ask before Stop / Restart

    // --- Remote control (see remote::RemoteControlServer / redstonectl) ---
    bool remoteEnabled = false;      // off by default: nothing listens until the user opts in
    int remotePort = 25585;          // loopback only; reach it from other machines through SSH
    QString remoteToken;             // shared secret (never sent over the wire, only an HMAC of it)

    // Loads from data/app_settings.ini (missing file/keys = defaults).
    static AppSettings Load();
    void Save() const;
};

// Process-wide current settings (loaded once at startup, updated when the
// settings dialog applies changes). UI thread only.
AppSettings& CurrentSettings();

} // namespace ui
