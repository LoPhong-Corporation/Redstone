#pragma once

// The application's main window, now built on Qt Widgets instead of raw
// Win32 controls. The process/console/server backend underneath is
// unchanged (still native Win32 CreateProcessW + Job Objects) - only this
// UI layer talks Qt.
//
// Threading contract: every method on this class must only run on the Qt
// UI (main) thread. Background threads (console readers, the watcher
// thread in MinecraftServer) notify this window exclusively through
// NotifyEvent(), which marshals onto the UI thread via
// QMetaObject::invokeMethod - the Qt equivalent of the PostMessageW
// pattern used by a raw Win32 window.

#include "config/ConfigManager.hpp"
#include "core/EventDispatcher.hpp"
#include "server/ServerManager.hpp"

#include <QMainWindow>
#include <QMap>
#include <QString>

#include <memory>
#include <string>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class QToolBar;
class QSystemTrayIcon;
class QCloseEvent;
class QAction;
namespace remote { class RemoteControlServer; }
class QComboBox;
class QTimer;
class QAbstractButton;

namespace ui
{

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(
        std::shared_ptr<server::ServerManager> serverManager,
        std::shared_ptr<config::ConfigManager> configManager,
        std::shared_ptr<core::EventDispatcher> events,
        QWidget* parent = nullptr);

    // Thread-safe: callable from any thread. Marshals the actual handling
    // onto the Qt UI thread.
    void NotifyEvent(const core::AppEvent& event);

protected:
    // Closing the window (the X button) hides it to the system tray
    // instead of quitting - the app (and any running servers) keeps
    // going in the background. Use the tray icon's "Thoát" action, or
    // Task Manager, to actually end the process.
    void closeEvent(QCloseEvent* event) override;

private slots:
    void OnAddServerClicked();
    void OnRemoveServerClicked();
    void OnStartClicked();
    void OnStopClicked();
    void OnRestartClicked();
    void OnSendCommandClicked();
    void OnServerSelectionChanged();
    void OnOpenLibraryClicked();    // Modrinth mods/plugins/modpacks browser
    void OnOpenPropertiesClicked(); // Phase 3: server.properties editor
    void OnOpenPlayersClicked();    // Phase 3: whitelist/op UI
    void OnOpenBackupsClicked();    // Phase 4: backup manager
    void OnAcceptEulaClicked();     // Phase 3: EULA helper
    void OnImportServerClicked();       // Fork-inspired: import an existing server folder
    void OnOpenInstalledAddonsClicked(); // Fork-inspired: view/remove installed plugins/mods
    void OnOpenAdvancedSettingsClicked(); // Phase 6 tunnel + scheduled restart
    void OnOpenAppSettingsClicked();      // global app settings (theme, console, system)
    void OnOpenServerOptionsClicked();    // per-server run options (RAM, JVM flags, backups...)
    void OnAppSettingsApplied();
    void ApplyRemoteSettings(bool interactive);          // theme/console settings changed: refresh the chrome
    void OnEditConfigFileClicked();       // Fork-inspired generic file editor

private:
    void BuildToolbar();
    void ApplyThemedIcons();      // (re)renders every glyph icon in the current theme colours
    void ApplyConsoleSettings();  // font / wrapping / history size from AppSettings
    void AppendConsoleLineFormatted(const QString& line, bool withTimestamp);
    [[nodiscard]] bool PassesConsoleFilter(const QString& line) const;
    void OnConsoleFilterChanged();
    void UpdateFilterCountLabel();
    void RegisterIcon(QAction* action, char16_t glyph);
    void RegisterIcon(QAbstractButton* button, char16_t glyph, bool onAccent);
    void BuildTrayIcon();
    void ShowAndRaiseWindow();
    void HandleAppEvent(core::AppEvent event); // always runs on the UI thread
    void AcceptEulaForCurrentServer(); // shared by the toolbar action and the auto-prompt
    void RefreshServerList();
    void UpdateServerListItem(const std::wstring& serverId); // perf: patch one row instead of a full rebuild
    void RefreshSelectedServerConsole();
    void RefreshControlsForState();
    void UpdateStatsLabel(); // rebuilds labelStats_ from the two cached parts below
    void AppendConsoleLine(const QString& line);
    [[nodiscard]] std::wstring GetSelectedServerId() const;

    std::shared_ptr<server::ServerManager> serverManager_;
    std::shared_ptr<config::ConfigManager> configManager_;
    std::shared_ptr<core::EventDispatcher> events_;

    struct ThemedIcon
    {
        QAction* action = nullptr;
        QAbstractButton* button = nullptr;
        char16_t glyph = 0;
        bool onAccent = false; // true for filled accent/danger buttons (white glyph)
    };
    std::vector<ThemedIcon> themedIcons_;

    remote::RemoteControlServer* remote_ = nullptr;
    QString remoteActiveToken_;
    quint16 remoteActivePort_ = 0;

    QToolBar* toolbar_ = nullptr;
    QSystemTrayIcon* trayIcon_ = nullptr;
    bool trayNotificationShown_ = false; // only show the "still running" balloon once per session

    // Startup/empty-state page (index 0) vs. the normal server view
    // (index 1) - shown depending on whether any server profile exists
    // yet, so a brand new install greets the user with a call-to-action
    // instead of a blank console.
    QStackedWidget* stackedPages_ = nullptr;

    QListWidget* listServers_ = nullptr;
    QMap<QString, QListWidgetItem*> serverListItems_; // serverId -> row, for O(1) per-server updates
    QPushButton* buttonAdd_ = nullptr;
    QPushButton* buttonRemove_ = nullptr;
    QLabel* labelStatus_ = nullptr;
    QLabel* labelStats_ = nullptr;
    QPlainTextEdit* editConsole_ = nullptr;

    // Console output filter (free text + category). The server keeps the full
    // log; the view is rebuilt from it whenever the filter changes.
    QLineEdit* editFilter_ = nullptr;
    QComboBox* comboFilterKind_ = nullptr;
    QLabel* labelFilterCount_ = nullptr;
    QTimer* filterTimer_ = nullptr; // debounce for typing
    int shownLineCount_ = 0;
    int totalLineCount_ = 0;
    QLineEdit* editCommand_ = nullptr;
    QPushButton* buttonSend_ = nullptr;
    QPushButton* buttonStart_ = nullptr;
    QPushButton* buttonStop_ = nullptr;
    QPushButton* buttonRestart_ = nullptr;

    // Cached pieces of the stats line, so a cpu/ram sample and a
    // separately-arriving TPS reply (see core::AppEvent's cpuPercent
    // sentinel) don't clobber each other - each updates only its own
    // piece, then UpdateStatsLabel() joins them for display.
    QString cachedCpuRamText_;
    QString cachedTpsText_;
};

} // namespace ui
