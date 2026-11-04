#include "remote/RemoteControlServer.hpp"
#include "ui/I18n.hpp"

#include "backup/BackupManager.hpp"
#include "config/ServerProperties.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <thread>

namespace remote
{
namespace
{
// Language of the session currently being served. The endpoint runs on the UI thread, so one
// variable is enough; it is set from the client's "lang" before every request is handled.
QString g_sessionLanguage;
} // namespace

// Messages in this file go to the remote client, so translate them into ITS language.
#undef TR
#define TR(text) ::i18n::TrIn(g_sessionLanguage, QStringLiteral(text))

namespace
{

constexpr int kMaxClients = 8;
constexpr int kMaxLineBytes = 64 * 1024;
constexpr int kAuthTimeoutMs = 10 * 1000;
constexpr int kMaxFailuresBeforeLock = 5;
constexpr qint64 kLockoutMs = 30 * 1000;
constexpr int kMaxConsoleLines = 2000;

QString W2Q(const std::wstring& s) { return QString::fromStdWString(s); }
std::wstring Q2W(const QString& s) { return s.toStdWString(); }

QString StateName(core::ServerState state)
{
    return QString::fromWCharArray(core::ToString(state));
}

// Collapses a user-supplied value to one printable line. Returns false if it is
// empty after trimming or too long - this is what stops a value from smuggling
// a second console command in through an embedded newline.
bool CleanLine(QString& text, int maxLen)
{
    text = text.trimmed();
    if (text.isEmpty() || text.size() > maxLen)
    {
        return false;
    }
    for (const QChar ch : text)
    {
        if (ch.unicode() < 32 || ch.unicode() == 127)
        {
            return false;
        }
    }
    return true;
}

bool ValidPlayerName(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.]{1,32}$"));
    return pattern.match(name).hasMatch();
}

bool ValidIp(const QString& ip)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F:.]{3,45}$"));
    return pattern.match(ip).hasMatch();
}

QJsonArray ReadJsonArray(const core::ServerConfig& cfg, const wchar_t* fileName)
{
    QFile file(W2Q((cfg.directory / fileName).wstring()));
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isArray() ? doc.array() : QJsonArray();
}

bool ConstantTimeEquals(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
    {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

} // namespace

RemoteControlServer::RemoteControlServer(
    std::shared_ptr<server::ServerManager> serverManager, std::shared_ptr<core::EventDispatcher> events, QObject* parent)
    : QObject(parent)
    , serverManager_(std::move(serverManager))
    , events_(std::move(events))
{
    uptime_.start();

    // The dispatcher calls handlers on arbitrary threads (and has no
    // Unsubscribe), so hop to the UI thread through a guarded pointer.
    QPointer<RemoteControlServer> self(this);
    events_->Subscribe([self](const core::AppEvent& event) {
        if (RemoteControlServer* s = self.data())
        {
            QMetaObject::invokeMethod(s, [self, event]() {
                if (RemoteControlServer* alive = self.data())
                {
                    alive->BroadcastEvent(event);
                }
            }, Qt::QueuedConnection);
        }
    });
}

RemoteControlServer::~RemoteControlServer()
{
    Stop();
}

QString RemoteControlServer::GenerateToken()
{
    QByteArray bytes(32, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()), bytes.size() / 4);
    return QString::fromLatin1(bytes.toHex());
}

QString RemoteControlServer::ComputeProof(const QString& token, const QString& nonce)
{
    const QByteArray mac = QMessageAuthenticationCode::hash(nonce.toUtf8(), token.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(mac.toHex());
}

bool RemoteControlServer::Start(quint16 port, const QList<Account>& accounts, QString* error)
{
    Stop();
    if (accounts.isEmpty())
    {
        if (error != nullptr) *error = TR("Chưa có tài khoản truy cập nào.");
        return false;
    }
    QSet<QString> names;
    for (const Account& a : accounts)
    {
        if (!ValidAccountName(a.name))
        {
            if (error != nullptr) *error = TR("Tên tài khoản không hợp lệ: %1").arg(a.name);
            return false;
        }
        if (a.secret.size() < 16)
        {
            if (error != nullptr) *error = TR("Token của \"%1\" quá ngắn (cần ít nhất 16 ký tự).").arg(a.name);
            return false;
        }
        if (names.contains(a.name.toLower()))
        {
            if (error != nullptr) *error = TR("Trùng tên tài khoản: %1").arg(a.name);
            return false;
        }
        names.insert(a.name.toLower());
    }
    accounts_ = accounts;
    listener_ = new QTcpServer(this);
    // Loopback ONLY: never expose this port to the network. Remote access goes through SSH.
    if (!listener_->listen(QHostAddress::LocalHost, port))
    {
        if (error != nullptr) *error = listener_->errorString();
        listener_->deleteLater();
        listener_ = nullptr;
        return false;
    }
    connect(listener_, &QTcpServer::newConnection, this, &RemoteControlServer::OnNewConnection);
    return true;
}

void RemoteControlServer::Stop()
{
    const auto sockets = clients_.keys();
    for (QTcpSocket* s : sockets)
    {
        DropClient(s);
    }
    if (listener_ != nullptr)
    {
        listener_->close();
        listener_->deleteLater();
        listener_ = nullptr;
    }
}

bool RemoteControlServer::IsListening() const
{
    return listener_ != nullptr && listener_->isListening();
}

quint16 RemoteControlServer::Port() const
{
    return listener_ != nullptr ? listener_->serverPort() : 0;
}

void RemoteControlServer::OnNewConnection()
{
    while (listener_ != nullptr && listener_->hasPendingConnections())
    {
        QTcpSocket* socket = listener_->nextPendingConnection();
        if (QDateTime::currentMSecsSinceEpoch() < lockedUntilMs_ || clients_.size() >= kMaxClients)
        {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        Client client;
        QByteArray nonceBytes(16, '\0');
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(nonceBytes.data()), nonceBytes.size() / 4);
        client.nonce = QString::fromLatin1(nonceBytes.toHex());
        client.authTimeout = new QTimer(socket);
        client.authTimeout->setSingleShot(true);
        client.authTimeout->setInterval(kAuthTimeoutMs);
        connect(client.authTimeout, &QTimer::timeout, this, [this, socket]() { DropClient(socket); });
        client.authTimeout->start();
        clients_.insert(socket, client);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { OnReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { DropClient(socket); });

        QJsonObject hello;
        hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
        hello.insert(QStringLiteral("app"), QStringLiteral("Redstone"));
        hello.insert(QStringLiteral("protocol"), kProtocolVersion);
        hello.insert(QStringLiteral("nonce"), clients_.value(socket).nonce);
        SendObject(socket, hello);
    }
}

void RemoteControlServer::DropClient(QTcpSocket* socket)
{
    if (!clients_.contains(socket))
    {
        return;
    }
    clients_.remove(socket);
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void RemoteControlServer::OnReadyRead(QTcpSocket* socket)
{
    const auto it = clients_.find(socket);
    if (it == clients_.end())
    {
        return;
    }
    it->buffer.append(socket->readAll());

    while (true)
    {
        const auto again = clients_.find(socket); // HandleLine may have dropped the client
        if (again == clients_.end())
        {
            return;
        }
        const int newline = again->buffer.indexOf('\n');
        if (newline < 0)
        {
            if (again->buffer.size() > kMaxLineBytes)
            {
                DropClient(socket);
            }
            return;
        }
        const QByteArray line = again->buffer.left(newline).trimmed();
        again->buffer.remove(0, newline + 1);
        if (!line.isEmpty())
        {
            HandleLine(socket, line);
        }
    }
}

void RemoteControlServer::SendObject(QTcpSocket* socket, const QJsonObject& object)
{
    if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState)
    {
        return;
    }
    socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
}

void RemoteControlServer::Reply(QTcpSocket* socket, qint64 id, const Result& result)
{
    QJsonObject response;
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("ok"), result.ok);
    if (result.ok)
    {
        response.insert(QStringLiteral("data"), result.data);
    }
    else
    {
        response.insert(QStringLiteral("error"), result.error);
        if (!result.code.isEmpty()) response.insert(QStringLiteral("code"), result.code);
        if (!result.requiredRole.isEmpty()) response.insert(QStringLiteral("required"), result.requiredRole);
    }
    SendObject(socket, response);
}

void RemoteControlServer::HandleLine(QTcpSocket* socket, const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        Result invalid;
        invalid.ok = false;
        invalid.error = TR("Yêu cầu không phải JSON object hợp lệ.");
        Reply(socket, 0, invalid);
        return;
    }
    const QJsonObject request = doc.object();
    const QString cmd = request.value(QStringLiteral("cmd")).toString();

    const auto it = clients_.find(socket);
    if (it == clients_.end())
    {
        return;
    }
    if (!it->authenticated)
    {
        if (cmd == QLatin1String("auth"))
        {
            HandleAuth(socket, request);
        }
        else
        {
            DropClient(socket); // anything before auth is a protocol violation
        }
        return;
    }

    const qint64 id = static_cast<qint64>(request.value(QStringLiteral("id")).toDouble());
    const Client& session = *it;
    g_sessionLanguage = session.language;

    // Role check BEFORE anything is looked up or executed. Unknown commands fall through to Dispatch.
    if (const auto required = RequiredRole(cmd); required.has_value() && static_cast<int>(session.account.role) < static_cast<int>(*required))
    {
        Result denied;
        denied.ok = false;
        denied.code = QStringLiteral("forbidden");
        denied.requiredRole = RoleName(*required);
        denied.error = TR("Không đủ quyền: lệnh \"%1\" cần vai trò %2 (bạn là %3).").arg(cmd, RoleName(*required), RoleName(session.account.role));
        Audit(session, cmd, request, QStringLiteral("DENIED (needs %1)").arg(RoleName(*required)));
        Reply(socket, id, denied);
        return;
    }

    const Result result = Dispatch(socket, cmd, request, id);
    if (cmd != QLatin1String("ping") && cmd != QLatin1String("unsubscribe"))
    {
        const auto again = clients_.constFind(socket);
        if (again != clients_.constEnd())
        {
            Audit(*again, cmd, request, result.deferred ? QStringLiteral("started") : (result.ok ? QStringLiteral("ok") : (result.code == QLatin1String("forbidden") ? QStringLiteral("DENIED (") + result.error + QLatin1Char(')') : QStringLiteral("error: ") + result.error)));
        }
    }
    if (!result.deferred)
    {
        Reply(socket, id, result);
    }
}

void RemoteControlServer::HandleAuth(QTcpSocket* socket, const QJsonObject& request)
{
    Client& client = clients_[socket];
    client.language = request.value(QStringLiteral("lang")).toString();
    g_sessionLanguage = client.language;
    // The client names the account it wants to use; v1 clients (no "user") get "admin".
    const QString user = request.value(QStringLiteral("user")).toString(QStringLiteral("admin")).trimmed();
    const Account* found = nullptr;
    for (const Account& a : accounts_)
    {
        if (a.name.compare(user, Qt::CaseInsensitive) == 0)
        {
            found = &a;
            break;
        }
    }
    // Always compute a proof (against a dummy secret for unknown users) so response
    // timing does not reveal which account names exist.
    const QString secret = found != nullptr ? found->secret : QStringLiteral("0000000000000000-no-such-account");
    const QByteArray expected = ComputeProof(secret, client.nonce).toLatin1();
    const QByteArray given = request.value(QStringLiteral("proof")).toString().toLower().toLatin1();

    QJsonObject response;
    response.insert(QStringLiteral("type"), QStringLiteral("auth"));
    if (found != nullptr && ConstantTimeEquals(expected, given))
    {
        consecutiveAuthFailures_ = 0;
        client.authenticated = true;
        client.account = *found;
        if (client.authTimeout != nullptr)
        {
            client.authTimeout->stop();
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("user"), found->name);
        response.insert(QStringLiteral("role"), RoleName(found->role));
        SendObject(socket, response);
        Audit(client, QStringLiteral("auth"), QJsonObject(), QStringLiteral("ok"));
        return;
    }

    if (++consecutiveAuthFailures_ >= kMaxFailuresBeforeLock)
    {
        lockedUntilMs_ = QDateTime::currentMSecsSinceEpoch() + kLockoutMs;
        consecutiveAuthFailures_ = 0;
    }
    Client attempt = client;
    attempt.account.name = user.left(32);
    attempt.account.role = Role::Viewer;
    Audit(attempt, QStringLiteral("auth"), QJsonObject(), QStringLiteral("DENIED (wrong token/user)"));
    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), TR("Sai tên tài khoản hoặc token."));
    SendObject(socket, response);
    QTimer::singleShot(200, this, [this, socket]() { DropClient(socket); }); // let the reply flush
}

// ------------------------------------------------------------- commands

std::shared_ptr<server::MinecraftServer> RemoteControlServer::ResolveServer(const QString& key, QString* error) const
{
    const QString wanted = key.trimmed();
    if (wanted.isEmpty())
    {
        *error = TR("Thiếu tham số \"server\" (id hoặc tên server).");
        return nullptr;
    }
    std::shared_ptr<server::MinecraftServer> byName;
    int nameMatches = 0;
    for (const auto& srv : serverManager_->GetAll())
    {
        const core::ServerConfig cfg = srv->GetConfig();
        if (W2Q(cfg.id).compare(wanted, Qt::CaseSensitive) == 0)
        {
            return srv;
        }
        if (W2Q(cfg.name).compare(wanted, Qt::CaseInsensitive) == 0)
        {
            byName = srv;
            ++nameMatches;
        }
    }
    if (nameMatches == 1)
    {
        return byName;
    }
    *error = nameMatches > 1 ? TR("Có nhiều server trùng tên \"%1\" - hãy dùng id.").arg(wanted)
                             : TR("Không tìm thấy server \"%1\".").arg(wanted);
    return nullptr;
}

QJsonObject RemoteControlServer::DescribeServer(const std::shared_ptr<server::MinecraftServer>& srv, bool detailed)
{
    const core::ServerConfig cfg = srv->GetConfig();
    QJsonObject o;
    o.insert(QStringLiteral("id"), W2Q(cfg.id));
    o.insert(QStringLiteral("name"), W2Q(cfg.name));
    o.insert(QStringLiteral("type"), W2Q(cfg.serverType));
    o.insert(QStringLiteral("state"), StateName(srv->GetState()));
    o.insert(QStringLiteral("port"), static_cast<int>(cfg.port));
    const auto players = srv->GetOnlinePlayers();
    o.insert(QStringLiteral("playersOnline"), static_cast<int>(players.size()));
    if (detailed)
    {
        o.insert(QStringLiteral("minecraftVersion"), W2Q(cfg.minecraftVersion));
        o.insert(QStringLiteral("minMemoryMB"), static_cast<double>(cfg.minMemoryMB));
        o.insert(QStringLiteral("maxMemoryMB"), static_cast<double>(cfg.maxMemoryMB));
        o.insert(QStringLiteral("autoRestart"), cfg.autoRestart);
        o.insert(QStringLiteral("autoBackup"), cfg.autoBackup);
        o.insert(QStringLiteral("autoStartOnAppLaunch"), cfg.autoStartOnAppLaunch);
        o.insert(QStringLiteral("autoBackupIntervalMinutes"), cfg.autoBackupIntervalMinutes);
        o.insert(QStringLiteral("backupRetention"), cfg.backupRetention);
        o.insert(QStringLiteral("stopTimeoutSeconds"), cfg.stopTimeoutSeconds);
        QJsonArray names;
        for (const auto& p : players)
        {
            names.append(W2Q(p));
        }
        o.insert(QStringLiteral("players"), names);
    }
    return o;
}

RemoteControlServer::Result RemoteControlServer::Dispatch(
    QTcpSocket* socket, const QString& cmd, const QJsonObject& request, qint64 id)
{
    Result r;
    const Account account = clients_.value(socket).account;
    auto fail = [&r](const QString& message) {
        r.ok = false;
        r.error = message;
        return r;
    };
    auto outOfScope = [&r, &account](const QString& serverName) {
        r.ok = false;
        r.code = QStringLiteral("forbidden");
        r.error = TR("Tài khoản \"%1\" không được phép truy cập server \"%2\".").arg(account.name, serverName);
        return r;
    };

    if (cmd == QLatin1String("ping"))
    {
        r.data.insert(QStringLiteral("pong"), true);
        r.data.insert(QStringLiteral("protocol"), kProtocolVersion);
        return r;
    }
    if (cmd == QLatin1String("whoami")) return CmdWhoAmI(account);
    if (cmd == QLatin1String("app")) return CmdApp(account);
    if (cmd == QLatin1String("audit")) return CmdAudit(request);
    if (cmd == QLatin1String("list"))
    {
        QJsonArray servers;
        for (const auto& srv : serverManager_->GetAll())
        {
            if (!CanAccessServer(account, W2Q(srv->GetConfig().id))) continue; // hidden, not just forbidden
            servers.append(DescribeServer(srv, false));
        }
        r.data.insert(QStringLiteral("servers"), servers);
        return r;
    }
    if (cmd == QLatin1String("subscribe") || cmd == QLatin1String("unsubscribe"))
    {
        Client& client = clients_[socket];
        client.subscribed = (cmd == QLatin1String("subscribe"));
        client.subscribedServerId.clear();
        const QString key = request.value(QStringLiteral("server")).toString();
        if (client.subscribed && !key.isEmpty())
        {
            QString error;
            const auto srv = ResolveServer(key, &error);
            if (!srv)
            {
                client.subscribed = false;
                return fail(error);
            }
            if (!CanAccessServer(account, W2Q(srv->GetConfig().id)))
            {
                client.subscribed = false;
                return outOfScope(W2Q(srv->GetConfig().name));
            }
            client.subscribedServerId = W2Q(srv->GetConfig().id);
        }
        r.data.insert(QStringLiteral("subscribed"), client.subscribed);
        return r;
    }

    // Everything below targets one server.
    QString error;
    const auto srv = ResolveServer(request.value(QStringLiteral("server")).toString(), &error);
    if (!srv)
    {
        return fail(error);
    }
    if (!CanAccessServer(account, W2Q(srv->GetConfig().id)))
    {
        return outOfScope(W2Q(srv->GetConfig().name));
    }
    const core::ServerState state = srv->GetState();

    if (cmd == QLatin1String("status"))
    {
        r.data = DescribeServer(srv, true);
        return r;
    }
    if (cmd == QLatin1String("start"))
    {
        if (state != core::ServerState::Stopped && state != core::ServerState::Crashed)
        {
            return fail(TR("Server đang ở trạng thái %1, không thể Start.").arg(StateName(state)));
        }
        if (!srv->Start())
        {
            return fail(TR("Không khởi động được server (kiểm tra Java, file jar, EULA...)."));
        }
        r.data.insert(QStringLiteral("state"), StateName(srv->GetState()));
        return r;
    }
    if (cmd == QLatin1String("stop"))
    {
        if (state != core::ServerState::Running && state != core::ServerState::Starting)
        {
            return fail(TR("Server đang ở trạng thái %1, không thể Stop.").arg(StateName(state)));
        }
        std::thread([srv]() { srv->Stop(); }).detach(); // blocks up to stopTimeoutSeconds
        r.data.insert(QStringLiteral("accepted"), true);
        return r;
    }
    if (cmd == QLatin1String("restart"))
    {
        if (state != core::ServerState::Running)
        {
            return fail(TR("Chỉ Restart được khi server đang Running (hiện: %1).").arg(StateName(state)));
        }
        std::thread([srv]() { srv->Restart(); }).detach();
        r.data.insert(QStringLiteral("accepted"), true);
        return r;
    }
    if (cmd == QLatin1String("console"))
    {
        const int wanted = std::clamp(request.value(QStringLiteral("lines")).toInt(100), 1, kMaxConsoleLines);
        const auto lines = srv->GetConsoleLines();
        QJsonArray out;
        for (size_t i = lines.size() > static_cast<size_t>(wanted) ? lines.size() - static_cast<size_t>(wanted) : 0; i < lines.size(); ++i)
        {
            out.append(W2Q(lines[i]));
        }
        r.data.insert(QStringLiteral("lines"), out);
        return r;
    }
    if (cmd == QLatin1String("send"))
    {
        QString command = request.value(QStringLiteral("command")).toString();
        if (!CleanLine(command, 512))
        {
            return fail(TR("Lệnh trống, quá dài hoặc chứa ký tự điều khiển."));
        }
        if (!srv->SendCommand(Q2W(command)))
        {
            return fail(TR("Không gửi được lệnh - server chưa Running."));
        }
        r.data.insert(QStringLiteral("sent"), command);
        return r;
    }
    if (cmd == QLatin1String("players")) return CmdPlayers(srv);
    if (cmd == QLatin1String("player")) return CmdPlayerAction(srv, request);
    if (cmd == QLatin1String("props")) return CmdProps(srv, request, false);
    if (cmd == QLatin1String("prop-set")) return CmdProps(srv, request, true);
    if (cmd == QLatin1String("backups")) return CmdBackups(srv);
    if (cmd == QLatin1String("backup")) return CmdBackup(socket, srv, id);

    return fail(TR("Lệnh không được hỗ trợ: \"%1\".").arg(cmd));
}

RemoteControlServer::Result RemoteControlServer::CmdPlayers(const std::shared_ptr<server::MinecraftServer>& srv) const
{
    const core::ServerConfig cfg = srv->GetConfig();
    Result r;

    QJsonArray online;
    for (const auto& p : srv->GetOnlinePlayers())
    {
        online.append(W2Q(p));
    }
    r.data.insert(QStringLiteral("online"), online);

    auto namesFrom = [&cfg](const wchar_t* file, const QString& key, bool withExtra) {
        QJsonArray out;
        for (const auto& v : ReadJsonArray(cfg, file))
        {
            const QJsonObject o = v.toObject();
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), o.value(key));
            if (withExtra)
            {
                entry.insert(QStringLiteral("reason"), o.value(QStringLiteral("reason")));
                entry.insert(QStringLiteral("created"), o.value(QStringLiteral("created")));
                entry.insert(QStringLiteral("level"), o.value(QStringLiteral("level")));
            }
            out.append(entry);
        }
        return out;
    };
    r.data.insert(QStringLiteral("whitelist"), namesFrom(L"whitelist.json", QStringLiteral("name"), false));
    r.data.insert(QStringLiteral("ops"), namesFrom(L"ops.json", QStringLiteral("name"), true));
    r.data.insert(QStringLiteral("banned"), namesFrom(L"banned-players.json", QStringLiteral("name"), true));
    r.data.insert(QStringLiteral("bannedIps"), namesFrom(L"banned-ips.json", QStringLiteral("ip"), true));
    return r;
}

RemoteControlServer::Result RemoteControlServer::CmdPlayerAction(
    const std::shared_ptr<server::MinecraftServer>& srv, const QJsonObject& request) const
{
    Result r;
    auto fail = [&r](const QString& message) {
        r.ok = false;
        r.error = message;
        return r;
    };

    const QString action = request.value(QStringLiteral("action")).toString();
    QString name = request.value(QStringLiteral("name")).toString().trimmed();
    QString reason = request.value(QStringLiteral("reason")).toString();
    QString text = request.value(QStringLiteral("text")).toString();

    QString command;
    const bool needsName = action != QLatin1String("say") && action != QLatin1String("whitelist-on") &&
        action != QLatin1String("whitelist-off") && action != QLatin1String("whitelist-reload");
    if (needsName && !ValidPlayerName(name) && !(action == QLatin1String("ban-ip") || action == QLatin1String("pardon-ip")))
    {
        return fail(TR("Tên người chơi không hợp lệ (chỉ gồm chữ, số, _ hoặc .)."));
    }
    if (!reason.isEmpty() && !CleanLine(reason, 200))
    {
        return fail(TR("Lý do chứa ký tự không hợp lệ hoặc quá dài."));
    }

    if (action == QLatin1String("op")) command = QStringLiteral("op %1").arg(name);
    else if (action == QLatin1String("deop")) command = QStringLiteral("deop %1").arg(name);
    else if (action == QLatin1String("kick")) command = QStringLiteral("kick %1 %2").arg(name, reason);
    else if (action == QLatin1String("ban")) command = QStringLiteral("ban %1 %2").arg(name, reason);
    else if (action == QLatin1String("pardon")) command = QStringLiteral("pardon %1").arg(name);
    else if (action == QLatin1String("ban-ip") || action == QLatin1String("pardon-ip"))
    {
        const QString ip = request.value(QStringLiteral("name")).toString().trimmed();
        if (!ValidIp(ip))
        {
            return fail(TR("Địa chỉ IP không hợp lệ."));
        }
        command = action == QLatin1String("ban-ip") ? QStringLiteral("ban-ip %1 %2").arg(ip, reason) : QStringLiteral("pardon-ip %1").arg(ip);
    }
    else if (action == QLatin1String("whitelist-add")) command = QStringLiteral("whitelist add %1").arg(name);
    else if (action == QLatin1String("whitelist-remove")) command = QStringLiteral("whitelist remove %1").arg(name);
    else if (action == QLatin1String("whitelist-on")) command = QStringLiteral("whitelist on");
    else if (action == QLatin1String("whitelist-off")) command = QStringLiteral("whitelist off");
    else if (action == QLatin1String("whitelist-reload")) command = QStringLiteral("whitelist reload");
    else if (action == QLatin1String("gamemode"))
    {
        const QString mode = request.value(QStringLiteral("mode")).toString();
        static const QStringList modes = {QStringLiteral("survival"), QStringLiteral("creative"), QStringLiteral("adventure"), QStringLiteral("spectator")};
        if (!modes.contains(mode))
        {
            return fail(TR("Gamemode phải là survival, creative, adventure hoặc spectator."));
        }
        command = QStringLiteral("gamemode %1 %2").arg(mode, name);
    }
    else if (action == QLatin1String("msg") || action == QLatin1String("say"))
    {
        if (!CleanLine(text, 256))
        {
            return fail(TR("Nội dung tin nhắn trống, quá dài hoặc chứa ký tự điều khiển."));
        }
        command = action == QLatin1String("msg") ? QStringLiteral("msg %1 %2").arg(name, text) : QStringLiteral("say %1").arg(text);
    }
    else
    {
        return fail(TR("Hành động người chơi không hợp lệ: \"%1\".").arg(action));
    }

    command = command.trimmed();
    if (!srv->SendCommand(Q2W(command)))
    {
        return fail(TR("Không gửi được - server chưa Running."));
    }
    r.data.insert(QStringLiteral("sent"), command);
    return r;
}

RemoteControlServer::Result RemoteControlServer::CmdProps(
    const std::shared_ptr<server::MinecraftServer>& srv, const QJsonObject& request, bool set) const
{
    Result r;
    const core::ServerConfig cfg = srv->GetConfig();

    if (!set)
    {
        QJsonObject props;
        for (const auto& [key, value] : config::ReadServerProperties(cfg))
        {
            const QString k = W2Q(key);
            props.insert(k, k == QLatin1String("rcon.password") ? QStringLiteral("***") : W2Q(value));
        }
        r.data.insert(QStringLiteral("properties"), props);
        return r;
    }

    static const QRegularExpression keyPattern(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
    const QString key = request.value(QStringLiteral("key")).toString();
    QString value = request.value(QStringLiteral("value")).toString();
    if (!keyPattern.match(key).hasMatch())
    {
        r.ok = false;
        r.error = TR("Tên khoá không hợp lệ.");
        return r;
    }
    if (value.size() > 512 || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
    {
        r.ok = false;
        r.error = TR("Giá trị quá dài hoặc có xuống dòng.");
        return r;
    }
    if (!config::WriteServerProperties(cfg, {{Q2W(key), Q2W(value)}}))
    {
        r.ok = false;
        r.error = TR("Không ghi được server.properties.");
        return r;
    }
    r.data.insert(QStringLiteral("key"), key);
    r.data.insert(QStringLiteral("restartRequired"), true);
    return r;
}

RemoteControlServer::Result RemoteControlServer::CmdBackups(const std::shared_ptr<server::MinecraftServer>& srv) const
{
    Result r;
    QJsonArray list;
    for (const auto& b : backup::ListBackups(srv->GetConfig()))
    {
        QJsonObject o;
        o.insert(QStringLiteral("name"), W2Q(b.name));
        o.insert(QStringLiteral("sizeBytes"), static_cast<double>(b.sizeBytes));
        list.append(o);
    }
    r.data.insert(QStringLiteral("backups"), list);
    return r;
}

RemoteControlServer::Result RemoteControlServer::CmdBackup(
    QTcpSocket* socket, const std::shared_ptr<server::MinecraftServer>& srv, qint64 id)
{
    // Compressing a world takes a while: run it on a worker thread and reply
    // from the UI thread when it finishes (the client just waits for the reply).
    QPointer<QTcpSocket> target(socket);
    QPointer<RemoteControlServer> self(this);
    const QString sessionLanguage = g_sessionLanguage; // copy: the worker must not touch the shared variable
    std::thread([self, target, srv, id, sessionLanguage]() {
        srv->PrepareForBackup();
        const auto created = backup::CreateBackup(srv->GetConfig());
        srv->FinishBackup();

        Result result;
        if (created.has_value())
        {
            result.data.insert(QStringLiteral("name"), W2Q(*created));
        }
        else
        {
            result.ok = false;
            result.error = ::i18n::TrIn(sessionLanguage, QStringLiteral("Backup thất bại (server chưa có world?)."));
        }
        if (RemoteControlServer* s = self.data())
        {
            QMetaObject::invokeMethod(s, [self, target, id, result]() {
                if (self.data() != nullptr && !target.isNull())
                {
                    self.data()->Reply(target.data(), id, result);
                }
            }, Qt::QueuedConnection);
        }
    }).detach();

    Result deferred;
    deferred.deferred = true;
    return deferred;
}

RemoteControlServer::Result RemoteControlServer::CmdWhoAmI(const Account& account) const
{
    Result r;
    r.data.insert(QStringLiteral("user"), account.name);
    r.data.insert(QStringLiteral("role"), RoleName(account.role));
    r.data.insert(QStringLiteral("allServers"), account.serverIds.isEmpty());
    QJsonArray scope;
    for (const QString& id : account.serverIds)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), id);
        QString name = id;
        for (const auto& srv : serverManager_->GetAll())
        {
            if (W2Q(srv->GetConfig().id) == id) name = W2Q(srv->GetConfig().name);
        }
        o.insert(QStringLiteral("name"), name);
        scope.append(o);
    }
    r.data.insert(QStringLiteral("servers"), scope);
    QJsonArray allowed;
    for (const QString& c : CommandsForRole(account.role)) allowed.append(c);
    r.data.insert(QStringLiteral("commands"), allowed);
    r.data.insert(QStringLiteral("matrix"), PermissionMatrix());
    r.data.insert(QStringLiteral("protocol"), kProtocolVersion);
    return r;
}

RemoteControlServer::Result RemoteControlServer::CmdApp(const Account& account) const
{
    Result r;
    r.data.insert(QStringLiteral("name"), QStringLiteral("Redstone"));
    r.data.insert(QStringLiteral("version"), QCoreApplication::applicationVersion());
    r.data.insert(QStringLiteral("protocol"), kProtocolVersion);
    r.data.insert(QStringLiteral("language"), i18n::CurrentLanguage());
    r.data.insert(QStringLiteral("uptimeSeconds"), static_cast<double>(uptime_.elapsed() / 1000));
    int total = 0, running = 0, players = 0;
    for (const auto& srv : serverManager_->GetAll())
    {
        if (!CanAccessServer(account, W2Q(srv->GetConfig().id))) continue;
        ++total;
        if (srv->GetState() == core::ServerState::Running) ++running;
        players += static_cast<int>(srv->GetOnlinePlayers().size());
    }
    r.data.insert(QStringLiteral("servers"), total);
    r.data.insert(QStringLiteral("serversRunning"), running);
    r.data.insert(QStringLiteral("playersOnline"), players);
    r.data.insert(QStringLiteral("sessions"), static_cast<int>(clients_.size()));
    return r;
}

namespace
{
QString AuditPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/remote_audit.log"));
}
} // namespace

QStringList RemoteControlServer::ReadAuditTail(int lines)
{
    QFile file(AuditPath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    const qint64 size = file.size();
    file.seek(std::max<qint64>(0, size - 256 * 1024));
    QStringList all = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (size > 256 * 1024 && !all.isEmpty()) all.removeFirst(); // first line is probably cut
    return all.mid(std::max(0, static_cast<int>(all.size()) - lines));
}

RemoteControlServer::Result RemoteControlServer::CmdAudit(const QJsonObject& request) const
{
    Result r;
    const int n = std::clamp(request.value(QStringLiteral("lines")).toInt(50), 1, 500);
    QJsonArray out;
    for (const QString& line : ReadAuditTail(n)) out.append(line);
    r.data.insert(QStringLiteral("lines"), out);
    return r;
}

void RemoteControlServer::Audit(const Client& client, const QString& cmd, const QJsonObject& request, const QString& outcome)
{
    // What was asked, without secrets: raw console commands and messages are logged (admins/operators
    // are accountable for them); property values for anything password-like are masked.
    QString what = cmd;
    const QString server = request.value(QStringLiteral("server")).toString();
    if (!server.isEmpty()) what += QLatin1Char(' ') + server;
    if (cmd == QLatin1String("player"))
    {
        what += QStringLiteral(" %1 %2").arg(request.value(QStringLiteral("action")).toString(), request.value(QStringLiteral("name")).toString());
    }
    else if (cmd == QLatin1String("send"))
    {
        what += QStringLiteral(" \"%1\"").arg(request.value(QStringLiteral("command")).toString().left(120));
    }
    else if (cmd == QLatin1String("prop-set"))
    {
        const QString key = request.value(QStringLiteral("key")).toString();
        const bool secret = key.contains(QLatin1String("password"), Qt::CaseInsensitive);
        what += QStringLiteral(" %1=%2").arg(key, secret ? QStringLiteral("***") : request.value(QStringLiteral("value")).toString().left(80));
    }
    const QString line = QStringLiteral("%1 | %2 (%3) | %4 | %5\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), client.account.name, client.authenticated ? RoleName(client.account.role) : QStringLiteral("-"), what, outcome);

    const QString path = AuditPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo(path).size() > 1024 * 1024) // keep it small: one rotated generation
    {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
    QFile file(path);
    if (file.open(QIODevice::Append))
    {
        file.write(line.toUtf8());
    }
}

void RemoteControlServer::BroadcastEvent(const core::AppEvent& event)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("event"));
    o.insert(QStringLiteral("server"), W2Q(event.serverId));
    switch (event.type)
    {
        case core::EventType::ConsoleLine:
            o.insert(QStringLiteral("event"), QStringLiteral("console"));
            o.insert(QStringLiteral("line"), W2Q(event.message));
            break;
        case core::EventType::ServerStateChanged:
            o.insert(QStringLiteral("event"), QStringLiteral("state"));
            o.insert(QStringLiteral("state"), StateName(event.state));
            break;
        default:
            return; // stats/EULA events are not part of the remote protocol
    }

    for (auto it = clients_.constBegin(); it != clients_.constEnd(); ++it)
    {
        const Client& client = it.value();
        if (!client.authenticated || !client.subscribed || !CanAccessServer(client.account, W2Q(event.serverId)))
        {
            continue;
        }
        if (!client.subscribedServerId.isEmpty() && client.subscribedServerId != W2Q(event.serverId))
        {
            continue;
        }
        SendObject(it.key(), o);
    }
}

} // namespace remote
