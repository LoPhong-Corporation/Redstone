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
    QString changelog;       // markdown, only when requested
    qint64 downloads = 0;

    [[nodiscard]] std::optional<ModrinthFile> PrimaryFile() const;
};

struct ModrinthGalleryImage
{
    QString url;
    QString title;
    QString description;
    bool featured = false;
};

// Full project page data (GET /project/{id}).
struct ModrinthProjectDetail
{
    QString id;
    QString slug;
    QString title;
    QString description; // one-line summary
    QString body;        // long markdown description
    QString projectType;
    QString iconUrl;
    QString licenseName;
    QString sourceUrl;
    QString issuesUrl;
    QString wikiUrl;
    QString discordUrl;
    QStringList donationNames;
    QStringList donationUrls;
    QString published;
    QString updated;
    QStringList gameVersions;
    QStringList loaders;
    QStringList categories;
    QString clientSide;
    QString serverSide;
    qint64 downloads = 0;
    qint64 followers = 0;
    QList<ModrinthGalleryImage> gallery;
};

struct ModrinthMember
{
    QString name;
    QString role;
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

    // Long description, links, license, gallery...
    void GetProject(const QString& idOrSlug, std::function<void(std::optional<ModrinthProjectDetail> project, QString error)> onResult);
    void GetMembers(const QString& projectId, std::function<void(QList<ModrinthMember> members)> onResult);

    // Compatible versions (newest first) with changelogs, for the version list.
    void ListVersions(
        const QString& projectId,
        const QString& gameVersion,
        const QStringList& loaders,
        std::function<void(QList<ModrinthVersion> versions, QString error)> onResult);

    void GetVersionById(
        const QString& versionId, std::function<void(std::optional<ModrinthVersion> version, QString error)> onResult);

    // Identifies installed files by SHA-512: hash -> the exact Modrinth version it belongs to.
    // Files unknown to Modrinth are simply absent from the map.
    void IdentifyByHash(const QStringList& sha512Hashes, std::function<void(QHash<QString, ModrinthVersion> versions, QString error)> onResult);

    // For each hash: the newest version of the same project compatible with the given
    // game versions/loaders (may be the installed version itself).
    void LatestByHash(
        const QStringList& sha512Hashes,
        const QStringList& loaders,
        const QStringList& gameVersions,
        std::function<void(QHash<QString, ModrinthVersion> versions, QString error)> onResult);

    // project id -> title for up to 100 ids.
    void GetProjectTitles(const QStringList& projectIds, std::function<void(QHash<QString, QString> titles)> onResult);

    // Project title lookup (for showing dependency names).
    void GetProjectTitle(const QString& projectId, std::function<void(QString title)> onResult);

    // Streams to "<dest>.part", verifies SHA-512 when `file.sha512` is set,
    // then renames to `destinationFilePath` (replacing an existing file).
    // `onProgress(received, total)` may be called many times.
    void DownloadFile(
        const ModrinthFile& file,
        const QString& destinationFilePath,
        std::function<void(qint64 received, qint64 total)> onProgress,
        std::function<void(bool success, QString error)> onFinished,
        bool allowModpackHosts = false);

    // Small cached icon fetch (only modrinth CDN hosts). Empty data = failure.
    void FetchIcon(const QString& url, std::function<void(QByteArray data)> onResult);
    // Same, for larger images such as gallery screenshots.
    void FetchImage(const QString& url, qint64 maxBytes, std::function<void(QByteArray data)> onResult);

    // True if `url` is https and its host is modrinth.com or a subdomain.
    [[nodiscard]] static bool IsTrustedUrl(const QString& url);
    // https URL on one of the hosts the .mrpack specification allows
    // (cdn.modrinth.com, github.com, raw.githubusercontent.com, gitlab.com).
    [[nodiscard]] static bool IsModpackUrl(const QString& url);
    // Strips directories and rejects empty/dot names; returns "" if unsafe.
    [[nodiscard]] static QString SafeFileName(const QString& name);

private:
    QNetworkReply* Get(const QUrl& url);
    QNetworkReply* PostJson(const QUrl& url, const QByteArray& body);
public:
    static std::optional<ModrinthVersion> ParseVersion(const QJsonObject& o);

private:
    QNetworkAccessManager* manager_;
    QHash<QString, QByteArray> iconCache_;
};

} // namespace net
