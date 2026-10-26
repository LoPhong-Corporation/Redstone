#pragma once

// Player management for one server. Works even when the server is stopped
// (lists are read straight from whitelist.json / ops.json /
// banned-players.json / banned-ips.json); every *change* is sent as the
// equivalent console command to the RUNNING server so Minecraft itself
// resolves usernames to UUIDs and keeps the files consistent.
//
// Tabs: Online (live list tracked from join/leave log lines, with per-player
// actions), Whitelist, Operators, Bans (players + IPs), and Quick settings
// (broadcast, default gamemode, difficulty, AFK timeout, whitelist toggle...).

#include <QDialog>

#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace server
{
class MinecraftServer;
}

namespace ui
{

class PlayersDialog : public QDialog
{
    Q_OBJECT

public:
    PlayersDialog(std::shared_ptr<server::MinecraftServer> server, QWidget* parent = nullptr);

private:
    QWidget* BuildOnlineTab();
    QWidget* BuildWhitelistTab();
    QWidget* BuildOpsTab();
    QWidget* BuildBansTab();
    QWidget* BuildQuickTab();

    void RefreshOnline();
    void RefreshFiles();
    void UpdateEnabledState();

    // Sends one console command; returns false (and explains in the status
    // line) if the server is not running. Schedules a list refresh since
    // Minecraft rewrites the JSON files shortly after the command runs.
    bool Send(const QString& command);
    // Validates a player name; shows a message and returns an empty string if invalid.
    QString RequirePlayerName(const QString& raw);
    void SetStatus(const QString& text);

    std::shared_ptr<server::MinecraftServer> server_;

    QLabel* labelStatus_ = nullptr;
    QLabel* labelSummary_ = nullptr;
    QTimer* timer_ = nullptr;

    // Online tab
    QListWidget* listOnline_ = nullptr;
    QLineEdit* editOnlineName_ = nullptr;
    QComboBox* comboGamemode_ = nullptr;

    // Lists
    QTableWidget* tableWhitelist_ = nullptr;
    QLineEdit* editWhitelistName_ = nullptr;
    QTableWidget* tableOps_ = nullptr;
    QLineEdit* editOpName_ = nullptr;
    QTableWidget* tableBans_ = nullptr;
    QLineEdit* editBanTarget_ = nullptr;
    QLineEdit* editBanReason_ = nullptr;

    // Quick settings
    QLineEdit* editBroadcast_ = nullptr;
    QComboBox* comboDefaultGamemode_ = nullptr;
    QComboBox* comboDifficulty_ = nullptr;
    QSpinBox* spinIdleTimeout_ = nullptr;

    // Buttons that need a running server
    QList<QPushButton*> liveButtons_;
};

} // namespace ui
