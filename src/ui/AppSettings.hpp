#pragma once

// Global, per-user UI preferences (not per-server). Persisted as a small
// INI file next to the executable (data/app_settings.ini) via QSettings,
// so there is no extra dependency and the file is easy to hand-edit.

#include <QByteArray>
#include <QString>

#include <atomic>

namespace ui
{

struct AppSettings
{
    enum class ThemeMode { System = 0, Dark = 1, Light = 2 };

    // --- Appearance ---
    ThemeMode theme = ThemeMode::Dark;
    int accentIndex = 0;        // index into FluentTheme's accent presets
    int fontScalePercent = 100; // text size inside the app, 80..200 (also Ctrl +/-/0)
    int uiScalePercent = 0;     // whole-window scale: 0 = automatic (follow Windows display scaling), else 100..250.
                                // Applied through QT_SCALE_FACTOR, so it takes effect after a restart.
    QString language = QStringLiteral("auto"); // "auto" | "vi" | "en" | any dictionary in translations/

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

    // Window placement (QMainWindow::saveGeometry, base64 in the ini); empty = pick a size from the screen.
    QByteArray windowGeometry;

    // Reads only the whole-window scale. Needed BEFORE QApplication exists, because
    // QT_SCALE_FACTOR must be set before Qt initialises its screens.
    static int PeekUiScalePercent(const QString& executableDirectory);

    // Loads from data/app_settings.ini (missing file/keys = defaults).
    static AppSettings Load();
    void Save() const;
};

// Set by the UI when the user asked to restart the app (e.g. after changing the
// language / window scale). Application::Run starts the new instance only AFTER
// every server has been stopped, so the new process never fights for their ports.
std::atomic<bool>& RestartRequested();

// Process-wide current settings (loaded once at startup, updated when the
// settings dialog applies changes). UI thread only.
AppSettings& CurrentSettings();

} // namespace ui
