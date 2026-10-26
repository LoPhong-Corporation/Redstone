#pragma once

// Phase 6 (tunnel companion process) + Fork-inspired scheduled restart,
// combined into one "advanced settings" dialog since both are optional,
// per-server, set-once-and-forget settings edited after a server already
// exists (unlike AddServerDialog's fields, which matter at creation time).

#include "config/ConfigManager.hpp"
#include "server/ServerManager.hpp"

#include <QDialog>

#include <memory>
#include <thread>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QPushButton;
class QLabel;

namespace ui
{

class AdvancedSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    AdvancedSettingsDialog(
        std::shared_ptr<server::MinecraftServer> server,
        std::shared_ptr<server::ServerManager> serverManager,
        std::shared_ptr<config::ConfigManager> configManager,
        QWidget* parent = nullptr);
    ~AdvancedSettingsDialog() override;

private slots:
    void OnPresetChanged();
    void OnBrowseTunnelExecutable();
    void OnStartTunnelClicked();
    void OnStopTunnelClicked();
    void OnOpenFirewallPortClicked();
    void OnSaveClicked();

private:
    void RefreshTunnelStatus();
    void SetTunnelButtonsEnabled(bool enabled);
    void ApplyFieldsToConfig(); // pushes the current form values into server_'s config and persists them

    std::shared_ptr<server::MinecraftServer> server_;
    std::shared_ptr<server::ServerManager> serverManager_;
    std::shared_ptr<config::ConfigManager> configManager_;

    QComboBox* comboPreset_ = nullptr;
    QLineEdit* editTunnelExecutable_ = nullptr;
    QLineEdit* editTunnelArguments_ = nullptr;
    QCheckBox* checkTunnelEnabled_ = nullptr;
    QCheckBox* checkTunnelAutoStart_ = nullptr;
    QPushButton* buttonStartTunnel_ = nullptr;
    QPushButton* buttonStopTunnel_ = nullptr;
    QLabel* labelTunnelStatus_ = nullptr;
    QLabel* labelPresetHint_ = nullptr;

    QCheckBox* checkScheduledRestartEnabled_ = nullptr;
    QSpinBox* spinRestartIntervalHours_ = nullptr;
    QCheckBox* checkAutoStartWithApp_ = nullptr;

    QPushButton* buttonOpenFirewallPort_ = nullptr;
    QLabel* labelFirewallStatus_ = nullptr;

    QLabel* labelSaveStatus_ = nullptr;

    // Starting/stopping the tunnel process (and running the firewall
    // rule command) happen off the UI thread (the same reasoning as everywhere else in this app: never risk blocking
    // the window on a CreateProcessW/TerminateProcess call). Joinable
    // members rather than detached threads, so the destructor can
    // guarantee neither is still calling QMetaObject::invokeMethod(this,
    // ...) once this dialog's widgets start being torn down - same
    // pattern as ui::BackupsDialog.
    std::jthread tunnelStartThread_;
    std::jthread tunnelStopThread_;
    std::jthread firewallThread_;
};

} // namespace ui
