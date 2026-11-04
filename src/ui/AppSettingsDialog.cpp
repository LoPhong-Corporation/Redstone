#include "ui/AppSettingsDialog.hpp"
#include "ui/I18n.hpp"

#include "platform/StartupManager.hpp"
#include "remote/RemoteControlServer.hpp"
#include "ui/AppSettings.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"
#include "ui/OpenSshHelper.hpp"

#include <QApplication>
#include <QClipboard>
#include <algorithm>
#include <QSet>
#include <QScrollBar>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QGuiApplication>
#include <QLineEdit>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ui
{
namespace
{

QIcon SwatchIcon(const QColor& color)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(1, 1, 14, 14), 4, 4);
    p.end();
    return QIcon(pm);
}

// Wraps a page's content in a scroll area so tall pages still fit small screens.
QWidget* WrapScrollable(QWidget* content)
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
    scroll->setWidget(content);
    return scroll;
}

QWidget* NewPage(QVBoxLayout*& layoutOut, const QString& title, const QString& subtitle)
{
    auto* page = new QWidget();
    layoutOut = new QVBoxLayout(page);
    layoutOut->setContentsMargins(0, 0, 12, 0);
    layoutOut->setSpacing(8);
    AddPageHeader(layoutOut, title, subtitle);
    layoutOut->addSpacing(8);
    return page;
}

} // namespace

AppSettingsDialog::AppSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(TR("Cài đặt"));
    resize(900, 620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(14);

    auto* body = new QHBoxLayout();
    body->setSpacing(24);

    categories_ = new QListWidget(this);
    categories_->setFixedWidth(190);
    categories_->setIconSize(QSize(18, 18));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Color), TR("Giao diện")));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Console), QStringLiteral("Console")));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Settings), TR("Hệ thống")));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Globe), TR("Điều khiển từ xa")));
    body->addWidget(categories_);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(WrapScrollable(BuildAppearancePage()));
    pages_->addWidget(WrapScrollable(BuildConsolePage()));
    pages_->addWidget(WrapScrollable(BuildSystemPage()));
    pages_->addWidget(WrapScrollable(BuildRemotePage()));
    body->addWidget(pages_, 1);
    root->addLayout(body, 1);

    connect(categories_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    categories_->setCurrentRow(0);

    auto* footer = new QHBoxLayout();
    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    footer->addWidget(labelStatus_, 1);
    auto* applyButton = new QPushButton(TR("Áp dụng"), this);
    applyButton->setObjectName(QStringLiteral("btnStart"));
    auto* closeButton = new QPushButton(TR("Đóng"), this);
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    footer->addWidget(applyButton);
    footer->addWidget(closeButton);
    root->addLayout(footer);

    connect(applyButton, &QPushButton::clicked, this, &AppSettingsDialog::OnApplyClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    LoadFromSettings();
}

QWidget* AppSettingsDialog::BuildAppearancePage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(layout, TR("Giao diện"), TR("Phong cách Fluent — chủ đề, màu nhấn và cỡ chữ."));

    comboTheme_ = new QComboBox();
    comboTheme_->addItems({TR("Theo hệ thống"), TR("Tối"), TR("Sáng")});
    comboTheme_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        TR("Chủ đề"), TR("Chế độ \"Theo hệ thống\" đọc cài đặt app sáng/tối của Windows lúc áp dụng."), comboTheme_));

    comboAccent_ = new QComboBox();
    const QStringList names = FluentTheme::AccentNames();
    for (int i = 0; i < names.size(); ++i)
    {
        comboAccent_->addItem(SwatchIcon(FluentTheme::AccentSwatch(i)), names[i]);
    }
    comboAccent_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        TR("Màu nhấn"), TR("Dùng cho nút chính, thanh chọn, viền focus và công tắc."), comboAccent_));

    comboFontScale_ = new QComboBox();
    for (const int pct : {80, 90, 100, 110, 125, 150, 175, 200})
    {
        comboFontScale_->addItem(QStringLiteral("%1%").arg(pct), pct);
    }
    comboFontScale_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        TR("Cỡ chữ giao diện"), TR("Phóng to/thu nhỏ chữ trong app. Phím tắt: Ctrl + , Ctrl - , Ctrl 0."), comboFontScale_));

    comboUiScale_ = new QComboBox();
    comboUiScale_->addItem(TR("Tự động (theo Windows)"), 0);
    for (const int pct : {100, 125, 150, 175, 200, 250})
    {
        comboUiScale_->addItem(QStringLiteral("%1%").arg(pct), pct);
    }
    comboUiScale_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        TR("Phóng to cả cửa sổ"),
        TR("Phóng to toàn bộ giao diện (chữ, nút, icon) cho màn hình độ phân giải cao. Cần khởi động lại app."), comboUiScale_));

    comboLanguage_ = new QComboBox();
    comboLanguage_->addItem(TR("Tự động (theo hệ thống)"), QStringLiteral("auto"));
    for (const auto& lang : i18n::AvailableLanguages())
    {
        comboLanguage_->addItem(lang.name, lang.code);
    }
    comboLanguage_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        TR("Ngôn ngữ"),
        TR("Muốn thêm ngôn ngữ: đặt file <mã>.json vào thư mục translations/ cạnh file exe. Cần khởi động lại app."), comboLanguage_));

    layout->addStretch(1);
    return page;
}

QWidget* AppSettingsDialog::BuildConsolePage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(layout, QStringLiteral("Console"), TR("Cách hiển thị log của server."));

    comboConsoleFont_ = new QFontComboBox();
    comboConsoleFont_->setFontFilters(QFontComboBox::MonospacedFonts);
    comboConsoleFont_->setMinimumWidth(200);
    layout->addWidget(MakeSettingRow(QStringLiteral("Font"), TR("Chỉ liệt kê font đơn cách (monospace)."), comboConsoleFont_));

    spinConsoleFontSize_ = new QSpinBox();
    spinConsoleFontSize_->setRange(7, 28);
    spinConsoleFontSize_->setSuffix(QStringLiteral(" pt"));
    spinConsoleFontSize_->setMinimumWidth(110);
    layout->addWidget(MakeSettingRow(TR("Cỡ chữ console"), QString(), spinConsoleFontSize_));

    spinConsoleMaxLines_ = new QSpinBox();
    spinConsoleMaxLines_->setRange(500, 50000);
    spinConsoleMaxLines_->setSingleStep(500);
    spinConsoleMaxLines_->setMinimumWidth(110);
    layout->addWidget(MakeSettingRow(
        TR("Số dòng tối đa hiển thị"), TR("Dòng cũ hơn bị bỏ khỏi khung hiển thị để app luôn mượt."), spinConsoleMaxLines_));

    layout->addWidget(MakeToggleRow(TR("Tự xuống dòng"), TR("Dòng dài được bẻ gọn thay vì cuộn ngang."), toggleWrap_));
    layout->addWidget(MakeToggleRow(
        TR("Tô màu theo mức log"), TR("WARN màu vàng, ERROR/lỗi màu đỏ, dòng [manager]/[tunnel] màu nhấn."), toggleColorize_));
    layout->addWidget(MakeToggleRow(
        TR("Hiện giờ nhận dòng"), TR("Thêm [HH:mm:ss] đầu mỗi dòng mới (không áp dụng cho log cũ đã có)."), toggleTimestamps_));
    layout->addWidget(MakeToggleRow(
        TR("Tự cuộn xuống cuối"), TR("Luôn bám theo dòng mới nhất khi đang ở cuối khung."), toggleAutoScroll_));

    layout->addStretch(1);
    return page;
}

QWidget* AppSettingsDialog::BuildSystemPage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(layout, TR("Hệ thống"), TR("Khởi động, khay hệ thống và xác nhận thao tác."));

    layout->addWidget(MakeToggleRow(
        TR("Khởi động cùng Windows"), TR("Ghi vào Registry Run của user hiện tại (không cần quyền Admin)."), toggleStartWithWindows_));
    layout->addWidget(MakeToggleRow(
        TR("Chạy ẩn (Silent) khi khởi động cùng Windows"),
        TR("Chỉ hiện icon khay hệ thống, không bật cửa sổ. Server đã đánh dấu \"tự Start khi mở app\" vẫn tự chạy."),
        toggleSilent_));
    layout->addWidget(MakeToggleRow(
        TR("Nút X thu xuống khay"), TR("Tắt = bấm X sẽ thoát hẳn app (và dừng các server đang chạy)."), toggleMinimizeToTray_));
    layout->addWidget(MakeToggleRow(
        TR("Thông báo khay hệ thống"), TR("Báo khi server khởi động xong, dừng hoặc bị crash."), toggleTrayNotifications_));
    layout->addWidget(MakeToggleRow(
        TR("Hỏi xác nhận trước khi Stop/Restart"), TR("Tránh lỡ tay làm gián đoạn người chơi đang online."), toggleConfirmStop_));

    layout->addStretch(1);
    return page;
}

QWidget* AppSettingsDialog::BuildRemotePage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(
        layout, TR("Điều khiển từ xa"),
        TR("Cho phép công cụ dòng lệnh redstonectl điều khiển các server từ terminal."));

    layout->addWidget(MakeToggleRow(
        TR("Bật điều khiển từ xa"),
        TR("Mặc định TẮT. Khi bật, app chỉ lắng nghe ở 127.0.0.1 (chính máy này), không mở ra mạng."),
        toggleRemote_));

    spinRemotePort_ = new QSpinBox();
    spinRemotePort_->setRange(1024, 65535);
    spinRemotePort_->setMinimumWidth(120);
    layout->addWidget(MakeSettingRow(TR("Port"), TR("Port trên 127.0.0.1 mà redstonectl kết nối tới."), spinRemotePort_));

    // OpenSSH Server status (needed to control this machine from another one)
    labelSshStatus_ = new QLabel();
    buttonSshInstall_ = new QPushButton(TR("Cài đặt OpenSSH Server"));
    auto* sshBox = new QWidget();
    auto* sshLayout = new QHBoxLayout(sshBox);
    sshLayout->setContentsMargins(0, 0, 0, 0);
    sshLayout->setSpacing(10);
    sshLayout->addWidget(labelSshStatus_);
    sshLayout->addWidget(buttonSshInstall_);
    layout->addWidget(MakeSettingRow(
        TR("OpenSSH Server (để điều khiển từ máy khác)"),
        TR("Không cần nếu chỉ dùng redstonectl trên chính máy này. Trên Windows, app có thể cài và bật giúp bạn (hiện hộp thoại xin quyền Admin)."),
        sshBox, nullptr, true));
    sshPollTimer_ = new QTimer(this);
    sshPollTimer_->setInterval(3000);
    connect(sshPollTimer_, &QTimer::timeout, this, [this]() {
        RefreshSshStatus();
        if (--sshPollsLeft_ <= 0 || openssh::QueryServerStatus() == openssh::ServerStatus::Running) sshPollTimer_->stop();
    });
    connect(buttonSshInstall_, &QPushButton::clicked, this, &AppSettingsDialog::OnInstallSshClicked);
    RefreshSshStatus();

    // ---- Accounts (roles) ----
    layout->addSpacing(8);
    layout->addWidget(MakeSectionLabel(TR("TÀI KHOẢN TRUY CẬP & PHÂN QUYỀN")));
    auto* accountCard = new QFrame();
    accountCard->setObjectName(QStringLiteral("settingRow"));
    auto* accountLayout = new QHBoxLayout(accountCard);
    accountLayout->setContentsMargins(14, 14, 14, 14);
    accountLayout->setSpacing(16);

    auto* left = new QVBoxLayout();
    listAccounts_ = new QListWidget();
    listAccounts_->setMinimumWidth(170);
    listAccounts_->setMaximumWidth(210);
    left->addWidget(listAccounts_, 1);
    auto* addAccount = new QPushButton(TR("Thêm tài khoản"));
    auto* removeAccount = new QPushButton(TR("Xoá tài khoản"));
    left->addWidget(addAccount);
    left->addWidget(removeAccount);
    accountLayout->addLayout(left);

    auto* form = new QVBoxLayout();
    form->setSpacing(8);
    editAccountName_ = new QLineEdit();
    editAccountName_->setPlaceholderText(TR("Tên tài khoản (chữ, số, _ . -)"));
    form->addWidget(new QLabel(TR("Tên tài khoản")));
    form->addWidget(editAccountName_);

    comboAccountRole_ = new QComboBox();
    comboAccountRole_->addItem(TR("viewer — chỉ xem"), QStringLiteral("viewer"));
    comboAccountRole_->addItem(TR("operator — vận hành"), QStringLiteral("operator"));
    comboAccountRole_->addItem(TR("admin — toàn quyền"), QStringLiteral("admin"));
    form->addWidget(new QLabel(TR("Vai trò")));
    form->addWidget(comboAccountRole_);
    labelRoleHelp_ = new QLabel();
    labelRoleHelp_->setObjectName(QStringLiteral("hintLabel"));
    labelRoleHelp_->setWordWrap(true);
    form->addWidget(labelRoleHelp_);

    checkAllServers_ = new QCheckBox(TR("Được dùng trên mọi server"));
    form->addWidget(checkAllServers_);
    listAccountServers_ = new QListWidget();
    listAccountServers_->setMaximumHeight(110);
    form->addWidget(listAccountServers_);

    editRemoteToken_ = new QLineEdit();
    editRemoteToken_->setEchoMode(QLineEdit::Password);
    editRemoteToken_->setReadOnly(true);
    editRemoteToken_->setMinimumWidth(120);
    auto* showButton = new QPushButton(TR("Hiện"));
    auto* copyButton = new QPushButton(TR("Sao chép"));
    auto* newButton = new QPushButton(TR("Tạo mới"));
    auto* tokenRow = new QHBoxLayout();
    tokenRow->setSpacing(8);
    tokenRow->addWidget(editRemoteToken_, 1);
    tokenRow->addWidget(showButton);
    tokenRow->addWidget(copyButton);
    tokenRow->addWidget(newButton);
    form->addWidget(new QLabel(TR("Token truy cập")));
    form->addLayout(tokenRow);
    auto* tokenHint = new QLabel(TR("Token không bao giờ được gửi qua kết nối, chỉ một mã HMAC của nó. Tạo mới sẽ ngắt các phiên đang dùng token cũ."));
    tokenHint->setObjectName(QStringLiteral("hintLabel"));
    tokenHint->setWordWrap(true);
    form->addWidget(tokenHint);
    accountLayout->addLayout(form, 1);
    layout->addWidget(accountCard);

    // ---- Audit log ----
    layout->addSpacing(8);
    layout->addWidget(MakeSectionLabel(TR("NHẬT KÝ TRUY CẬP")));
    viewAudit_ = new QPlainTextEdit();
    viewAudit_->setReadOnly(true);
    viewAudit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    viewAudit_->setMinimumHeight(120);
    viewAudit_->setMaximumHeight(180);
    viewAudit_->setFont(QFont(QStringLiteral("Cascadia Mono"), 9));
    layout->addWidget(viewAudit_);
    auto* refreshAudit = new QPushButton(TR("Làm mới nhật ký"));
    layout->addWidget(refreshAudit, 0, Qt::AlignLeft);

    auto* hint = new QLabel(TR(
        "<b>Dùng từ máy khác qua SSH</b> (cần OpenSSH Server trên máy này):<br>"
        "<code>redstonectl --ssh bạn@may-chu --user &lt;tên&gt; --token &lt;token&gt; info</code><br><br>"
        "Cùng máy: <code>redstonectl --user &lt;tên&gt; --token &lt;token&gt; list</code><br><br>"
        "Mỗi lệnh được kiểm tra quyền ở phía app và ghi vào nhật ký (data/remote_audit.log). "
        "Không thể thêm/xoá server, khôi phục backup hay đổi cài đặt app từ xa."));
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    layout->addWidget(hint);
    layout->addStretch(1);

    // ---- wiring ----
    connect(listAccounts_, &QListWidget::currentRowChanged, this, [this](int row) {
        CommitAccountForm();
        currentAccount_ = row;
        LoadAccountForm();
    });
    connect(editAccountName_, &QLineEdit::textEdited, this, [this]() { CommitAccountForm(); if (currentAccount_ >= 0) listAccounts_->item(currentAccount_)->setText(QStringLiteral("%1 · %2").arg(accounts_[currentAccount_].name, remote::RoleName(accounts_[currentAccount_].role))); });
    connect(comboAccountRole_, &QComboBox::currentIndexChanged, this, [this]() {
        if (loadingForm_) return; // LoadAccountForm() sets the combo itself - re-entering it would reset the form half-loaded
        CommitAccountForm();
        if (currentAccount_ >= 0) listAccounts_->item(currentAccount_)->setText(QStringLiteral("%1 · %2").arg(accounts_[currentAccount_].name, remote::RoleName(accounts_[currentAccount_].role)));
        LoadAccountForm();
    });
    connect(checkAllServers_, &QCheckBox::toggled, this, [this](bool all) { listAccountServers_->setEnabled(!all); CommitAccountForm(); });
    connect(listAccountServers_, &QListWidget::itemChanged, this, [this]() { CommitAccountForm(); });
    connect(addAccount, &QPushButton::clicked, this, [this]() {
        CommitAccountForm();
        remote::Account a;
        int n = accounts_.size() + 1;
        auto taken = [this](const QString& name) { for (const auto& x : accounts_) if (x.name.compare(name, Qt::CaseInsensitive) == 0) return true; return false; };
        do { a.name = QStringLiteral("user%1").arg(n++); } while (taken(a.name));
        a.secret = remote::RemoteControlServer::GenerateToken();
        a.role = remote::Role::Viewer;
        accounts_.push_back(a);
        RefreshAccountList(static_cast<int>(accounts_.size()) - 1);
    });
    connect(removeAccount, &QPushButton::clicked, this, [this]() {
        if (currentAccount_ < 0 || currentAccount_ >= accounts_.size()) return;
        accounts_.removeAt(currentAccount_);
        currentAccount_ = -1;
        RefreshAccountList(std::min<int>(0, static_cast<int>(accounts_.size()) - 1));
    });
    connect(refreshAudit, &QPushButton::clicked, this, &AppSettingsDialog::RefreshAudit);

    // Turning remote control on while OpenSSH Server is missing: offer to install it now.
    connect(toggleRemote_, &QAbstractButton::toggled, this, [this](bool on) {
        if (!on || !isVisible() || !openssh::CanInstallAutomatically()) return;
        if (openssh::QueryServerStatus() == openssh::ServerStatus::NotInstalled)
        {
            OnInstallSshClicked(); // asks for confirmation itself
        }
    });
    connect(showButton, &QPushButton::clicked, this, [this, showButton]() {
        const bool hidden = editRemoteToken_->echoMode() == QLineEdit::Password;
        editRemoteToken_->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        showButton->setText(hidden ? TR("Ẩn") : TR("Hiện"));
    });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        if (!editRemoteToken_->text().isEmpty())
        {
            QGuiApplication::clipboard()->setText(editRemoteToken_->text());
            labelStatus_->setText(TR("Đã sao chép token vào clipboard."));
        }
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        if (currentAccount_ < 0 || currentAccount_ >= accounts_.size()) return;
        accounts_[currentAccount_].secret = remote::RemoteControlServer::GenerateToken();
        LoadAccountForm();
        labelStatus_->setText(TR("Đã tạo token mới - bấm Áp dụng để dùng."));
    });
    return page;
}

void AppSettingsDialog::SetKnownServers(const QList<QPair<QString, QString>>& servers)
{
    knownServers_ = servers;
    LoadAccountForm();
}

void AppSettingsDialog::RefreshAccountList(int select)
{
    listAccounts_->blockSignals(true);
    listAccounts_->clear();
    for (const auto& a : accounts_)
    {
        listAccounts_->addItem(QStringLiteral("%1 · %2").arg(a.name, remote::RoleName(a.role)));
    }
    currentAccount_ = (select >= 0 && select < accounts_.size()) ? select : -1;
    // Select the row WITHOUT firing currentRowChanged: that handler saves the form into the
    // "previous" account, and here the form still holds stale data - it would overwrite this one.
    if (currentAccount_ >= 0) listAccounts_->setCurrentRow(currentAccount_);
    listAccounts_->blockSignals(false);
    LoadAccountForm();
}

void AppSettingsDialog::LoadAccountForm()
{
    loadingForm_ = true;
    const bool valid = currentAccount_ >= 0 && currentAccount_ < accounts_.size();
    editAccountName_->setEnabled(valid);
    comboAccountRole_->setEnabled(valid);
    checkAllServers_->setEnabled(valid);
    listAccountServers_->setEnabled(valid);
    if (!valid)
    {
        editAccountName_->clear();
        editRemoteToken_->clear();
        labelRoleHelp_->clear();
        listAccountServers_->clear();
        loadingForm_ = false;
        return;
    }
    const remote::Account& a = accounts_[currentAccount_];
    editAccountName_->setText(a.name);
    comboAccountRole_->setCurrentIndex(std::max(0, comboAccountRole_->findData(remote::RoleName(a.role))));
    editRemoteToken_->setText(a.secret);
    switch (a.role)
    {
        case remote::Role::Viewer: labelRoleHelp_->setText(TR("Chỉ xem: danh sách, trạng thái, console, người chơi, backup, server.properties.")); break;
        case remote::Role::Operator: labelRoleHelp_->setText(TR("Như viewer, thêm: Start/Stop/Restart, thao tác người chơi (op, kick, ban, whitelist...), tạo backup.")); break;
        case remote::Role::Admin: labelRoleHelp_->setText(TR("Như operator, thêm: gửi lệnh console thô, sửa server.properties, xem nhật ký truy cập.")); break;
    }
    checkAllServers_->setChecked(a.serverIds.isEmpty());
    listAccountServers_->clear();
    for (const auto& [id, name] : knownServers_)
    {
        auto* item = new QListWidgetItem(name, listAccountServers_);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(a.serverIds.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    listAccountServers_->setEnabled(!a.serverIds.isEmpty());
    loadingForm_ = false;
}

void AppSettingsDialog::CommitAccountForm()
{
    if (loadingForm_ || currentAccount_ < 0 || currentAccount_ >= accounts_.size()) return;
    remote::Account& a = accounts_[currentAccount_];
    a.name = editAccountName_->text().trimmed();
    a.role = remote::ParseRole(comboAccountRole_->currentData().toString()).value_or(remote::Role::Viewer);
    a.serverIds.clear();
    if (!checkAllServers_->isChecked())
    {
        for (int i = 0; i < listAccountServers_->count(); ++i)
        {
            if (listAccountServers_->item(i)->checkState() == Qt::Checked) a.serverIds << listAccountServers_->item(i)->data(Qt::UserRole).toString();
        }
    }
}

void AppSettingsDialog::RefreshAudit()
{
    const QStringList lines = remote::RemoteControlServer::ReadAuditTail(120);
    viewAudit_->setPlainText(lines.isEmpty() ? TR("(chưa có hoạt động nào)") : lines.join(QStringLiteral("\n")));
    viewAudit_->verticalScrollBar()->setValue(viewAudit_->verticalScrollBar()->maximum());
}

void AppSettingsDialog::RefreshSshStatus()
{
    const auto status = openssh::QueryServerStatus();
    labelSshStatus_->setText(openssh::StatusText(status));
    const bool missing = status == openssh::ServerStatus::NotInstalled || status == openssh::ServerStatus::InstalledStopped;
    buttonSshInstall_->setVisible(missing && openssh::CanInstallAutomatically());
}

void AppSettingsDialog::OnInstallSshClicked()
{
    if (QMessageBox::question(
            this, TR("Cài đặt OpenSSH Server"),
            TR("App sẽ cài OpenSSH Server, bật dịch vụ sshd (tự chạy cùng Windows) và mở port 22 trên Firewall. "
               "Windows sẽ hỏi quyền Administrator. Tiếp tục?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    if (!openssh::StartElevatedInstall())
    {
        QMessageBox::warning(this, TR("Cài đặt OpenSSH Server"), TR("Không chạy được trình cài đặt. Hãy cài thủ công: Settings > Optional features > OpenSSH Server."));
        return;
    }
    labelStatus_->setText(TR("Đang cài OpenSSH Server... (có thể mất vài phút)"));
    sshPollsLeft_ = 60;
    sshPollTimer_->start();
}

void AppSettingsDialog::LoadFromSettings()
{
    const AppSettings& s = CurrentSettings();
    comboTheme_->setCurrentIndex(static_cast<int>(s.theme));
    comboAccent_->setCurrentIndex(std::min(s.accentIndex, comboAccent_->count() - 1));
    int scaleIndex = comboFontScale_->findData(s.fontScalePercent);
    if (scaleIndex < 0)
    {
        comboFontScale_->addItem(QStringLiteral("%1%").arg(s.fontScalePercent), s.fontScalePercent);
        scaleIndex = comboFontScale_->count() - 1;
    }
    comboFontScale_->setCurrentIndex(scaleIndex);
    const int uiIndex = comboUiScale_->findData(s.uiScalePercent);
    comboUiScale_->setCurrentIndex(uiIndex >= 0 ? uiIndex : 0);
    const int langIndex = comboLanguage_->findData(s.language);
    comboLanguage_->setCurrentIndex(langIndex >= 0 ? langIndex : 0);

    comboConsoleFont_->setCurrentFont(QFont(s.consoleFontFamily));
    spinConsoleFontSize_->setValue(s.consoleFontSize);
    spinConsoleMaxLines_->setValue(s.consoleMaxLines);
    toggleWrap_->setChecked(s.consoleWrap);
    toggleColorize_->setChecked(s.consoleColorize);
    toggleTimestamps_->setChecked(s.consoleTimestamps);
    toggleAutoScroll_->setChecked(s.consoleAutoScroll);

    toggleStartWithWindows_->setChecked(platform::IsStartOnBootEnabled());
    toggleSilent_->setChecked(true);
    toggleMinimizeToTray_->setChecked(s.minimizeToTrayOnClose);
    toggleTrayNotifications_->setChecked(s.trayNotifications);
    toggleConfirmStop_->setChecked(s.confirmStopRestart);

    toggleRemote_->setChecked(s.remoteEnabled);
    spinRemotePort_->setValue(s.remotePort);
    accounts_ = s.remoteAccounts;
    RefreshAccountList(accounts_.isEmpty() ? -1 : 0);
    RefreshAudit();
}

void AppSettingsDialog::OnApplyClicked()
{
    AppSettings s = CurrentSettings();
    s.theme = static_cast<AppSettings::ThemeMode>(comboTheme_->currentIndex());
    s.accentIndex = comboAccent_->currentIndex();
    s.fontScalePercent = comboFontScale_->currentData().toInt();
    const bool needsRestart = s.uiScalePercent != comboUiScale_->currentData().toInt() || s.language != comboLanguage_->currentData().toString();
    s.uiScalePercent = comboUiScale_->currentData().toInt();
    s.language = comboLanguage_->currentData().toString();

    s.consoleFontFamily = comboConsoleFont_->currentFont().family();
    s.consoleFontSize = spinConsoleFontSize_->value();
    s.consoleMaxLines = spinConsoleMaxLines_->value();
    s.consoleWrap = toggleWrap_->isChecked();
    s.consoleColorize = toggleColorize_->isChecked();
    s.consoleTimestamps = toggleTimestamps_->isChecked();
    s.consoleAutoScroll = toggleAutoScroll_->isChecked();

    s.minimizeToTrayOnClose = toggleMinimizeToTray_->isChecked();
    s.trayNotifications = toggleTrayNotifications_->isChecked();
    s.confirmStopRestart = toggleConfirmStop_->isChecked();

    s.remoteEnabled = toggleRemote_->isChecked();
    s.remotePort = spinRemotePort_->value();
    CommitAccountForm();
    {
        QSet<QString> names;
        for (const remote::Account& a : accounts_)
        {
            if (!remote::ValidAccountName(a.name) || names.contains(a.name.toLower()))
            {
                QMessageBox::warning(this, TR("Tài khoản truy cập"), TR("Tên tài khoản \"%1\" không hợp lệ hoặc bị trùng.").arg(a.name));
                return;
            }
            names.insert(a.name.toLower());
        }
        if (s.remoteEnabled && accounts_.isEmpty())
        {
            remote::Account admin;
            admin.name = QStringLiteral("admin");
            admin.secret = remote::RemoteControlServer::GenerateToken(); // never run with no account
            admin.role = remote::Role::Admin;
            accounts_.push_back(admin);
            RefreshAccountList(0);
        }
        s.remoteAccounts = accounts_;
    }

    CurrentSettings() = s;
    s.Save();

    const bool startupOk = platform::SetStartOnBoot(toggleStartWithWindows_->isChecked(), toggleSilent_->isChecked());

    FluentTheme::Apply(*qApp, s);
    emit settingsApplied();

    labelStatus_->setText(startupOk
        ? TR("Đã áp dụng.")
        : TR("Đã áp dụng, nhưng không ghi được mục khởi động cùng Windows vào Registry."));

    if (needsRestart &&
        QMessageBox::question(
            this, TR("Cần khởi động lại"),
            TR("Ngôn ngữ / độ phóng cửa sổ chỉ có hiệu lực sau khi khởi động lại app. Khởi động lại ngay?"),
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
    {
        emit restartRequested();
        accept();
    }
}

} // namespace ui
