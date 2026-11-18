#include "remote/Access.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace remote
{
namespace
{

struct Rule
{
    const char* cmd;
    Role role;
};

// The single source of truth for who may run what.
constexpr Rule kRules[] = {
    {"ping", Role::Viewer},        {"whoami", Role::Viewer},    {"app", Role::Viewer},      {"list", Role::Viewer},
    {"status", Role::Viewer},      {"console", Role::Viewer},   {"players", Role::Viewer},  {"backups", Role::Viewer},
    {"props", Role::Viewer},       {"subscribe", Role::Viewer}, {"unsubscribe", Role::Viewer},

    {"start", Role::Operator},     {"stop", Role::Operator},    {"restart", Role::Operator},
    {"player", Role::Operator},    {"backup", Role::Operator},

    {"send", Role::Admin},         {"prop-set", Role::Admin},   {"audit", Role::Admin},
    {"create", Role::Admin},       {"delete", Role::Admin},
};

} // namespace

QString RoleName(Role role)
{
    switch (role)
    {
        case Role::Viewer: return QStringLiteral("viewer");
        case Role::Operator: return QStringLiteral("operator");
        case Role::Admin: return QStringLiteral("admin");
    }
    return QStringLiteral("viewer");
}

std::optional<Role> ParseRole(const QString& name)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("viewer")) return Role::Viewer;
    if (n == QLatin1String("operator")) return Role::Operator;
    if (n == QLatin1String("admin")) return Role::Admin;
    return std::nullopt;
}

std::optional<Role> RequiredRole(const QString& cmd)
{
    for (const Rule& r : kRules)
    {
        if (cmd == QLatin1String(r.cmd)) return r.role;
    }
    return std::nullopt;
}

QStringList CommandsForRole(Role role)
{
    QStringList list;
    for (const Rule& r : kRules)
    {
        if (static_cast<int>(role) >= static_cast<int>(r.role)) list << QString::fromLatin1(r.cmd);
    }
    list.sort();
    return list;
}

QJsonObject PermissionMatrix()
{
    QJsonObject o;
    for (Role role : {Role::Viewer, Role::Operator, Role::Admin})
    {
        QJsonArray a;
        for (const QString& c : CommandsForRole(role)) a.append(c);
        o.insert(RoleName(role), a);
    }
    return o;
}

bool ValidAccountName(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.-]{1,32}$"));
    return pattern.match(name).hasMatch();
}

bool CanAccessServer(const Account& account, const QString& serverId)
{
    return account.serverIds.isEmpty() || account.serverIds.contains(serverId);
}

QString AccountsToJson(const QList<Account>& accounts)
{
    QJsonArray array;
    for (const Account& a : accounts)
    {
        QJsonObject o;
        o.insert(QStringLiteral("name"), a.name);
        o.insert(QStringLiteral("secret"), a.secret);
        o.insert(QStringLiteral("role"), RoleName(a.role));
        o.insert(QStringLiteral("servers"), QJsonArray::fromStringList(a.serverIds));
        array.append(o);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<Account> AccountsFromJson(const QString& json)
{
    QList<Account> accounts;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    for (const auto& v : doc.array())
    {
        const QJsonObject o = v.toObject();
        Account a;
        a.name = o.value(QStringLiteral("name")).toString();
        a.secret = o.value(QStringLiteral("secret")).toString();
        a.role = ParseRole(o.value(QStringLiteral("role")).toString()).value_or(Role::Viewer); // unknown role -> least privilege
        for (const auto& s : o.value(QStringLiteral("servers")).toArray()) a.serverIds << s.toString();
        if (ValidAccountName(a.name) && !a.secret.isEmpty()) accounts.push_back(a);
    }
    return accounts;
}

} // namespace remote
