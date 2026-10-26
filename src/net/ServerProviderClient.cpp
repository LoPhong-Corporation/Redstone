#include "net/ServerProviderClient.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace net
{
namespace
{
constexpr const char* kUserAgent = "MinecraftServerManager/1.0 (+https://github.com/minecraft-server-manager/desktop-app)";

void Get(QNetworkAccessManager* manager, const QUrl& url, std::function<void(QNetworkReply*)> onFinished)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply* reply = manager->get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, onFinished]() {
        onFinished(reply);
        reply->deleteLater();
    });
}

// Convenience for the common case of "GET this URL, parse it as JSON,
// report a QString error either for a network failure or a JSON one".
void GetJson(QNetworkAccessManager* manager, const QUrl& url, std::function<void(QJsonDocument, QString)> onResult)
{
    Get(manager, url, [onResult](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError)
        {
            onResult(QJsonDocument(), reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull())
        {
            onResult(QJsonDocument(), QStringLiteral("Phản hồi không phải JSON hợp lệ."));
            return;
        }
        onResult(doc, QString());
    });
}

} // namespace

ServerProviderClient::ServerProviderClient(QObject* parent)
    : QObject(parent)
    , manager_(new QNetworkAccessManager(this))
{
}

void ServerProviderClient::ListVersions(ServerProviderType type, std::function<void(QStringList, QString)> onResult)
{
    switch (type)
    {
        case ServerProviderType::Vanilla:
        {
            GetJson(
                manager_,
                QUrl(QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")),
                [this, onResult](QJsonDocument doc, QString error) {
                    if (!error.isEmpty())
                    {
                        onResult({}, error);
                        return;
                    }
                    vanillaVersionMetaUrls_.clear();
                    QStringList releases;
                    const QJsonArray versions = doc.object().value(QStringLiteral("versions")).toArray();
                    for (const auto& v : versions)
                    {
                        const QJsonObject o = v.toObject();
                        if (o.value(QStringLiteral("type")).toString() != QStringLiteral("release"))
                        {
                            continue;
                        }
                        const QString id = o.value(QStringLiteral("id")).toString();
                        vanillaVersionMetaUrls_.insert(id, o.value(QStringLiteral("url")).toString());
                        releases << id;
                    }
                    onResult(releases, QString());
                });
            return;
        }
        case ServerProviderType::Paper:
        {
            // PaperMC retired the old api.papermc.io/v2 API (it now
            // returns HTTP 410 Gone) in favour of the new "Fill" API at
            // fill.papermc.io/v3. Versions come back grouped by version
            // family: {"versions": {"1.21": ["1.21.9","1.21.10",...], ...}}.
            // Groups are newest-first and each group's own array is
            // newest-first too (per PaperMC's own example script), so
            // concatenating every group's array in encounter order keeps
            // the overall flattened list newest-first.
            GetJson(
                manager_,
                QUrl(QStringLiteral("https://fill.papermc.io/v3/projects/paper")),
                [onResult](QJsonDocument doc, QString error) {
                    if (!error.isEmpty())
                    {
                        onResult({}, error);
                        return;
                    }
                    const QJsonObject root = doc.object();
                    if (root.contains(QStringLiteral("ok")) && !root.value(QStringLiteral("ok")).toBool(true))
                    {
                        onResult({}, root.value(QStringLiteral("message")).toString(QStringLiteral("Lỗi không rõ từ PaperMC.")));
                        return;
                    }
                    QStringList versions;
                    const QJsonObject versionGroups = root.value(QStringLiteral("versions")).toObject();
                    for (auto it = versionGroups.constBegin(); it != versionGroups.constEnd(); ++it)
                    {
                        for (const auto& v : it.value().toArray())
                        {
                            versions << v.toString();
                        }
                    }
                    onResult(versions, QString());
                });
            return;
        }
        case ServerProviderType::Fabric:
        {
            GetJson(
                manager_,
                QUrl(QStringLiteral("https://meta.fabricmc.net/v2/versions/game")),
                [onResult](QJsonDocument doc, QString error) {
                    if (!error.isEmpty())
                    {
                        onResult({}, error);
                        return;
                    }
                    QStringList versions;
                    for (const auto& v : doc.array())
                    {
                        const QJsonObject o = v.toObject();
                        if (o.value(QStringLiteral("stable")).toBool())
                        {
                            versions << o.value(QStringLiteral("version")).toString();
                        }
                    }
                    onResult(versions, QString());
                });
            return;
        }
    }
}

void ServerProviderClient::ResolveServerJar(
    ServerProviderType type,
    const QString& version,
    std::function<void(std::optional<ServerJarInfo>, QString)> onResult)
{
    switch (type)
    {
        case ServerProviderType::Vanilla:
        {
            const auto it = vanillaVersionMetaUrls_.constFind(version);
            if (it == vanillaVersionMetaUrls_.constEnd())
            {
                onResult(std::nullopt, QStringLiteral("Chưa có danh sách phiên bản - hãy tải danh sách trước."));
                return;
            }
            GetJson(manager_, QUrl(it.value()), [onResult](QJsonDocument doc, QString error) {
                if (!error.isEmpty())
                {
                    onResult(std::nullopt, error);
                    return;
                }
                const QJsonObject server = doc.object().value(QStringLiteral("downloads")).toObject().value(QStringLiteral("server")).toObject();
                const QString url = server.value(QStringLiteral("url")).toString();
                if (url.isEmpty())
                {
                    onResult(std::nullopt, QStringLiteral("Phiên bản này không có server.jar (có thể là bản quá cũ)."));
                    return;
                }
                ServerJarInfo info;
                info.filename = QStringLiteral("server.jar");
                info.downloadUrl = url;
                info.sha1 = server.value(QStringLiteral("sha1")).toString();
                onResult(info, QString());
            });
            return;
        }
        case ServerProviderType::Paper:
        {
            // Fill's builds endpoint returns a plain JSON array (not
            // wrapped in a "builds" key like the old v2 API), newest
            // build first, each with a "downloads" map keyed by
            // "server:default" for the runnable server jar.
            const QUrl buildsUrl(QStringLiteral("https://fill.papermc.io/v3/projects/paper/versions/%1/builds").arg(version));
            GetJson(manager_, buildsUrl, [onResult](QJsonDocument doc, QString error) {
                if (!error.isEmpty())
                {
                    onResult(std::nullopt, error);
                    return;
                }
                if (!doc.isArray())
                {
                    // An unknown/invalid version returns {"ok": false, "message": "..."} instead of an array.
                    const QString message = doc.object().value(QStringLiteral("message"))
                        .toString(QStringLiteral("Không tìm thấy build nào cho phiên bản này."));
                    onResult(std::nullopt, message);
                    return;
                }
                for (const auto& b : doc.array())
                {
                    const QJsonObject build = b.toObject();
                    if (build.value(QStringLiteral("channel")).toString() != QStringLiteral("STABLE"))
                    {
                        continue; // skip EXPERIMENTAL builds - only ever serve stable ones automatically
                    }
                    const QJsonObject download =
                        build.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("server:default")).toObject();
                    const QString url = download.value(QStringLiteral("url")).toString();
                    if (url.isEmpty())
                    {
                        continue;
                    }
                    ServerJarInfo info;
                    info.filename = download.value(QStringLiteral("name")).toString();
                    info.downloadUrl = url;
                    onResult(info, QString());
                    return;
                }
                onResult(std::nullopt, QStringLiteral("Không tìm thấy bản STABLE nào cho phiên bản này."));
            });
            return;
        }
        case ServerProviderType::Fabric:
        {
            const QUrl loaderUrl(QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1").arg(version));
            GetJson(manager_, loaderUrl, [this, onResult, version](QJsonDocument doc, QString error) {
                if (!error.isEmpty())
                {
                    onResult(std::nullopt, error);
                    return;
                }
                const QJsonArray loaderEntries = doc.array();
                if (loaderEntries.isEmpty())
                {
                    onResult(std::nullopt, QStringLiteral("Fabric chưa có loader cho phiên bản này."));
                    return;
                }
                const QString loaderVersion =
                    loaderEntries.first().toObject().value(QStringLiteral("loader")).toObject().value(QStringLiteral("version")).toString();

                GetJson(
                    manager_,
                    QUrl(QStringLiteral("https://meta.fabricmc.net/v2/versions/installer")),
                    [onResult, version, loaderVersion](QJsonDocument installerDoc, QString installerError) {
                        if (!installerError.isEmpty())
                        {
                            onResult(std::nullopt, installerError);
                            return;
                        }
                        const QJsonArray installers = installerDoc.array();
                        if (installers.isEmpty())
                        {
                            onResult(std::nullopt, QStringLiteral("Không lấy được phiên bản Fabric installer."));
                            return;
                        }
                        const QString installerVersion = installers.first().toObject().value(QStringLiteral("version")).toString();

                        ServerJarInfo info;
                        info.filename = QStringLiteral("fabric-server-mc.%1-loader.%2-launcher.%3.jar")
                                            .arg(version, loaderVersion, installerVersion);
                        info.downloadUrl = QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1/%2/%3/server/jar")
                                                .arg(version, loaderVersion, installerVersion);
                        onResult(info, QString());
                    });
            });
            return;
        }
    }
}

void ServerProviderClient::DownloadFile(const QString& url, const QString& destinationFilePath, std::function<void(bool, QString)> onFinished)
{
    Get(manager_, QUrl(url), [destinationFilePath, onFinished](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError)
        {
            onFinished(false, reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();

        const QFileInfo info(destinationFilePath);
        QDir().mkpath(info.absolutePath());

        QFile outFile(destinationFilePath);
        if (!outFile.open(QIODevice::WriteOnly))
        {
            onFinished(false, QStringLiteral("Không thể ghi file: %1").arg(destinationFilePath));
            return;
        }
        outFile.write(data);
        outFile.close();
        onFinished(true, QString());
    });
}

} // namespace net
