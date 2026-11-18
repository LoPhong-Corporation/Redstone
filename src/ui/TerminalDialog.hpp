#pragma once

// Dedicated Application Terminal for Redstone Minecraft Server Manager.
// Provides an interactive CLI console embedded inside the GUI to manage
// servers (create, delete, start, stop, restart, console, backup, props,
// players, audit, etc.) with command history and shortcut support.

#include "config/ConfigManager.hpp"
#include "core/EventDispatcher.hpp"
#include "server/ServerManager.hpp"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <memory>

class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;

namespace ui
{

class TerminalDialog : public QDialog
{
    Q_OBJECT

public:
    TerminalDialog(
        std::shared_ptr<server::ServerManager> serverManager,
        std::shared_ptr<config::ConfigManager> configManager,
        std::shared_ptr<core::EventDispatcher> events,
        const std::wstring& initialServerId = L"",
        QWidget* parent = nullptr);
    ~TerminalDialog() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void OnExecuteCommand();
    void OnClearClicked();
    void OnHelpClicked();
    void OnExternalTerminalClicked();

private:
    void PrintBanner();
    void PrintLine(const QString& text, const QString& colorHex = QString());
    void PrintError(const QString& text);
    void PrintSuccess(const QString& text);
    void PrintWarning(const QString& text);
    void PrintInfo(const QString& text);
    void UpdatePrompt();

    void ExecuteLine(const QString& rawLine);
    std::shared_ptr<server::MinecraftServer> ResolveTargetServer(const QString& key, QString* error = nullptr);

    std::shared_ptr<server::ServerManager> serverManager_;
    std::shared_ptr<config::ConfigManager> configManager_;
    std::shared_ptr<core::EventDispatcher> events_;

    std::wstring currentServerId_;
    QStringList history_;
    int historyIndex_ = -1;

    QLabel* labelPrompt_ = nullptr;
    QPlainTextEdit* textOutput_ = nullptr;
    QLineEdit* editInput_ = nullptr;
    QPushButton* buttonRun_ = nullptr;
    QPushButton* buttonClear_ = nullptr;
    QPushButton* buttonHelp_ = nullptr;
    QPushButton* buttonExternal_ = nullptr;
    QLabel* labelServerBadge_ = nullptr;
};

} // namespace ui
