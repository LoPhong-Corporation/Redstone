#include "net/ModpackInstaller.hpp"
#include "ui/I18n.hpp"

#include "archive/ZipArchive.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace net
{
namespace
{
constexpr int kParallelDownloads = 4;
constexpr std::uint64_t kMaxIndexBytes = 8ULL * 1024 * 1024;
constexpr std::uint64_t kMaxOverrideFileBytes = 256ULL * 1024 * 1024;
constexpr std::uint64_t kMaxOverrideTotalBytes = 1024ULL * 1024 * 1024;
constexpr int kMaxFiles = 5000;

QString StdToQ(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<int>(s.size())); }
} // namespace

struct ModpackInstaller::Run
{
    int next = 0;
    int inflight = 0;
    int done = 0;
    int total = 0;
    bool failed = false;
    bool finishedCalled = false;
    QStringList errors;
    std::function<void(int, int, const QString&)> progress;
    std::function<void(Result)> finished;
};

ModpackInstaller::ModpackInstaller(ModrinthClient* client, QObject* parent)
    : QObject(parent)
    , client_(client)
{
}

bool ModpackInstaller::Prepare(const QString& mrpackPath, const QString& serverDirectory, QString* error)
{
    info_ = ModpackInfo();
    files_.clear();
    mrpackPath_ = mrpackPath;
    serverDir_ = QDir::cleanPath(QFileInfo(serverDirectory).absoluteFilePath());

    archive::ZipArchive zip;
    std::string zipError;
    if (!zip.Open(mrpackPath.toStdWString(), &zipError))
    {
        *error = TR("Không đọc được file modpack: %1").arg(StdToQ(zipError));
        return false;
    }
    const archive::ZipEntryInfo* indexEntry = zip.Find("modrinth.index.json");
    std::vector<std::uint8_t> indexBytes;
    if (indexEntry == nullptr || !zip.ReadToMemory(*indexEntry, indexBytes, kMaxIndexBytes, &zipError))
    {
        *error = TR("Modpack thiếu hoặc hỏng modrinth.index.json. %1").arg(StdToQ(zipError));
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(indexBytes.data()), static_cast<int>(indexBytes.size())));
    if (!doc.isObject())
    {
        *error = TR("modrinth.index.json không phải JSON hợp lệ.");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("game")).toString() != QLatin1String("minecraft") || root.value(QStringLiteral("formatVersion")).toInt() != 1)
    {
        *error = TR("Định dạng modpack không được hỗ trợ (cần formatVersion 1, game minecraft).");
        return false;
    }

    info_.name = root.value(QStringLiteral("name")).toString();
    info_.versionId = root.value(QStringLiteral("versionId")).toString();
    info_.summary = root.value(QStringLiteral("summary")).toString();
    const QJsonObject deps = root.value(QStringLiteral("dependencies")).toObject();
    info_.minecraft = deps.value(QStringLiteral("minecraft")).toString();
    static const QList<QPair<QString, QString>> loaders = {
        {QStringLiteral("fabric-loader"), QStringLiteral("fabric")},
        {QStringLiteral("quilt-loader"), QStringLiteral("quilt")},
        {QStringLiteral("neoforge"), QStringLiteral("neoforge")},
        {QStringLiteral("forge"), QStringLiteral("forge")},
    };
    for (const auto& [key, name] : loaders)
    {
        if (deps.contains(key))
        {
            info_.loader = name;
            info_.loaderVersion = deps.value(key).toString();
            break;
        }
    }

    const QJsonArray filesJson = root.value(QStringLiteral("files")).toArray();
    if (filesJson.size() > kMaxFiles)
    {
        *error = TR("Modpack có quá nhiều file (%1).").arg(filesJson.size());
        return false;
    }
    for (const auto& value : filesJson)
    {
        const QJsonObject o = value.toObject();
        const QString path = o.value(QStringLiteral("path")).toString();
        if (!archive::IsSafeRelativePath(path.toStdString()))
        {
            *error = TR("Modpack chứa đường dẫn không an toàn, đã từ chối: %1").arg(path);
            return false;
        }
        // Belt and braces: the resolved target must stay inside the server directory.
        const QString destination = QDir::cleanPath(QDir(serverDir_).filePath(QDir::fromNativeSeparators(path)));
        if (!destination.startsWith(serverDir_ + QLatin1Char('/')))
        {
            *error = TR("Đường dẫn nằm ngoài thư mục server: %1").arg(path);
            return false;
        }

        const QJsonObject env = o.value(QStringLiteral("env")).toObject();
        if (env.value(QStringLiteral("server")).toString() == QLatin1String("unsupported"))
        {
            ++info_.clientOnlySkipped;
            continue;
        }

        PackFile pf;
        pf.path = path;
        pf.destination = destination;
        pf.file.filename = QFileInfo(path).fileName();
        pf.file.sha512 = o.value(QStringLiteral("hashes")).toObject().value(QStringLiteral("sha512")).toString().toLower();
        pf.file.size = static_cast<qint64>(o.value(QStringLiteral("fileSize")).toDouble());
        for (const auto& d : o.value(QStringLiteral("downloads")).toArray())
        {
            if (ModrinthClient::IsModpackUrl(d.toString()))
            {
                pf.file.url = d.toString();
                break;
            }
        }
        if (pf.file.url.isEmpty())
        {
            *error = TR("File \"%1\" không có link tải từ host được phép (cdn.modrinth.com, github.com, raw.githubusercontent.com, gitlab.com).").arg(path);
            return false;
        }
        if (pf.file.sha512.size() != 128)
        {
            *error = TR("File \"%1\" thiếu mã SHA-512 - từ chối vì không thể kiểm tra toàn vẹn.").arg(path);
            return false;
        }
        info_.totalBytes += pf.file.size;
        files_.push_back(pf);
    }
    info_.serverFileCount = static_cast<int>(files_.size());
    return true;
}

void ModpackInstaller::Install(
    std::function<void(int done, int total, const QString& current)> onProgress, std::function<void(Result result)> onFinished)
{
    auto run = std::make_shared<Run>();
    run->total = static_cast<int>(files_.size());
    run->progress = std::move(onProgress);
    run->finished = std::move(onFinished);
    Pump(run);
}

void ModpackInstaller::Pump(const std::shared_ptr<Run>& run)
{
    while (!run->failed && run->inflight < kParallelDownloads && run->next < files_.size())
    {
        const PackFile pf = files_[run->next++];
        ++run->inflight;
        client_->DownloadFile(
            pf.file, pf.destination, nullptr,
            [this, run, pf](bool ok, QString error) {
                --run->inflight;
                if (ok)
                {
                    ++run->done;
                }
                else
                {
                    run->failed = true;
                    run->errors << QStringLiteral("%1: %2").arg(pf.path, error);
                }
                if (run->progress)
                {
                    run->progress(run->done, run->total, pf.path);
                }
                Pump(run);
            },
            true);
    }
    if (run->inflight == 0 && (run->failed || run->next >= files_.size()))
    {
        Finish(run);
    }
}

void ModpackInstaller::Finish(const std::shared_ptr<Run>& run)
{
    if (run->finishedCalled)
    {
        return;
    }
    run->finishedCalled = true;

    Result result;
    result.filesDownloaded = run->done;
    result.errors = run->errors;
    if (!run->failed)
    {
        result.overridesApplied = ApplyOverrides(&result.errors, &result.warnings);
        result.ok = result.errors.isEmpty();
    }
    if (run->finished)
    {
        run->finished(result);
    }
}

int ModpackInstaller::ApplyOverrides(QStringList* errors, QStringList* warnings) const
{
    archive::ZipArchive zip;
    std::string zipError;
    if (!zip.Open(mrpackPath_.toStdWString(), &zipError))
    {
        *errors << StdToQ(zipError);
        return 0;
    }

    int applied = 0;
    std::uint64_t total = 0;
    // Order matters: server-overrides win over overrides.
    for (const char* prefix : {"overrides/", "server-overrides/"})
    {
        const std::string pre(prefix);
        for (const auto& entry : zip.Entries())
        {
            if (entry.name.compare(0, pre.size(), pre) != 0 || entry.IsDirectory())
            {
                continue;
            }
            const std::string relative = entry.name.substr(pre.size());
            if (relative.empty() || !archive::IsSafeRelativePath(relative))
            {
                *warnings << TR("Bỏ qua đường dẫn không an toàn trong overrides: %1").arg(StdToQ(entry.name));
                continue;
            }
            const QString destination = QDir::cleanPath(QDir(serverDir_).filePath(StdToQ(relative)));
            if (!destination.startsWith(serverDir_ + QLatin1Char('/')))
            {
                *warnings << TR("Đường dẫn overrides nằm ngoài thư mục server: %1").arg(StdToQ(relative));
                continue;
            }
            total += entry.size;
            if (total > kMaxOverrideTotalBytes)
            {
                *errors << TR("Overrides vượt quá giới hạn dung lượng cho phép - dừng.");
                return applied;
            }
            if (!zip.ExtractToFile(entry, destination.toStdWString(), kMaxOverrideFileBytes, &zipError))
            {
                *errors << StdToQ(zipError);
                continue;
            }
            ++applied;
        }
    }
    return applied;
}

} // namespace net
