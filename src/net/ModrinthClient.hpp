#pragma once

// Async wrapper around the public Modrinth v2 API (docs.modrinth.com) used by
// ui::LibraryDialog. Every callback runs on the Qt UI thread, never
// synchronously from inside the call.
//
// What it adds over a plain "latest file" lookup:
//  - search filtered by Minecraft version, loader/platform and server-side
//    support, with sorting and paging
//  - version resolution that actually matches the target server
//    (game_versions + loaders), preferring stable releases
//  - required-dependency resolution (recursive, depth-limited)
//  - downloads streamed to a .part file with progress, SHA-512 verified
//    against the hash Modrinth publishes, then atomically renamed
//  - hardening: only https downloads from *.modrinth.com hosts, and the
//    destination file name is reduced to a bare, safe file name
//
// Endpoints: GET /v2/search, /v2/project/{id}, /v2/project/{id}/version,
//            /v2/version/{id}

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace net
{

struct ModrinthProjectHit
{
    QString projectId;
    QString slug;
    QString title;
    QString description;
    QString author;
    QString projectType; // mod | plugin | modpack | resourcepack | shader | datapack
    QString iconUrl;
    QStringList categories;   // loaders + tags, e.g. fabric, paper, optimization
    QStringList gameVersions; // every supported Minecraft version
    QString clientSide;       // required | optional | unsupported
    QString serverSide;
    QString dateModified;     // ISO-8601
    qint64 downloads = 0;
    qint64 follows = 0;
};

struct ModrinthFile
{
    QString url;
    QString filename;
    QString sha512; // lowercase hex, may be empty
    qint64 size = 0;
    bool primary = false;
};

struct ModrinthDependency
{
    QString projectId;
    QString versionId;
    QString type; // required | optional | incompatible | embedded
};

struct ModrinthVersion
{
    QString id;
    QString projectId;
    QString name;
    QString versionNumber;
    QString versionType; // release | beta | alpha
    QString datePublished;
    QStringList gameVersions;
    QStringList loaders;
    QList<ModrinthFile> files;
    QList<ModrinthDependency> dependencies;

    [[nodiscard]] std::optional<ModrinthFile> PrimaryFile() const;
};

struct SearchOptions
{
    QString query;
    QString projectType;        // facet value: mod, plugin, modpack, resourcepack, shader, datapack
    QString gameVersion;        // empty = any
    QStringList loaders;        // categories facet, OR-ed (e.g. "fabric"); empty = any
    bool serverSideOnly = false; // exclude client-only mods
    QString sortIndex = QStringLiteral("relevance"); // relevance | downloads | follows | newest | updated
    int offset = 0;
    int limit = 20;
};

struct SearchResult
{
    QList<ModrinthProjectHit> hits;
    int totalHits = 0;
    int offset = 0;
};

class ModrinthClient : public QObject
{
    Q_OBJECT

public:
    explicit ModrinthClient(QObject* parent = nullptr);

    void Search(const SearchOptions& options, std::function<void(SearchResult result, QString error)> onResult);

    // Newest version of `projectId` that matches the game version and any of
    // the loaders (either may be empty = ignore). Prefers "release" over
    // beta/alpha. `error` is empty on success; `version` empty with a
    // non-empty error means "no compatible version".
    void GetCompatibleVersion(
        const QString& projectId,
        const QString& gameVersion,
        const QStringList& loaders,
        std::function<void(std::optional<ModrinthVersion> version, QString error)> onResult);

    void GetVersionById(
        const QString& versionId, std::function<void(std::optional<ModrinthVersion> version, QString error)> onResult);

    // Project title lookup (for showing dependency names).
    void GetProjectTitle(const QString& projectId, std::function<void(QString title)> onResult);

    // Streams to "<dest>.part", verifies SHA-512 when `file.sha512` is set,
    // then renames to `destinationFilePath` (replacing an existing file).
    // `onProgress(received, total)` may be called many times.
    void DownloadFile(
        const ModrinthFile& file,
        const QString& destinationFilePath,
        std::function<void(qint64 received, qint64 total)> onProgress,
        std::function<void(bool success, QString error)> onFinished);

    // Small cached icon fetch (only modrinth CDN hosts). Empty data = failure.
    void FetchIcon(const QString& url, std::function<void(QByteArray data)> onResult);

    // True if `url` is https and its host is modrinth.com or a subdomain.
    [[nodiscard]] static bool IsTrustedUrl(const QString& url);
    // Strips directories and rejects empty/dot names; returns "" if unsafe.
    [[nodiscard]] static QString SafeFileName(const QString& name);

private:
    QNetworkReply* Get(const QUrl& url);
    static std::optional<ModrinthVersion> ParseVersion(const QJsonObject& o);

    QNetworkAccessManager* manager_;
    QHash<QString, QByteArray> iconCache_;
};

} // namespace net
