#include "ui/TerminalDialog.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/I18n.hpp"

#include "backup/BackupManager.hpp"
#include "config/ServerProperties.hpp"
#include "javamanager/JavaManager.hpp"
#include "remote/Access.hpp"
#include "remote/RemoteControlServer.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <thread>

namespace ui
{

namespace
{

QString W2Q(const std::wstring& s) { return QString::fromStdWString(s); }
std::wstring Q2W(const QString& s) { return s.toStdWString(); }

QStringList SplitTokens(const QString& line)
{
    QStringList tokens;
    QString cur;
    bool inQuote = false;
    QChar quoteChar;

    for (const QChar ch : line)
    {
        if (inQuote)
        {
            if (ch == quoteChar)
            {
                inQuote = false;
            }
            else
            {
                cur.append(ch);
            }
        }
        else
        {
            if (ch == QLatin1Char('\"') || ch == QLatin1Char('\''))
            {
                inQuote = true;
                quoteChar = ch;
            }
            else if (ch.isSpace())
            {
                if (!cur.isEmpty())
                {
                    tokens.append(cur);
                    cur.clear();
                }
            }
            else
            {
                cur.append(ch);
            }
        }
    }
    if (!cur.isEmpty())
    {
        tokens.append(cur);
    }
    return tokens;
}

} // namespace

TerminalDialog::TerminalDialog(
    std::shared_ptr<server::ServerManager> serverManager,
    std::shared_ptr<config::ConfigManager> configManager,
    std::shared_ptr<core::EventDispatcher> events,
    const std::wstring& initialServerId,
    QWidget* parent)
    : QDialog(parent)
    , serverManager_(std::move(serverManager))
    , configManager_(std::move(configManager))
    , events_(std::move(events))
    , currentServerId_(initialServerId)
{
    setWindowTitle(TR("Redstone — App Terminal"));
    resize(880, 560);
    setMinimumSize(640, 420);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);

    // Header Toolbar
    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    auto* titleLabel = new QLabel(QStringLiteral("<b>REDSTONE TERMINAL</b>"), this);
    topBar->addWidget(titleLabel);

    labelServerBadge_ = new QLabel(this);
    labelServerBadge_->setStyleSheet(QStringLiteral(
        "background: #2a2a2a; color: #38bdf8; padding: 2px 8px; border-radius: 4px; font-weight: bold; font-size: 11px;"));
    topBar->addWidget(labelServerBadge_);

    topBar->addStretch(1);

    buttonHelp_ = new QPushButton(TR("Trợ giúp (help)"), this);
    buttonHelp_->setCursor(Qt::PointingHandCursor);
    connect(buttonHelp_, &QPushButton::clicked, this, &TerminalDialog::OnHelpClicked);
    topBar->addWidget(buttonHelp_);

    buttonClear_ = new QPushButton(TR("Xoá màn hình"), this);
    buttonClear_->setCursor(Qt::PointingHandCursor);
    connect(buttonClear_, &QPushButton::clicked, this, &TerminalDialog::OnClearClicked);
    topBar->addWidget(buttonClear_);

    buttonExternal_ = new QPushButton(TR("Mở CMD ngoài"), this);
    buttonExternal_->setCursor(Qt::PointingHandCursor);
    connect(buttonExternal_, &QPushButton::clicked, this, &TerminalDialog::OnExternalTerminalClicked);
    topBar->addWidget(buttonExternal_);

    rootLayout->addLayout(topBar);

    // Terminal Screen
    textOutput_ = new QPlainTextEdit(this);
    textOutput_->setReadOnly(true);
    textOutput_->setObjectName(QStringLiteral("terminalView"));
    textOutput_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background-color: #121212;"
        "  color: #d4d4d4;"
        "  border: 1px solid #2d2d2d;"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "  selection-background-color: #264f78;"
        "}"));

    QFont monoFont(QStringLiteral("Cascadia Code"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    textOutput_->setFont(monoFont);
    rootLayout->addWidget(textOutput_, 1);

    // Input Bar
    auto* inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(6);

    labelPrompt_ = new QLabel(this);
    labelPrompt_->setStyleSheet(QStringLiteral("color: #ef4444; font-weight: bold; font-family: monospace; font-size: 13px;"));
    inputLayout->addWidget(labelPrompt_);

    editInput_ = new QLineEdit(this);
    editInput_->setFont(monoFont);
    editInput_->setPlaceholderText(TR("Nhập lệnh (vd: help, list, create, delete, status, start, stop, backup...)"));
    editInput_->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background-color: #1a1a1a;"
        "  color: #f1f5f9;"
        "  border: 1px solid #333333;"
        "  border-radius: 4px;"
        "  padding: 6px 10px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #ef4444;"
        "}"));
    editInput_->installEventFilter(this);
    inputLayout->addWidget(editInput_, 1);

    buttonRun_ = new QPushButton(TR("Gửi"), this);
    buttonRun_->setObjectName(QStringLiteral("btnSend"));
    buttonRun_->setCursor(Qt::PointingHandCursor);
    connect(buttonRun_, &QPushButton::clicked, this, &TerminalDialog::OnExecuteCommand);
    connect(editInput_, &QLineEdit::returnPressed, this, &TerminalDialog::OnExecuteCommand);
    inputLayout->addWidget(buttonRun_);

    rootLayout->addLayout(inputLayout);

    UpdatePrompt();
    PrintBanner();
    editInput_->setFocus();
}

TerminalDialog::~TerminalDialog() = default;

void TerminalDialog::UpdatePrompt()
{
    if (currentServerId_.empty())
    {
        labelPrompt_->setText(QStringLiteral("redstone>"));
        labelServerBadge_->setText(TR("Server: (chưa chọn)"));
    }
    else
    {
        auto srv = serverManager_->Get(currentServerId_);
        QString name = srv ? W2Q(srv->GetConfig().name) : W2Q(currentServerId_);
        labelPrompt_->setText(QStringLiteral("redstone[%1]>").arg(name));
        labelServerBadge_->setText(QStringLiteral("Server: %1").arg(name));
    }
}

void TerminalDialog::PrintBanner()
{
    PrintLine(QStringLiteral("======================================================================="), QStringLiteral("#ef4444"));
    PrintLine(QStringLiteral("  REDSTONE APPLICATION TERMINAL v2.0 - Management Console"), QStringLiteral("#f87171"));
    PrintLine(QStringLiteral("======================================================================="), QStringLiteral("#ef4444"));
    PrintLine(TR("Gõ 'help' để xem các lệnh quản lý, 'clear' để xoá màn hình."), QStringLiteral("#94a3b8"));
    PrintLine(TR("Gõ 'use <tên_server>' để chọn server mặc định cho các lệnh tiếp theo."), QStringLiteral("#94a3b8"));
    PrintLine(QString());
}

void TerminalDialog::PrintLine(const QString& text, const QString& colorHex)
{
    if (colorHex.isEmpty())
    {
        textOutput_->appendPlainText(text);
    }
    else
    {
        QString html = QStringLiteral("<span style=\"color: %1;\">%2</span>")
                           .arg(colorHex, text.toHtmlEscaped());
        textOutput_->appendHtml(html);
    }
    textOutput_->verticalScrollBar()->setValue(textOutput_->verticalScrollBar()->maximum());
}

void TerminalDialog::PrintError(const QString& text)
{
    PrintLine(QStringLiteral("❌ ") + text, QStringLiteral("#ef4444"));
}

void TerminalDialog::PrintSuccess(const QString& text)
{
    PrintLine(QStringLiteral("✔ ") + text, QStringLiteral("#22c55e"));
}

void TerminalDialog::PrintWarning(const QString& text)
{
    PrintLine(QStringLiteral("⚠ ") + text, QStringLiteral("#f59e0b"));
}

void TerminalDialog::PrintInfo(const QString& text)
{
    PrintLine(QStringLiteral("ℹ ") + text, QStringLiteral("#38bdf8"));
}

bool TerminalDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == editInput_ && event->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up)
        {
            if (!history_.isEmpty())
            {
                if (historyIndex_ == -1)
                {
                    historyIndex_ = history_.size() - 1;
                }
                else if (historyIndex_ > 0)
                {
                    --historyIndex_;
                }
                editInput_->setText(history_.at(historyIndex_));
                return true;
            }
        }
        else if (keyEvent->key() == Qt::Key_Down)
        {
            if (!history_.isEmpty() && historyIndex_ != -1)
            {
                if (historyIndex_ < history_.size() - 1)
                {
                    ++historyIndex_;
                    editInput_->setText(history_.at(historyIndex_));
                }
                else
                {
                    historyIndex_ = -1;
                    editInput_->clear();
                }
                return true;
            }
        }
        else if (keyEvent->key() == Qt::Key_Tab)
        {
            // Auto complete command names
            static const QStringList knownCommands = {
                QStringLiteral("help"), QStringLiteral("list"), QStringLiteral("create"),
                QStringLiteral("delete"), QStringLiteral("status"), QStringLiteral("start"),
                QStringLiteral("stop"), QStringLiteral("restart"), QStringLiteral("console"),
                QStringLiteral("send"), QStringLiteral("players"), QStringLiteral("op"),
                QStringLiteral("deop"), QStringLiteral("kick"), QStringLiteral("ban"),
                QStringLiteral("pardon"), QStringLiteral("backup"), QStringLiteral("backups"),
                QStringLiteral("props"), QStringLiteral("prop-set"), QStringLiteral("whoami"),
                QStringLiteral("perms"), QStringLiteral("audit"), QStringLiteral("use"),
                QStringLiteral("clear"), QStringLiteral("cls"), QStringLiteral("cmd")
            };
            const QString current = editInput_->text().trimmed();
            if (!current.contains(QLatin1Char(' ')))
            {
                for (const auto& cmd : knownCommands)
                {
                    if (cmd.startsWith(current, Qt::CaseInsensitive))
                    {
                        editInput_->setText(cmd + QLatin1Char(' '));
                        return true;
                    }
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void TerminalDialog::OnExecuteCommand()
{
    const QString line = editInput_->text().trimmed();
    if (line.isEmpty())
    {
        return;
    }

    history_.append(line);
    historyIndex_ = -1;
    editInput_->clear();

    PrintLine(labelPrompt_->text() + QStringLiteral(" ") + line, QStringLiteral("#e2e8f0"));
    ExecuteLine(line);
}

void TerminalDialog::OnClearClicked()
{
    textOutput_->clear();
    PrintBanner();
}

void TerminalDialog::OnHelpClicked()
{
    ExecuteLine(QStringLiteral("help"));
}

void TerminalDialog::OnExternalTerminalClicked()
{
    // Launch a Windows CMD shell in workspace / project directory
    QStringList args;
    args << QStringLiteral("/k") << QStringLiteral("echo Redstone CLI environment ready. && where redstonectl");
    QProcess::startDetached(QStringLiteral("cmd.exe"), args);
    PrintInfo(TR("Đã mở terminal cmd.exe bên ngoài."));
}

std::shared_ptr<server::MinecraftServer> TerminalDialog::ResolveTargetServer(const QString& key, QString* error)
{
    if (key.isEmpty())
    {
        if (!currentServerId_.empty())
        {
            auto srv = serverManager_->Get(currentServerId_);
            if (srv) return srv;
        }
        if (error) *error = TR("Chưa chọn server (dùng 'use <tên_server>' hoặc truyền tên server làm tham số).");
        return nullptr;
    }

    const std::wstring wKey = Q2W(key);
    for (const auto& srv : serverManager_->GetAll())
    {
        if (srv->GetConfig().id == wKey || QString::fromStdWString(srv->GetConfig().name).compare(key, Qt::CaseInsensitive) == 0)
        {
            return srv;
        }
    }

    if (error) *error = TR("Không tìm thấy server \"%1\".").arg(key);
    return nullptr;
}

void TerminalDialog::ExecuteLine(const QString& rawLine)
{
    const QStringList tokens = SplitTokens(rawLine);
    if (tokens.isEmpty())
    {
        return;
    }

    const QString cmd = tokens.first().toLower();
    const QStringList args = tokens.mid(1);

    if (cmd == QLatin1String("clear") || cmd == QLatin1String("cls"))
    {
        textOutput_->clear();
        return;
    }

    if (cmd == QLatin1String("help") || cmd == QLatin1String("?"))
    {
        PrintLine(TR("--- DANH SÁCH LỆNH REDSTONE TERMINAL ---"), QStringLiteral("#38bdf8"));
        PrintLine(QStringLiteral("  list                                        - ") + TR("Liệt kê tất cả server và trạng thái"));
        PrintLine(QStringLiteral("  create <tên> [tuỳ chọn...]                  - ") + TR("Tạo server mới (--dir, --jar, --port, --type, --min-ram, --max-ram)"));
        PrintLine(QStringLiteral("  delete <server> [-y] [--delete-files]       - ") + TR("Xoá server"));
        PrintLine(QStringLiteral("  status [server]                             - ") + TR("Xem thông tin chi tiết một server"));
        PrintLine(QStringLiteral("  start [server]                              - ") + TR("Khởi động server"));
        PrintLine(QStringLiteral("  stop [server]                               - ") + TR("Dừng server an toàn"));
        PrintLine(QStringLiteral("  restart [server]                            - ") + TR("Khởi động lại server"));
        PrintLine(QStringLiteral("  console [server] [-n <dòng>]                - ") + TR("Xem log console gần nhất"));
        PrintLine(QStringLiteral("  send <server> <lệnh...>                     - ") + TR("Gửi lệnh vào console Minecraft"));
        PrintLine(QStringLiteral("  players [server]                            - ") + TR("Xem danh sách người chơi online/whitelist/ops"));
        PrintLine(QStringLiteral("  op / deop <server> <người_chơi>             - ") + TR("Cấp hoặc thu hồi OP"));
        PrintLine(QStringLiteral("  kick / ban / pardon <server> <tên>          - ") + TR("Kick, ban hoặc gỡ ban người chơi"));
        PrintLine(QStringLiteral("  backup [server]                             - ") + TR("Tạo bản sao lưu thế giới"));
        PrintLine(QStringLiteral("  backups [server]                            - ") + TR("Liệt kê các bản sao lưu"));
        PrintLine(QStringLiteral("  props [server] [khoá]                       - ") + TR("Xem cấu hình server.properties"));
        PrintLine(QStringLiteral("  prop-set [server] <khoá> <giá trị>          - ") + TR("Đặt thuộc tính server.properties"));
        PrintLine(QStringLiteral("  use [server]                                - ") + TR("Đặt server mặc định (bỏ trống để huỷ)"));
        PrintLine(QStringLiteral("  whoami / perms / audit                      - ") + TR("Xem quyền hạn và nhật ký truy cập"));
        PrintLine(QStringLiteral("  clear / cls                                 - ") + TR("Xoá màn hình terminal"));
        PrintLine(QStringLiteral("  cmd / external                              - ") + TR("Mở terminal Windows CMD"));
        return;
    }

    if (cmd == QLatin1String("use"))
    {
        if (args.isEmpty())
        {
            currentServerId_.clear();
            UpdatePrompt();
            PrintInfo(TR("Đã bỏ chọn server mặc định."));
            return;
        }
        QString err;
        auto srv = ResolveTargetServer(args.first(), &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        currentServerId_ = srv->GetConfig().id;
        UpdatePrompt();
        PrintSuccess(TR("Đã đặt server mặc định thành \"%1\".").arg(W2Q(srv->GetConfig().name)));
        return;
    }

    if (cmd == QLatin1String("list"))
    {
        const auto servers = serverManager_->GetAll();
        if (servers.empty())
        {
            PrintInfo(TR("Chưa có server nào trong app. Dùng lệnh 'create <tên>' để tạo server mới."));
            return;
        }
        PrintLine(QStringLiteral("-----------------------------------------------------------------------"), QStringLiteral("#475569"));
        PrintLine(QStringLiteral("%-20s %-12s %-8s %-14s %-6s").arg(TR("TÊN SERVER"), TR("TRẠNG THÁI"), TR("PORT"), TR("RAM"), TR("ONLINE")), QStringLiteral("#94a3b8"));
        PrintLine(QStringLiteral("-----------------------------------------------------------------------"), QStringLiteral("#475569"));
        for (const auto& s : servers)
        {
            const auto cfg = s->GetConfig();
            QString stateStr = W2Q(core::ToString(s->GetState()));
            QString stateColor = (s->GetState() == core::ServerState::Running) ? QStringLiteral("#22c55e")
                               : (s->GetState() == core::ServerState::Crashed) ? QStringLiteral("#ef4444")
                               : (s->GetState() == core::ServerState::Starting || s->GetState() == core::ServerState::Stopping) ? QStringLiteral("#f59e0b")
                               : QStringLiteral("#94a3b8");

            QString ramStr = QStringLiteral("%1-%2 MB").arg(cfg.minMemoryMB).arg(cfg.maxMemoryMB);
            int onlineCount = static_cast<int>(s->GetOnlinePlayers().size());
            QString row = QStringLiteral("%-20s %-12s %-8d %-14s %-6d")
                              .arg(W2Q(cfg.name).left(19))
                              .arg(stateStr)
                              .arg(cfg.port)
                              .arg(ramStr)
                              .arg(onlineCount);
            PrintLine(row, stateColor);
        }
        PrintLine(QStringLiteral("-----------------------------------------------------------------------"), QStringLiteral("#475569"));
        return;
    }

    if (cmd == QLatin1String("create"))
    {
        if (args.isEmpty())
        {
            PrintError(TR("Cú pháp: create <tên_server> [--dir <path>] [--jar <path>] [--java <path>] [--port <port>] [--type <type>] [--min-ram <mb>] [--max-ram <mb>]"));
            return;
        }
        QString name = args[0];
        QString dirStr;
        QString jarStr;
        QString javaStr;
        int port = 25565;
        QString typeStr = QStringLiteral("Vanilla");
        int minRam = 1024;
        int maxRam = 2048;

        for (int i = 1; i < args.size(); ++i)
        {
            const QString& flag = args[i];
            if (flag == QLatin1String("--dir") && i + 1 < args.size()) dirStr = args[++i];
            else if (flag == QLatin1String("--jar") && i + 1 < args.size()) jarStr = args[++i];
            else if (flag == QLatin1String("--java") && i + 1 < args.size()) javaStr = args[++i];
            else if (flag == QLatin1String("--port") && i + 1 < args.size()) port = args[++i].toInt();
            else if (flag == QLatin1String("--type") && i + 1 < args.size()) typeStr = args[++i];
            else if (flag == QLatin1String("--min-ram") && i + 1 < args.size()) minRam = args[++i].toInt();
            else if (flag == QLatin1String("--max-ram") && i + 1 < args.size()) maxRam = args[++i].toInt();
        }

        if (dirStr.isEmpty())
        {
            dirStr = QStringLiteral("servers/") + name;
        }
        std::filesystem::path dir(dirStr.toStdWString());
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        if (jarStr.isEmpty()) jarStr = QStringLiteral("server.jar");
        std::filesystem::path jarPath(jarStr.toStdWString());
        if (jarPath.is_relative()) jarPath = dir / jarPath;

        if (javaStr.isEmpty())
        {
            const auto installs = javamanager::DetectInstallations();
            javaStr = (!installs.empty()) ? QString::fromStdWString(installs.front().path.wstring()) : QStringLiteral("java.exe");
        }

        core::ServerConfig cfg;
        cfg.name = name.toStdWString();
        cfg.directory = dir;
        cfg.serverJar = jarPath;
        cfg.javaExecutable = javaStr.toStdWString();
        cfg.serverType = typeStr.toStdWString();
        cfg.port = static_cast<std::uint16_t>(port);
        cfg.minMemoryMB = static_cast<std::uint64_t>(minRam);
        cfg.maxMemoryMB = static_cast<std::uint64_t>(maxRam);

        const std::wstring id = serverManager_->AddServer(cfg, *configManager_);

        core::AppEvent ev;
        ev.type = core::EventType::ServerListChanged;
        events_->Publish(ev);

        currentServerId_ = id;
        UpdatePrompt();

        PrintSuccess(TR("Đã tạo server mới thành công!"));
        PrintLine(QStringLiteral("  Tên:     %1 (%2)").arg(name, W2Q(id)));
        PrintLine(QStringLiteral("  Thư mục: %1").arg(dirStr));
        PrintLine(QStringLiteral("  Port:    %1").arg(port));
        return;
    }

    if (cmd == QLatin1String("delete"))
    {
        if (args.isEmpty() && currentServerId_.empty())
        {
            PrintError(TR("Cú pháp: delete <tên_server> [-y] [--delete-files]"));
            return;
        }
        QString targetKey = args.isEmpty() ? W2Q(currentServerId_) : args[0];
        bool deleteFiles = args.contains(QStringLiteral("--delete-files"));

        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }

        const std::wstring id = srv->GetConfig().id;
        const std::wstring name = srv->GetConfig().name;
        const std::filesystem::path dir = srv->GetConfig().directory;

        if (serverManager_->RemoveServer(id, *configManager_))
        {
            if (deleteFiles)
            {
                std::error_code ec;
                std::filesystem::remove_all(dir, ec);
            }
            core::AppEvent ev;
            ev.type = core::EventType::ServerListChanged;
            events_->Publish(ev);

            if (currentServerId_ == id)
            {
                currentServerId_.clear();
                UpdatePrompt();
            }
            PrintSuccess(TR("Đã xoá server \"%1\" (%2).").arg(W2Q(name), W2Q(id)));
        }
        else
        {
            PrintError(TR("Không thể xoá server."));
        }
        return;
    }

    if (cmd == QLatin1String("status"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        const auto cfg = srv->GetConfig();
        PrintLine(QStringLiteral("=== THÔNG TIN SERVER: %1 ===").arg(W2Q(cfg.name)), QStringLiteral("#38bdf8"));
        PrintLine(QStringLiteral("  ID:         %1").arg(W2Q(cfg.id)));
        PrintLine(QStringLiteral("  Trạng thái: %1").arg(W2Q(core::ToString(srv->GetState()))));
        PrintLine(QStringLiteral("  Port:       %1").arg(cfg.port));
        PrintLine(QStringLiteral("  Loại:       %1").arg(W2Q(cfg.serverType)));
        PrintLine(QStringLiteral("  Thư mục:    %1").arg(W2Q(cfg.directory.wstring())));
        PrintLine(QStringLiteral("  Jar:        %1").arg(W2Q(cfg.serverJar.filename().wstring())));
        PrintLine(QStringLiteral("  Java:       %1").arg(W2Q(cfg.javaExecutable.wstring())));
        PrintLine(QStringLiteral("  RAM:        %1 - %2 MB").arg(cfg.minMemoryMB).arg(cfg.maxMemoryMB));
        PrintLine(QStringLiteral("  Online:     %1").arg(srv->GetOnlinePlayers().size()));
        return;
    }

    if (cmd == QLatin1String("start"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        if (srv->GetState() != core::ServerState::Stopped && srv->GetState() != core::ServerState::Crashed)
        {
            PrintWarning(TR("Server đang ở trạng thái %1, không thể Start.").arg(W2Q(core::ToString(srv->GetState()))));
            return;
        }
        PrintInfo(TR("Đang khởi động server \"%1\"...").arg(W2Q(srv->GetConfig().name)));
        std::thread([srv]() { srv->Start(); }).detach();
        return;
    }

    if (cmd == QLatin1String("stop"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        if (srv->GetState() != core::ServerState::Running && srv->GetState() != core::ServerState::Starting)
        {
            PrintWarning(TR("Server đang ở trạng thái %1, không thể Stop.").arg(W2Q(core::ToString(srv->GetState()))));
            return;
        }
        PrintInfo(TR("Đang dừng server \"%1\"...").arg(W2Q(srv->GetConfig().name)));
        std::thread([srv]() { srv->Stop(); }).detach();
        return;
    }

    if (cmd == QLatin1String("restart"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        PrintInfo(TR("Đang khởi động lại server \"%1\"...").arg(W2Q(srv->GetConfig().name)));
        std::thread([srv]() { srv->Restart(); }).detach();
        return;
    }

    if (cmd == QLatin1String("console"))
    {
        QString targetKey;
        int linesWanted = 50;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == QLatin1String("-n") && i + 1 < args.size())
            {
                linesWanted = args[++i].toInt();
            }
            else if (targetKey.isEmpty())
            {
                targetKey = args[i];
            }
        }
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        const auto lines = srv->GetConsoleLines();
        PrintLine(QStringLiteral("--- Console: %1 (%2 dòng gần nhất) ---").arg(W2Q(srv->GetConfig().name)).arg(linesWanted), QStringLiteral("#38bdf8"));
        const size_t start = lines.size() > static_cast<size_t>(linesWanted) ? lines.size() - linesWanted : 0;
        for (size_t i = start; i < lines.size(); ++i)
        {
            PrintLine(W2Q(lines[i]));
        }
        return;
    }

    if (cmd == QLatin1String("send"))
    {
        if (args.isEmpty())
        {
            PrintError(TR("Cú pháp: send [server] <lệnh_minecraft...>"));
            return;
        }
        QString targetKey;
        QString commandToSend;
        if (args.size() == 1)
        {
            commandToSend = args[0];
        }
        else
        {
            // If first arg matches a server, treat it as server
            QString err;
            if (ResolveTargetServer(args[0], nullptr))
            {
                targetKey = args[0];
                commandToSend = args.mid(1).join(QLatin1Char(' '));
            }
            else
            {
                commandToSend = args.join(QLatin1Char(' '));
            }
        }
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        if (srv->SendCommand(Q2W(commandToSend)))
        {
            PrintSuccess(TR("Đã gửi lệnh \"%1\" tới server %2.").arg(commandToSend, W2Q(srv->GetConfig().name)));
        }
        else
        {
            PrintError(TR("Không gửi được lệnh - server \"%1\" chưa Running.").arg(W2Q(srv->GetConfig().name)));
        }
        return;
    }

    if (cmd == QLatin1String("players"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        const auto online = srv->GetOnlinePlayers();
        PrintLine(QStringLiteral("=== Người chơi server: %1 ===").arg(W2Q(srv->GetConfig().name)), QStringLiteral("#38bdf8"));
        PrintLine(QStringLiteral("Online (%1):").arg(online.size()));
        if (online.empty())
        {
            PrintLine(TR("  (Không có người chơi nào online)"), QStringLiteral("#94a3b8"));
        }
        else
        {
            for (const auto& p : online)
            {
                PrintLine(QStringLiteral("  • ") + W2Q(p), QStringLiteral("#22c55e"));
            }
        }
        return;
    }

    if (cmd == QLatin1String("op") || cmd == QLatin1String("deop"))
    {
        if (args.isEmpty())
        {
            PrintError(QStringLiteral("Cú pháp: %1 [server] <tên_người_chơi>").arg(cmd));
            return;
        }
        QString targetKey;
        QString playerName;
        if (args.size() == 1)
        {
            playerName = args[0];
        }
        else
        {
            targetKey = args[0];
            playerName = args[1];
        }
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        srv->SendCommand(Q2W(QStringLiteral("%1 %2").arg(cmd, playerName)));
        PrintSuccess(TR("Đã gửi lệnh %1 cho người chơi %2.").arg(cmd, playerName));
        return;
    }

    if (cmd == QLatin1String("kick") || cmd == QLatin1String("ban") || cmd == QLatin1String("pardon"))
    {
        if (args.isEmpty())
        {
            PrintError(QStringLiteral("Cú pháp: %1 [server] <tên_người_chơi> [lý do]").arg(cmd));
            return;
        }
        QString targetKey;
        QString playerName;
        QString reason;
        int argIdx = 0;
        if (args.size() > 1 && ResolveTargetServer(args[0], nullptr))
        {
            targetKey = args[argIdx++];
        }
        playerName = args[argIdx++];
        if (argIdx < args.size())
        {
            reason = args.mid(argIdx).join(QLatin1Char(' '));
        }

        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        QString mcCmd = QStringLiteral("%1 %2").arg(cmd, playerName);
        if (!reason.isEmpty()) mcCmd += QLatin1Char(' ') + reason;
        srv->SendCommand(Q2W(mcCmd));
        PrintSuccess(TR("Đã gửi lệnh %1 %2.").arg(cmd, playerName));
        return;
    }

    if (cmd == QLatin1String("backup"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        PrintInfo(TR("Đang tạo backup cho server \"%1\"...").arg(W2Q(srv->GetConfig().name)));
        std::thread([this, srv]() {
            std::wstring backupPath;
            bool ok = backup::CreateBackup(*srv, backupPath);
            QMetaObject::invokeMethod(this, [this, ok, backupPath]() {
                if (ok)
                {
                    PrintSuccess(TR("Đã tạo backup thành công: %1").arg(W2Q(std::filesystem::path(backupPath).filename().wstring())));
                }
                else
                {
                    PrintError(TR("Tạo backup thất bại."));
                }
            });
        }).detach();
        return;
    }

    if (cmd == QLatin1String("backups"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        const auto backups = backup::ListBackups(srv->GetConfig());
        PrintLine(QStringLiteral("=== Danh sách Backups: %1 ===").arg(W2Q(srv->GetConfig().name)), QStringLiteral("#38bdf8"));
        if (backups.empty())
        {
            PrintLine(TR("  (Chưa có bản backup nào)"), QStringLiteral("#94a3b8"));
        }
        else
        {
            for (const auto& b : backups)
            {
                double mb = static_cast<double>(b.sizeBytes) / (1024.0 * 1024.0);
                PrintLine(QStringLiteral("  • %-35s %8.1f MB").arg(W2Q(b.name)).arg(mb));
            }
        }
        return;
    }

    if (cmd == QLatin1String("props"))
    {
        QString targetKey = args.isEmpty() ? QString() : args[0];
        QString err;
        auto srv = ResolveTargetServer(targetKey, &err);
        if (!srv)
        {
            PrintError(err);
            return;
        }
        config::ServerProperties props;
        props.Load(srv->GetConfig().directory);
        PrintLine(QStringLiteral("=== server.properties: %1 ===").arg(W2Q(srv->GetConfig().name)), QStringLiteral("#38bdf8"));
        for (const auto& [k, v] : props.GetAll())
        {
            PrintLine(QStringLiteral("  %1=%2").arg(W2Q(k), W2Q(v)));
        }
        return;
    }

    if (cmd == QLatin1String("whoami"))
    {
        PrintLine(TR("Tài khoản:     admin"), QStringLiteral("#38bdf8"));
        PrintLine(TR("Vai trò:       admin (Toàn quyền quản trị)"), QStringLiteral("#22c55e"));
        PrintLine(TR("Quyền hạn:     create, delete, start, stop, restart, send, prop-set, audit, backup..."));
        return;
    }

    if (cmd == QLatin1String("perms"))
    {
        PrintLine(TR("=== BẢNG PHÂN QUYỀN HỆ THỐNG ==="), QStringLiteral("#38bdf8"));
        PrintLine(QStringLiteral("  Viewer:   ping, whoami, app, list, status, console, players, backups, props"));
        PrintLine(QStringLiteral("  Operator: + start, stop, restart, player (op, deop, kick, ban...), backup"));
        PrintLine(QStringLiteral("  Admin:    + send, prop-set, audit, create, delete"));
        return;
    }

    if (cmd == QLatin1String("audit"))
    {
        int lines = 20;
        if (!args.isEmpty() && args[0] == QLatin1String("-n") && args.size() > 1)
        {
            lines = args[1].toInt();
        }
        const auto auditLines = remote::RemoteControlServer::ReadAuditTail(lines);
        PrintLine(QStringLiteral("=== Nhật ký truy cập (%1 dòng gần nhất) ===").arg(lines), QStringLiteral("#38bdf8"));
        if (auditLines.isEmpty())
        {
            PrintLine(TR("  (Chưa có nhật ký truy cập nào)"), QStringLiteral("#94a3b8"));
        }
        else
        {
            for (const auto& l : auditLines)
            {
                PrintLine(QStringLiteral("  ") + l);
            }
        }
        return;
    }

    if (cmd == QLatin1String("cmd") || cmd == QLatin1String("external"))
    {
        OnExternalTerminalClicked();
        return;
    }

    PrintError(TR("Lệnh không hợp lệ: \"%1\". Gõ 'help' để xem danh sách lệnh.").arg(cmd));
}

} // namespace ui
