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
#include "server/ServerManager.hpp"

#include <QHash>
#include <QJsonObject>
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
    static constexpr int kProtocolVersion = 1;

    RemoteControlServer(
        std::shared_ptr<server::ServerManager> serverManager,
        std::shared_ptr<core::EventDispatcher> events,
        QObject* parent = nullptr);
    ~RemoteControlServer() override;

    // Starts listening on 127.0.0.1:port. Returns false and fills `error` on failure.
    bool Start(quint16 port, const QString& token, QString* error);
    void Stop();
    [[nodiscard]] bool IsListening() const;
    [[nodiscard]] quint16 Port() const;

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
        bool subscribed = false;
        QString subscribedServerId; // empty = every server
        QTimer* authTimeout = nullptr;
    };

    struct Result
    {
        bool ok = true;
        QJsonObject data;
        QString error;
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

    std::shared_ptr<server::ServerManager> serverManager_;
    std::shared_ptr<core::EventDispatcher> events_;
    QTcpServer* listener_ = nullptr;
    QString token_;
    QHash<QTcpSocket*, Client> clients_;

    int consecutiveAuthFailures_ = 0;
    qint64 lockedUntilMs_ = 0; // epoch ms; new connections are refused while locked
};

} // namespace remote
