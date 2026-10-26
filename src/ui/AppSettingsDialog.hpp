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

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QComboBox;
class QFontComboBox;
class QSpinBox;
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

signals:
    void settingsApplied();

private slots:
    void OnApplyClicked();

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

    // System
    ToggleSwitch* toggleStartWithWindows_ = nullptr;
    ToggleSwitch* toggleSilent_ = nullptr;
    ToggleSwitch* toggleMinimizeToTray_ = nullptr;
    ToggleSwitch* toggleTrayNotifications_ = nullptr;
    ToggleSwitch* toggleConfirmStop_ = nullptr;
};

} // namespace ui
