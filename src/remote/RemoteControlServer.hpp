#pragma once

// Remote-control endpoint used by the Go command-line client (cli/redstonectl).
//
// Transport: plain TCP, newline-delimited JSON ("JSON Lines"), bound to the
// LOOPBACK interface only (127.0.0.1). It is never reachable from the network
// directly; to control a server on another machine the CLI opens an OpenSSH
// port-forward (ssh -L) to this port, so transport security, host-key
// verification and user authentication are all handled by SSH.
//
// Authentication (on top of loopback, so a rogue local process still needs the
// secret): the server sends a random nonce; the client answers with
// HMAC-SHA256(key = shared token, message = nonce). The token itself never
// crosses the socket. Wrong proofs are rate-limited.
//
// Exposed operations mirror what the app's own UI can do for a server:
// list/status, start/stop/restart, console tail + live stream, console
// commands, player management (op/deop/kick/ban/pardon/whitelist/gamemode/
// msg/say), backups (create/list) and server.properties get/set.
// Deliberately NOT exposed: adding/removing servers, restoring backups,
// editing arbitrary files, installing mods, changing app settings.
//
// Full wire format: docs/REMOTE_PROTOCOL.md

#include "core/EventDispatcher.hpp"
#include "remote/Access.hpp"
#include "server/ServerManager.hpp"

#include <QHash>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace remote
{

class RemoteControlServer : public QObject
{
    Q_OBJECT

public:
    static constexpr int kProtocolVersion = 2; // v2: per-account authentication (see Access.hpp)

    RemoteControlServer(
        std::shared_ptr<server::ServerManager> serverManager,
        std::shared_ptr<core::EventDispatcher> events,
        QObject* parent = nullptr);
    ~RemoteControlServer() override;

    // Starts listening on 127.0.0.1:port with the given accounts. Returns false and fills `error` on failure.
    bool Start(quint16 port, const QList<Account>& accounts, QString* error);
    void Stop();
    [[nodiscard]] bool IsListening() const;
    [[nodiscard]] quint16 Port() const;

    // Last `lines` lines of data/remote_audit.log (newest last); empty if there is none.
    [[nodiscard]] static QStringList ReadAuditTail(int lines);

    // 256-bit random token as 64 hex characters.
    [[nodiscard]] static QString GenerateToken();
    // HMAC-SHA256(token, nonce) as lowercase hex - what a client must send.
    [[nodiscard]] static QString ComputeProof(const QString& token, const QString& nonce);

private:
    struct Client
    {
        QByteArray buffer;
        QString nonce;
        bool authenticated = false;
        Account account; // who this session is (valid once authenticated)
        QString language; // language the client asked for; its error messages are translated into it
        bool subscribed = false;
        QString subscribedServerId; // empty = every server
        QTimer* authTimeout = nullptr;
    };

    struct Result
    {
        bool ok = true;
        QJsonObject data;
        QString error;
        QString code;          // "forbidden" for permission errors
        QString requiredRole;  // set with code == "forbidden"
        bool deferred = false; // a worker thread will reply later
    };

    void OnNewConnection();
    void OnReadyRead(QTcpSocket* socket);
    void DropClient(QTcpSocket* socket);
    void HandleLine(QTcpSocket* socket, const QByteArray& line);
    void HandleAuth(QTcpSocket* socket, const QJsonObject& request);
    Result Dispatch(QTcpSocket* socket, const QString& cmd, const QJsonObject& request, qint64 id);
    void BroadcastEvent(const core::AppEvent& event);

    void SendObject(QTcpSocket* socket, const QJsonObject& object);
    void Reply(QTcpSocket* socket, qint64 id, const Result& result);

    // Command implementations
    [[nodiscard]] std::shared_ptr<server::MinecraftServer> ResolveServer(const QString& key, QString* error) const;
    [[nodiscard]] static QJsonObject DescribeServer(const std::shared_ptr<server::MinecraftServer>& srv, bool detailed);
    Result CmdPlayers(const std::shared_ptr<server::MinecraftServer>& srv) const;
    Result CmdPlayerAction(const std::shared_ptr<server::MinecraftServer>& srv, const QJsonObject& request) const;
    Result CmdProps(const std::shared_ptr<server::MinecraftServer>& srv, const QJsonObject& request, bool set) const;
    Result CmdBackup(QTcpSocket* socket, const std::shared_ptr<server::MinecraftServer>& srv, qint64 id);
    Result CmdBackups(const std::shared_ptr<server::MinecraftServer>& srv) const;
    Result CmdWhoAmI(const Account& account) const;
    Result CmdApp(const Account& account) const;
    Result CmdAudit(const QJsonObject& request) const;
    void Audit(const Client& client, const QString& cmd, const QJsonObject& request, const QString& outcome);

    std::shared_ptr<server::ServerManager> serverManager_;
    std::shared_ptr<core::EventDispatcher> events_;
    QTcpServer* listener_ = nullptr;
    QList<Account> accounts_;
    QElapsedTimer uptime_;
    QHash<QTcpSocket*, Client> clients_;

    int consecutiveAuthFailures_ = 0;
    qint64 lockedUntilMs_ = 0; // epoch ms; new connections are refused while locked
};

} // namespace remote
