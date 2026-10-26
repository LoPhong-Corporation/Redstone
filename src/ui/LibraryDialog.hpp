#pragma once

// Modrinth library for the selected server: mods, plugins, datapacks,
// resource packs, shaders and modpacks.
//
// Unlike a plain "latest file" downloader it is server-aware:
//  - detects the server's Minecraft version (config, jar name, or latest.log)
//    and loader/platform (Fabric/Forge/Paper/...) and filters search results
//    and the version it installs accordingly
//  - shows exactly which version/file will be installed before installing
//  - resolves REQUIRED dependencies (e.g. Fabric API) and installs them too
//  - downloads with a progress bar and verifies the SHA-512 Modrinth publishes
// Installing a .mrpack modpack only downloads it into <server>/modpacks/;
// unpacking a modpack is not implemented.

#include "core/Types.hpp"
#include "net/ModrinthClient.hpp"

#include <QDialog>
#include <QHash>
#include <QList>

#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;

namespace ui
{

class ToggleSwitch;

class LibraryDialog : public QDialog
{
    Q_OBJECT

public:
    LibraryDialog(core::ServerConfig targetServer, QWidget* parent = nullptr);

private:
    struct PlanItem
    {
        QString title;
        QString projectId;
        net::ModrinthVersion version;
        net::ModrinthFile file;
        QString destination;
        bool dependency = false;
    };
    struct PlanState;

    void StartSearch(bool append);
    void PopulateResults(const QList<net::ModrinthProjectHit>& hits, bool append);
    void OnSelectionChanged();
    void ResolveSelectedVersion();
    void OnInstallClicked();
    void ResolveDependencies(const std::shared_ptr<PlanState>& state, const net::ModrinthVersion& version, int depth);
    void ConfirmAndRun(const std::shared_ptr<PlanState>& state);
    void DownloadNext();
    void SetBusy(bool busy);
    void UpdateDetailPanel();

    [[nodiscard]] QString CurrentType() const;
    [[nodiscard]] QStringList LoadersForType(const QString& projectType) const;
    [[nodiscard]] QString GameVersionFilter() const;
    [[nodiscard]] QStringList LoaderFilter() const;
    [[nodiscard]] QString DestinationFor(const QString& projectType, const QString& fileName) const;
    [[nodiscard]] std::optional<net::ModrinthProjectHit> CurrentSelection() const;

    core::ServerConfig targetServer_;
    net::ModrinthClient* client_;

    // Search state
    QList<net::ModrinthProjectHit> hits_;
    QHash<QString, QListWidgetItem*> itemsByProject_;
    int searchToken_ = 0;
    int totalHits_ = 0;

    // Selection / resolved version
    int selectionToken_ = 0;
    std::optional<net::ModrinthVersion> selectedVersion_;
    QString selectedVersionError_;
    bool resolving_ = false;

    // Install queue
    QList<PlanItem> queue_;
    int queueIndex_ = 0;
    bool busy_ = false;

    // Widgets
    QComboBox* comboType_ = nullptr;
    QComboBox* comboSort_ = nullptr;
    QLineEdit* editQuery_ = nullptr;
    QLineEdit* editGameVersion_ = nullptr;
    QPushButton* buttonSearch_ = nullptr;
    ToggleSwitch* toggleCompatible_ = nullptr;
    ToggleSwitch* toggleServerOnly_ = nullptr;
    QListWidget* listResults_ = nullptr;
    QPushButton* buttonMore_ = nullptr;

    QLabel* labelDetailTitle_ = nullptr;
    QLabel* labelDetailMeta_ = nullptr;
    QLabel* labelDetailDescription_ = nullptr;
    QLabel* labelDetailTags_ = nullptr;
    QLabel* labelDetailVersion_ = nullptr;
    QLabel* labelDetailDeps_ = nullptr;
    QPushButton* buttonOpenWeb_ = nullptr;
    QPushButton* buttonInstall_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* labelStatus_ = nullptr;
};

} // namespace ui
