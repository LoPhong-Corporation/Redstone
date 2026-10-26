#include "ui/LibraryDialog.hpp"

#include "config/ServerProperties.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"

#include <QBuffer>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

namespace ui
{

struct LibraryDialog::PlanState
{
    QList<PlanItem> items;
    QSet<QString> visited; // dependency keys already requested
    QSet<QString> added;   // project ids already in the plan
    QStringList warnings;
    int pending = 0;
    std::function<void()> done;
};

namespace
{

constexpr int kPageSize = 20;
constexpr int kMaxDependencyDepth = 3;

QString Lower(const std::wstring& s)
{
    return QString::fromStdWString(s).trimmed().toLower();
}

QString HumanCount(qint64 n)
{
    if (n >= 1000000) return QStringLiteral("%1M").arg(QString::number(n / 1000000.0, 'f', 1));
    if (n >= 1000) return QStringLiteral("%1K").arg(QString::number(n / 1000.0, 'f', 1));
    return QString::number(n);
}

QString HumanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024) return QStringLiteral("%1 MB").arg(QString::number(bytes / 1048576.0, 'f', 1));
    if (bytes >= 1024) return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0));
    return QStringLiteral("%1 B").arg(bytes);
}

QString HumanDate(const QString& iso)
{
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    return dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy")) : QString();
}

QPixmap RoundedIcon(const QByteArray& data, int size)
{
    QPixmap src;
    if (!src.loadFromData(data))
    {
        return QPixmap();
    }
    src = src.scaled(size * 2, size * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap out(size * 2, size * 2);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size * 2, size * 2), 14, 14);
    p.setClipPath(clip);
    p.drawPixmap((size * 2 - src.width()) / 2, (size * 2 - src.height()) / 2, src);
    p.end();
    out.setDevicePixelRatio(2.0);
    return out;
}

QString TypeLabel(const QString& type)
{
    if (type == QLatin1String("mod")) return QStringLiteral("Mod");
    if (type == QLatin1String("plugin")) return QStringLiteral("Plugin");
    if (type == QLatin1String("datapack")) return QStringLiteral("Datapack");
    if (type == QLatin1String("resourcepack")) return QStringLiteral("Resource pack");
    if (type == QLatin1String("shader")) return QStringLiteral("Shader");
    if (type == QLatin1String("modpack")) return QStringLiteral("Modpack");
    return type;
}

// Version like "1.21.4" from the config, the jar name, or the server's latest.log.
QString DetectMinecraftVersion(const core::ServerConfig& cfg)
{
    if (!cfg.minecraftVersion.empty())
    {
        return QString::fromStdWString(cfg.minecraftVersion).trimmed();
    }

    QFile log(QString::fromStdWString((cfg.directory / L"logs" / L"latest.log").wstring()));
    if (log.open(QIODevice::ReadOnly))
    {
        const QString head = QString::fromUtf8(log.read(96 * 1024));
        static const QRegularExpression logPattern(QStringLiteral("Starting minecraft server version (\\d+(?:\\.\\d+){1,2})"));
        const auto m = logPattern.match(head);
        if (m.hasMatch())
        {
            return m.captured(1);
        }
    }

    static const QRegularExpression jarPattern(QStringLiteral("(\\d+\\.\\d+(?:\\.\\d+)?)"));
    const auto m = jarPattern.match(QString::fromStdWString(cfg.serverJar.filename().wstring()));
    return m.hasMatch() ? m.captured(1) : QString();
}

QString DefaultTypeFor(const QString& serverType)
{
    static const QStringList pluginPlatforms = {
        QStringLiteral("paper"), QStringLiteral("spigot"), QStringLiteral("purpur"), QStringLiteral("folia"), QStringLiteral("bukkit")};
    return pluginPlatforms.contains(serverType) ? QStringLiteral("plugin") : QStringLiteral("mod");
}

} // namespace

LibraryDialog::LibraryDialog(core::ServerConfig targetServer, QWidget* parent)
    : QDialog(parent)
    , targetServer_(std::move(targetServer))
    , client_(new net::ModrinthClient(this))
{
    setWindowTitle(QStringLiteral("Thư viện Mod / Plugin — %1").arg(QString::fromStdWString(targetServer_.name)));
    resize(1080, 720);

    const QString serverType = Lower(targetServer_.serverType);
    const QString detectedVersion = DetectMinecraftVersion(targetServer_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 14);
    root->setSpacing(12);

    auto* headerBox = new QVBoxLayout();
    headerBox->setSpacing(2);
    AddPageHeader(
        headerBox,
        QStringLiteral("Thư viện Mod / Plugin"),
        QStringLiteral("Server: %1  ·  Loại: %2  ·  Minecraft: %3")
            .arg(QString::fromStdWString(targetServer_.name),
                 QString::fromStdWString(targetServer_.serverType),
                 detectedVersion.isEmpty() ? QStringLiteral("chưa rõ") : detectedVersion));
    root->addLayout(headerBox);

    // ---- Filter bar --------------------------------------------------
    auto* filterCard = new QFrame(this);
    filterCard->setObjectName(QStringLiteral("card"));
    auto* filterLayout = new QVBoxLayout(filterCard);
    filterLayout->setContentsMargins(14, 12, 14, 12);
    filterLayout->setSpacing(10);

    auto* row1 = new QHBoxLayout();
    row1->setSpacing(8);
    comboType_ = new QComboBox(filterCard);
    comboType_->addItem(QStringLiteral("Mod"), QStringLiteral("mod"));
    comboType_->addItem(QStringLiteral("Plugin"), QStringLiteral("plugin"));
    comboType_->addItem(QStringLiteral("Datapack"), QStringLiteral("datapack"));
    comboType_->addItem(QStringLiteral("Resource pack"), QStringLiteral("resourcepack"));
    comboType_->addItem(QStringLiteral("Shader"), QStringLiteral("shader"));
    comboType_->addItem(QStringLiteral("Modpack"), QStringLiteral("modpack"));
    comboType_->setCurrentIndex(comboType_->findData(DefaultTypeFor(serverType)));
    row1->addWidget(comboType_);

    editQuery_ = new QLineEdit(filterCard);
    editQuery_->setPlaceholderText(QStringLiteral("Tìm trên Modrinth... (vd: sodium, worldedit, terralith) — để trống để xem phổ biến nhất"));
    row1->addWidget(editQuery_, 1);

    comboSort_ = new QComboBox(filterCard);
    comboSort_->addItem(QStringLiteral("Liên quan"), QStringLiteral("relevance"));
    comboSort_->addItem(QStringLiteral("Nhiều lượt tải"), QStringLiteral("downloads"));
    comboSort_->addItem(QStringLiteral("Nhiều người theo dõi"), QStringLiteral("follows"));
    comboSort_->addItem(QStringLiteral("Mới nhất"), QStringLiteral("newest"));
    comboSort_->addItem(QStringLiteral("Cập nhật gần đây"), QStringLiteral("updated"));
    row1->addWidget(comboSort_);

    buttonSearch_ = new QPushButton(QStringLiteral("Tìm kiếm"), filterCard);
    buttonSearch_->setObjectName(QStringLiteral("btnStart"));
    row1->addWidget(buttonSearch_);
    filterLayout->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    row2->setSpacing(10);
    row2->addWidget(new QLabel(QStringLiteral("Phiên bản Minecraft:"), filterCard));
    editGameVersion_ = new QLineEdit(detectedVersion, filterCard);
    editGameVersion_->setPlaceholderText(QStringLiteral("vd: 1.21.4"));
    editGameVersion_->setFixedWidth(110);
    row2->addWidget(editGameVersion_);
    row2->addSpacing(12);
    toggleCompatible_ = new ToggleSwitch(filterCard);
    toggleCompatible_->setChecked(true);
    row2->addWidget(toggleCompatible_);
    row2->addWidget(new QLabel(QStringLiteral("Chỉ bản tương thích với server"), filterCard));
    row2->addSpacing(12);
    toggleServerOnly_ = new ToggleSwitch(filterCard);
    toggleServerOnly_->setChecked(true);
    row2->addWidget(toggleServerOnly_);
    row2->addWidget(new QLabel(QStringLiteral("Ẩn mod chỉ chạy ở client"), filterCard));
    row2->addStretch(1);
    filterLayout->addLayout(row2);
    root->addWidget(filterCard);

    // ---- Results + detail --------------------------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(12);

    auto* leftCard = new QFrame(splitter);
    leftCard->setObjectName(QStringLiteral("card"));
    auto* leftLayout = new QVBoxLayout(leftCard);
    leftLayout->setContentsMargins(10, 10, 10, 10);
    leftLayout->setSpacing(8);
    listResults_ = new QListWidget(leftCard);
    listResults_->setIconSize(QSize(40, 40));
    listResults_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    leftLayout->addWidget(listResults_, 1);
    buttonMore_ = new QPushButton(QStringLiteral("Tải thêm kết quả"), leftCard);
    buttonMore_->setEnabled(false);
    leftLayout->addWidget(buttonMore_);
    splitter->addWidget(leftCard);

    auto* rightCard = new QFrame(splitter);
    rightCard->setObjectName(QStringLiteral("card"));
    auto* rightLayout = new QVBoxLayout(rightCard);
    rightLayout->setContentsMargins(18, 16, 18, 16);
    rightLayout->setSpacing(8);

    labelDetailTitle_ = new QLabel(rightCard);
    labelDetailTitle_->setObjectName(QStringLiteral("titleLabel"));
    labelDetailTitle_->setWordWrap(true);
    rightLayout->addWidget(labelDetailTitle_);

    labelDetailMeta_ = new QLabel(rightCard);
    labelDetailMeta_->setObjectName(QStringLiteral("subtitleLabel"));
    labelDetailMeta_->setWordWrap(true);
    rightLayout->addWidget(labelDetailMeta_);

    labelDetailDescription_ = new QLabel(rightCard);
    labelDetailDescription_->setWordWrap(true);
    labelDetailDescription_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    rightLayout->addWidget(labelDetailDescription_);

    labelDetailTags_ = new QLabel(rightCard);
    labelDetailTags_->setObjectName(QStringLiteral("hintLabel"));
    labelDetailTags_->setWordWrap(true);
    rightLayout->addWidget(labelDetailTags_);

    rightLayout->addSpacing(6);
    rightLayout->addWidget(MakeSectionLabel(QStringLiteral("SẼ CÀI ĐẶT"), rightCard));
    labelDetailVersion_ = new QLabel(rightCard);
    labelDetailVersion_->setWordWrap(true);
    rightLayout->addWidget(labelDetailVersion_);
    labelDetailDeps_ = new QLabel(rightCard);
    labelDetailDeps_->setObjectName(QStringLiteral("hintLabel"));
    labelDetailDeps_->setWordWrap(true);
    rightLayout->addWidget(labelDetailDeps_);
    rightLayout->addStretch(1);

    progress_ = new QProgressBar(rightCard);
    progress_->setRange(0, 100);
    progress_->setTextVisible(false);
    progress_->setVisible(false);
    rightLayout->addWidget(progress_);

    auto* actionRow = new QHBoxLayout();
    buttonOpenWeb_ = new QPushButton(QStringLiteral("Mở trên Modrinth"), rightCard);
    buttonOpenWeb_->setEnabled(false);
    buttonInstall_ = new QPushButton(QStringLiteral("Cài vào server"), rightCard);
    buttonInstall_->setObjectName(QStringLiteral("btnStart"));
    buttonInstall_->setEnabled(false);
    actionRow->addWidget(buttonOpenWeb_);
    actionRow->addStretch(1);
    actionRow->addWidget(buttonInstall_);
    rightLayout->addLayout(actionRow);
    splitter->addWidget(rightCard);

    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({560, 460});
    root->addWidget(splitter, 1);

    // ---- Footer --------------------------------------------------------
    auto* footer = new QHBoxLayout();
    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelStatus_->setWordWrap(true);
    footer->addWidget(labelStatus_, 1);
    auto* closeButton = new QPushButton(QStringLiteral("Đóng"), this);
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    footer->addWidget(closeButton);
    root->addLayout(footer);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttonSearch_, &QPushButton::clicked, this, [this]() { StartSearch(false); });
    connect(editQuery_, &QLineEdit::returnPressed, this, [this]() { StartSearch(false); });
    connect(comboType_, &QComboBox::currentIndexChanged, this, [this]() { StartSearch(false); });
    connect(comboSort_, &QComboBox::currentIndexChanged, this, [this]() { StartSearch(false); });
    connect(toggleCompatible_, &QAbstractButton::toggled, this, [this]() { StartSearch(false); });
    connect(toggleServerOnly_, &QAbstractButton::toggled, this, [this]() { StartSearch(false); });
    editGameVersion_->setProperty("lastValue", editGameVersion_->text());
    connect(editGameVersion_, &QLineEdit::editingFinished, this, [this]() {
        // editingFinished also fires on plain focus loss - only search when the text really changed.
        if (editGameVersion_->text() != editGameVersion_->property("lastValue").toString())
        {
            editGameVersion_->setProperty("lastValue", editGameVersion_->text());
            StartSearch(false);
        }
    });
    connect(buttonMore_, &QPushButton::clicked, this, [this]() { StartSearch(true); });
    connect(listResults_, &QListWidget::itemSelectionChanged, this, &LibraryDialog::OnSelectionChanged);
    connect(buttonInstall_, &QPushButton::clicked, this, &LibraryDialog::OnInstallClicked);
    connect(buttonOpenWeb_, &QPushButton::clicked, this, [this]() {
        if (const auto sel = CurrentSelection())
        {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://modrinth.com/%1/%2").arg(sel->projectType, sel->slug)));
        }
    });

    UpdateDetailPanel();
    StartSearch(false); // show the most relevant/popular items straight away
}

// ------------------------------------------------------------------ filters

QString LibraryDialog::CurrentType() const
{
    return comboType_->currentData().toString();
}

QStringList LibraryDialog::LoadersForType(const QString& projectType) const
{
    const QString st = Lower(targetServer_.serverType);
    if (projectType == QLatin1String("datapack"))
    {
        return {QStringLiteral("datapack")};
    }
    if (projectType == QLatin1String("plugin"))
    {
        if (st == QLatin1String("paper")) return {QStringLiteral("paper"), QStringLiteral("spigot"), QStringLiteral("bukkit")};
        if (st == QLatin1String("purpur")) return {QStringLiteral("purpur"), QStringLiteral("paper"), QStringLiteral("spigot"), QStringLiteral("bukkit")};
        if (st == QLatin1String("folia")) return {QStringLiteral("folia")};
        if (st == QLatin1String("spigot")) return {QStringLiteral("spigot"), QStringLiteral("bukkit")};
        if (st == QLatin1String("bukkit")) return {QStringLiteral("bukkit")};
        return {};
    }
    if (projectType == QLatin1String("mod") || projectType == QLatin1String("modpack"))
    {
        if (st == QLatin1String("fabric")) return {QStringLiteral("fabric")};
        if (st == QLatin1String("quilt")) return {QStringLiteral("quilt"), QStringLiteral("fabric")};
        if (st == QLatin1String("forge")) return {QStringLiteral("forge")};
        if (st == QLatin1String("neoforge")) return {QStringLiteral("neoforge")};
        return {};
    }
    return {}; // resource packs / shaders are not loader specific
}

QString LibraryDialog::GameVersionFilter() const
{
    return toggleCompatible_->isChecked() ? editGameVersion_->text().trimmed() : QString();
}

QStringList LibraryDialog::LoaderFilter() const
{
    return toggleCompatible_->isChecked() ? LoadersForType(CurrentType()) : QStringList();
}

QString LibraryDialog::DestinationFor(const QString& projectType, const QString& fileName) const
{
    const QString safe = net::ModrinthClient::SafeFileName(fileName);
    const QString serverDir = QString::fromStdWString(targetServer_.directory.wstring());
    QString subfolder;
    if (projectType == QLatin1String("mod")) subfolder = QStringLiteral("mods");
    else if (projectType == QLatin1String("plugin")) subfolder = QStringLiteral("plugins");
    else if (projectType == QLatin1String("resourcepack")) subfolder = QStringLiteral("resourcepacks");
    else if (projectType == QLatin1String("shader")) subfolder = QStringLiteral("shaderpacks");
    else if (projectType == QLatin1String("modpack")) subfolder = QStringLiteral("modpacks");
    else if (projectType == QLatin1String("datapack"))
    {
        const auto props = config::ReadServerProperties(targetServer_);
        const auto it = props.find(L"level-name");
        const QString level = (it != props.end() && !it->second.empty()) ? QString::fromStdWString(it->second) : QStringLiteral("world");
        subfolder = net::ModrinthClient::SafeFileName(level) + QStringLiteral("/datapacks");
    }
    return QDir(serverDir).filePath(subfolder + QStringLiteral("/") + safe);
}

// ------------------------------------------------------------------- search

void LibraryDialog::StartSearch(bool append)
{
    if (busy_)
    {
        return;
    }
    const int token = ++searchToken_;
    if (!append)
    {
        hits_.clear();
        listResults_->clear();
        itemsByProject_.clear();
        totalHits_ = 0;
        selectedVersion_.reset();
        UpdateDetailPanel();
    }

    net::SearchOptions options;
    options.query = editQuery_->text().trimmed();
    options.projectType = CurrentType();
    options.gameVersion = GameVersionFilter();
    options.loaders = LoaderFilter();
    options.serverSideOnly = toggleServerOnly_->isChecked();
    options.sortIndex = comboSort_->currentData().toString();
    options.offset = append ? static_cast<int>(hits_.size()) : 0;
    options.limit = kPageSize;

    buttonSearch_->setEnabled(false);
    buttonMore_->setEnabled(false);
    labelStatus_->setText(QStringLiteral("Đang tìm kiếm..."));

    client_->Search(options, [this, token, append](net::SearchResult result, QString error) {
        if (token != searchToken_)
        {
            return; // a newer search superseded this one
        }
        buttonSearch_->setEnabled(true);
        if (!error.isEmpty())
        {
            labelStatus_->setText(QStringLiteral("Lỗi: %1").arg(error));
            return;
        }
        totalHits_ = result.totalHits;
        PopulateResults(result.hits, append);
        if (hits_.isEmpty())
        {
            QString hint = QStringLiteral("Không tìm thấy kết quả nào.");
            if (toggleCompatible_->isChecked() && LoadersForType(CurrentType()).isEmpty() && CurrentType() == QLatin1String("plugin"))
            {
                hint += QStringLiteral(" Loại server \"%1\" thường không chạy plugin Bukkit/Paper.")
                            .arg(QString::fromStdWString(targetServer_.serverType));
            }
            labelStatus_->setText(hint);
        }
        else
        {
            labelStatus_->setText(QStringLiteral("Hiển thị %1 / %2 kết quả.").arg(hits_.size()).arg(totalHits_));
        }
        buttonMore_->setEnabled(hits_.size() < totalHits_);
    });
}

void LibraryDialog::PopulateResults(const QList<net::ModrinthProjectHit>& hits, bool append)
{
    Q_UNUSED(append);
    const int token = searchToken_;
    for (const auto& hit : hits)
    {
        hits_.push_back(hit);
        QString second = QStringLiteral("%1  ·  %2 lượt tải  ·  %3 theo dõi").arg(hit.author, HumanCount(hit.downloads), HumanCount(hit.follows));
        const QString updated = HumanDate(hit.dateModified);
        if (!updated.isEmpty())
        {
            second += QStringLiteral("  ·  cập nhật %1").arg(updated);
        }
        auto* item = new QListWidgetItem(FluentTheme::Icon(glyph::Puzzle, 28), hit.title + QStringLiteral("\n") + second, listResults_);
        item->setData(Qt::UserRole, hit.projectId);
        item->setSizeHint(QSize(0, 58));
        itemsByProject_.insert(hit.projectId, item);

        client_->FetchIcon(hit.iconUrl, [this, token, id = hit.projectId](QByteArray data) {
            if (token != searchToken_ || data.isEmpty())
            {
                return;
            }
            const QPixmap pm = RoundedIcon(data, 40);
            const auto it = itemsByProject_.constFind(id);
            if (!pm.isNull() && it != itemsByProject_.constEnd())
            {
                it.value()->setIcon(QIcon(pm));
            }
        });
    }
}

std::optional<net::ModrinthProjectHit> LibraryDialog::CurrentSelection() const
{
    const auto* item = listResults_->currentItem();
    if (item == nullptr)
    {
        return std::nullopt;
    }
    const QString id = item->data(Qt::UserRole).toString();
    for (const auto& hit : hits_)
    {
        if (hit.projectId == id)
        {
            return hit;
        }
    }
    return std::nullopt;
}

// ------------------------------------------------------------ detail panel

void LibraryDialog::OnSelectionChanged()
{
    selectedVersion_.reset();
    selectedVersionError_.clear();
    UpdateDetailPanel();
    if (CurrentSelection().has_value())
    {
        ResolveSelectedVersion();
    }
}

void LibraryDialog::ResolveSelectedVersion()
{
    const auto sel = CurrentSelection();
    if (!sel.has_value())
    {
        return;
    }
    const int token = ++selectionToken_;
    resolving_ = true;
    UpdateDetailPanel();

    client_->GetCompatibleVersion(
        sel->projectId, GameVersionFilter(), LoaderFilter(),
        [this, token](std::optional<net::ModrinthVersion> version, QString error) {
            if (token != selectionToken_)
            {
                return;
            }
            resolving_ = false;
            selectedVersion_ = version;
            selectedVersionError_ = error;
            UpdateDetailPanel();
        });
}

void LibraryDialog::UpdateDetailPanel()
{
    const auto sel = CurrentSelection();
    if (!sel.has_value())
    {
        labelDetailTitle_->setText(QStringLiteral("Chọn một mục để xem chi tiết"));
        labelDetailMeta_->clear();
        labelDetailDescription_->clear();
        labelDetailTags_->clear();
        labelDetailVersion_->clear();
        labelDetailDeps_->clear();
        buttonInstall_->setEnabled(false);
        buttonOpenWeb_->setEnabled(false);
        return;
    }

    labelDetailTitle_->setText(sel->title);
    labelDetailMeta_->setText(QStringLiteral("%1  ·  bởi %2").arg(TypeLabel(sel->projectType), sel->author));
    labelDetailDescription_->setText(sel->description);
    labelDetailTags_->setText(sel->categories.isEmpty() ? QString() : sel->categories.join(QStringLiteral("  ·  ")));
    buttonOpenWeb_->setEnabled(!sel->slug.isEmpty());

    labelDetailDeps_->clear();
    if (resolving_)
    {
        labelDetailVersion_->setText(QStringLiteral("Đang tìm phiên bản phù hợp..."));
        buttonInstall_->setEnabled(false);
        return;
    }
    if (!selectedVersion_.has_value())
    {
        labelDetailVersion_->setText(selectedVersionError_.isEmpty() ? QString() : selectedVersionError_);
        buttonInstall_->setEnabled(false);
        return;
    }

    const net::ModrinthVersion& v = *selectedVersion_;
    const auto file = v.PrimaryFile();
    QString text = QStringLiteral("<b>%1</b>  (%2)").arg(v.versionNumber.toHtmlEscaped(), v.versionType);
    if (file.has_value())
    {
        text += QStringLiteral("<br>%1  ·  %2").arg(file->filename.toHtmlEscaped(), HumanSize(file->size));
        const QString dest = DestinationFor(sel->projectType, file->filename);
        if (QFileInfo::exists(dest))
        {
            text += QStringLiteral("<br><i>File này đã có trong server — sẽ được ghi đè.</i>");
        }
    }
    if (v.versionType != QLatin1String("release"))
    {
        text += QStringLiteral("<br><i>Đây là bản %1, có thể chưa ổn định.</i>").arg(v.versionType);
    }
    if (!toggleCompatible_->isChecked())
    {
        text += QStringLiteral("<br><i>Đang tắt lọc tương thích — bản này có thể không chạy trên server của bạn.</i>");
    }
    labelDetailVersion_->setText(text);

    int required = 0;
    for (const auto& dep : v.dependencies)
    {
        if (dep.type == QLatin1String("required"))
        {
            ++required;
        }
    }
    if (required > 0)
    {
        labelDetailDeps_->setText(QStringLiteral("Cần %1 phụ thuộc bắt buộc — sẽ được kiểm tra và cài kèm khi bạn bấm Cài.").arg(required));
    }
    if (sel->projectType == QLatin1String("modpack"))
    {
        labelDetailDeps_->setText(labelDetailDeps_->text() + QStringLiteral("\nModpack chỉ được tải về thư mục modpacks/ — chưa tự giải nén/cài từng mod."));
    }
    buttonInstall_->setText(sel->projectType == QLatin1String("modpack") ? QStringLiteral("Tải modpack") : QStringLiteral("Cài vào server"));
    buttonInstall_->setEnabled(!busy_ && file.has_value());
}

// ------------------------------------------------------------------ install

void LibraryDialog::SetBusy(bool busy)
{
    busy_ = busy;
    buttonSearch_->setEnabled(!busy);
    buttonMore_->setEnabled(!busy && hits_.size() < totalHits_);
    listResults_->setEnabled(!busy);
    comboType_->setEnabled(!busy);
    buttonInstall_->setEnabled(!busy && selectedVersion_.has_value());
    progress_->setVisible(busy);
    if (busy)
    {
        progress_->setRange(0, 0); // indeterminate until the first progress callback
    }
}

void LibraryDialog::OnInstallClicked()
{
    const auto sel = CurrentSelection();
    if (!sel.has_value() || !selectedVersion_.has_value() || busy_)
    {
        return;
    }
    const auto mainFile = selectedVersion_->PrimaryFile();
    const QString mainName = net::ModrinthClient::SafeFileName(mainFile ? mainFile->filename : QString());
    if (!mainFile.has_value() || mainName.isEmpty())
    {
        labelStatus_->setText(QStringLiteral("Tên file từ Modrinth không hợp lệ — đã huỷ cài đặt."));
        return;
    }

    SetBusy(true);
    labelStatus_->setText(QStringLiteral("Đang kiểm tra phụ thuộc..."));

    auto state = std::make_shared<PlanState>();
    PlanItem mainItem;
    mainItem.title = sel->title;
    mainItem.projectId = sel->projectId;
    mainItem.version = *selectedVersion_;
    mainItem.file = *mainFile;
    mainItem.destination = DestinationFor(sel->projectType, mainFile->filename);
    state->items.push_back(mainItem);
    state->added.insert(sel->projectId);
    state->visited.insert(sel->projectId);
    state->done = [this, state]() { ConfirmAndRun(state); };

    // Modpacks bundle their own mods; nothing to resolve.
    if (sel->projectType == QLatin1String("modpack"))
    {
        ConfirmAndRun(state);
        return;
    }
    ResolveDependencies(state, mainItem.version, 0);
}

void LibraryDialog::ResolveDependencies(const std::shared_ptr<PlanState>& state, const net::ModrinthVersion& version, int depth)
{
    const QString projectType = CurrentType();
    const QString gameVersion = GameVersionFilter();
    const QStringList loaders = LoaderFilter();

    state->pending++; // guard so `done` cannot fire while we are still iterating
    if (depth < kMaxDependencyDepth)
    {
        for (const auto& dep : version.dependencies)
        {
            if (dep.type != QLatin1String("required"))
            {
                continue;
            }
            const QString key = !dep.projectId.isEmpty() ? dep.projectId : dep.versionId;
            if (key.isEmpty() || state->visited.contains(key))
            {
                continue;
            }
            state->visited.insert(key);
            state->pending++;

            auto handle = [this, state, depth, key, projectType](std::optional<net::ModrinthVersion> ver, QString error) {
                if (ver.has_value() && !state->added.contains(ver->projectId))
                {
                    const auto file = ver->PrimaryFile();
                    const QString safe = net::ModrinthClient::SafeFileName(file ? file->filename : QString());
                    if (file.has_value() && !safe.isEmpty())
                    {
                        state->added.insert(ver->projectId);
                        PlanItem item;
                        item.title = ver->name.isEmpty() ? ver->projectId : ver->name;
                        item.projectId = ver->projectId;
                        item.version = *ver;
                        item.file = *file;
                        item.destination = DestinationFor(projectType, file->filename);
                        item.dependency = true;
                        if (QFileInfo::exists(item.destination))
                        {
                            // Already present: keep the existing file, just don't re-download it.
                            state->warnings << QStringLiteral("%1 đã có sẵn — giữ nguyên.").arg(safe);
                        }
                        else
                        {
                            state->items.push_back(item);
                        }
                        ResolveDependencies(state, *ver, depth + 1);
                    }
                }
                else if (!ver.has_value())
                {
                    state->warnings << QStringLiteral("Không tìm được bản tương thích cho phụ thuộc %1: %2").arg(key, error);
                }
                if (--state->pending == 0)
                {
                    state->done();
                }
            };

            if (!dep.projectId.isEmpty())
            {
                client_->GetCompatibleVersion(dep.projectId, gameVersion, loaders, handle);
            }
            else
            {
                client_->GetVersionById(dep.versionId, handle);
            }
        }
    }
    if (--state->pending == 0)
    {
        state->done();
    }
}

void LibraryDialog::ConfirmAndRun(const std::shared_ptr<PlanState>& state)
{
    QString text = QStringLiteral("Sẽ tải %1 file vào server \"%2\":\n\n").arg(state->items.size()).arg(QString::fromStdWString(targetServer_.name));
    qint64 totalBytes = 0;
    for (const auto& item : state->items)
    {
        text += QStringLiteral("  •  %1%2  —  %3 (%4)\n")
                    .arg(item.dependency ? QStringLiteral("[phụ thuộc] ") : QString(), item.file.filename, item.version.versionNumber, HumanSize(item.file.size));
        totalBytes += item.file.size;
    }
    text += QStringLiteral("\nTổng dung lượng: %1").arg(HumanSize(totalBytes));
    if (!state->warnings.isEmpty())
    {
        text += QStringLiteral("\n\nLưu ý:\n  –  ") + state->warnings.join(QStringLiteral("\n  –  "));
    }
    text += QStringLiteral("\n\nTiếp tục?");

    if (QMessageBox::question(this, QStringLiteral("Xác nhận cài đặt"), text, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        SetBusy(false);
        UpdateDetailPanel();
        labelStatus_->setText(QStringLiteral("Đã huỷ."));
        return;
    }

    // Dependencies first, the requested project last (so a failed dependency
    // leaves the main file uninstalled rather than a broken half-install).
    queue_.clear();
    for (int i = 1; i < state->items.size(); ++i)
    {
        queue_.push_back(state->items[i]);
    }
    queue_.push_back(state->items.first());
    queueIndex_ = 0;
    DownloadNext();
}

void LibraryDialog::DownloadNext()
{
    if (queueIndex_ >= queue_.size())
    {
        SetBusy(false);
        UpdateDetailPanel();
        const bool modpack = !queue_.isEmpty() && queue_.last().destination.contains(QStringLiteral("/modpacks/"));
        labelStatus_->setText(modpack
            ? QStringLiteral("Đã tải modpack vào thư mục modpacks/ của server.")
            : QStringLiteral("Đã cài %1 file. Khởi động lại server để áp dụng.").arg(queue_.size()));
        return;
    }

    const PlanItem item = queue_[queueIndex_];
    labelStatus_->setText(QStringLiteral("Đang tải (%1/%2): %3").arg(queueIndex_ + 1).arg(queue_.size()).arg(item.file.filename));
    progress_->setRange(0, 0);

    client_->DownloadFile(
        item.file,
        item.destination,
        [this](qint64 received, qint64 total) {
            if (total > 0)
            {
                progress_->setRange(0, 1000);
                progress_->setValue(static_cast<int>(received * 1000 / total));
            }
        },
        [this, item](bool success, QString error) {
            if (!success)
            {
                SetBusy(false);
                UpdateDetailPanel();
                labelStatus_->setText(QStringLiteral("Lỗi khi tải %1: %2 — đã dừng.").arg(item.file.filename, error));
                return;
            }
            ++queueIndex_;
            DownloadNext();
        });
}

} // namespace ui
