#pragma once

// Role-based access control for the remote-control endpoint.
//
// Every token is an ACCOUNT: a name, a secret, a role and an optional list of
// servers it may touch. The server enforces this on every request - the CLI
// only displays it.
//
//   viewer    read-only: list, status, console (tail + live), players, backups, props
//   operator  + start / stop / restart, player actions (op, kick, ban, whitelist,
//             gamemode, msg, say), create a backup
//   admin     + raw console commands (`send`), editing server.properties,
//             reading the audit log, creating/deleting servers (`create`, `delete`)
//
// Server scope: an empty `serverIds` means "all servers"; otherwise the account
// cannot see or act on any other server (they are hidden from `list` too).

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace remote
{

enum class Role
{
    Viewer = 0,
    Operator = 1,
    Admin = 2
};

struct Account
{
    QString name;
    QString secret;
    Role role = Role::Viewer;
    QStringList serverIds; // server ids; empty = every server
};

QString RoleName(Role role);                        // "viewer" | "operator" | "admin"
std::optional<Role> ParseRole(const QString& name); // case-insensitive

// Lowest role allowed to run `cmd`; nullopt if the command does not exist.
std::optional<Role> RequiredRole(const QString& cmd);
QStringList CommandsForRole(Role role);             // everything `role` may run, sorted
QJsonObject PermissionMatrix();                     // {"viewer":[...],"operator":[...],"admin":[...]} (cumulative)

bool ValidAccountName(const QString& name);         // ^[A-Za-z0-9_.-]{1,32}$
bool CanAccessServer(const Account& account, const QString& serverId);

// Persistence helpers (stored as one JSON string in app_settings.ini).
QString AccountsToJson(const QList<Account>& accounts);
QList<Account> AccountsFromJson(const QString& json);

} // namespace remote
