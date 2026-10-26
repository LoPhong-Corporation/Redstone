#pragma once

// Per-server run options that can be edited AFTER a server exists:
// name, port, RAM, Java, jar, JVM flags (with presets), extra server
// arguments, crash auto-restart, graceful-stop timeout and automatic
// backups. Changes are persisted immediately on Save and take effect the
// next time the server starts (except the automatic-backup schedule,
// which also needs a restart because it is armed in Start()).

#include "config/ConfigManager.hpp"
#include "server/ServerManager.hpp"

#include <QDialog>

#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace ui
{

class ToggleSwitch;

class ServerOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    ServerOptionsDialog(
        std::shared_ptr<server::MinecraftServer> server,
        std::shared_ptr<server::ServerManager> serverManager,
        std::shared_ptr<config::ConfigManager> configManager,
        QWidget* parent = nullptr);

private slots:
    void OnSaveClicked();
    void OnBrowseJava();
    void OnBrowseJar();
    void OnJvmPresetChanged(int index);

private:
    void LoadFromConfig();

    std::shared_ptr<server::MinecraftServer> server_;
    std::shared_ptr<server::ServerManager> serverManager_;
    std::shared_ptr<config::ConfigManager> configManager_;

    QLineEdit* editName_ = nullptr;
    QSpinBox* spinPort_ = nullptr;
    QSpinBox* spinMinMemory_ = nullptr;
    QSpinBox* spinMaxMemory_ = nullptr;
    QLineEdit* editJava_ = nullptr;
    QLineEdit* editJar_ = nullptr;
    QLineEdit* editMinecraftVersion_ = nullptr;
    QComboBox* comboJvmPreset_ = nullptr;
    QLineEdit* editJvmArgs_ = nullptr;
    QLineEdit* editServerArgs_ = nullptr;
    ToggleSwitch* toggleAutoRestart_ = nullptr;
    QSpinBox* spinStopTimeout_ = nullptr;
    ToggleSwitch* toggleAutoBackup_ = nullptr;
    QSpinBox* spinBackupInterval_ = nullptr;
    QSpinBox* spinBackupRetention_ = nullptr;
    QLabel* labelStatus_ = nullptr;
};

} // namespace ui
