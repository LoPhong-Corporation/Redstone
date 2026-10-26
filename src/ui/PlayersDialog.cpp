#include "ui/PlayersDialog.hpp"

#include "server/MinecraftServer.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"

#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace ui
{
namespace
{

QJsonArray ReadJsonArray(const core::ServerConfig& cfg, const wchar_t* fileName)
{
    QFile file(QString::fromStdWString((cfg.directory / fileName).wstring()));
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isArray() ? doc.array() : QJsonArray();
}

QTableWidget* MakeTable(const QStringList& headers, QWidget* parent)
{
    auto* table = new QTableWidget(0, static_cast<int>(headers.size()), parent);
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setShowGrid(false);
    return table;
}

void FillTable(QTableWidget* table, const QList<QStringList>& rows)
{
    table->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < rows.size(); ++r)
    {
        for (int c = 0; c < rows[r].size(); ++c)
        {
            table->setItem(r, c, new QTableWidgetItem(rows[r][c]));
        }
    }
    table->resizeColumnToContents(0);
}

// Text of column `col` in the selected row, or empty.
QString SelectedCell(const QTableWidget* table, int col)
{
    const auto selected = table->selectedItems();
    if (selected.isEmpty())
    {
        return QString();
    }
    const auto* item = table->item(selected.first()->row(), col);
    return item != nullptr ? item->text() : QString();
}

QString OneLine(QString text)
{
    text.replace(QLatin1Char('\r'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed();
}

QPushButton* MakeButton(const QString& text, const char* objectName, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setObjectName(QString::fromLatin1(objectName));
    return b;
}

// A card-like container with a vertical layout.
QWidget* TabPage(QVBoxLayout*& layoutOut)
{
    auto* page = new QWidget();
    layoutOut = new QVBoxLayout(page);
    layoutOut->setContentsMargins(0, 12, 0, 0);
    layoutOut->setSpacing(10);
    return page;
}

} // namespace

PlayersDialog::PlayersDialog(std::shared_ptr<server::MinecraftServer> server, QWidget* parent)
    : QDialog(parent)
    , server_(std::move(server))
{
    setWindowTitle(QStringLiteral("Người chơi — %1").arg(QString::fromStdWString(server_->GetConfig().name)));
    resize(860, 640);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(10);

    auto* header = new QVBoxLayout();
    AddPageHeader(header, QStringLiteral("Người chơi"), QString());
    root->addLayout(header);
    labelSummary_ = new QLabel(this);
    labelSummary_->setObjectName(QStringLiteral("subtitleLabel"));
    root->addWidget(labelSummary_);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(BuildOnlineTab(), QStringLiteral("Đang online"));
    tabs->addTab(BuildWhitelistTab(), QStringLiteral("Whitelist"));
    tabs->addTab(BuildOpsTab(), QStringLiteral("Operators"));
    tabs->addTab(BuildBansTab(), QStringLiteral("Bị cấm"));
    tabs->addTab(BuildQuickTab(), QStringLiteral("Thiết lập nhanh"));
    root->addWidget(tabs, 1);

    auto* footer = new QHBoxLayout();
    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelStatus_->setWordWrap(true);
    footer->addWidget(labelStatus_, 1);
    auto* closeButton = MakeButton(QStringLiteral("Đóng"), "btnNeutral", this);
    footer->addWidget(closeButton);
    root->addLayout(footer);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    timer_ = new QTimer(this);
    timer_->setInterval(2000);
    connect(timer_, &QTimer::timeout, this, [this]() {
        RefreshOnline();
        UpdateEnabledState();
    });
    timer_->start();

    RefreshOnline();
    RefreshFiles();
    UpdateEnabledState();
}

// ---------------------------------------------------------------- tabs

QWidget* PlayersDialog::BuildOnlineTab()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = TabPage(layout);

    auto* body = new QHBoxLayout();
    body->setSpacing(16);

    auto* left = new QVBoxLayout();
    left->addWidget(MakeSectionLabel(QStringLiteral("ĐANG ONLINE")));
    listOnline_ = new QListWidget(page);
    left->addWidget(listOnline_, 1);
    body->addLayout(left, 1);

    auto* right = new QVBoxLayout();
    right->setSpacing(8);
    right->addWidget(MakeSectionLabel(QStringLiteral("THAO TÁC")));
    editOnlineName_ = new QLineEdit(page);
    editOnlineName_->setPlaceholderText(QStringLiteral("Chọn trong danh sách hoặc nhập tên..."));
    right->addWidget(editOnlineName_);

    auto* opRow = new QHBoxLayout();
    auto* opButton = MakeButton(QStringLiteral("Op"), "btnNeutral", page);
    auto* deopButton = MakeButton(QStringLiteral("Deop"), "btnNeutral", page);
    opRow->addWidget(opButton);
    opRow->addWidget(deopButton);
    right->addLayout(opRow);

    auto* wlRow = new QHBoxLayout();
    auto* wlAdd = MakeButton(QStringLiteral("+ Whitelist"), "btnNeutral", page);
    auto* wlRemove = MakeButton(QStringLiteral("− Whitelist"), "btnNeutral", page);
    wlRow->addWidget(wlAdd);
    wlRow->addWidget(wlRemove);
    right->addLayout(wlRow);

    auto* modRow = new QHBoxLayout();
    auto* kickButton = MakeButton(QStringLiteral("Kick"), "btnNeutral", page);
    auto* banButton = MakeButton(QStringLiteral("Ban"), "btnStop", page);
    modRow->addWidget(kickButton);
    modRow->addWidget(banButton);
    right->addLayout(modRow);

    auto* gmRow = new QHBoxLayout();
    comboGamemode_ = new QComboBox(page);
    comboGamemode_->addItems({QStringLiteral("survival"), QStringLiteral("creative"), QStringLiteral("adventure"), QStringLiteral("spectator")});
    auto* gmButton = MakeButton(QStringLiteral("Đặt gamemode"), "btnNeutral", page);
    gmRow->addWidget(comboGamemode_, 1);
    gmRow->addWidget(gmButton);
    right->addLayout(gmRow);

    auto* msgButton = MakeButton(QStringLiteral("Gửi tin nhắn riêng..."), "btnNeutral", page);
    right->addWidget(msgButton);
    right->addStretch(1);
    body->addLayout(right, 1);
    layout->addLayout(body, 1);

    liveButtons_ << opButton << deopButton << wlAdd << wlRemove << kickButton << banButton << gmButton << msgButton;

    connect(listOnline_, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        if (!text.isEmpty())
        {
            editOnlineName_->setText(text);
        }
    });
    connect(opButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (!n.isEmpty()) Send(QStringLiteral("op %1").arg(n));
    });
    connect(deopButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (!n.isEmpty()) Send(QStringLiteral("deop %1").arg(n));
    });
    connect(wlAdd, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (!n.isEmpty()) Send(QStringLiteral("whitelist add %1").arg(n));
    });
    connect(wlRemove, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (!n.isEmpty()) Send(QStringLiteral("whitelist remove %1").arg(n));
    });
    connect(kickButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (n.isEmpty()) return;
        bool ok = false;
        const QString reason = QInputDialog::getText(
            this, QStringLiteral("Kick %1").arg(n), QStringLiteral("Lý do (có thể để trống):"), QLineEdit::Normal, QString(), &ok);
        if (ok) Send(QStringLiteral("kick %1 %2").arg(n, OneLine(reason)));
    });
    connect(banButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (n.isEmpty()) return;
        bool ok = false;
        const QString reason = QInputDialog::getText(
            this, QStringLiteral("Ban %1").arg(n), QStringLiteral("Lý do (có thể để trống):"), QLineEdit::Normal, QString(), &ok);
        if (ok) Send(QStringLiteral("ban %1 %2").arg(n, OneLine(reason)));
    });
    connect(gmButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (!n.isEmpty()) Send(QStringLiteral("gamemode %1 %2").arg(comboGamemode_->currentText(), n));
    });
    connect(msgButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOnlineName_->text());
        if (n.isEmpty()) return;
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, QStringLiteral("Nhắn cho %1").arg(n), QStringLiteral("Nội dung:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !OneLine(text).isEmpty()) Send(QStringLiteral("msg %1 %2").arg(n, OneLine(text)));
    });
    return page;
}

QWidget* PlayersDialog::BuildWhitelistTab()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = TabPage(layout);

    auto* top = new QHBoxLayout();
    editWhitelistName_ = new QLineEdit(page);
    editWhitelistName_->setPlaceholderText(QStringLiteral("Tên người chơi cần thêm..."));
    auto* addButton = MakeButton(QStringLiteral("Thêm"), "btnStart", page);
    auto* removeButton = MakeButton(QStringLiteral("Xoá mục chọn"), "btnNeutral", page);
    top->addWidget(editWhitelistName_, 1);
    top->addWidget(addButton);
    top->addWidget(removeButton);
    layout->addLayout(top);

    tableWhitelist_ = MakeTable({QStringLiteral("Tên"), QStringLiteral("UUID")}, page);
    layout->addWidget(tableWhitelist_, 1);

    auto* bottom = new QHBoxLayout();
    auto* onButton = MakeButton(QStringLiteral("Bật whitelist"), "btnNeutral", page);
    auto* offButton = MakeButton(QStringLiteral("Tắt whitelist"), "btnNeutral", page);
    auto* reloadButton = MakeButton(QStringLiteral("Reload"), "btnNeutral", page);
    bottom->addWidget(onButton);
    bottom->addWidget(offButton);
    bottom->addWidget(reloadButton);
    bottom->addStretch(1);
    layout->addLayout(bottom);

    liveButtons_ << addButton << removeButton << onButton << offButton << reloadButton;

    connect(addButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editWhitelistName_->text());
        if (!n.isEmpty() && Send(QStringLiteral("whitelist add %1").arg(n))) editWhitelistName_->clear();
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(SelectedCell(tableWhitelist_, 0));
        if (!n.isEmpty()) Send(QStringLiteral("whitelist remove %1").arg(n));
    });
    connect(onButton, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("whitelist on")); });
    connect(offButton, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("whitelist off")); });
    connect(reloadButton, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("whitelist reload")); });
    return page;
}

QWidget* PlayersDialog::BuildOpsTab()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = TabPage(layout);

    auto* top = new QHBoxLayout();
    editOpName_ = new QLineEdit(page);
    editOpName_->setPlaceholderText(QStringLiteral("Tên người chơi cần cấp OP..."));
    auto* addButton = MakeButton(QStringLiteral("Op"), "btnStart", page);
    auto* removeButton = MakeButton(QStringLiteral("Deop mục chọn"), "btnNeutral", page);
    top->addWidget(editOpName_, 1);
    top->addWidget(addButton);
    top->addWidget(removeButton);
    layout->addLayout(top);

    tableOps_ = MakeTable({QStringLiteral("Tên"), QStringLiteral("Cấp quyền"), QStringLiteral("Bỏ qua giới hạn người chơi")}, page);
    layout->addWidget(tableOps_, 1);

    auto* hint = new QLabel(
        QStringLiteral("Cấp quyền mặc định của OP mới đặt trong server.properties (op-permission-level). "
                       "Muốn đổi cấp riêng từng người, sửa ops.json khi server đang tắt."),
        page);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    liveButtons_ << addButton << removeButton;
    connect(addButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(editOpName_->text());
        if (!n.isEmpty() && Send(QStringLiteral("op %1").arg(n))) editOpName_->clear();
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const QString n = RequirePlayerName(SelectedCell(tableOps_, 0));
        if (!n.isEmpty()) Send(QStringLiteral("deop %1").arg(n));
    });
    return page;
}

QWidget* PlayersDialog::BuildBansTab()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = TabPage(layout);

    auto* top = new QHBoxLayout();
    editBanTarget_ = new QLineEdit(page);
    editBanTarget_->setPlaceholderText(QStringLiteral("Tên người chơi hoặc địa chỉ IP..."));
    editBanReason_ = new QLineEdit(page);
    editBanReason_->setPlaceholderText(QStringLiteral("Lý do (tuỳ chọn)"));
    auto* banButton = MakeButton(QStringLiteral("Ban"), "btnStop", page);
    top->addWidget(editBanTarget_, 2);
    top->addWidget(editBanReason_, 2);
    top->addWidget(banButton);
    layout->addLayout(top);

    tableBans_ = MakeTable({QStringLiteral("Đối tượng"), QStringLiteral("Loại"), QStringLiteral("Lý do"), QStringLiteral("Ngày cấm"), QStringLiteral("Bởi")}, page);
    layout->addWidget(tableBans_, 1);

    auto* bottom = new QHBoxLayout();
    auto* unbanButton = MakeButton(QStringLiteral("Gỡ cấm mục chọn"), "btnStart", page);
    bottom->addWidget(unbanButton);
    bottom->addStretch(1);
    layout->addLayout(bottom);

    liveButtons_ << banButton << unbanButton;

    connect(banButton, &QPushButton::clicked, this, [this]() {
        const QString target = OneLine(editBanTarget_->text());
        static const QRegularExpression ipPattern(QStringLiteral("^[0-9a-fA-F:.]{3,45}$"));
        const bool looksLikeIp = target.contains(QLatin1Char('.')) && ipPattern.match(target).hasMatch() &&
            target.at(0).isDigit();
        const QString reason = OneLine(editBanReason_->text());
        if (looksLikeIp)
        {
            if (Send(QStringLiteral("ban-ip %1 %2").arg(target, reason))) { editBanTarget_->clear(); editBanReason_->clear(); }
            return;
        }
        const QString n = RequirePlayerName(target);
        if (!n.isEmpty() && Send(QStringLiteral("ban %1 %2").arg(n, reason)))
        {
            editBanTarget_->clear();
            editBanReason_->clear();
        }
    });
    connect(unbanButton, &QPushButton::clicked, this, [this]() {
        const QString target = SelectedCell(tableBans_, 0);
        const QString type = SelectedCell(tableBans_, 1);
        if (target.isEmpty())
        {
            SetStatus(QStringLiteral("Chọn một dòng trong danh sách trước."));
            return;
        }
        if (type == QStringLiteral("IP"))
        {
            static const QRegularExpression ipPattern(QStringLiteral("^[0-9a-fA-F:.]{3,45}$"));
            if (ipPattern.match(target).hasMatch()) Send(QStringLiteral("pardon-ip %1").arg(target));
        }
        else
        {
            const QString n = RequirePlayerName(target);
            if (!n.isEmpty()) Send(QStringLiteral("pardon %1").arg(n));
        }
    });
    return page;
}

QWidget* PlayersDialog::BuildQuickTab()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = TabPage(layout);

    editBroadcast_ = new QLineEdit();
    editBroadcast_->setPlaceholderText(QStringLiteral("Nội dung thông báo tới toàn server..."));
    editBroadcast_->setMinimumWidth(260);
    auto* sendButton = MakeButton(QStringLiteral("Gửi"), "btnStart", page);
    auto* broadcastBox = new QWidget();
    auto* broadcastLayout = new QHBoxLayout(broadcastBox);
    broadcastLayout->setContentsMargins(0, 0, 0, 0);
    broadcastLayout->addWidget(editBroadcast_, 1);
    broadcastLayout->addWidget(sendButton);
    layout->addWidget(MakeSettingRow(QStringLiteral("Thông báo toàn server"), QStringLiteral("Gửi bằng lệnh say, hiện trong chat cho mọi người."), broadcastBox));

    comboDefaultGamemode_ = new QComboBox();
    comboDefaultGamemode_->addItems({QStringLiteral("survival"), QStringLiteral("creative"), QStringLiteral("adventure"), QStringLiteral("spectator")});
    auto* gmApply = MakeButton(QStringLiteral("Áp dụng"), "btnNeutral", page);
    auto* gmBox = new QWidget();
    auto* gmLayout = new QHBoxLayout(gmBox);
    gmLayout->setContentsMargins(0, 0, 0, 0);
    gmLayout->addWidget(comboDefaultGamemode_);
    gmLayout->addWidget(gmApply);
    layout->addWidget(MakeSettingRow(QStringLiteral("Gamemode mặc định cho người mới"), QStringLiteral("Có hiệu lực ngay, không cần restart (lệnh defaultgamemode)."), gmBox));

    comboDifficulty_ = new QComboBox();
    comboDifficulty_->addItems({QStringLiteral("peaceful"), QStringLiteral("easy"), QStringLiteral("normal"), QStringLiteral("hard")});
    auto* diffApply = MakeButton(QStringLiteral("Áp dụng"), "btnNeutral", page);
    auto* diffBox = new QWidget();
    auto* diffLayout = new QHBoxLayout(diffBox);
    diffLayout->setContentsMargins(0, 0, 0, 0);
    diffLayout->addWidget(comboDifficulty_);
    diffLayout->addWidget(diffApply);
    layout->addWidget(MakeSettingRow(QStringLiteral("Độ khó"), QStringLiteral("Đổi ngay trong game (nhớ lưu vào server.properties nếu muốn giữ lâu dài)."), diffBox));

    spinIdleTimeout_ = new QSpinBox();
    spinIdleTimeout_->setRange(0, 1440);
    spinIdleTimeout_->setSuffix(QStringLiteral(" phút"));
    auto* idleApply = MakeButton(QStringLiteral("Áp dụng"), "btnNeutral", page);
    auto* idleBox = new QWidget();
    auto* idleLayout = new QHBoxLayout(idleBox);
    idleLayout->setContentsMargins(0, 0, 0, 0);
    idleLayout->addWidget(spinIdleTimeout_);
    idleLayout->addWidget(idleApply);
    layout->addWidget(MakeSettingRow(QStringLiteral("Kick người chơi AFK"), QStringLiteral("0 = không kick."), idleBox));

    auto* worldBox = new QWidget();
    auto* worldLayout = new QHBoxLayout(worldBox);
    worldLayout->setContentsMargins(0, 0, 0, 0);
    auto* dayButton = MakeButton(QStringLiteral("Ban ngày"), "btnNeutral", page);
    auto* nightButton = MakeButton(QStringLiteral("Ban đêm"), "btnNeutral", page);
    auto* clearButton = MakeButton(QStringLiteral("Trời quang"), "btnNeutral", page);
    worldLayout->addWidget(dayButton);
    worldLayout->addWidget(nightButton);
    worldLayout->addWidget(clearButton);
    layout->addWidget(MakeSettingRow(QStringLiteral("Thời gian & thời tiết"), QString(), worldBox));

    layout->addStretch(1);

    liveButtons_ << sendButton << gmApply << diffApply << idleApply << dayButton << nightButton << clearButton;

    connect(sendButton, &QPushButton::clicked, this, [this]() {
        const QString text = OneLine(editBroadcast_->text());
        if (!text.isEmpty() && Send(QStringLiteral("say %1").arg(text))) editBroadcast_->clear();
    });
    connect(editBroadcast_, &QLineEdit::returnPressed, sendButton, &QPushButton::click);
    connect(gmApply, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("defaultgamemode %1").arg(comboDefaultGamemode_->currentText())); });
    connect(diffApply, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("difficulty %1").arg(comboDifficulty_->currentText())); });
    connect(idleApply, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("setidletimeout %1").arg(spinIdleTimeout_->value())); });
    connect(dayButton, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("time set day")); });
    connect(nightButton, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("time set night")); });
    connect(clearButton, &QPushButton::clicked, this, [this]() { Send(QStringLiteral("weather clear")); });
    return page;
}

// ------------------------------------------------------------- helpers

void PlayersDialog::SetStatus(const QString& text)
{
    labelStatus_->setText(text);
}

QString PlayersDialog::RequirePlayerName(const QString& raw)
{
    // Java names are 3-16 chars of [A-Za-z0-9_]; Bedrock/Floodgate names may
    // carry a leading '.' and be longer. Also guarantees no whitespace/newline
    // can smuggle a second console command in.
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_.]{1,32}$"));
    const QString name = raw.trimmed();
    if (!pattern.match(name).hasMatch())
    {
        SetStatus(QStringLiteral("Tên người chơi không hợp lệ (chỉ gồm chữ, số, _ hoặc .)."));
        return QString();
    }
    return name;
}

bool PlayersDialog::Send(const QString& command)
{
    if (!server_->SendCommand(command.trimmed().toStdWString()))
    {
        SetStatus(QStringLiteral("Không gửi được — server chưa chạy. Hãy Start server rồi thử lại."));
        return false;
    }
    SetStatus(QStringLiteral("Đã gửi: %1").arg(command.trimmed()));
    // Minecraft rewrites the JSON lists a moment after the command executes.
    QTimer::singleShot(1200, this, [this]() { RefreshFiles(); });
    return true;
}

void PlayersDialog::UpdateEnabledState()
{
    const bool running = server_->GetState() == core::ServerState::Running;
    for (QPushButton* b : liveButtons_)
    {
        b->setEnabled(running);
    }
    if (!running && labelStatus_->text().isEmpty())
    {
        SetStatus(QStringLiteral("Server chưa chạy — chỉ xem được danh sách; các thao tác sẽ bật khi server Running."));
    }
}

void PlayersDialog::RefreshOnline()
{
    const auto players = server_->GetOnlinePlayers();
    const QString selected = listOnline_->currentItem() != nullptr ? listOnline_->currentItem()->text() : QString();

    listOnline_->blockSignals(true);
    listOnline_->clear();
    for (const auto& p : players)
    {
        listOnline_->addItem(QString::fromStdWString(p));
    }
    const auto matches = listOnline_->findItems(selected, Qt::MatchExactly);
    if (!matches.isEmpty())
    {
        listOnline_->setCurrentItem(matches.first());
    }
    listOnline_->blockSignals(false);

    labelSummary_->setText(QStringLiteral("%1 người đang online").arg(players.size()));
}

void PlayersDialog::RefreshFiles()
{
    const core::ServerConfig cfg = server_->GetConfig();

    QList<QStringList> rows;
    for (const auto& v : ReadJsonArray(cfg, L"whitelist.json"))
    {
        const QJsonObject o = v.toObject();
        rows.append({o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("uuid")).toString()});
    }
    FillTable(tableWhitelist_, rows);

    rows.clear();
    for (const auto& v : ReadJsonArray(cfg, L"ops.json"))
    {
        const QJsonObject o = v.toObject();
        rows.append({o.value(QStringLiteral("name")).toString(),
                     QString::number(o.value(QStringLiteral("level")).toInt()),
                     o.value(QStringLiteral("bypassesPlayerLimit")).toBool() ? QStringLiteral("Có") : QStringLiteral("Không")});
    }
    FillTable(tableOps_, rows);

    rows.clear();
    for (const auto& v : ReadJsonArray(cfg, L"banned-players.json"))
    {
        const QJsonObject o = v.toObject();
        rows.append({o.value(QStringLiteral("name")).toString(), QStringLiteral("Người chơi"),
                     o.value(QStringLiteral("reason")).toString(), o.value(QStringLiteral("created")).toString(),
                     o.value(QStringLiteral("source")).toString()});
    }
    for (const auto& v : ReadJsonArray(cfg, L"banned-ips.json"))
    {
        const QJsonObject o = v.toObject();
        rows.append({o.value(QStringLiteral("ip")).toString(), QStringLiteral("IP"),
                     o.value(QStringLiteral("reason")).toString(), o.value(QStringLiteral("created")).toString(),
                     o.value(QStringLiteral("source")).toString()});
    }
    FillTable(tableBans_, rows);
}

} // namespace ui
