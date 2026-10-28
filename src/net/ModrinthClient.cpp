#include "net/ModrinthClient.hpp"
#include "ui/I18n.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

namespace net
{
namespace
{
// Modrinth asks API consumers to identify themselves with a descriptive User-Agent.
constexpr const char* kUserAgent = "MinecraftServerManager/1.1 (+https://github.com/minecraft-server-manager/desktop-app)";
constexpr const char* kApi = "https://api.modrinth.com/v2";

QStringList ToStringList(const QJsonArray& array)
{
    QStringList list;
    for (const auto& v : array)
    {
        list << v.toString();
    }
    return list;
}

QString JsonStringArray(const QStringList& values)
{
    QJsonArray array;
    for (const auto& v : values)
    {
        array.append(v);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
} // namespace

std::optional<ModrinthFile> ModrinthVersion::PrimaryFile() const
{
    if (files.isEmpty())
    {
        return std::nullopt;
    }
    for (const auto& f : files)
    {
        if (f.primary)
        {
            return f;
        }
    }
    return files.first();
}

bool ModrinthClient::IsTrustedUrl(const QString& url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    return u.scheme() == QLatin1String("https") &&
        (host == QLatin1String("modrinth.com") || host.endsWith(QLatin1String(".modrinth.com")));
}

bool ModrinthClient::IsModpackUrl(const QString& url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    static const QStringList allowed = {
        QStringLiteral("cdn.modrinth.com"), QStringLiteral("github.com"), QStringLiteral("raw.githubusercontent.com"), QStringLiteral("gitlab.com")};
    return u.scheme() == QLatin1String("https") && allowed.contains(host);
}

QString ModrinthClient::SafeFileName(const QString& name)
{
    // Take only the last path component (blocks "../../x" and absolute paths).
    QString base = QFileInfo(name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    if (base.isEmpty() || base == QLatin1String(".") || base == QLatin1String(".."))
    {
        return QString();
    }
    static const QString forbidden = QStringLiteral("<>:\"|?*");
    for (QChar& ch : base)
    {
        if (forbidden.contains(ch) || ch.unicode() < 32)
        {
            ch = QLatin1Char('_');
        }
    }
    return base;
}

ModrinthClient::ModrinthClient(QObject* parent)
    : QObject(parent)
    , manager_(new QNetworkAccessManager(this))
{
}

QNetworkReply* ModrinthClient::Get(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return manager_->get(request);
}

void ModrinthClient::Search(const SearchOptions& o, std::function<void(SearchResult result, QString error)> onResult)
{
    // facets: AND between groups, OR inside a group.
    QJsonArray facets;
    facets.append(QJsonArray{QStringLiteral("project_type:%1").arg(o.projectType)});
    if (!o.gameVersion.isEmpty())
    {
        facets.append(QJsonArray{QStringLiteral("versions:%1").arg(o.gameVersion)});
    }
    if (!o.loaders.isEmpty())
    {
        QJsonArray group;
        for (const auto& loader : o.loaders)
        {
            group.append(QStringLiteral("categories:%1").arg(loader));
        }
        facets.append(group);
    }
    if (o.serverSideOnly && (o.projectType == QLatin1String("mod") || o.projectType == QLatin1String("modpack")))
    {
        facets.append(QJsonArray{QStringLiteral("server_side:required"), QStringLiteral("server_side:optional")});
    }

    QUrl url(QStringLiteral("%1/search").arg(QLatin1String(kApi)));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("query"), o.query);
    q.addQueryItem(QStringLiteral("facets"), QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    q.addQueryItem(QStringLiteral("index"), o.sortIndex);
    q.addQueryItem(QStringLiteral("offset"), QString::number(o.offset));
    q.addQueryItem(QStringLiteral("limit"), QString::number(o.limit));
    url.setQuery(q);

    QNetworkReply* reply = Get(url);
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult({}, reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject())
        {
            onResult({}, TR("Phản hồi không hợp lệ từ Modrinth."));
            return;
        }
        const QJsonObject root = doc.object();
        SearchResult result;
        result.totalHits = root.value(QStringLiteral("total_hits")).toInt();
        result.offset = root.value(QStringLiteral("offset")).toInt();
        for (const auto& item : root.value(QStringLiteral("hits")).toArray())
        {
            const QJsonObject h = item.toObject();
            ModrinthProjectHit hit;
            hit.projectId = h.value(QStringLiteral("project_id")).toString();
            hit.slug = h.value(QStringLiteral("slug")).toString();
            hit.title = h.value(QStringLiteral("title")).toString();
            hit.description = h.value(QStringLiteral("description")).toString();
            hit.author = h.value(QStringLiteral("author")).toString();
            hit.projectType = h.value(QStringLiteral("project_type")).toString();
            hit.iconUrl = h.value(QStringLiteral("icon_url")).toString();
            hit.categories = ToStringList(h.value(QStringLiteral("display_categories")).toArray());
            if (hit.categories.isEmpty())
            {
                hit.categories = ToStringList(h.value(QStringLiteral("categories")).toArray());
            }
            hit.gameVersions = ToStringList(h.value(QStringLiteral("versions")).toArray());
            hit.clientSide = h.value(QStringLiteral("client_side")).toString();
            hit.serverSide = h.value(QStringLiteral("server_side")).toString();
            hit.dateModified = h.value(QStringLiteral("date_modified")).toString();
            hit.downloads = static_cast<qint64>(h.value(QStringLiteral("downloads")).toDouble());
            hit.follows = static_cast<qint64>(h.value(QStringLiteral("follows")).toDouble());
            result.hits.push_back(hit);
        }
        onResult(result, QString());
    });
}

std::optional<ModrinthVersion> ModrinthClient::ParseVersion(const QJsonObject& o)
{
    ModrinthVersion v;
    v.id = o.value(QStringLiteral("id")).toString();
    v.projectId = o.value(QStringLiteral("project_id")).toString();
    v.name = o.value(QStringLiteral("name")).toString();
    v.versionNumber = o.value(QStringLiteral("version_number")).toString();
    v.versionType = o.value(QStringLiteral("version_type")).toString();
    v.datePublished = o.value(QStringLiteral("date_published")).toString();
    v.changelog = o.value(QStringLiteral("changelog")).toString();
    v.downloads = static_cast<qint64>(o.value(QStringLiteral("downloads")).toDouble());
    v.gameVersions = ToStringList(o.value(QStringLiteral("game_versions")).toArray());
    v.loaders = ToStringList(o.value(QStringLiteral("loaders")).toArray());
    for (const auto& fv : o.value(QStringLiteral("files")).toArray())
    {
        const QJsonObject f = fv.toObject();
        ModrinthFile file;
        file.url = f.value(QStringLiteral("url")).toString();
        file.filename = f.value(QStringLiteral("filename")).toString();
        file.sha512 = f.value(QStringLiteral("hashes")).toObject().value(QStringLiteral("sha512")).toString().toLower();
        file.size = static_cast<qint64>(f.value(QStringLiteral("size")).toDouble());
        file.primary = f.value(QStringLiteral("primary")).toBool();
        v.files.push_back(file);
    }
    for (const auto& dv : o.value(QStringLiteral("dependencies")).toArray())
    {
        const QJsonObject d = dv.toObject();
        ModrinthDependency dep;
        dep.projectId = d.value(QStringLiteral("project_id")).toString();
        dep.versionId = d.value(QStringLiteral("version_id")).toString();
        dep.type = d.value(QStringLiteral("dependency_type")).toString();
        v.dependencies.push_back(dep);
    }
    if (v.files.isEmpty())
    {
        return std::nullopt;
    }
    return v;
}

void ModrinthClient::GetCompatibleVersion(
    const QString& projectId,
    const QString& gameVersion,
    const QStringList& loaders,
    std::function<void(std::optional<ModrinthVersion> version, QString error)> onResult)
{
    QUrl url(QStringLiteral("%1/project/%2/version").arg(QLatin1String(kApi), projectId));
    QUrlQuery q;
    if (!gameVersion.isEmpty())
    {
        q.addQueryItem(QStringLiteral("game_versions"), JsonStringArray({gameVersion}));
    }
    if (!loaders.isEmpty())
    {
        q.addQueryItem(QStringLiteral("loaders"), JsonStringArray(loaders));
    }
    q.addQueryItem(QStringLiteral("include_changelog"), QStringLiteral("false"));
    url.setQuery(q);

    QNetworkReply* reply = Get(url);
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult(std::nullopt, reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isArray() || doc.array().isEmpty())
        {
            onResult(std::nullopt, TR("Không có phiên bản nào tương thích với server này."));
            return;
        }
        // Newest-first from the API. Take the newest stable release if any,
        // otherwise the newest of whatever exists (beta/alpha).
        std::optional<ModrinthVersion> best;
        std::optional<ModrinthVersion> firstAny;
        for (const auto& item : doc.array())
        {
            auto parsed = ParseVersion(item.toObject());
            if (!parsed.has_value())
            {
                continue;
            }
            if (!firstAny.has_value())
            {
                firstAny = parsed;
            }
            if (parsed->versionType == QLatin1String("release"))
            {
                best = parsed;
                break;
            }
        }
        if (!best.has_value())
        {
            best = firstAny;
        }
        if (!best.has_value())
        {
            onResult(std::nullopt, TR("Phiên bản tìm được không có file để tải."));
            return;
        }
        onResult(best, QString());
    });
}

void ModrinthClient::GetVersionById(
    const QString& versionId, std::function<void(std::optional<ModrinthVersion> version, QString error)> onResult)
{
    QNetworkReply* reply = Get(QUrl(QStringLiteral("%1/version/%2").arg(QLatin1String(kApi), versionId)));
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult(std::nullopt, reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        auto parsed = doc.isObject() ? ParseVersion(doc.object()) : std::nullopt;
        if (!parsed.has_value())
        {
            onResult(std::nullopt, TR("Phản hồi phiên bản không hợp lệ."));
            return;
        }
        onResult(parsed, QString());
    });
}

QNetworkReply* ModrinthClient::PostJson(const QUrl& url, const QByteArray& body)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return manager_->post(request, body);
}

namespace
{
QHash<QString, ModrinthVersion> ParseHashMap(const QByteArray& data, const std::function<std::optional<ModrinthVersion>(const QJsonObject&)>& parse)
{
    QHash<QString, ModrinthVersion> map;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
    {
        return map;
    }
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        if (auto v = parse(it.value().toObject()); v.has_value())
        {
            map.insert(it.key().toLower(), *v);
        }
    }
    return map;
}
} // namespace

void ModrinthClient::IdentifyByHash(
    const QStringList& hashes, std::function<void(QHash<QString, ModrinthVersion> versions, QString error)> onResult)
{
    QJsonObject body;
    body.insert(QStringLiteral("hashes"), QJsonArray::fromStringList(hashes));
    body.insert(QStringLiteral("algorithm"), QStringLiteral("sha512"));
    QNetworkReply* reply = PostJson(QUrl(QStringLiteral("%1/version_files").arg(QLatin1String(kApi))), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult({}, reply->errorString());
            return;
        }
        onResult(ParseHashMap(reply->readAll(), &ModrinthClient::ParseVersion), QString());
    });
}

void ModrinthClient::LatestByHash(
    const QStringList& hashes,
    const QStringList& loaders,
    const QStringList& gameVersions,
    std::function<void(QHash<QString, ModrinthVersion> versions, QString error)> onResult)
{
    QJsonObject body;
    body.insert(QStringLiteral("hashes"), QJsonArray::fromStringList(hashes));
    body.insert(QStringLiteral("algorithm"), QStringLiteral("sha512"));
    body.insert(QStringLiteral("loaders"), QJsonArray::fromStringList(loaders));
    body.insert(QStringLiteral("game_versions"), QJsonArray::fromStringList(gameVersions));
    QNetworkReply* reply =
        PostJson(QUrl(QStringLiteral("%1/version_files/update").arg(QLatin1String(kApi))), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult({}, reply->errorString());
            return;
        }
        onResult(ParseHashMap(reply->readAll(), &ModrinthClient::ParseVersion), QString());
    });
}

void ModrinthClient::GetProjectTitles(const QStringList& projectIds, std::function<void(QHash<QString, QString> titles)> onResult)
{
    if (projectIds.isEmpty())
    {
        onResult({});
        return;
    }
    QUrl url(QStringLiteral("%1/projects").arg(QLatin1String(kApi)));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ids"), JsonStringArray(projectIds));
    url.setQuery(q);
    QNetworkReply* reply = Get(url);
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        QHash<QString, QString> titles;
        if (reply->error() == QNetworkReply::NoError)
        {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            for (const auto& v : doc.array())
            {
                const QJsonObject o = v.toObject();
                titles.insert(o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("title")).toString());
            }
        }
        onResult(titles);
    });
}

void ModrinthClient::GetProject(
    const QString& idOrSlug, std::function<void(std::optional<ModrinthProjectDetail> project, QString error)> onResult)
{
    QNetworkReply* reply = Get(QUrl(QStringLiteral("%1/project/%2").arg(QLatin1String(kApi), idOrSlug)));
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult(std::nullopt, reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject())
        {
            onResult(std::nullopt, TR("Phản hồi dự án không hợp lệ."));
            return;
        }
        const QJsonObject o = doc.object();
        ModrinthProjectDetail p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.slug = o.value(QStringLiteral("slug")).toString();
        p.title = o.value(QStringLiteral("title")).toString();
        p.description = o.value(QStringLiteral("description")).toString();
        p.body = o.value(QStringLiteral("body")).toString();
        p.projectType = o.value(QStringLiteral("project_type")).toString();
        p.iconUrl = o.value(QStringLiteral("icon_url")).toString();
        p.licenseName = o.value(QStringLiteral("license")).toObject().value(QStringLiteral("name")).toString();
        if (p.licenseName.isEmpty())
        {
            p.licenseName = o.value(QStringLiteral("license")).toObject().value(QStringLiteral("id")).toString();
        }
        p.sourceUrl = o.value(QStringLiteral("source_url")).toString();
        p.issuesUrl = o.value(QStringLiteral("issues_url")).toString();
        p.wikiUrl = o.value(QStringLiteral("wiki_url")).toString();
        p.discordUrl = o.value(QStringLiteral("discord_url")).toString();
        for (const auto& d : o.value(QStringLiteral("donation_urls")).toArray())
        {
            const QJsonObject don = d.toObject();
            p.donationNames << don.value(QStringLiteral("platform")).toString();
            p.donationUrls << don.value(QStringLiteral("url")).toString();
        }
        p.published = o.value(QStringLiteral("published")).toString();
        p.updated = o.value(QStringLiteral("updated")).toString();
        p.gameVersions = ToStringList(o.value(QStringLiteral("game_versions")).toArray());
        p.loaders = ToStringList(o.value(QStringLiteral("loaders")).toArray());
        p.categories = ToStringList(o.value(QStringLiteral("categories")).toArray());
        p.categories << ToStringList(o.value(QStringLiteral("additional_categories")).toArray());
        p.clientSide = o.value(QStringLiteral("client_side")).toString();
        p.serverSide = o.value(QStringLiteral("server_side")).toString();
        p.downloads = static_cast<qint64>(o.value(QStringLiteral("downloads")).toDouble());
        p.followers = static_cast<qint64>(o.value(QStringLiteral("followers")).toDouble());
        for (const auto& g : o.value(QStringLiteral("gallery")).toArray())
        {
            const QJsonObject go = g.toObject();
            ModrinthGalleryImage img;
            img.url = go.value(QStringLiteral("url")).toString();
            img.title = go.value(QStringLiteral("title")).toString();
            img.description = go.value(QStringLiteral("description")).toString();
            img.featured = go.value(QStringLiteral("featured")).toBool();
            p.gallery.push_back(img);
        }
        onResult(p, QString());
    });
}

void ModrinthClient::GetMembers(const QString& projectId, std::function<void(QList<ModrinthMember> members)> onResult)
{
    QNetworkReply* reply = Get(QUrl(QStringLiteral("%1/project/%2/members").arg(QLatin1String(kApi), projectId)));
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        QList<ModrinthMember> members;
        if (reply->error() == QNetworkReply::NoError)
        {
            for (const auto& v : QJsonDocument::fromJson(reply->readAll()).array())
            {
                const QJsonObject o = v.toObject();
                ModrinthMember m;
                m.name = o.value(QStringLiteral("user")).toObject().value(QStringLiteral("username")).toString();
                m.role = o.value(QStringLiteral("role")).toString();
                if (!m.name.isEmpty())
                {
                    members.push_back(m);
                }
            }
        }
        onResult(members);
    });
}

void ModrinthClient::ListVersions(
    const QString& projectId,
    const QString& gameVersion,
    const QStringList& loaders,
    std::function<void(QList<ModrinthVersion> versions, QString error)> onResult)
{
    QUrl url(QStringLiteral("%1/project/%2/version").arg(QLatin1String(kApi), projectId));
    QUrlQuery q;
    if (!gameVersion.isEmpty())
    {
        q.addQueryItem(QStringLiteral("game_versions"), JsonStringArray({gameVersion}));
    }
    if (!loaders.isEmpty())
    {
        q.addQueryItem(QStringLiteral("loaders"), JsonStringArray(loaders));
    }
    q.addQueryItem(QStringLiteral("include_changelog"), QStringLiteral("true"));
    url.setQuery(q);
    QNetworkReply* reply = Get(url);
    connect(reply, &QNetworkReply::finished, this, [reply, onResult]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult({}, reply->errorString());
            return;
        }
        QList<ModrinthVersion> versions;
        for (const auto& v : QJsonDocument::fromJson(reply->readAll()).array())
        {
            if (auto parsed = ParseVersion(v.toObject()); parsed.has_value())
            {
                versions.push_back(*parsed);
            }
        }
        onResult(versions, QString());
    });
}

void ModrinthClient::GetProjectTitle(const QString& projectId, std::function<void(QString title)> onResult)
{
    QNetworkReply* reply = Get(QUrl(QStringLiteral("%1/project/%2").arg(QLatin1String(kApi), projectId)));
    connect(reply, &QNetworkReply::finished, this, [reply, onResult, projectId]() {
        reply->deleteLater();
        QString title = projectId;
        if (reply->error() == QNetworkReply::NoError)
        {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject() && !doc.object().value(QStringLiteral("title")).toString().isEmpty())
            {
                title = doc.object().value(QStringLiteral("title")).toString();
            }
        }
        onResult(title);
    });
}

void ModrinthClient::FetchIcon(const QString& url, std::function<void(QByteArray data)> onResult)
{
    FetchImage(url, 2 * 1024 * 1024, std::move(onResult));
}

void ModrinthClient::FetchImage(const QString& url, qint64 maxBytes, std::function<void(QByteArray data)> onResult)
{
    if (url.isEmpty() || !IsTrustedUrl(url))
    {
        onResult({});
        return;
    }
    if (const auto it = iconCache_.constFind(url); it != iconCache_.constEnd())
    {
        onResult(it.value());
        return;
    }
    QNetworkReply* reply = Get(QUrl(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, maxBytes, onResult]() {
        reply->deleteLater();
        QByteArray data;
        if (reply->error() == QNetworkReply::NoError)
        {
            data = reply->readAll();
            if (data.size() < maxBytes) // ignore absurdly large images
            {
                iconCache_.insert(url, data);
            }
            else
            {
                data.clear();
            }
        }
        onResult(data);
    });
}

void ModrinthClient::DownloadFile(
    const ModrinthFile& file,
    const QString& destinationFilePath,
    std::function<void(qint64 received, qint64 total)> onProgress,
    std::function<void(bool success, QString error)> onFinished,
    bool allowModpackHosts)
{
    if (!(allowModpackHosts ? IsModpackUrl(file.url) : IsTrustedUrl(file.url)))
    {
        onFinished(false, TR("Từ chối tải: URL không nằm trong danh sách host được phép (%1).").arg(file.url));
        return;
    }
    const QFileInfo info(destinationFilePath);
    if (!QDir().mkpath(info.absolutePath()))
    {
        onFinished(false, TR("Không tạo được thư mục: %1").arg(info.absolutePath()));
        return;
    }

    const QString partPath = destinationFilePath + QStringLiteral(".part");
    auto out = std::make_shared<QFile>(partPath);
    if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        onFinished(false, TR("Không thể ghi file: %1").arg(partPath));
        return;
    }
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha512);
    auto writeError = std::make_shared<bool>(false);

    QNetworkReply* reply = Get(QUrl(file.url));

    // Stream chunks to disk instead of buffering the whole jar in memory.
    connect(reply, &QNetworkReply::readyRead, this, [reply, out, hash, writeError]() {
        const QByteArray chunk = reply->readAll();
        if (out->write(chunk) != chunk.size())
        {
            *writeError = true;
        }
        hash->addData(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [onProgress](qint64 received, qint64 total) {
        if (onProgress)
        {
            onProgress(received, total);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, out, hash, writeError, file, partPath, destinationFilePath, onFinished]() {
        reply->deleteLater();
        // Flush anything still buffered.
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty())
        {
            out->write(rest);
            hash->addData(rest);
        }
        out->close();

        auto fail = [&](const QString& message) {
            QFile::remove(partPath);
            onFinished(false, message);
        };

        if (reply->error() != QNetworkReply::NoError)
        {
            fail(reply->errorString());
            return;
        }
        if (*writeError)
        {
            fail(TR("Ghi file thất bại (đầy ổ đĩa?)."));
            return;
        }
        if (!file.sha512.isEmpty() && QString::fromLatin1(hash->result().toHex()).toLower() != file.sha512)
        {
            fail(TR("Sai mã SHA-512 — file tải về bị lỗi hoặc bị can thiệp, đã huỷ."));
            return;
        }
        QFile::remove(destinationFilePath); // QFile::rename will not overwrite
        if (!QFile::rename(partPath, destinationFilePath))
        {
            fail(TR("Không đổi tên được file tải về thành %1").arg(destinationFilePath));
            return;
        }
        onFinished(true, QString());
    });
}

} // namespace net
