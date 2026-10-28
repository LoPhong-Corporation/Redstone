#include "ui/LibraryDialog.hpp"
#include "ui/I18n.hpp"

#include "config/ServerProperties.hpp"
#include "net/ModpackInstaller.hpp"
#include "ui/AddonCompat.hpp"
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
#include <QHeaderView>
#include <QPointer>
#include <QProgressBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <algorithm>
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


// Modrinth bodies mix markdown with raw HTML and remote images. QTextBrowser cannot
// fetch images and only understands a little HTML, so drop images/tags and keep the text.
QString PrepareMarkdown(QString text)
{
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression images(QStringLiteral("!\\[[^\\]]*\\]\\([^)]*\\)"));
    static const QRegularExpression breaks(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("</?[A-Za-z][^>]*>"));
    static const QRegularExpression manyBlank(QStringLiteral("\\n{3,}"));
    text.remove(comments);
    text.replace(breaks, QStringLiteral("\n"));
    text.remove(images);
    text.remove(tags);
    text.replace(manyBlank, QStringLiteral("\n\n"));
    return text.trimmed();
}

QList<int> VersionKey(const QString& v)
{
    QList<int> key;
    for (const QString& part : v.split(QLatin1Char('.')))
    {
        key.push_back(part.toInt());
    }
    return key;
}

// "1.20 - 1.21.4 (23 phiên bản)" from a long list of game versions (snapshots ignored).
QString SummarizeGameVersions(const QStringList& versions)
{
    static const QRegularExpression release(QStringLiteral("^\\d+\\.\\d+(\\.\\d+)?$"));
    QStringList rel;
    for (const QString& v : versions)
    {
        if (release.match(v).hasMatch()) rel << v;
    }
    if (rel.isEmpty()) return versions.join(QStringLiteral(", "));
    std::sort(rel.begin(), rel.end(), [](const QString& a, const QString& b) { return VersionKey(a) < VersionKey(b); });
    if (rel.size() == 1) return rel.first();
    return QStringLiteral("%1 – %2  (%3)").arg(rel.first(), rel.last(), TR("%1 phiên bản").arg(rel.size()));
}

QString SideText(const QString& v)
{
    if (v == QLatin1String("required")) return TR("bắt buộc");
    if (v == QLatin1String("optional")) return TR("tuỳ chọn");
    if (v == QLatin1String("unsupported")) return TR("không hỗ trợ");
    return v;
}

} // namespace

LibraryDialog::LibraryDialog(core::ServerConfig targetServer, QWidget* parent)
    : QDialog(parent)
    , targetServer_(std::move(targetServer))
    , client_(new net::ModrinthClient(this))
{
    setWindowTitle(TR("Thư viện Mod / Plugin — %1").arg(QString::fromStdWString(targetServer_.name)));
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
        TR("Thư viện Mod / Plugin"),
        TR("Server: %1  ·  Loại: %2  ·  Minecraft: %3")
            .arg(QString::fromStdWString(targetServer_.name),
                 QString::fromStdWString(targetServer_.serverType),
                 detectedVersion.isEmpty() ? TR("chưa rõ") : detectedVersion));
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
    comboType_->setCurrentIndex(comboType_->findData(DefaultProjectTypeFor(targetServer_)));
    row1->addWidget(comboType_);

    editQuery_ = new QLineEdit(filterCard);
    editQuery_->setPlaceholderText(TR("Tìm trên Modrinth... (vd: sodium, worldedit, terralith) — để trống để xem phổ biến nhất"));
    row1->addWidget(editQuery_, 1);

    comboSort_ = new QComboBox(filterCard);
    comboSort_->addItem(TR("Liên quan"), QStringLiteral("relevance"));
    comboSort_->addItem(TR("Nhiều lượt tải"), QStringLiteral("downloads"));
    comboSort_->addItem(TR("Nhiều người theo dõi"), QStringLiteral("follows"));
    comboSort_->addItem(TR("Mới nhất"), QStringLiteral("newest"));
    comboSort_->addItem(TR("Cập nhật gần đây"), QStringLiteral("updated"));
    row1->addWidget(comboSort_);

    buttonSearch_ = new QPushButton(TR("Tìm kiếm"), filterCard);
    buttonSearch_->setObjectName(QStringLiteral("btnStart"));
    row1->addWidget(buttonSearch_);
    filterLayout->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    row2->setSpacing(10);
    row2->addWidget(new QLabel(TR("Phiên bản Minecraft:"), filterCard));
    editGameVersion_ = new QLineEdit(detectedVersion, filterCard);
    editGameVersion_->setPlaceholderText(TR("vd: 1.21.4"));
    editGameVersion_->setFixedWidth(110);
    row2->addWidget(editGameVersion_);
    row2->addSpacing(12);
    toggleCompatible_ = new ToggleSwitch(filterCard);
    toggleCompatible_->setChecked(true);
    row2->addWidget(toggleCompatible_);
    row2->addWidget(new QLabel(TR("Chỉ bản tương thích với server"), filterCard));
    row2->addSpacing(12);
    toggleServerOnly_ = new ToggleSwitch(filterCard);
    toggleServerOnly_->setChecked(true);
    row2->addWidget(toggleServerOnly_);
    row2->addWidget(new QLabel(TR("Ẩn mod chỉ chạy ở client"), filterCard));
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
    buttonMore_ = new QPushButton(TR("Tải thêm kết quả"), leftCard);
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

    // ---- Detail tabs: overview / full description / versions + changelog / gallery ----
    tabs_ = new QTabWidget(rightCard);
    browserOverview_ = new QTextBrowser(tabs_);
    browserOverview_->setOpenExternalLinks(true);
    browserOverview_->setFrameShape(QFrame::NoFrame);
    tabs_->addTab(browserOverview_, TR("Tổng quan"));

    browserBody_ = new QTextBrowser(tabs_);
    browserBody_->setOpenExternalLinks(true);
    browserBody_->setFrameShape(QFrame::NoFrame);
    tabs_->addTab(browserBody_, TR("Mô tả"));

    auto* versionsPage = new QWidget(tabs_);
    auto* versionsLayout = new QVBoxLayout(versionsPage);
    versionsLayout->setContentsMargins(0, 8, 0, 0);
    versionsLayout->setSpacing(8);
    tableVersions_ = new QTableWidget(0, 6, versionsPage);
    tableVersions_->setHorizontalHeaderLabels(
        {TR("Phiên bản"), TR("Loại"), TR("Minecraft"), TR("Loader"), TR("Ngày"), TR("Lượt tải")});
    tableVersions_->horizontalHeader()->setStretchLastSection(true);
    tableVersions_->verticalHeader()->setVisible(false);
    tableVersions_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableVersions_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableVersions_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableVersions_->setShowGrid(false);
    versionsLayout->addWidget(tableVersions_, 3);
    browserChangelog_ = new QTextBrowser(versionsPage);
    browserChangelog_->setOpenExternalLinks(true);
    versionsLayout->addWidget(browserChangelog_, 2);
    buttonUseVersion_ = new QPushButton(TR("Dùng phiên bản đã chọn"), versionsPage);
    buttonUseVersion_->setEnabled(false);
    versionsLayout->addWidget(buttonUseVersion_, 0, Qt::AlignRight);
    tabs_->addTab(versionsPage, TR("Phiên bản"));

    auto* galleryScroll = new QScrollArea(tabs_);
    galleryScroll->setWidgetResizable(true);
    galleryScroll->setFrameShape(QFrame::NoFrame);
    galleryHost_ = new QWidget();
    galleryLayout_ = new QVBoxLayout(galleryHost_);
    galleryLayout_->setContentsMargins(0, 8, 8, 8);
    galleryLayout_->setSpacing(12);
    galleryScroll->setWidget(galleryHost_);
    tabs_->addTab(galleryScroll, TR("Ảnh"));
    rightLayout->addWidget(tabs_, 1);

    rightLayout->addSpacing(6);
    labelInstallSection_ = MakeSectionLabel(TR("SẼ CÀI ĐẶT"), rightCard);
    rightLayout->addWidget(labelInstallSection_);
    labelDetailVersion_ = new QLabel(rightCard);
    labelDetailVersion_->setWordWrap(true);
    rightLayout->addWidget(labelDetailVersion_);
    labelDetailDeps_ = new QLabel(rightCard);
    labelDetailDeps_->setObjectName(QStringLiteral("hintLabel"));
    labelDetailDeps_->setWordWrap(true);
    rightLayout->addWidget(labelDetailDeps_);

    progress_ = new QProgressBar(rightCard);
    progress_->setRange(0, 100);
    progress_->setTextVisible(false);
    progress_->setVisible(false);
    rightLayout->addWidget(progress_);

    auto* actionRow = new QHBoxLayout();
    buttonOpenWeb_ = new QPushButton(TR("Mở trên Modrinth"), rightCard);
    buttonOpenWeb_->setEnabled(false);
    buttonInstall_ = new QPushButton(TR("Cài vào server"), rightCard);
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
    auto* closeButton = new QPushButton(TR("Đóng"), this);
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
    connect(tableVersions_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto items = tableVersions_->selectedItems();
        buttonUseVersion_->setEnabled(!items.isEmpty() && !busy_);
        if (items.isEmpty() || items.first()->row() >= versionList_.size())
        {
            browserChangelog_->clear();
            return;
        }
        const QString log = PrepareMarkdown(versionList_[items.first()->row()].changelog);
        if (log.isEmpty()) browserChangelog_->setPlainText(TR("(Phiên bản này không có changelog)"));
        else browserChangelog_->setMarkdown(log);
    });
    connect(buttonUseVersion_, &QPushButton::clicked, this, [this]() {
        const auto items = tableVersions_->selectedItems();
        if (items.isEmpty() || items.first()->row() >= versionList_.size() || busy_) return;
        selectedVersion_ = versionList_[items.first()->row()];
        selectedVersionError_.clear();
        resolving_ = false;
        UpdateDetailPanel();
        labelStatus_->setText(TR("Sẽ cài phiên bản %1 do bạn chọn.").arg(selectedVersion_->versionNumber));
    });
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
    return LoadersForServer(targetServer_, projectType);
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
    labelStatus_->setText(TR("Đang tìm kiếm..."));

    client_->Search(options, [this, token, append](net::SearchResult result, QString error) {
        if (token != searchToken_)
        {
            return; // a newer search superseded this one
        }
        buttonSearch_->setEnabled(true);
        if (!error.isEmpty())
        {
            labelStatus_->setText(TR("Lỗi: %1").arg(error));
            return;
        }
        totalHits_ = result.totalHits;
        PopulateResults(result.hits, append);
        if (hits_.isEmpty())
        {
            QString hint = TR("Không tìm thấy kết quả nào.");
            if (toggleCompatible_->isChecked() && LoadersForType(CurrentType()).isEmpty() && CurrentType() == QLatin1String("plugin"))
            {
                hint += TR(" Loại server \"%1\" thường không chạy plugin Bukkit/Paper.")
                            .arg(QString::fromStdWString(targetServer_.serverType));
            }
            labelStatus_->setText(hint);
        }
        else
        {
            labelStatus_->setText(TR("Hiển thị %1 / %2 kết quả.").arg(hits_.size()).arg(totalHits_));
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
        QString second = TR("%1  ·  %2 lượt tải  ·  %3 theo dõi").arg(hit.author, HumanCount(hit.downloads), HumanCount(hit.follows));
        const QString updated = HumanDate(hit.dateModified);
        if (!updated.isEmpty())
        {
            second += TR("  ·  cập nhật %1").arg(updated);
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
    ClearDetails();
    UpdateDetailPanel();
    if (CurrentSelection().has_value())
    {
        RebuildOverview(); // search-hit data first, richer details arrive asynchronously
        ResolveSelectedVersion();
        LoadProjectDetails();
    }
}

void LibraryDialog::ClearDetails()
{
    ++detailToken_;
    projectDetail_.reset();
    members_.clear();
    versionList_.clear();
    browserOverview_->clear();
    browserBody_->clear();
    browserChangelog_->clear();
    tableVersions_->setRowCount(0);
    buttonUseVersion_->setEnabled(false);
    RebuildGallery();
}

void LibraryDialog::LoadProjectDetails()
{
    const auto sel = CurrentSelection();
    if (!sel.has_value()) return;
    const int token = ++detailToken_;
    const QString id = sel->projectId;

    client_->GetProject(id, [this, token](std::optional<net::ModrinthProjectDetail> project, QString error) {
        if (token != detailToken_) return;
        if (!project.has_value())
        {
            browserBody_->setPlainText(TR("Không tải được chi tiết: %1").arg(error));
            return;
        }
        projectDetail_ = project;
        const QString body = PrepareMarkdown(project->body);
        if (body.isEmpty()) browserBody_->setPlainText(project->description);
        else browserBody_->setMarkdown(body);
        RebuildOverview();
        RebuildGallery();
    });
    client_->GetMembers(id, [this, token](QList<net::ModrinthMember> members) {
        if (token != detailToken_) return;
        members_ = members;
        RebuildOverview();
    });
    client_->ListVersions(id, GameVersionFilter(), LoaderFilter(), [this, token](QList<net::ModrinthVersion> versions, QString) {
        if (token != detailToken_) return;
        versionList_ = versions;
        RebuildVersionsTable();
    });
}

void LibraryDialog::RebuildOverview()
{
    const auto sel = CurrentSelection();
    if (!sel.has_value())
    {
        browserOverview_->clear();
        return;
    }
    const net::ModrinthProjectDetail* p = projectDetail_.has_value() ? &*projectDetail_ : nullptr;
    const QString muted = FluentTheme::Colors().textSecondary.name();

    QString h = QStringLiteral("<p>%1</p><table cellspacing='0'>").arg((p != nullptr ? p->description : sel->description).toHtmlEscaped());
    auto row = [&](const QString& key, const QString& valueHtml) {
        if (valueHtml.trimmed().isEmpty()) return;
        h += QStringLiteral("<tr><td style='padding:3px 16px 3px 0; color:%1; white-space:nowrap' valign='top'>%2</td><td>%3</td></tr>")
                 .arg(muted, key.toHtmlEscaped(), valueHtml);
    };

    QStringList people;
    for (const auto& m : members_)
    {
        people << (m.role.isEmpty() ? m.name : QStringLiteral("%1 (%2)").arg(m.name, m.role)).toHtmlEscaped();
    }
    row(TR("Tác giả"), people.isEmpty() ? sel->author.toHtmlEscaped() : people.join(QStringLiteral(", ")));
    row(TR("Thống kê"), TR("%1 lượt tải  ·  %2 người theo dõi").arg(HumanCount(sel->downloads), HumanCount(p != nullptr ? p->followers : sel->follows)));

    const QString client = p != nullptr ? p->clientSide : sel->clientSide;
    const QString serverSide = p != nullptr ? p->serverSide : sel->serverSide;
    if (!client.isEmpty() || !serverSide.isEmpty())
    {
        row(TR("Môi trường"), TR("Server: %1  ·  Client: %2").arg(SideText(serverSide), SideText(client)));
    }
    const QStringList loaders = p != nullptr ? p->loaders : QStringList();
    row(TR("Loader / nền tảng"), loaders.join(QStringLiteral(", ")).toHtmlEscaped());

    const QStringList versions = p != nullptr ? p->gameVersions : sel->gameVersions;
    QString mc = SummarizeGameVersions(versions).toHtmlEscaped();
    const QString want = editGameVersion_->text().trimmed();
    if (!want.isEmpty() && !versions.isEmpty())
    {
        mc += versions.contains(want)
            ? QStringLiteral("<br><b style='color:%1'>✓ %2</b>").arg(FluentTheme::Colors().success.name(), TR("Hỗ trợ Minecraft %1 của server bạn").arg(want).toHtmlEscaped())
            : QStringLiteral("<br><b style='color:%1'>✗ %2</b>").arg(FluentTheme::Colors().warning.name(), TR("Chưa thấy hỗ trợ Minecraft %1").arg(want).toHtmlEscaped());
    }
    row(TR("Phiên bản Minecraft"), mc);

    if (p != nullptr)
    {
        row(TR("Giấy phép"), p->licenseName.toHtmlEscaped());
        row(TR("Đăng lần đầu"), HumanDate(p->published));
        row(TR("Cập nhật gần nhất"), HumanDate(p->updated));
        QStringList links;
        auto addLink = [&links](const QString& label, const QString& url) {
            if (url.startsWith(QStringLiteral("http"))) links << QStringLiteral("<a href='%1'>%2</a>").arg(url.toHtmlEscaped(), label.toHtmlEscaped());
        };
        addLink(TR("Mã nguồn"), p->sourceUrl);
        addLink(TR("Báo lỗi"), p->issuesUrl);
        addLink(TR("Wiki"), p->wikiUrl);
        addLink(TR("Discord"), p->discordUrl);
        for (int i = 0; i < p->donationUrls.size(); ++i)
        {
            addLink(TR("Ủng hộ (%1)").arg(p->donationNames.value(i)), p->donationUrls[i]);
        }
        links << QStringLiteral("<a href='https://modrinth.com/%1/%2'>Modrinth</a>").arg(sel->projectType, sel->slug.toHtmlEscaped());
        row(TR("Liên kết"), links.join(QStringLiteral("  ·  ")));
    }
    const QStringList tags = p != nullptr ? p->categories : sel->categories;
    row(TR("Thẻ"), tags.join(QStringLiteral("  ·  ")).toHtmlEscaped());
    h += QStringLiteral("</table>");
    browserOverview_->setHtml(h);
}

void LibraryDialog::RebuildVersionsTable()
{
    tableVersions_->setRowCount(0);
    const int shown = std::min<int>(versionList_.size(), 60);
    tableVersions_->setRowCount(shown);
    for (int r = 0; r < shown; ++r)
    {
        const net::ModrinthVersion& v = versionList_[r];
        QStringList mc = v.gameVersions;
        QString mcText = mc.size() > 3 ? mc.mid(0, 3).join(QStringLiteral(", ")) + QStringLiteral(" …") : mc.join(QStringLiteral(", "));
        const QStringList cells = {v.versionNumber, v.versionType, mcText, v.loaders.join(QStringLiteral(", ")), HumanDate(v.datePublished), HumanCount(v.downloads)};
        for (int c = 0; c < cells.size(); ++c)
        {
            tableVersions_->setItem(r, c, new QTableWidgetItem(cells[c]));
        }
    }
    tableVersions_->resizeColumnsToContents();
    if (shown > 0) tableVersions_->selectRow(0); // newest first; shows its changelog
}

void LibraryDialog::RebuildGallery()
{
    while (QLayoutItem* item = galleryLayout_->takeAt(0))
    {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    if (!projectDetail_.has_value() || projectDetail_->gallery.isEmpty())
    {
        auto* none = new QLabel(projectDetail_.has_value() ? TR("Dự án này không có ảnh.") : QString(), galleryHost_);
        none->setObjectName(QStringLiteral("hintLabel"));
        galleryLayout_->addWidget(none);
        galleryLayout_->addStretch(1);
        return;
    }
    const int token = detailToken_;
    const int maxWidth = std::max(260, tabs_->width() - 60);
    int count = 0;
    for (const auto& image : projectDetail_->gallery)
    {
        if (++count > 8) break;
        auto* picture = new QLabel(TR("Đang tải ảnh..."), galleryHost_);
        picture->setAlignment(Qt::AlignCenter);
        picture->setMinimumHeight(60);
        galleryLayout_->addWidget(picture);
        const QString caption = image.description.isEmpty() ? image.title : (image.title.isEmpty() ? image.description : image.title + QStringLiteral(" — ") + image.description);
        if (!caption.isEmpty())
        {
            auto* cap = new QLabel(caption, galleryHost_);
            cap->setObjectName(QStringLiteral("hintLabel"));
            cap->setWordWrap(true);
            galleryLayout_->addWidget(cap);
        }
        QPointer<QLabel> target(picture);
        client_->FetchImage(image.url, 8 * 1024 * 1024, [this, token, target, maxWidth](QByteArray data) {
            if (token != detailToken_ || target.isNull()) return;
            QPixmap pm;
            if (data.isEmpty() || !pm.loadFromData(data))
            {
                target->setText(TR("(không tải được ảnh)"));
                return;
            }
            target->setPixmap(pm.width() > maxWidth ? pm.scaledToWidth(maxWidth, Qt::SmoothTransformation) : pm);
        });
    }
    galleryLayout_->addStretch(1);
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
        labelDetailTitle_->setText(TR("Chọn một mục để xem chi tiết"));
        labelDetailMeta_->clear();
        labelDetailVersion_->clear();
        labelDetailDeps_->clear();
        buttonInstall_->setEnabled(false);
        buttonOpenWeb_->setEnabled(false);
        labelInstallSection_->setVisible(false);
        return;
    }

    labelInstallSection_->setVisible(true);
    labelDetailTitle_->setText(sel->title);
    labelDetailMeta_->setText(TR("%1  ·  bởi %2").arg(TypeLabel(sel->projectType), sel->author));
    buttonOpenWeb_->setEnabled(!sel->slug.isEmpty());

    labelDetailDeps_->clear();
    if (resolving_)
    {
        labelDetailVersion_->setText(TR("Đang tìm phiên bản phù hợp..."));
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
            text += TR("<br><i>File này đã có trong server — sẽ được ghi đè.</i>");
        }
    }
    if (v.versionType != QLatin1String("release"))
    {
        text += TR("<br><i>Đây là bản %1, có thể chưa ổn định.</i>").arg(v.versionType);
    }
    if (!toggleCompatible_->isChecked())
    {
        text += TR("<br><i>Đang tắt lọc tương thích — bản này có thể không chạy trên server của bạn.</i>");
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
        QString depsText = depTitleCache_.value(v.id);
        if (depsText.isEmpty())
        {
            depsText = TR("Cần %1 phụ thuộc bắt buộc — sẽ được kiểm tra và cài kèm khi bạn bấm Cài.").arg(required);
            QStringList ids;
            for (const auto& dep : v.dependencies)
            {
                if (dep.type == QLatin1String("required") && !dep.projectId.isEmpty()) ids << dep.projectId;
            }
            if (!ids.isEmpty())
            {
                const QString versionId = v.id;
                client_->GetProjectTitles(ids, [this, versionId, required](QHash<QString, QString> titles) {
                    QStringList names = titles.values();
                    names.sort();
                    if (names.isEmpty()) return;
                    depTitleCache_.insert(versionId, TR("Phụ thuộc bắt buộc (%1): %2 — sẽ được kiểm tra và cài kèm khi bạn bấm Cài.").arg(required).arg(names.join(QStringLiteral(", "))));
                    if (selectedVersion_.has_value() && selectedVersion_->id == versionId)
                        labelDetailDeps_->setText(depTitleCache_.value(versionId));
                });
            }
        }
        labelDetailDeps_->setText(depsText);
    }
    if (sel->projectType == QLatin1String("modpack"))
    {
        labelDetailDeps_->setText(TR("Modpack: sau khi tải sẽ hỏi cài thẳng vào server (mod + cấu hình). Server phải đã chạy đúng loader."));
    }
    buttonInstall_->setText(sel->projectType == QLatin1String("modpack") ? TR("Tải modpack") : TR("Cài vào server"));
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
        labelStatus_->setText(TR("Tên file từ Modrinth không hợp lệ — đã huỷ cài đặt."));
        return;
    }

    SetBusy(true);
    labelStatus_->setText(TR("Đang kiểm tra phụ thuộc..."));

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
                            state->warnings << TR("%1 đã có sẵn — giữ nguyên.").arg(safe);
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
                    state->warnings << TR("Không tìm được bản tương thích cho phụ thuộc %1: %2").arg(key, error);
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
    QString text = TR("Sẽ tải %1 file vào server \"%2\":\n\n").arg(state->items.size()).arg(QString::fromStdWString(targetServer_.name));
    qint64 totalBytes = 0;
    for (const auto& item : state->items)
    {
        text += QStringLiteral("  •  %1%2  —  %3 (%4)\n")
                    .arg(item.dependency ? TR("[phụ thuộc] ") : QString(), item.file.filename, item.version.versionNumber, HumanSize(item.file.size));
        totalBytes += item.file.size;
    }
    text += TR("\nTổng dung lượng: %1").arg(HumanSize(totalBytes));
    if (!state->warnings.isEmpty())
    {
        text += TR("\n\nLưu ý:\n  –  ") + state->warnings.join(QStringLiteral("\n  –  "));
    }
    text += TR("\n\nTiếp tục?");

    if (QMessageBox::question(this, TR("Xác nhận cài đặt"), text, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        SetBusy(false);
        UpdateDetailPanel();
        labelStatus_->setText(TR("Đã huỷ."));
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

void LibraryDialog::OfferModpackInstall(const QString& mrpackPath)
{
    auto* installer = new net::ModpackInstaller(client_, this);
    QString error;
    const QString serverDir = QString::fromStdWString(targetServer_.directory.wstring());
    if (!installer->Prepare(mrpackPath, serverDir, &error))
    {
        installer->deleteLater();
        SetBusy(false);
        UpdateDetailPanel();
        labelStatus_->setText(TR("Đã tải modpack nhưng không cài được: %1").arg(error));
        return;
    }

    const net::ModpackInfo& info = installer->Info();
    QString text = QStringLiteral("Modpack \"%1\" %2\n\n").arg(info.name, info.versionId);
    if (!info.summary.isEmpty())
    {
        text += info.summary + QStringLiteral("\n\n");
    }
    text += TR("Yêu cầu: Minecraft %1").arg(info.minecraft.isEmpty() ? TR("(không rõ)") : info.minecraft);
    if (!info.loader.isEmpty())
    {
        text += QStringLiteral(" + %1 %2").arg(info.loader, info.loaderVersion);
    }
    text += TR("\nSẽ tải %1 file (%2) cho server, bỏ qua %3 file chỉ dành cho client, rồi chép cấu hình (overrides) vào server.")
                .arg(info.serverFileCount).arg(HumanSize(info.totalBytes)).arg(info.clientOnlySkipped);

    QStringList warnings;
    const QString haveVersion = editGameVersion_->text().trimmed();
    if (!info.minecraft.isEmpty() && !haveVersion.isEmpty() && haveVersion != info.minecraft)
    {
        warnings << TR("Server hiện là Minecraft %1, modpack cần %2.").arg(haveVersion, info.minecraft);
    }
    const QStringList serverLoaders = LoadersForType(QStringLiteral("mod"));
    if (!info.loader.isEmpty() && !serverLoaders.contains(info.loader))
    {
        warnings << TR("Loại server \"%1\" không khớp loader %2 của modpack.")
                        .arg(QString::fromStdWString(targetServer_.serverType), info.loader);
    }
    warnings << TR("App KHÔNG tự cài file server/loader - server phải đã chạy đúng phiên bản và loader trên.");
    warnings << TR("File trùng tên trong mods/, config/... sẽ bị ghi đè. Nên tạo backup trước.");
    text += TR("\n\nLưu ý:\n  –  ") + warnings.join(QStringLiteral("\n  –  ")) + TR("\n\nCài modpack vào server ngay bây giờ?");

    if (QMessageBox::question(this, TR("Cài modpack"), text, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        installer->deleteLater();
        SetBusy(false);
        UpdateDetailPanel();
        labelStatus_->setText(TR("Đã tải modpack vào thư mục modpacks/ (chưa cài vào server)."));
        return;
    }

    progress_->setRange(0, 1000);
    progress_->setValue(0);
    installer->Install(
        [this](int done, int total, const QString& current) {
            progress_->setValue(total > 0 ? done * 1000 / total : 1000);
            labelStatus_->setText(TR("Đang tải mod (%1/%2): %3").arg(done).arg(total).arg(current));
        },
        [this, installer](net::ModpackInstaller::Result result) {
            installer->deleteLater();
            SetBusy(false);
            UpdateDetailPanel();
            if (result.ok)
            {
                QString message = TR("Đã cài modpack: %1 file mod và %2 file cấu hình. Khởi động lại server để áp dụng.")
                                      .arg(result.filesDownloaded).arg(result.overridesApplied);
                if (!result.warnings.isEmpty())
                {
                    message += TR(" (%1 cảnh báo)").arg(result.warnings.size());
                }
                labelStatus_->setText(message);
            }
            else
            {
                labelStatus_->setText(TR("Cài modpack thất bại: %1").arg(result.errors.join(QStringLiteral("; "))));
            }
        });
}

void LibraryDialog::DownloadNext()
{
    if (queueIndex_ >= queue_.size())
    {
        const bool modpack = !queue_.isEmpty() && queue_.last().destination.endsWith(QStringLiteral(".mrpack"), Qt::CaseInsensitive);
        if (modpack)
        {
            OfferModpackInstall(queue_.last().destination); // keeps the dialog busy until it finishes
            return;
        }
        SetBusy(false);
        UpdateDetailPanel();
        labelStatus_->setText(TR("Đã cài %1 file. Khởi động lại server để áp dụng.").arg(queue_.size()));
        return;
    }

    const PlanItem item = queue_[queueIndex_];
    labelStatus_->setText(TR("Đang tải (%1/%2): %3").arg(queueIndex_ + 1).arg(queue_.size()).arg(item.file.filename));
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
                labelStatus_->setText(TR("Lỗi khi tải %1: %2 — đã dừng.").arg(item.file.filename, error));
                return;
            }
            ++queueIndex_;
            DownloadNext();
        });
}

} // namespace ui
