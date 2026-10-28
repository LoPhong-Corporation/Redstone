#pragma once

// Mods / plugins already in the server folder, with update checking.
// Files are identified by the SHA-512 of the .jar (so renamed files still match),
// compared against the newest Modrinth version compatible with this server's
// Minecraft version and loader. An update downloads the new jar (SHA-512 verified),
// keeps a copy of the old one in <server>/addons-backup/<timestamp>/, then removes it.
// Dependencies introduced by a newer version are NOT resolved here - use the Library for that.

#include "core/Types.hpp"
#include "net/ModrinthClient.hpp"

#include <QDialog>
#include <QList>

#include <optional>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace ui
{

class InstalledAddonsDialog : public QDialog
{
    Q_OBJECT

public:
    InstalledAddonsDialog(core::ServerConfig server, QWidget* parent = nullptr);

private:
    struct Addon
    {
        QString path;
        QString folder; // "mods" | "plugins"
        QString fileName;
        qint64 sizeBytes = 0;
        QString sha512;
        bool known = false; // recognised by Modrinth
        QString title;
        QString projectId;
        QString currentVersion;
        QString currentVersionId;
        QString currentDate;
        std::optional<net::ModrinthVersion> latest;
        bool hasUpdate = false;
    };

    void Scan();
    void CheckUpdates();
    void OnHashesReady(QStringList hashesInOrder);
    void OnIdentified(const QHash<QString, net::ModrinthVersion>& versions);
    void RequestLatest(const QString& folder, const QStringList& hashes, int* pending);
    void FinishCheck();
    void UpdateItemText(int index);
    void UpdateButtons();
    void StartUpdate(const QList<int>& indexes);
    void UpdateNext();
    void SetBusy(bool busy);
    [[nodiscard]] QList<int> SelectedUpdatable() const;
    [[nodiscard]] QString BackupFolderFor() const;

    core::ServerConfig server_;
    net::ModrinthClient* client_;
    QList<Addon> addons_;
    int checkToken_ = 0;
    bool busy_ = false;
    int latestPending_ = 0;
    QList<int> updateQueue_;
    int updateIndex_ = 0;
    QString backupRoot_;

    QListWidget* list_ = nullptr;
    QLabel* labelStatus_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* buttonCheck_ = nullptr;
    QPushButton* buttonUpdateSelected_ = nullptr;
    QPushButton* buttonUpdateAll_ = nullptr;
    QPushButton* buttonDelete_ = nullptr;
};

} // namespace ui
