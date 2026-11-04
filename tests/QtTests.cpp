// Qt-based smoke tests for the pieces added after the original MVP: role-based access control, the
// general zip reader (untrusted .mrpack files), the modpack installer's validation, and the
// translation dictionary. Same style as ConfigTests.cpp: no framework, just Check().

#include "archive/ZipArchive.hpp"
#include "backup/ZipWriter.hpp"
#include "net/ModpackInstaller.hpp"
#include "platform/SecretStore.hpp"
#include "remote/Access.hpp"
#include "ui/I18n.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* description)
{
    std::printf("%s: %s\n", condition ? "ok" : "FAILED", description);
    if (!condition) ++g_failures;
}

// A zip written with Python zipfile (DEFLATE, level 9): a.txt (1949 bytes, crc32 0x210E2092) and dir/b.bin (300 bytes, crc32 0x4ECE0D35).
const unsigned char kDeflateZip[] = {
    0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x32, 0x32, 0x62, 0x5D, 0x92, 0x20,
    0x0E, 0x21, 0xAF, 0x00, 0x00, 0x00, 0x9D, 0x07, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x61, 0x2E,
    0x74, 0x78, 0x74, 0xED, 0xD1, 0x4D, 0x0E, 0x82, 0x30, 0x14, 0x46, 0xD1, 0xB9, 0xAB, 0x60, 0x01,
    0xC4, 0xF8, 0xFE, 0x54, 0x96, 0x83, 0xDA, 0x81, 0x09, 0x08, 0x29, 0xC4, 0xF5, 0x6B, 0xEC, 0xED,
    0x1E, 0x1C, 0x74, 0x74, 0x27, 0x1F, 0x27, 0x2D, 0xCD, 0xE9, 0xB1, 0xED, 0xCB, 0x2B, 0x75, 0xD3,
    0x38, 0xAF, 0x7D, 0xB7, 0xDC, 0xB6, 0x94, 0xDF, 0x29, 0xF7, 0xDD, 0x7D, 0x99, 0xD7, 0x31, 0x8F,
    0xFB, 0x92, 0x8F, 0x5D, 0x6E, 0xA3, 0x36, 0x6A, 0xA3, 0x36, 0x6A, 0xA3, 0x7F, 0x1D, 0x4D, 0xCF,
    0xEF, 0xE0, 0x74, 0xF8, 0x45, 0x4A, 0xB4, 0xC4, 0x4A, 0xBC, 0x24, 0x4A, 0xCE, 0x25, 0x97, 0x92,
    0x6B, 0xC9, 0xC0, 0xE7, 0x95, 0xC1, 0x11, 0x20, 0x41, 0x12, 0x28, 0xC1, 0x12, 0x30, 0x41, 0x13,
    0x38, 0xC1, 0x53, 0x3C, 0xAD, 0xE7, 0xC2, 0x53, 0x3C, 0xC5, 0x53, 0x3C, 0xC5, 0x53, 0x3C, 0xC5,
    0x53, 0x3C, 0xC3, 0x33, 0x3C, 0xAB, 0x17, 0xC5, 0x33, 0x3C, 0xC3, 0x33, 0x3C, 0xC3, 0x33, 0x3C,
    0xC3, 0x73, 0x3C, 0xC7, 0x73, 0x3C, 0xAF, 0x7F, 0x0E, 0xCF, 0xF1, 0x1C, 0xCF, 0xF1, 0x1C, 0xCF,
    0xF1, 0x02, 0x2F, 0xF0, 0x02, 0x2F, 0xF0, 0xA2, 0x3E, 0x05, 0x5E, 0xE0, 0x05, 0x5E, 0xE0, 0xC5,
    0xF0, 0x01, 0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x32, 0x32, 0x62, 0x5D,
    0x35, 0x0D, 0xCE, 0x4E, 0x31, 0x01, 0x00, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x64, 0x69, 0x72, 0x2F, 0x62, 0x2E, 0x62, 0x69, 0x6E, 0x01, 0x2C, 0x01, 0xD3, 0xFE, 0x3C, 0x97,
    0x8B, 0x21, 0x5E, 0xEA, 0x9A, 0x79, 0xA0, 0x94, 0x10, 0x9B, 0x03, 0xE8, 0xD6, 0x78, 0x42, 0x8D,
    0x3B, 0x31, 0xFE, 0xB7, 0x78, 0x8A, 0xD6, 0x8C, 0x79, 0x65, 0xA3, 0xDC, 0x26, 0x3B, 0xA2, 0x26,
    0xDE, 0xED, 0x85, 0x63, 0xBD, 0x03, 0xAB, 0xC6, 0x10, 0x28, 0xC2, 0xF5, 0x97, 0x0A, 0x4D, 0xC7,
    0x07, 0xD2, 0xDD, 0x44, 0x79, 0x98, 0xB8, 0xEB, 0xE0, 0x63, 0xB6, 0xC9, 0xEB, 0x6D, 0x65, 0xBA,
    0xCD, 0x93, 0x71, 0xF6, 0xEF, 0x22, 0xE0, 0x5D, 0x18, 0x09, 0x22, 0x7E, 0x37, 0x42, 0xF7, 0xAC,
    0x6F, 0xC7, 0xA0, 0xDA, 0x4D, 0x6B, 0x81, 0xD5, 0x62, 0x92, 0x59, 0x88, 0x95, 0x68, 0x95, 0x3B,
    0xE7, 0x56, 0xAE, 0xEA, 0xED, 0x07, 0xDB, 0x47, 0xFD, 0x9B, 0xAB, 0xB2, 0x29, 0xB2, 0xDC, 0x53,
    0xF6, 0x8A, 0xE7, 0x92, 0x91, 0x1A, 0xB6, 0xA7, 0x36, 0xA2, 0xD4, 0xFC, 0x92, 0x44, 0x48, 0x1F,
    0x10, 0x7B, 0xDA, 0xA3, 0xFD, 0x7B, 0x16, 0x58, 0xCC, 0x11, 0x69, 0xE5, 0x26, 0x05, 0x4B, 0x6D,
    0xC4, 0x6A, 0xDF, 0x1E, 0x0B, 0x9A, 0x9D, 0xC2, 0x0B, 0x60, 0xB7, 0x96, 0x54, 0x8D, 0xE1, 0xEC,
    0xFB, 0x47, 0x81, 0x3C, 0xFF, 0x09, 0x4F, 0x01, 0x13, 0x1B, 0x99, 0x89, 0x08, 0xF2, 0x32, 0xF8,
    0x68, 0x4A, 0x9C, 0x43, 0x27, 0xB0, 0x0A, 0xFA, 0xDE, 0x56, 0x50, 0x5C, 0xF5, 0x23, 0xE5, 0xDC,
    0x60, 0x60, 0x75, 0xDE, 0x85, 0x62, 0xA4, 0xDD, 0x98, 0xAE, 0x8F, 0x1A, 0x9E, 0xF9, 0xF0, 0xCF,
    0x81, 0x45, 0x6E, 0xA2, 0xB8, 0xB7, 0x3C, 0xEF, 0x4D, 0x6F, 0xFA, 0x42, 0x85, 0x4D, 0x8C, 0x56,
    0x02, 0xC9, 0x6A, 0xFC, 0x94, 0x50, 0x05, 0x60, 0x9D, 0x96, 0xA1, 0x22, 0x0F, 0xA2, 0xA0, 0x55,
    0x77, 0x5A, 0xAD, 0xEA, 0x5A, 0x9B, 0xB4, 0x47, 0xBC, 0x7D, 0x05, 0x96, 0x0F, 0xF4, 0xAD, 0x05,
    0xF6, 0x5E, 0x40, 0xA0, 0x74, 0x4C, 0x97, 0x99, 0x51, 0x2D, 0x5D, 0x2F, 0x50, 0xC2, 0x5E, 0xD8,
    0x98, 0x43, 0x4C, 0xC9, 0x60, 0x1A, 0xC5, 0xD0, 0x06, 0xF8, 0x91, 0xAF, 0xBC, 0x21, 0x4F, 0x80,
    0x38, 0xA7, 0xCD, 0x44, 0x3D, 0x53, 0x2F, 0xAD, 0x6F, 0xA6, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x03,
    0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x32, 0x32, 0x62, 0x5D, 0x92, 0x20, 0x0E, 0x21, 0xAF, 0x00,
    0x00, 0x00, 0x9D, 0x07, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x61, 0x2E, 0x74, 0x78, 0x74, 0x50, 0x4B, 0x01,
    0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x32, 0x32, 0x62, 0x5D, 0x35, 0x0D, 0xCE,
    0x4E, 0x31, 0x01, 0x00, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0xD2, 0x00, 0x00, 0x00, 0x64, 0x69, 0x72, 0x2F, 0x62,
    0x2E, 0x62, 0x69, 0x6E, 0x50, 0x4B, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00,
    0x6A, 0x00, 0x00, 0x00, 0x2A, 0x02, 0x00, 0x00, 0x00, 0x00};

std::filesystem::path WriteBytes(const QTemporaryDir& dir, const char* name, const unsigned char* data, size_t size)
{
    const std::filesystem::path p = QDir(dir.path()).filePath(QString::fromLatin1(name)).toStdWString();
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return p;
}

// ------------------------------------------------------------------ access control

void TestAccess()
{
    using remote::Role;
    Check(remote::RequiredRole(QStringLiteral("list")) == Role::Viewer, "Access: list needs viewer");
    Check(remote::RequiredRole(QStringLiteral("restart")) == Role::Operator, "Access: restart needs operator");
    Check(remote::RequiredRole(QStringLiteral("send")) == Role::Admin, "Access: raw console 'send' needs admin");
    Check(remote::RequiredRole(QStringLiteral("prop-set")) == Role::Admin, "Access: prop-set needs admin");
    Check(remote::RequiredRole(QStringLiteral("audit")) == Role::Admin, "Access: audit needs admin");
    Check(!remote::RequiredRole(QStringLiteral("format-c-drive")).has_value(), "Access: unknown command has no rule");

    const auto viewer = remote::CommandsForRole(Role::Viewer);
    const auto op = remote::CommandsForRole(Role::Operator);
    const auto admin = remote::CommandsForRole(Role::Admin);
    Check(!viewer.contains(QStringLiteral("start")) && !viewer.contains(QStringLiteral("send")), "Access: viewer cannot start or send");
    Check(op.contains(QStringLiteral("start")) && !op.contains(QStringLiteral("send")), "Access: operator can start but not send");
    Check(admin.contains(QStringLiteral("send")) && admin.contains(QStringLiteral("audit")), "Access: admin has everything");
    Check(viewer.size() < op.size() && op.size() < admin.size(), "Access: roles are cumulative");
    for (const QString& c : viewer) Check(op.contains(c) && admin.contains(c), "Access: every viewer command is also operator/admin");

    Check(remote::ParseRole(QStringLiteral(" Admin ")) == Role::Admin, "Access: ParseRole is case/space tolerant");
    Check(!remote::ParseRole(QStringLiteral("root")).has_value(), "Access: ParseRole rejects unknown roles");

    Check(remote::ValidAccountName(QStringLiteral("ops-1.a_b")), "Access: normal account name is valid");
    Check(!remote::ValidAccountName(QStringLiteral("bad name")) && !remote::ValidAccountName(QString()) &&
              !remote::ValidAccountName(QStringLiteral("a\nb")), "Access: spaces/empty/newline names are invalid");

    remote::Account scoped;
    scoped.serverIds = {QStringLiteral("id-1")};
    remote::Account everywhere;
    Check(remote::CanAccessServer(everywhere, QStringLiteral("anything")), "Access: empty scope = every server");
    Check(remote::CanAccessServer(scoped, QStringLiteral("id-1")) && !remote::CanAccessServer(scoped, QStringLiteral("id-2")),
          "Access: scoped account only reaches its servers");

    QList<remote::Account> list;
    list.push_back({QStringLiteral("admin"), QStringLiteral("secret-secret-secret"), Role::Admin, {}});
    list.push_back({QStringLiteral("w"), QStringLiteral("secret2-secret2-secret2"), Role::Viewer, {QStringLiteral("a"), QStringLiteral("b")}});
    const auto back = remote::AccountsFromJson(remote::AccountsToJson(list));
    Check(back.size() == 2 && back[1].serverIds.size() == 2 && back[0].role == Role::Admin, "Access: accounts survive a JSON round trip");

    // A tampered settings file must never escalate privileges.
    const auto tampered = remote::AccountsFromJson(QStringLiteral(
        R"([{"name":"x","secret":"s","role":"superuser"},{"name":"bad name","secret":"s","role":"admin"},{"name":"nosecret","role":"admin"}])"));
    Check(tampered.size() == 1 && tampered[0].role == Role::Viewer, "Access: unknown role -> viewer; invalid/secretless accounts are dropped");
}

// -------------------------------------------------------------------------- zip

void TestZipArchive()
{
    QTemporaryDir dir;
    const auto path = WriteBytes(dir, "deflate.zip", kDeflateZip, sizeof(kDeflateZip));

    archive::ZipArchive zip;
    std::string error;
    Check(zip.Open(path, &error), "Zip: opens a DEFLATE archive");
    Check(zip.Entries().size() == 2, "Zip: lists both entries");

    std::vector<std::uint8_t> out;
    const auto* a = zip.Find("a.txt");
    Check(a != nullptr && zip.ReadToMemory(*a, out, 1 << 20, &error), "Zip: inflates a.txt");
    Check(out.size() == 1949 && archive::Crc32(out.data(), out.size()) == 0x210e2092U, "Zip: a.txt has the right size and CRC-32");
    Check(std::string(out.begin(), out.begin() + 14) == "redstone lamp,", "Zip: a.txt content is intact");
    const auto* b = zip.Find("dir/b.bin");
    Check(b != nullptr && zip.ReadToMemory(*b, out, 1 << 20, &error) && archive::Crc32(out.data(), out.size()) == 0x4ece0d35U,
          "Zip: random (stored-block) data round-trips");
    Check(!zip.ReadToMemory(*a, out, 100, &error), "Zip: per-entry size cap rejects a big entry (zip-bomb guard)");

    // Corrupt the compressed stream: must fail cleanly (never crash, never succeed silently).
    std::vector<unsigned char> broken(kDeflateZip, kDeflateZip + sizeof(kDeflateZip));
    for (size_t i = 60; i < 90; ++i) broken[i] ^= 0xFF;
    const auto brokenPath = WriteBytes(dir, "broken.zip", broken.data(), broken.size());
    archive::ZipArchive bad;
    bool anyFailed = false;
    if (bad.Open(brokenPath, &error))
    {
        for (const auto& e : bad.Entries())
        {
            std::vector<std::uint8_t> tmp;
            if (!bad.ReadToMemory(e, tmp, 1 << 20, &error)) anyFailed = true;
        }
    }
    else
    {
        anyFailed = true;
    }
    Check(anyFailed, "Zip: corrupted data is rejected");

    const auto truncatedPath = WriteBytes(dir, "trunc.zip", kDeflateZip, sizeof(kDeflateZip) / 3);
    archive::ZipArchive truncated;
    Check(!truncated.Open(truncatedPath, &error), "Zip: truncated archive is rejected");

    Check(archive::IsSafeRelativePath("overrides/config/a.toml") && archive::IsSafeRelativePath("mods/x.jar"), "Zip: normal paths are safe");
    Check(!archive::IsSafeRelativePath("../evil.txt") && !archive::IsSafeRelativePath("a/../../b") && !archive::IsSafeRelativePath("/abs") &&
              !archive::IsSafeRelativePath("C:/w.txt") && !archive::IsSafeRelativePath("a\\..\\..\\b") && !archive::IsSafeRelativePath(""),
          "Zip: traversal / absolute / drive-letter paths are refused");
    Check(archive::Crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926U, "Zip: CRC-32 test vector");

    std::srand(7);
    int survived = 0;
    for (int i = 0; i < 1000; ++i)
    {
        std::vector<std::uint8_t> junk(static_cast<size_t>(std::rand() % 300)), sink;
        for (auto& x : junk) x = static_cast<std::uint8_t>(std::rand());
        archive::Inflate(junk.data(), junk.size(), sink, 1 << 20);
        ++survived;
    }
    Check(survived == 1000, "Zip: 1000 random garbage inputs never crash the inflater");
}

// -------------------------------------------------------------------- modpacks

// Builds a .mrpack with the app's own (store-only) ZipWriter; the reader handles both methods.
std::filesystem::path MakePack(const QTemporaryDir& dir, const char* name, const QByteArray& indexJson,
                               const std::vector<std::pair<std::string, QByteArray>>& extra)
{
    const QString work = QDir(dir.path()).filePath(QStringLiteral("work_") + QString::fromLatin1(name));
    QDir().mkpath(work);
    const auto add = [&](backup::ZipWriter& w, const std::string& archiveName, const QByteArray& bytes, int n) {
        const QString file = QDir(work).filePath(QString::number(n));
        QFile f(file);
        f.open(QIODevice::WriteOnly);
        f.write(bytes);
        f.close();
        return w.AddFile(archiveName, file.toStdWString());
    };
    const std::filesystem::path zipPath = QDir(dir.path()).filePath(QString::fromLatin1(name) + QStringLiteral(".mrpack")).toStdWString();
    backup::ZipWriter writer(zipPath);
    int n = 0;
    add(writer, "modrinth.index.json", indexJson, n++);
    for (const auto& [entryName, bytes] : extra) add(writer, entryName, bytes, n++);
    writer.Finish();
    return zipPath;
}

QByteArray Index(const QString& filesJson)
{
    return QStringLiteral(R"({"formatVersion":1,"game":"minecraft","versionId":"1.2.3","name":"Cool Pack","summary":"s",
        "dependencies":{"minecraft":"1.21.4","fabric-loader":"0.16.9"},"files":%1})").arg(filesJson).toUtf8();
}

void TestModpack()
{
    QTemporaryDir dir;
    net::ModrinthClient client;
    const QString sha(128, QLatin1Char('a'));
    QString err;

    const auto good = MakePack(dir, "good", Index(QStringLiteral(
        R"([{"path":"mods/client.jar","hashes":{"sha512":"%1"},"env":{"server":"unsupported"},"downloads":["https://cdn.modrinth.com/a"],"fileSize":10},
            {"path":"mods/need.jar","hashes":{"sha512":"%1"},"downloads":["https://evil.example/x","https://cdn.modrinth.com/b"],"fileSize":2048}])").arg(sha)), {});
    net::ModpackInstaller ok(&client);
    Check(ok.Prepare(QString::fromStdWString(good.wstring()), dir.path() + QStringLiteral("/srv"), &err), "Modpack: a valid pack is accepted");
    Check(ok.Info().loader == QLatin1String("fabric") && ok.Info().minecraft == QLatin1String("1.21.4"), "Modpack: loader and Minecraft version are read");
    Check(ok.Info().serverFileCount == 1 && ok.Info().clientOnlySkipped == 1 && ok.Info().totalBytes == 2048, "Modpack: client-only files are skipped");

    const auto rejects = [&](const char* name, const QByteArray& index, const char* description) {
        const auto p = MakePack(dir, name, index, {});
        net::ModpackInstaller i(&client);
        QString e;
        Check(!i.Prepare(QString::fromStdWString(p.wstring()), dir.path() + QStringLiteral("/srv"), &e), description);
    };
    rejects("unsafe", Index(QStringLiteral(R"([{"path":"../../evil.jar","hashes":{"sha512":"%1"},"downloads":["https://cdn.modrinth.com/x"],"fileSize":1}])").arg(sha)),
            "Modpack: path traversal is refused");
    rejects("nohash", Index(QStringLiteral(R"([{"path":"mods/a.jar","hashes":{"sha1":"b"},"downloads":["https://cdn.modrinth.com/x"],"fileSize":1}])")),
            "Modpack: a file without SHA-512 is refused");
    rejects("badhost", Index(QStringLiteral(R"([{"path":"mods/a.jar","hashes":{"sha512":"%1"},"downloads":["http://cdn.modrinth.com/x","https://evil.example/x"],"fileSize":1}])").arg(sha)),
            "Modpack: plain-http / unknown download hosts are refused");
    rejects("format", QByteArrayLiteral(R"({"formatVersion":2,"game":"minecraft","files":[]})"), "Modpack: unsupported format version is refused");

    // Overrides: server-overrides win, client-overrides are ignored, traversal entries are skipped.
    const auto ovr = MakePack(dir, "ovr", Index(QStringLiteral("[]")), {
        {"overrides/config/a.toml", "from overrides"}, {"server-overrides/config/a.toml", "from server-overrides"},
        {"client-overrides/x.toml", "client"}, {"overrides/../../escape.txt", "evil"}, {"overrides/sub/ok.txt", "ok"}});
    const QString target = dir.path() + QStringLiteral("/srv2");
    net::ModpackInstaller inst(&client);
    Check(inst.Prepare(QString::fromStdWString(ovr.wstring()), target, &err), "Modpack: override-only pack is accepted");
    bool finished = false;
    net::ModpackInstaller::Result result;
    inst.Install([](int, int, const QString&) {}, [&](net::ModpackInstaller::Result r) { result = r; finished = true; });
    Check(finished && result.ok && result.errors.isEmpty(), "Modpack: pack with no downloads installs synchronously");
    const auto read = [](const QString& p) { QFile f(p); f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()); };
    Check(read(target + QStringLiteral("/config/a.toml")) == QLatin1String("from server-overrides"), "Modpack: server-overrides win over overrides");
    Check(read(target + QStringLiteral("/sub/ok.txt")) == QLatin1String("ok"), "Modpack: nested override files are written");
    Check(!QFile::exists(target + QStringLiteral("/x.toml")), "Modpack: client-overrides are ignored");
    Check(!QFile::exists(dir.path() + QStringLiteral("/escape.txt")) && !QFile::exists(target + QStringLiteral("/../escape.txt")),
          "Modpack: a traversal entry cannot escape the server folder");
    Check(result.warnings.size() == 1, "Modpack: the skipped traversal entry is reported as a warning");
}

// ---------------------------------------------------------------- secret store

void TestSecretStore()
{
    const QString token = QStringLiteral("0123456789abcdef0123456789abcdef");
    const QString stored = platform::ProtectSecret(token);
    bool ok = false;
    Check(platform::UnprotectSecret(stored, &ok) == token && ok, "Secrets: protect/unprotect round-trips");
    Check(platform::UnprotectSecret(QStringLiteral("legacy-plain-token"), &ok) == QLatin1String("legacy-plain-token") && ok,
          "Secrets: older plain-text values still load");
    Check(platform::UnprotectSecret(QStringLiteral("dpapi:AAAA"), &ok).isEmpty() && !ok, "Secrets: an undecryptable protected value is reported, not guessed");
    Check(platform::ProtectSecret(QString()).isEmpty(), "Secrets: empty stays empty");
    if (platform::IsProtectionAvailable())
    {
        Check(stored != token && stored.startsWith(QLatin1String("dpapi:")), "Secrets: the stored form is not the plain token (Windows DPAPI)");
    }
}

// ------------------------------------------------------------------------ i18n

void TestI18n()
{
    Check(i18n::SetLanguage(QStringLiteral("en")), "I18n: the built-in English dictionary loads");
    Check(i18n::Tr(QStringLiteral("Đóng")) == QLatin1String("Close"), "I18n: a Vietnamese source string is translated");
    Check(i18n::Tr(QStringLiteral("không có trong từ điển")) == QStringLiteral("không có trong từ điển"), "I18n: unknown text falls back to the source");
    i18n::SetLanguage(QStringLiteral("vi"));
    Check(i18n::Tr(QStringLiteral("Đóng")) == QStringLiteral("Đóng"), "I18n: Vietnamese keeps the source text");
    Check(i18n::TrIn(QStringLiteral("en"), QStringLiteral("Đóng")) == QLatin1String("Close"), "I18n: TrIn translates into another language on demand");
    Check(!i18n::AvailableLanguages().isEmpty() && i18n::AvailableLanguages().first().code == QLatin1String("vi"), "I18n: the source language is listed first");

    // Every translation must keep the placeholders of its source string (%1, %2, {}).
    QFile file(QStringLiteral(":/i18n/en.json"));
    Check(file.open(QIODevice::ReadOnly), "I18n: en.json is embedded in the executable");
    const QJsonObject dict = QJsonDocument::fromJson(file.readAll()).object();
    static const QRegularExpression placeholders(QStringLiteral(R"(%\d|\{\})"));
    const auto collect = [](const QString& s) {
        QStringList found;
        auto it = placeholders.globalMatch(s);
        while (it.hasNext()) found << it.next().captured();
        found.sort();
        return found;
    };
    int mismatches = 0;
    for (auto it = dict.begin(); it != dict.end(); ++it)
    {
        if (it.key() == QLatin1String("_name")) continue;
        if (collect(it.key()) != collect(it.value().toString()))
        {
            std::printf("  placeholder mismatch: %s\n", qPrintable(it.key().left(70)));
            ++mismatches;
        }
    }
    Check(mismatches == 0, "I18n: all English translations keep their placeholders");
    Check(dict.size() > 500, "I18n: the English dictionary is substantial");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestAccess();
    TestZipArchive();
    TestModpack();
    TestSecretStore();
    TestI18n();

    if (g_failures == 0)
    {
        std::printf("\nAll Qt tests passed.\n");
        return 0;
    }
    std::printf("\n%d Qt test(s) failed.\n", g_failures);
    return 1;
}
