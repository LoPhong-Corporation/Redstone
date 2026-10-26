#pragma once

// Phase 4: create/list/delete/restore world backups for one server.
// Restore requires the server to be Stopped (checked against the live
// server object, not just a config snapshot) and moves any existing
// world aside rather than deleting it - see backup::BackupManager's
// header comment for the full safety reasoning.

#include "server/MinecraftServer.hpp"

#include <QDialog>
#include <QList>

#include <memory>
#include <thread>

class QListWidget;
class QPushButton;
class QLabel;

namespace ui
{

class BackupsDialog : public QDialog
{
    Q_OBJECT

public:
    BackupsDialog(std::shared_ptr<server::MinecraftServer> server, QWidget* parent = nullptr);
    ~BackupsDialog() override;

private slots:
    void OnCreateClicked();
    void OnDeleteClicked();
    void OnRestoreClicked();
    void OnOpenFolderClicked();
    void OnSelectionChanged();

private:
    void Refresh();

    std::shared_ptr<server::MinecraftServer> server_;

    QListWidget* listBackups_ = nullptr;
    QPushButton* buttonCreate_ = nullptr;
    QPushButton* buttonDelete_ = nullptr;
    QPushButton* buttonRestore_ = nullptr;
    QPushButton* buttonOpenFolder_ = nullptr;
    QLabel* labelStatus_ = nullptr;

    // Backup creation and restore both stream a whole world folder
    // through disk I/O, so both run off the UI thread. Two separate
    // joinable members (not detached threads) rather than one shared one,
    // so clicking Create then Restore in quick succession can't block the
    // UI thread waiting for one jthread-reassignment's implicit join() -
    // and so the destructor can guarantee neither is still calling
    // QMetaObject::invokeMethod(this, ...) once this dialog's widgets
    // start being torn down. See the .cpp for the full reasoning
    // (originally documented on the single-thread version of this class).
    std::jthread createThread_;
    std::jthread restoreThread_;
};

} // namespace ui
