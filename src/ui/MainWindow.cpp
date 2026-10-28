#include "ui/MainWindow.hpp"
#include "ui/I18n.hpp"

#include "ui/AddServerDialog.hpp"
#include "ui/AdvancedSettingsDialog.hpp"
#include "ui/AppSettingsDialog.hpp"
#include "remote/RemoteControlServer.hpp"
#include "ui/AppSettings.hpp"
#include "ui/BackupsDialog.hpp"
#include "ui/FileEditorDialog.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"
#include "ui/ImportServerDialog.hpp"
#include "ui/InstalledAddonsDialog.hpp"
#include "ui/LibraryDialog.hpp"
#include "ui/PlayersDialog.hpp"
#include "ui/PropertiesDialog.hpp"
#include "ui/ServerOptionsDialog.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QAbstractButton>
#include <QGuiApplication>
#include <QScreen>
#include <QShortcut>
#include <QComboBox>
#include <QRegularExpression>
#include <QTimer>
#include <QDateTime>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <thread>

namespace ui
{
namespace
{

// Small helper so RefreshControlsForState() can pick a status colour that
// matches the state, instead of every state looking identical.
QString ColorForState(core::ServerState state)
{
    return FluentTheme::StateColor(QString::fromWCharArray(core::ToString(state))).name();
}

QIcon StateDotIcon(core::ServerState state)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(FluentTheme::StateColor(QString::fromWCharArray(core::ToString(state))));
    p.drawEllipse(QRectF(4, 4, 8, 8));
    p.end();
    return QIcon(pm);
}

QString ServerItemLabel(const QString& name, core::ServerState state)
{
    return name + QStringLiteral("\n") + QString::fromWCharArray(core::ToString(state));
}

// Every distinct section of the window (server list, console, controls)
// lives inside one of these "widget cards" - a QFrame with its own
// rounded background (see QSS in Application.cpp), so sections read as
// separate panels with breathing room instead of one flat wall of
// controls touching the window edges.
QFrame* MakeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::NoFrame); // the rounded border comes from QSS, not Qt's native frame drawing
    return card;
}

// Groups several QActions under one dropdown toolbar button instead of
// each being its own top-level toolbar item - this is the real fix for
// the toolbar overflowing on smaller/lower-DPI screens (see BuildToolbar()).
QToolButton* MakeToolbarMenuButton(QWidget* parent, const QString& text, std::initializer_list<QAction*> actions)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setPopupMode(QToolButton::InstantPopup);

    auto* menu = new QMenu(button);
    for (QAction* action : actions)
    {
        menu->addAction(action);
    }
    button->setMenu(menu);
    return button;
}

} // namespace

MainWindow::MainWindow(
    std::shared_ptr<server::ServerManager> serverManager,
    std::shared_ptr<config::ConfigManager> configManager,
    std::shared_ptr<core::EventDispatcher> events,
    QWidget* parent)
    : QMainWindow(parent)
    , serverManager_(std::move(serverManager))
    , configManager_(std::move(configManager))
    , events_(std::move(events))
{
    setWindowTitle(QStringLiteral("Redstone - Minecraft Server Manager"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/redstone.png")));
    // Size from the screen (not a fixed 1120x700 that is tiny on 4K and huge on small laptops),
    // restoring the last position/size when there is one.
    setMinimumSize(900, 600);
    if (!restoreGeometry(CurrentSettings().windowGeometry))
    {
        const QRect avail = QGuiApplication::primaryScreen() != nullptr ? QGuiApplication::primaryScreen()->availableGeometry() : QRect(0, 0, 1366, 768);
        const QSize size(std::min(static_cast<int>(avail.width() * 0.78), 1600), std::min(static_cast<int>(avail.height() * 0.82), 1000));
        resize(size.expandedTo(minimumSize()));
        move(avail.center() - QPoint(width() / 2, height() / 2));
    }

    BuildToolbar();
    BuildTrayIcon();

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* outerLayout = new QVBoxLayout(central);
    outerLayout->setContentsMargins(24, 20, 24, 24);
    outerLayout->setSpacing(18);

    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);
    auto* headerLogo = new QLabel(central);
    headerLogo->setPixmap(QIcon(QStringLiteral(":/icons/redstone.png")).pixmap(36, 36));
    auto* titleLabel = new QLabel(QStringLiteral("Redstone"), central);
    titleLabel->setObjectName(QStringLiteral("titleLabel"));
    auto* taglineLabel = new QLabel(QStringLiteral("Minecraft Server Manager"), central);
    taglineLabel->setObjectName(QStringLiteral("subtitleLabel"));
    headerRow->addWidget(headerLogo);
    headerRow->addWidget(titleLabel);
    headerRow->addWidget(taglineLabel, 0, Qt::AlignBottom);
    headerRow->addStretch(1);
    outerLayout->addLayout(headerRow);

    stackedPages_ = new QStackedWidget(central);

    // --- Page 0: startup / empty-state page --------------------------
    auto* emptyPage = new QWidget(stackedPages_);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto* emptyIcon = new QLabel(emptyPage);
    emptyIcon->setPixmap(QIcon(QStringLiteral(":/icons/redstone.png")).pixmap(96, 96));
    emptyIcon->setAlignment(Qt::AlignCenter);

    auto* emptyTitle = new QLabel(TR("Chào mừng đến với Redstone"), emptyPage);
    emptyTitle->setObjectName(QStringLiteral("titleLabel"));
    emptyTitle->setAlignment(Qt::AlignCenter);

    auto* emptySubtitle = new QLabel(
        TR("Bạn chưa có server nào. Tạo server đầu tiên để bắt đầu quản lý, xem console\n"
                        "thời gian thực, và cài mod/plugin trực tiếp từ Modrinth."),
        emptyPage);
    emptySubtitle->setObjectName(QStringLiteral("hintLabel"));
    emptySubtitle->setAlignment(Qt::AlignCenter);
    emptySubtitle->setWordWrap(true);
    emptySubtitle->setMaximumWidth(460);

    auto* emptyButton = new QPushButton(TR("Tạo server đầu tiên"), emptyPage);
    emptyButton->setObjectName(QStringLiteral("btnStart"));
    emptyButton->setMinimumSize(220, 42);
    RegisterIcon(emptyButton, glyph::Add, true);
    connect(emptyButton, &QPushButton::clicked, this, &MainWindow::OnAddServerClicked);

    emptyLayout->addStretch(1);
    emptyLayout->addWidget(emptyIcon);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addSpacing(4);
    emptyLayout->addWidget(emptySubtitle, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(16);
    emptyLayout->addWidget(emptyButton, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);

    stackedPages_->addWidget(emptyPage); // index 0

    // --- Page 1: the normal server-list + console + controls view ----
    auto* splitter = new QSplitter(Qt::Horizontal, stackedPages_);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(16);

    // Left card: server list
    auto* serversCard = MakeCard(splitter);
    auto* leftLayout = new QVBoxLayout(serversCard);
    leftLayout->setContentsMargins(18, 16, 18, 18);
    leftLayout->setSpacing(12);

    auto* serversLabel = new QLabel(QStringLiteral("SERVERS"), serversCard);
    serversLabel->setObjectName(QStringLiteral("sectionLabel"));
    leftLayout->addWidget(serversLabel);

    listServers_ = new QListWidget(serversCard);
    leftLayout->addWidget(listServers_, 1);

    auto* leftButtons = new QHBoxLayout();
    leftButtons->setSpacing(10);
    buttonAdd_ = new QPushButton(TR("Thêm"), serversCard);
    buttonAdd_->setObjectName(QStringLiteral("btnStart"));
    buttonRemove_ = new QPushButton(TR("Xoá"), serversCard);
    buttonRemove_->setObjectName(QStringLiteral("btnNeutral"));
    RegisterIcon(buttonAdd_, glyph::Add, true);
    RegisterIcon(buttonRemove_, glyph::Delete, false);
    leftButtons->addWidget(buttonAdd_);
    leftButtons->addWidget(buttonRemove_);
    leftLayout->addLayout(leftButtons);

    splitter->addWidget(serversCard);

    // Right side: two stacked cards (console, then controls)
    auto* rightContainer = new QWidget(splitter);
    auto* rightOuter = new QVBoxLayout(rightContainer);
    rightOuter->setContentsMargins(0, 0, 0, 0);
    rightOuter->setSpacing(18);

    auto* consoleCard = MakeCard(rightContainer);
    auto* consoleLayout = new QVBoxLayout(consoleCard);
    consoleLayout->setContentsMargins(18, 16, 18, 18);
    consoleLayout->setSpacing(12);

    auto* statusRow = new QHBoxLayout();
    labelStatus_ = new QLabel(TR("Chưa chọn server"), consoleCard);
    labelStatus_->setObjectName(QStringLiteral("statusLabel"));
    labelStats_ = new QLabel(QStringLiteral(""), consoleCard);
    labelStats_->setObjectName(QStringLiteral("statsLabel"));
    statusRow->addWidget(labelStatus_);
    statusRow->addStretch(1);
    statusRow->addWidget(labelStats_);
    consoleLayout->addLayout(statusRow);

    // Output filter row: free-text search + category + "shown/total" counter.
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);
    editFilter_ = new QLineEdit(consoleCard);
    editFilter_->setPlaceholderText(TR("Lọc console... (gõ chữ cần tìm, không phân biệt hoa/thường)"));
    editFilter_->setClearButtonEnabled(true);
    comboFilterKind_ = new QComboBox(consoleCard);
    comboFilterKind_->addItem(TR("Tất cả"));
    comboFilterKind_->addItem(TR("Cảnh báo + lỗi"));
    comboFilterKind_->addItem(TR("Chỉ lỗi"));
    comboFilterKind_->addItem(TR("Chat & người chơi"));
    comboFilterKind_->addItem(TR("Lệnh & manager"));
    labelFilterCount_ = new QLabel(consoleCard);
    labelFilterCount_->setObjectName(QStringLiteral("hintLabel"));
    filterRow->addWidget(editFilter_, 1);
    filterRow->addWidget(comboFilterKind_);
    filterRow->addWidget(labelFilterCount_);
    consoleLayout->addLayout(filterRow);

    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    filterTimer_->setInterval(200);
    connect(filterTimer_, &QTimer::timeout, this, &MainWindow::OnConsoleFilterChanged);
    connect(editFilter_, &QLineEdit::textChanged, filterTimer_, qOverload<>(&QTimer::start));
    connect(comboFilterKind_, &QComboBox::currentIndexChanged, this, &MainWindow::OnConsoleFilterChanged);

    editConsole_ = new QPlainTextEdit(consoleCard);
    editConsole_->setReadOnly(true);
    editConsole_->setObjectName(QStringLiteral("consoleView"));
    consoleLayout->addWidget(editConsole_, 1);

    rightOuter->addWidget(consoleCard, 1);

    auto* controlsCard = MakeCard(rightContainer);
    auto* controlsLayout = new QVBoxLayout(controlsCard);
    controlsLayout->setContentsMargins(18, 16, 18, 16);
    controlsLayout->setSpacing(12);

    auto* commandLayout = new QHBoxLayout();
    commandLayout->setSpacing(10);
    editCommand_ = new QLineEdit(controlsCard);
    editCommand_->setPlaceholderText(TR("Nhập lệnh console (vd: say hello, op Steve...)"));
    buttonSend_ = new QPushButton(TR("Gửi"), controlsCard);
    RegisterIcon(buttonSend_, glyph::Send, true);
    buttonSend_->setObjectName(QStringLiteral("btnSend"));
    commandLayout->addWidget(editCommand_, 1);
    commandLayout->addWidget(buttonSend_);
    controlsLayout->addLayout(commandLayout);

    auto* actionLayout = new QHBoxLayout();
    actionLayout_ = actionLayout;
    actionLayout->setSpacing(10);
    buttonStart_ = new QPushButton(QStringLiteral("Start"), controlsCard);
    buttonStart_->setObjectName(QStringLiteral("btnStart"));
    buttonStop_ = new QPushButton(QStringLiteral("Stop"), controlsCard);
    buttonStop_->setObjectName(QStringLiteral("btnStop"));
    buttonRestart_ = new QPushButton(QStringLiteral("Restart"), controlsCard);
    buttonRestart_->setObjectName(QStringLiteral("btnRestart"));
    RegisterIcon(buttonStart_, glyph::Play, true);
    RegisterIcon(buttonStop_, glyph::Stop, true);
    RegisterIcon(buttonRestart_, glyph::Restart, false);
    actionLayout->addWidget(buttonStart_);
    actionLayout->addWidget(buttonStop_);
    actionLayout->addWidget(buttonRestart_);
    actionLayout->addStretch(1);
    controlsLayout->addLayout(actionLayout);

    rightOuter->addWidget(controlsCard);

    splitter->addWidget(rightContainer);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 800});

    stackedPages_->addWidget(splitter); // index 1

    outerLayout->addWidget(stackedPages_, 1);

    connect(buttonAdd_, &QPushButton::clicked, this, &MainWindow::OnAddServerClicked);
    connect(buttonRemove_, &QPushButton::clicked, this, &MainWindow::OnRemoveServerClicked);
    connect(buttonStart_, &QPushButton::clicked, this, &MainWindow::OnStartClicked);
    connect(buttonStop_, &QPushButton::clicked, this, &MainWindow::OnStopClicked);
    connect(buttonRestart_, &QPushButton::clicked, this, &MainWindow::OnRestartClicked);
    connect(buttonSend_, &QPushButton::clicked, this, &MainWindow::OnSendCommandClicked);
    // Enter-to-send: something the raw Win32 version did not have without
    // extra plumbing - Qt gives it to us for free via QLineEdit::returnPressed.
    connect(editCommand_, &QLineEdit::returnPressed, this, &MainWindow::OnSendCommandClicked);
    connect(listServers_, &QListWidget::itemSelectionChanged, this, &MainWindow::OnServerSelectionChanged);

    // Marshal every server event onto the UI thread. The lambda below runs
    // on whichever background thread published the event (a console
    // reader, the stats sampler, or MinecraftServer's watcher thread);
    // NotifyEvent() is the only thing touching `this` from that thread,
    // and it is safe to call from any thread by design (see below).
    events_->Subscribe([this](const core::AppEvent& event) { NotifyEvent(event); });

    ApplyConsoleSettings();
    ApplyThemedIcons();
    RefreshServerList();

    // Zoom the whole UI like a browser: Ctrl + / Ctrl - / Ctrl 0.
    auto addZoomShortcut = [this](const QKeySequence& keys, int direction) {
        auto* shortcut = new QShortcut(keys, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, direction]() { ChangeZoom(direction); });
    };
    addZoomShortcut(QKeySequence(QKeySequence::ZoomIn), +1);
    addZoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), +1);
    addZoomShortcut(QKeySequence(QKeySequence::ZoomOut), -1);
    addZoomShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), 0);

    remote_ = new remote::RemoteControlServer(serverManager_, events_, this);
    ApplyRemoteSettings(false);
}

void MainWindow::ChangeZoom(int direction)
{
    static const int steps[] = {80, 90, 100, 110, 125, 150, 175, 200};
    constexpr int count = static_cast<int>(sizeof(steps) / sizeof(steps[0]));
    AppSettings& s = CurrentSettings();

    int index = 2; // 100%
    for (int i = 0; i < count; ++i)
    {
        if (steps[i] <= s.fontScalePercent) index = i; // nearest step not above the current value
    }
    if (direction == 0) index = 2;
    else index = std::clamp(index + direction, 0, count - 1);
    if (steps[index] == s.fontScalePercent) return;

    s.fontScalePercent = steps[index];
    s.Save();
    FluentTheme::Apply(*qApp, s);
    OnAppSettingsApplied();
}

void MainWindow::RequestRestart()
{
    int running = 0;
    for (const auto& srv : serverManager_->GetAll())
    {
        if (srv->GetState() != core::ServerState::Stopped) ++running;
    }
    if (running > 0 &&
        QMessageBox::question(
            this, TR("Khởi động lại ứng dụng"),
            TR("Có %1 server đang chạy. Chúng sẽ được dừng an toàn trước khi app khởi động lại. Tiếp tục?").arg(running),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    RestartRequested() = true;
    QApplication::quit(); // Application::Run starts the new instance after the servers have stopped
}

void MainWindow::LayoutActionButtons(const std::vector<QPushButton*>& visibleInOrder)
{
    // The row really changes shape with the server state instead of only recolouring
    // greyed-out buttons: the main action always sits in the first slot.
    const std::vector<QPushButton*> all = {buttonStart_, buttonStop_, buttonRestart_};
    for (QPushButton* b : all)
    {
        actionLayout_->removeWidget(b);
        b->setVisible(false);
    }
    int slot = 0;
    for (QPushButton* b : visibleInOrder)
    {
        actionLayout_->insertWidget(slot++, b);
        b->setVisible(true);
    }
}

void MainWindow::ApplyRemoteSettings(bool interactive)
{
    AppSettings& s = CurrentSettings();
    if (!s.remoteEnabled)
    {
        remote_->Stop();
        remoteActiveToken_.clear();
        remoteActivePort_ = 0;
        return;
    }
    if (s.remoteToken.isEmpty())
    {
        s.remoteToken = remote::RemoteControlServer::GenerateToken();
        s.Save();
    }
    const auto port = static_cast<quint16>(s.remotePort);
    if (remote_->IsListening() && remoteActivePort_ == port && remoteActiveToken_ == s.remoteToken)
    {
        return; // nothing changed - keep existing sessions
    }
    QString error;
    if (remote_->Start(port, s.remoteToken, &error))
    {
        remoteActiveToken_ = s.remoteToken;
        remoteActivePort_ = port;
        return;
    }
    remoteActiveToken_.clear();
    remoteActivePort_ = 0;
    const QString message = TR("Không bật được điều khiển từ xa trên 127.0.0.1:%1 — %2").arg(port).arg(error);
    if (interactive)
    {
        QMessageBox::warning(this, TR("Điều khiển từ xa"), message);
    }
    else if (trayIcon_ != nullptr)
    {
        trayIcon_->showMessage(QStringLiteral("Redstone"), message, QSystemTrayIcon::Warning, 6000);
    }
}

void MainWindow::RegisterIcon(QAction* action, char16_t glyphCode)
{
    ThemedIcon entry;
    entry.action = action;
    entry.glyph = glyphCode;
    themedIcons_.push_back(entry);
}

void MainWindow::RegisterIcon(QAbstractButton* button, char16_t glyphCode, bool onAccent)
{
    ThemedIcon entry;
    entry.button = button;
    entry.glyph = glyphCode;
    entry.onAccent = onAccent;
    themedIcons_.push_back(entry);
}

void MainWindow::ApplyThemedIcons()
{
    const FluentColors& c = FluentTheme::Colors();
    for (const ThemedIcon& entry : themedIcons_)
    {
        if (entry.action != nullptr)
        {
            entry.action->setIcon(FluentTheme::Icon(entry.glyph, c.text, 16));
        }
        else if (entry.button != nullptr)
        {
            // Filled accent/danger buttons keep a white glyph in both themes.
            entry.button->setIcon(FluentTheme::Icon(entry.glyph, entry.onAccent ? QColor(Qt::white) : c.text, 16));
            entry.button->setIconSize(QSize(16, 16));
        }
    }
    if (trayIcon_ != nullptr)
    {
        trayIcon_->setIcon(QIcon(QStringLiteral(":/icons/redstone.png")));
    }
}

void MainWindow::ApplyConsoleSettings()
{
    const AppSettings& s = CurrentSettings();
    QFont consoleFont(s.consoleFontFamily, s.consoleFontSize);
    consoleFont.setStyleHint(QFont::Monospace);
    editConsole_->setFont(consoleFont);
    editConsole_->setLineWrapMode(s.consoleWrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    editConsole_->setMaximumBlockCount(s.consoleMaxLines);
}

void MainWindow::BuildToolbar()
{
    toolbar_ = addToolBar(TR("Chính"));
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar_->setIconSize(QSize(16, 16));

    // A Fluent "command bar": a few flat buttons, each with a glyph icon.
    // Related actions are grouped under dropdown buttons so the bar fits
    // small windows without hiding half of it behind the overflow chevron.
    auto makeAction = [this](const QString& text, char16_t glyphCode, auto slot) {
        auto* action = new QAction(text, this);
        RegisterIcon(action, glyphCode);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    auto addMenuButton = [this](const QString& text, char16_t glyphCode, std::initializer_list<QAction*> actions) {
        auto* button = MakeToolbarMenuButton(toolbar_, text, actions);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        auto* holder = new QAction(this); // only used to carry the themed icon
        RegisterIcon(holder, glyphCode);
        connect(holder, &QAction::changed, button, [button, holder]() { button->setIcon(holder->icon()); });
        toolbar_->addWidget(button);
    };

    addMenuButton(
        QStringLiteral("Server"),
        glyph::Server,
        {makeAction(TR("Server mới"), glyph::Add, &MainWindow::OnAddServerClicked),
         makeAction(TR("Import server có sẵn"), glyph::Download, &MainWindow::OnImportServerClicked),
         makeAction(TR("Xoá server"), glyph::Delete, &MainWindow::OnRemoveServerClicked)});

    toolbar_->addSeparator();

    addMenuButton(
        TR("Cài đặt server"),
        glyph::Sliders,
        {makeAction(TR("Cấu hình chạy (RAM, JVM, backup tự động)"), glyph::Sliders, &MainWindow::OnOpenServerOptionsClicked),
         makeAction(TR("Gameplay & mạng (server.properties)"), glyph::Settings, &MainWindow::OnOpenPropertiesClicked),
         makeAction(TR("Tunnel, Firewall & lịch restart"), glyph::Globe, &MainWindow::OnOpenAdvancedSettingsClicked),
         makeAction(TR("Sửa file cấu hình"), glyph::Edit, &MainWindow::OnEditConfigFileClicked),
         makeAction(TR("Chấp nhận EULA"), glyph::Page, &MainWindow::OnAcceptEulaClicked)});

    auto* actionPlayers = makeAction(TR("Người chơi"), glyph::People, &MainWindow::OnOpenPlayersClicked);
    toolbar_->addAction(actionPlayers);
    auto* actionBackups = makeAction(QStringLiteral("Backups"), glyph::Save, &MainWindow::OnOpenBackupsClicked);
    toolbar_->addAction(actionBackups);

    addMenuButton(
        QStringLiteral("Mod / Plugin"),
        glyph::Puzzle,
        {makeAction(TR("Thư viện (Modrinth)"), glyph::Library, &MainWindow::OnOpenLibraryClicked),
         makeAction(TR("Đã cài"), glyph::Puzzle, &MainWindow::OnOpenInstalledAddonsClicked)});

    // Right-aligned: refresh + application settings.
    auto* spacer = new QWidget(toolbar_);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);
    toolbar_->addAction(makeAction(TR("Làm mới"), glyph::Refresh, [this]() { RefreshServerList(); }));
    toolbar_->addAction(makeAction(TR("Cài đặt"), glyph::Settings, &MainWindow::OnOpenAppSettingsClicked));
}

void MainWindow::BuildTrayIcon()
{
    trayIcon_ = new QSystemTrayIcon(this);
    // No custom .ico asset yet - a standard style icon is a reasonable
    // stand-in so the tray icon isn't blank; swap for a real app icon later.
    trayIcon_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon)); // replaced by a themed glyph in ApplyThemedIcons()
    trayIcon_->setToolTip(QStringLiteral("Redstone - Minecraft Server Manager"));

    auto* menu = new QMenu(this);
    auto* showAction = menu->addAction(TR("Hiện cửa sổ"));
    connect(showAction, &QAction::triggered, this, &MainWindow::ShowAndRaiseWindow);
    menu->addSeparator();
    auto* exitAction = menu->addAction(TR("Thoát"));
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);
    trayIcon_->setContextMenu(menu);

    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        {
            ShowAndRaiseWindow();
        }
    });

    trayIcon_->show();
}

void MainWindow::ShowAndRaiseWindow()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    CurrentSettings().windowGeometry = saveGeometry();
    CurrentSettings().Save();
    if (!CurrentSettings().minimizeToTrayOnClose)
    {
        // "Nút X thu xuống khay" is off: closing the window quits the app.
        event->accept();
        QApplication::quit();
        return;
    }
    if (trayIcon_ != nullptr && trayIcon_->isVisible())
    {
        hide();
        if (!trayNotificationShown_ && CurrentSettings().trayNotifications)
        {
            trayIcon_->showMessage(
                QStringLiteral("Redstone"),
                TR("Ứng dụng vẫn chạy trong khay hệ thống. Bấm phải vào icon để Thoát hẳn."),
                QSystemTrayIcon::Information,
                4000);
            trayNotificationShown_ = true;
        }
        event->ignore();
    }
    else
    {
        event->accept();
    }
}

void MainWindow::NotifyEvent(const core::AppEvent& event)
{
    // QMetaObject::invokeMethod with Qt::QueuedConnection posts the call
    // onto this object's thread affinity (the UI thread, since MainWindow
    // was constructed there) and returns immediately - this is Qt's
    // equivalent of PostMessageW, and is safe to call from any thread.
    QMetaObject::invokeMethod(this, [this, event]() { HandleAppEvent(event); }, Qt::QueuedConnection);
}

void MainWindow::HandleAppEvent(core::AppEvent event)
{
    switch (event.type)
    {
        case core::EventType::ServerStateChanged:
            // Perf: patch just this server's row instead of clearing and
            // rebuilding the whole QListWidget on every single state
            // tick (Starting -> Running -> ... for every server, not
            // just the selected one, used to trigger a full rebuild).
            UpdateServerListItem(event.serverId);
            if (event.serverId == GetSelectedServerId())
            {
                RefreshControlsForState();
            }
            if (CurrentSettings().trayNotifications && trayIcon_ != nullptr && !isActiveWindow())
            {
                if (auto srv = serverManager_->Get(event.serverId))
                {
                    const QString name = QString::fromStdWString(srv->GetConfig().name);
                    switch (srv->GetState())
                    {
                        case core::ServerState::Running:
                            trayIcon_->showMessage(name, TR("Server đã khởi động."), QSystemTrayIcon::Information, 3000);
                            break;
                        case core::ServerState::Stopped:
                            trayIcon_->showMessage(name, TR("Server đã dừng."), QSystemTrayIcon::Information, 3000);
                            break;
                        case core::ServerState::Crashed:
                            trayIcon_->showMessage(name, TR("Server bị crash!"), QSystemTrayIcon::Critical, 6000);
                            break;
                        default: break;
                    }
                }
            }
            break;
        case core::EventType::ConsoleLine:
            if (event.serverId == GetSelectedServerId())
            {
                AppendConsoleLine(QString::fromStdWString(event.message));
            }
            break;
        case core::EventType::StatsUpdated:
            if (event.serverId == GetSelectedServerId())
            {
                // cpuPercent < 0 is the sentinel meaning "this event only
                // carries a fresh TPS reply, not a new cpu/mem sample" -
                // see core::AppEvent's comment for why the two are decoupled.
                if (event.cpuPercent >= 0.0)
                {
                    const double memoryMB = static_cast<double>(event.memoryBytes) / (1024.0 * 1024.0);
                    cachedCpuRamText_ = QStringLiteral("CPU %1%   ·   RAM %2 MB")
                        .arg(QString::number(event.cpuPercent, 'f', 1))
                        .arg(QString::number(memoryMB, 'f', 0));
                }
                if (!event.tpsText.empty())
                {
                    cachedTpsText_ = QString::fromStdWString(event.tpsText);
                }
                UpdateStatsLabel();
            }
            break;
        case core::EventType::ServerCrashed:
            // The corresponding console line and state change are handled
            // by the two cases above; nothing extra needed for the MVP.
            break;
        case core::EventType::EulaRequired:
            if (event.serverId == GetSelectedServerId())
            {
                const auto reply = QMessageBox::question(
                    this,
                    TR("Cần chấp nhận Minecraft EULA"),
                    TR("Server vừa dừng vì chưa chấp nhận Minecraft End User License Agreement:\n"
                        "https://aka.ms/MinecraftEULA\n\n"
                        "Bạn đã đọc và đồng ý (eula=true)?"),
                    QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes)
                {
                    AcceptEulaForCurrentServer();
                }
            }
            break;
    }
}

void MainWindow::OnAddServerClicked()
{
    AddServerDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    auto config = dialog.GetResult();
    if (!config.has_value())
    {
        return;
    }
    serverManager_->AddServer(*config, *configManager_);
    RefreshServerList();
}

void MainWindow::OnImportServerClicked()
{
    ImportServerDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    auto config = dialog.GetResult();
    if (!config.has_value())
    {
        return;
    }
    serverManager_->AddServer(*config, *configManager_);
    RefreshServerList();
}

void MainWindow::OnOpenInstalledAddonsClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(this, TR("Plugin/Mod đã cài"), TR("Hãy chọn một server trước."));
        return;
    }
    InstalledAddonsDialog dialog(srv->GetConfig(), this);
    dialog.exec();
}

void MainWindow::OnEditConfigFileClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(this, TR("Sửa file cấu hình"), TR("Hãy chọn một server trước."));
        return;
    }

    const QString startDir = QString::fromStdWString(srv->GetConfig().directory.wstring());
    const QString path = QFileDialog::getOpenFileName(
        this,
        TR("Chọn file để sửa (bukkit.yml, spigot.yml, config plugin...)"),
        startDir,
        TR("File cấu hình (*.yml *.yaml *.properties *.json *.toml *.cfg *.conf *.txt);;Tất cả file (*.*)"));
    if (path.isEmpty())
    {
        return;
    }

    FileEditorDialog dialog(path.toStdWString(), this);
    dialog.exec();
}

void MainWindow::OnOpenAdvancedSettingsClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(this, TR("Cài đặt nâng cao"), TR("Hãy chọn một server trước."));
        return;
    }
    AdvancedSettingsDialog dialog(srv, serverManager_, configManager_, this);
    dialog.exec();
    RefreshServerList(); // in case a state-affecting change happened while the dialog was open
}

void MainWindow::OnOpenAppSettingsClicked()
{
    AppSettingsDialog dialog(this);
    connect(&dialog, &AppSettingsDialog::settingsApplied, this, &MainWindow::OnAppSettingsApplied);
    connect(&dialog, &AppSettingsDialog::restartRequested, this, &MainWindow::RequestRestart, Qt::QueuedConnection);
    dialog.exec();
}

void MainWindow::OnAppSettingsApplied()
{
    ApplyRemoteSettings(true);
    ApplyConsoleSettings();
    ApplyThemedIcons();
    RefreshServerList();          // state-dot colours depend on the theme
    RefreshSelectedServerConsole(); // re-colour the existing console text
    RefreshControlsForState();
}

void MainWindow::OnOpenServerOptionsClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(this, TR("Cài đặt server"), TR("Hãy chọn một server trước."));
        return;
    }
    ServerOptionsDialog dialog(srv, serverManager_, configManager_, this);
    dialog.exec();
    RefreshServerList(); // the name may have changed
}

void MainWindow::OnRemoveServerClicked()
{
    const std::wstring id = GetSelectedServerId();
    if (id.empty())
    {
        return;
    }
    auto srv = serverManager_->Get(id);
    const QString name = srv ? QString::fromStdWString(srv->GetConfig().name) : QString::fromStdWString(id);

    const auto reply = QMessageBox::question(
        this,
        TR("Xác nhận"),
        TR("Xoá server \"%1\"? Server sẽ bị dừng nếu đang chạy.").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    serverManager_->RemoveServer(id, *configManager_);
    RefreshServerList();
    editConsole_->clear();
}

void MainWindow::OnStartClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        return;
    }
    // Moved off the UI thread so a slow antivirus scan on java.exe (or
    // any other CreateProcessW hiccup) cannot freeze the window.
    std::thread([srv]() { srv->Start(); }).detach();
}

void MainWindow::OnStopClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        return;
    }
    if (CurrentSettings().confirmStopRestart &&
        QMessageBox::question(this, TR("Dừng server"), TR("Dừng server này? Người chơi đang online sẽ bị ngắt kết nối."),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    // Stop() can block for up to ~30 seconds waiting for a graceful
    // shutdown, so it must never run on the UI thread.
    std::thread([srv]() { srv->Stop(); }).detach();
}

void MainWindow::OnRestartClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        return;
    }
    if (CurrentSettings().confirmStopRestart &&
        QMessageBox::question(this, TR("Khởi động lại"), TR("Khởi động lại server này? Người chơi đang online sẽ bị ngắt kết nối."),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    std::thread([srv]() { srv->Restart(); }).detach();
}

void MainWindow::OnSendCommandClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        return;
    }
    const QString text = editCommand_->text();
    if (text.isEmpty())
    {
        return;
    }
    srv->SendCommand(text.toStdWString());
    editCommand_->clear();
}

void MainWindow::OnServerSelectionChanged()
{
    // Clear stale stats immediately - otherwise the previously-selected
    // server's last CPU/RAM/TPS text would stay on screen until the newly
    // selected server's next periodic sample arrives.
    cachedCpuRamText_.clear();
    cachedTpsText_.clear();
    labelStats_->setText(QString());

    RefreshSelectedServerConsole();
    RefreshControlsForState();
}

void MainWindow::OnOpenLibraryClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(
            this,
            TR("Thư viện Mod/Plugin"),
            TR("Hãy chọn (hoặc tạo) một server trước khi mở Thư viện."));
        return;
    }
    LibraryDialog dialog(srv->GetConfig(), this);
    dialog.exec();
}

void MainWindow::OnOpenPropertiesClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(
            this, QStringLiteral("server.properties"), TR("Hãy chọn một server trước."));
        return;
    }
    PropertiesDialog dialog(srv->GetConfig(), this);
    dialog.exec();
}

void MainWindow::OnOpenPlayersClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(
            this, TR("Quản lý người chơi"), TR("Hãy chọn một server trước."));
        return;
    }
    PlayersDialog dialog(srv, this); // read-only lists work while stopped; actions enable when Running
    dialog.exec();
}

void MainWindow::OnOpenBackupsClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(this, QStringLiteral("Backups"), TR("Hãy chọn một server trước."));
        return;
    }
    BackupsDialog dialog(srv, this);
    dialog.exec();
}

void MainWindow::OnAcceptEulaClicked()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        QMessageBox::information(this, QStringLiteral("EULA"), TR("Hãy chọn một server trước."));
        return;
    }

    const auto reply = QMessageBox::question(
        this,
        QStringLiteral("Minecraft EULA"),
        TR("Minecraft server yêu cầu bạn đọc và đồng ý với Minecraft End User License Agreement:\n"
            "https://aka.ms/MinecraftEULA\n\n"
            "Bạn có đồng ý không?"),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
    {
        AcceptEulaForCurrentServer();
    }
}

void MainWindow::AcceptEulaForCurrentServer()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        return;
    }
    if (srv->AcceptEula())
    {
        QMessageBox::information(
            this, QStringLiteral("EULA"), TR("Đã cập nhật eula.txt. Bạn có thể Start lại server."));
    }
    else
    {
        QMessageBox::warning(this, QStringLiteral("EULA"), TR("Không ghi được eula.txt."));
    }
}

void MainWindow::RefreshServerList()
{
    // Full rebuild: only needed for structural changes (add/remove/
    // import/initial load). Per-server state changes are patched in
    // place by UpdateServerListItem() instead - see HandleAppEvent().
    const std::wstring currentlySelected = GetSelectedServerId();

    listServers_->blockSignals(true);
    listServers_->clear();
    serverListItems_.clear();

    QListWidgetItem* itemToSelect = nullptr;
    const auto allServers = serverManager_->GetAll();
    for (const auto& srv : allServers)
    {
        const auto& cfg = srv->GetConfig();
        const QString label = ServerItemLabel(QString::fromStdWString(cfg.name), srv->GetState());

        auto* item = new QListWidgetItem(StateDotIcon(srv->GetState()), label, listServers_);
        const QString id = QString::fromStdWString(cfg.id);
        item->setData(Qt::UserRole, id);
        serverListItems_.insert(id, item);
        if (cfg.id == currentlySelected)
        {
            itemToSelect = item;
        }
    }

    if (itemToSelect != nullptr)
    {
        listServers_->setCurrentItem(itemToSelect);
    }
    else if (listServers_->count() > 0)
    {
        listServers_->setCurrentRow(0);
    }
    listServers_->blockSignals(false);

    if (itemToSelect == nullptr)
    {
        // Selection actually changed (or the list became empty) while
        // signals were blocked above, so the dependent panels need an
        // explicit refresh here - including clearing stale stats, same as
        // OnServerSelectionChanged does for a user-driven selection change.
        cachedCpuRamText_.clear();
        cachedTpsText_.clear();
        labelStats_->setText(QString());
        RefreshSelectedServerConsole();
    }
    RefreshControlsForState();

    // Startup page: greet a fresh install (or one where every server was
    // removed) with a call-to-action instead of an empty console.
    stackedPages_->setCurrentIndex(allServers.empty() ? 0 : 1);
}

void MainWindow::UpdateServerListItem(const std::wstring& serverId)
{
    const QString id = QString::fromStdWString(serverId);
    const auto it = serverListItems_.constFind(id);
    if (it == serverListItems_.constEnd())
    {
        return; // not currently in the list (e.g. removed just before this event arrived) - nothing to patch
    }
    auto srv = serverManager_->Get(serverId);
    if (!srv)
    {
        return;
    }
    const auto& cfg = srv->GetConfig();
    it.value()->setText(ServerItemLabel(QString::fromStdWString(cfg.name), srv->GetState()));
    it.value()->setIcon(StateDotIcon(srv->GetState()));
}

void MainWindow::RefreshSelectedServerConsole()
{
    shownLineCount_ = 0;
    totalLineCount_ = 0;
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        editConsole_->clear();
        UpdateFilterCountLabel();
        return;
    }

    editConsole_->setUpdatesEnabled(false);
    editConsole_->clear();
    for (const auto& line : srv->GetConsoleLines())
    {
        const QString text = QString::fromStdWString(line);
        ++totalLineCount_;
        if (PassesConsoleFilter(text))
        {
            ++shownLineCount_;
            AppendConsoleLineFormatted(text, false);
        }
    }
    UpdateFilterCountLabel();
    editConsole_->setUpdatesEnabled(true);
    editConsole_->verticalScrollBar()->setValue(editConsole_->verticalScrollBar()->maximum());
}

void MainWindow::RefreshControlsForState()
{
    auto srv = serverManager_->Get(GetSelectedServerId());
    if (!srv)
    {
        buttonStart_->setEnabled(false);
        buttonStop_->setEnabled(false);
        buttonRestart_->setEnabled(false);
        LayoutActionButtons({buttonStart_});
        buttonSend_->setEnabled(false);
        buttonRemove_->setEnabled(false);
        labelStatus_->setText(TR("Chưa chọn server"));
        labelStatus_->setStyleSheet(QString());
        labelStats_->setText(QString());
        return;
    }

    const auto state = srv->GetState();
    const bool running = state == core::ServerState::Running;
    const bool transitioning =
        state == core::ServerState::Starting ||
        state == core::ServerState::Stopping ||
        state == core::ServerState::Restarting;

    buttonStart_->setEnabled(!running && !transitioning);
    buttonStop_->setEnabled(running || state == core::ServerState::Starting);
    buttonRestart_->setEnabled(running);
    if (state == core::ServerState::Running)
    {
        LayoutActionButtons({buttonStop_, buttonRestart_});       // [Stop] [Restart]
    }
    else if (state == core::ServerState::Starting)
    {
        LayoutActionButtons({buttonStop_});                       // can still be cancelled
    }
    else if (transitioning)
    {
        buttonStop_->setEnabled(false);
        LayoutActionButtons({buttonStop_});                       // busy: one disabled button, nothing else to click
    }
    else
    {
        LayoutActionButtons({buttonStart_});                      // Stopped / Crashed
    }
    buttonSend_->setEnabled(running);
    buttonRemove_->setEnabled(!transitioning);

    labelStatus_->setText(QStringLiteral("●  ") + QString::fromWCharArray(core::ToString(state)));
    labelStatus_->setStyleSheet(
        QStringLiteral("QLabel#statusLabel { color: %1; font-weight: 600; font-size: 11pt; }")
            .arg(ColorForState(state)));

    if (!running)
    {
        labelStats_->setText(QString());
        cachedCpuRamText_.clear();
        cachedTpsText_.clear();
    }
}

void MainWindow::UpdateStatsLabel()
{
    if (cachedCpuRamText_.isEmpty())
    {
        labelStats_->setText(cachedTpsText_);
        return;
    }
    if (cachedTpsText_.isEmpty())
    {
        labelStats_->setText(cachedCpuRamText_);
        return;
    }
    labelStats_->setText(cachedCpuRamText_ + QStringLiteral("   ·   ") + cachedTpsText_);
}

bool MainWindow::PassesConsoleFilter(const QString& line) const
{
    const QString text = editFilter_ != nullptr ? editFilter_->text().trimmed() : QString();
    if (!text.isEmpty() && !line.contains(text, Qt::CaseInsensitive))
    {
        return false;
    }

    const int kind = comboFilterKind_ != nullptr ? comboFilterKind_->currentIndex() : 0;
    if (kind == 0)
    {
        return true;
    }

    const bool isError = line.contains(QStringLiteral("ERROR]")) || line.contains(QStringLiteral("FATAL]")) ||
        line.contains(QStringLiteral("Exception")) || line.contains(QStringLiteral("Crashed"));
    const bool isWarn = line.contains(QStringLiteral("WARN]"));
    switch (kind)
    {
        case 1: return isError || isWarn;
        case 2: return isError;
        case 3:
        {
            static const QRegularExpression chat(QStringLiteral("\\]: <[^>]+> "));
            return line.contains(QStringLiteral("joined the game")) || line.contains(QStringLiteral("left the game")) ||
                line.contains(QStringLiteral("lost connection")) || line.contains(QStringLiteral("logged in with entity id")) ||
                line.contains(QStringLiteral("UUID of player")) || line.contains(QStringLiteral("issued server command")) ||
                line.contains(chat);
        }
        case 4:
            return line.startsWith(QStringLiteral("[manager]")) || line.startsWith(QStringLiteral("[tunnel]")) ||
                line.startsWith(QStringLiteral("> "));
        default: return true;
    }
}

void MainWindow::UpdateFilterCountLabel()
{
    const bool active = !editFilter_->text().trimmed().isEmpty() || comboFilterKind_->currentIndex() != 0;
    labelFilterCount_->setText(active ? TR("%1 / %2 dòng").arg(shownLineCount_).arg(totalLineCount_) : QString());
}

void MainWindow::OnConsoleFilterChanged()
{
    RefreshSelectedServerConsole(); // rebuilds the view from the server's full log with the new filter
}

void MainWindow::AppendConsoleLine(const QString& line)
{
    ++totalLineCount_;
    if (PassesConsoleFilter(line))
    {
        ++shownLineCount_;
        AppendConsoleLineFormatted(line, CurrentSettings().consoleTimestamps);
    }
    UpdateFilterCountLabel();
}

void MainWindow::AppendConsoleLineFormatted(const QString& line, bool withTimestamp)
{
    const AppSettings& settings = CurrentSettings();
    const FluentColors& c = FluentTheme::Colors();

    QScrollBar* bar = editConsole_->verticalScrollBar();
    const bool wasAtBottom = bar->value() >= bar->maximum() - 4;

    QTextCharFormat format;
    format.setForeground(c.text);
    if (settings.consoleColorize)
    {
        const QColor errorColor = c.dark ? QColor("#FF99A4") : c.danger;
        if (line.contains(QStringLiteral("ERROR]")) || line.contains(QStringLiteral("FATAL]")) ||
            line.contains(QStringLiteral("Exception")) || line.contains(QStringLiteral("Crashed")))
        {
            format.setForeground(errorColor);
        }
        else if (line.contains(QStringLiteral("WARN]")))
        {
            format.setForeground(c.warning);
        }
        else if (line.startsWith(QStringLiteral("[manager]")) || line.startsWith(QStringLiteral("[tunnel]")) ||
                 line.startsWith(QStringLiteral("> ")))
        {
            format.setForeground(c.accentText);
        }
        else if (line.contains(QStringLiteral("joined the game")))
        {
            format.setForeground(c.success);
        }
        else if (line.contains(QStringLiteral("left the game")))
        {
            format.setForeground(c.textTertiary);
        }
    }

    QString text = line;
    if (withTimestamp)
    {
        text = QStringLiteral("[%1] ").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))) + text;
    }

    QTextCursor cursor(editConsole_->document());
    cursor.movePosition(QTextCursor::End);
    if (!editConsole_->document()->isEmpty())
    {
        cursor.insertBlock();
    }
    cursor.insertText(text, format);

    if (settings.consoleAutoScroll && wasAtBottom)
    {
        bar->setValue(bar->maximum());
    }
}

std::wstring MainWindow::GetSelectedServerId() const
{
    const auto* item = listServers_->currentItem();
    if (item == nullptr)
    {
        return L"";
    }
    return item->data(Qt::UserRole).toString().toStdWString();
}

} // namespace ui
