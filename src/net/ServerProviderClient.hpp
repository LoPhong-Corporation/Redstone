#pragma once

// Phase 5 "Server Providers": downloads a runnable server jar directly,
// so creating a new server does not require the user to have already
// found and downloaded a jar file themselves.
//
// APIs used (verified against official docs/sources before writing this,
// same as net::ModrinthClient):
//   Vanilla: piston-meta.mojang.com/mc/game/version_manifest_v2.json,
//            then the per-version JSON's downloads.server.{url,sha1}.
//   Paper:   api.papermc.io/v2/projects/paper (version list),
//            .../versions/{v}/builds (build list, filter channel=="default"),
//            download at .../builds/{b}/downloads/paper-{v}-{b}.jar
//   Fabric:  meta.fabricmc.net/v2/versions/game (version list, filter stable),
//            .../versions/loader/{gameVersion} -> [0].loader.version,
//            .../versions/installer -> [0].version,
//            download (a complete, self-bootstrapping server launcher) at
//            .../versions/loader/{game}/{loader}/{installer}/server/jar
//
// Forge/NeoForge are intentionally NOT included: they ship as an
// interactive installer (a .jar that runs its own install wizard/CLI and
// patches Minecraft's own jar), not a single static download URL, which
// does not fit this "resolve one URL, download one file" model. Users
// wanting Forge still add it as a server the normal way, running the
// Forge installer themselves first.

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace net
{

enum class ServerProviderType
{
    Vanilla,
    Paper,
    Fabric,
};

struct ServerJarInfo
{
    QString filename;
    QString downloadUrl;
    QString sha1; // empty when the provider does not expose one (Fabric)
};

class ServerProviderClient : public QObject
{
    Q_OBJECT

public:
    explicit ServerProviderClient(QObject* parent = nullptr);

    // For Vanilla, only "release" versions are listed (snapshots are
    // omitted to keep the picker short and because running a Minecraft
    // *server* on a snapshot is a niche, testing-only use case).
    // For Fabric, only versions marked "stable" by Fabric are listed.
    // Every list is newest-first.
    void ListVersions(ServerProviderType type, std::function<void(QStringList versions, QString errorMessage)> onResult);

    void ResolveServerJar(
        ServerProviderType type,
        const QString& version,
        std::function<void(std::optional<ServerJarInfo> info, QString errorMessage)> onResult);

    void DownloadFile(const QString& url, const QString& destinationFilePath, std::function<void(bool success, QString errorMessage)> onFinished);

private:
    QNetworkAccessManager* manager_;

    // Populated by ListVersions(Vanilla, ...); avoids re-downloading the
    // (fairly large) manifest again inside ResolveServerJar(Vanilla, ...).
    QHash<QString, QString> vanillaVersionMetaUrls_;
};

} // namespace net
