#include "ui/I18n.hpp"

#include "core/Localize.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QReadWriteLock>

namespace i18n
{
namespace
{

constexpr const char* kSourceLanguage = "vi";

QReadWriteLock g_lock;
QHash<QString, QString> g_dictionary;
QString g_language = QStringLiteral("vi");

QString ExternalDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations"));
}

// Reads one JSON dictionary file; returns false if it does not exist / is invalid.
bool ReadDictionary(const QString& path, QHash<QString, QString>& into, QString* nameOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
    {
        return false;
    }
    const QJsonObject o = doc.object();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
    {
        if (it.key() == QLatin1String("_name"))
        {
            if (nameOut != nullptr) *nameOut = it.value().toString();
            continue;
        }
        if (it.value().isString() && !it.value().toString().isEmpty())
        {
            into.insert(it.key(), it.value().toString());
        }
    }
    return true;
}

std::wstring LocalizeWide(const wchar_t* source)
{
    return TrW(source);
}

std::string LocalizeUtf8Text(const char* source)
{
    return Tr(QString::fromUtf8(source)).toUtf8().toStdString();
}

} // namespace

QString ResolveAuto()
{
    return QLocale::system().name().startsWith(QLatin1String("vi")) ? QStringLiteral("vi") : QStringLiteral("en");
}

bool SetLanguage(const QString& requested)
{
    const QString code = (requested.isEmpty() || requested == QLatin1String("auto")) ? ResolveAuto() : requested.toLower();

    QHash<QString, QString> dictionary;
    const bool builtIn = ReadDictionary(QStringLiteral(":/i18n/%1.json").arg(code), dictionary, nullptr);
    const bool external = ReadDictionary(QDir(ExternalDir()).filePath(code + QStringLiteral(".json")), dictionary, nullptr);
    // "vi" is the source language: its optional dictionary only fixes up a few English backend strings.
    const bool found = builtIn || external || code == QLatin1String(kSourceLanguage);

    {
        QWriteLocker lock(&g_lock);
        g_dictionary = std::move(dictionary);
        g_language = found ? code : QStringLiteral("vi");
    }
    core::SetLocalizer(&LocalizeWide, &LocalizeUtf8Text);
    return found;
}

QString CurrentLanguage()
{
    QReadLocker lock(&g_lock);
    return g_language;
}

QList<LanguageInfo> AvailableLanguages()
{
    QHash<QString, QString> names; // code -> name
    names.insert(QStringLiteral("vi"), QStringLiteral("Tiếng Việt"));

    auto scan = [&names](const QString& dirPath) {
        const QDir dir(dirPath);
        for (const QString& fileName : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        {
            const QString code = fileName.left(fileName.size() - 5).toLower();
            QHash<QString, QString> ignored;
            QString name;
            if (ReadDictionary(dir.filePath(fileName), ignored, &name))
            {
                names.insert(code, name.isEmpty() ? code : name);
            }
        }
    };
    scan(QStringLiteral(":/i18n"));
    scan(ExternalDir());

    QList<LanguageInfo> list;
    list.push_back({QStringLiteral("vi"), names.take(QStringLiteral("vi"))});
    QStringList codes = names.keys();
    codes.sort();
    for (const QString& code : codes)
    {
        list.push_back({code, names.value(code)});
    }
    return list;
}

QString Tr(const QString& source)
{
    QReadLocker lock(&g_lock);
    const auto it = g_dictionary.constFind(source);
    return it != g_dictionary.constEnd() ? it.value() : source;
}

std::wstring TrW(const wchar_t* source)
{
    return Tr(QString::fromWCharArray(source)).toStdWString();
}

} // namespace i18n
