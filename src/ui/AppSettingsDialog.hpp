#pragma once

// Global (not per-server) application settings, organised like the
// Windows 11 Settings app: a category list on the left, "setting cards"
// on the right.
//   - Giao diện: theme (system/dark/light), accent colour, text size
//   - Console: font, wrapping, colouring, timestamps, auto-scroll, history size
//   - Điều khiển từ xa: enable the redstonectl endpoint, port, access token
//   - Hệ thống: start with Windows (+ silent mode), tray behaviour, confirmations
// Changes are applied live (theme/console) when the user presses "Áp dụng"
// and persisted to data/app_settings.ini; the main window listens to
// settingsApplied() to refresh itself.

#include "remote/Access.hpp"

#include <QDialog>
#include <QList>
#include <QPair>

class QListWidget;
class QStackedWidget;
class QComboBox;
class QFontComboBox;
class QSpinBox;
class QTimer;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QLineEdit;

namespace ui
{

class ToggleSwitch;

class AppSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AppSettingsDialog(QWidget* parent = nullptr);

    // (id, name) of every server - lets an access account be limited to some of them.
    void SetKnownServers(const QList<QPair<QString, QString>>& servers);

signals:
    void settingsApplied();
    void restartRequested(); // language / window scale changed and the user chose "restart now"

private slots:
    void OnApplyClicked();
    void RefreshSshStatus();
    void RefreshAccountList(int select);
    void LoadAccountForm();
    void CommitAccountForm();
    void RefreshAudit();
    void OnInstallSshClicked();

private:
    QWidget* BuildAppearancePage();
    QWidget* BuildConsolePage();
    QWidget* BuildSystemPage();
    QWidget* BuildRemotePage();
    void LoadFromSettings();

    QListWidget* categories_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel* labelStatus_ = nullptr;

    // Appearance
    QComboBox* comboTheme_ = nullptr;
    QComboBox* comboAccent_ = nullptr;
    QComboBox* comboFontScale_ = nullptr;
    QComboBox* comboUiScale_ = nullptr;
    QComboBox* comboLanguage_ = nullptr;

    // Console
    QFontComboBox* comboConsoleFont_ = nullptr;
    QSpinBox* spinConsoleFontSize_ = nullptr;
    QSpinBox* spinConsoleMaxLines_ = nullptr;
    ToggleSwitch* toggleWrap_ = nullptr;
    ToggleSwitch* toggleColorize_ = nullptr;
    ToggleSwitch* toggleTimestamps_ = nullptr;
    ToggleSwitch* toggleAutoScroll_ = nullptr;

    // Remote control
    ToggleSwitch* toggleRemote_ = nullptr;
    QSpinBox* spinRemotePort_ = nullptr;
    QLineEdit* editRemoteToken_ = nullptr;
    QList<QPair<QString, QString>> knownServers_;
    QList<remote::Account> accounts_;
    int currentAccount_ = -1;
    bool loadingForm_ = false;
    QListWidget* listAccounts_ = nullptr;
    QLineEdit* editAccountName_ = nullptr;
    QComboBox* comboAccountRole_ = nullptr;
    QLabel* labelRoleHelp_ = nullptr;
    QCheckBox* checkAllServers_ = nullptr;
    QListWidget* listAccountServers_ = nullptr;
    QPlainTextEdit* viewAudit_ = nullptr;
    QLabel* labelSshStatus_ = nullptr;
    QPushButton* buttonSshInstall_ = nullptr;
    QTimer* sshPollTimer_ = nullptr;
    int sshPollsLeft_ = 0;

    // System
    ToggleSwitch* toggleStartWithWindows_ = nullptr;
    ToggleSwitch* toggleSilent_ = nullptr;
    ToggleSwitch* toggleMinimizeToTray_ = nullptr;
    ToggleSwitch* toggleTrayNotifications_ = nullptr;
    ToggleSwitch* toggleConfirmStop_ = nullptr;
};

} // namespace ui
