#pragma once

// Installs a Modrinth modpack (.mrpack) into a SERVER directory:
//  1. parses modrinth.index.json and validates every target path
//  2. downloads every file the server needs (env.server != "unsupported"),
//     4 at a time, each verified against its SHA-512, from the hosts the
//     .mrpack spec allows
//  3. applies the pack's "overrides/" then "server-overrides/" folders
//     ("client-overrides/" is ignored) through archive::ZipArchive, which
//     guards against path traversal, zip bombs and corrupt data
//
// It does NOT install the Minecraft server jar or the mod loader named in
// the pack's dependencies (Fabric/Forge/NeoForge/Quilt) - the caller shows
// those requirements to the user, and the server must already run them.

#include "net/ModrinthClient.hpp"

#include <QList>
#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

namespace net
{

struct ModpackInfo
{
    QString name;
    QString versionId;
    QString summary;
    QString minecraft;     // dependencies.minecraft
    QString loader;        // fabric | forge | neoforge | quilt | "" (vanilla-only pack)
    QString loaderVersion;
    int serverFileCount = 0;
    int clientOnlySkipped = 0;
    qint64 totalBytes = 0;
};

class ModpackInstaller : public QObject
{
    Q_OBJECT

public:
    ModpackInstaller(ModrinthClient* client, QObject* parent = nullptr);

    // Parses and validates. On failure returns false and fills `error`.
    bool Prepare(const QString& mrpackPath, const QString& serverDirectory, QString* error);
    [[nodiscard]] const ModpackInfo& Info() const { return info_; }

    struct Result
    {
        bool ok = false;
        QStringList errors;   // anything here means the install failed
        QStringList warnings; // e.g. unsafe override entries that were skipped
        int filesDownloaded = 0;
        int overridesApplied = 0;
    };

    void Install(
        std::function<void(int done, int total, const QString& current)> onProgress, std::function<void(Result result)> onFinished);

private:
    struct PackFile
    {
        QString path;
        ModrinthFile file;
        QString destination;
    };
    struct Run;

    void Pump(const std::shared_ptr<Run>& run);
    void Finish(const std::shared_ptr<Run>& run);
    int ApplyOverrides(QStringList* errors, QStringList* warnings) const;

    ModrinthClient* client_;
    QString mrpackPath_;
    QString serverDir_;
    ModpackInfo info_;
    QList<PackFile> files_;
};

} // namespace net
