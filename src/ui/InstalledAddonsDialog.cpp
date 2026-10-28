#include "ui/InstalledAddonsDialog.hpp"

#include "ui/AddonCompat.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"
#include "ui/I18n.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <thread>

namespace ui
{
namespace
{

QString HumanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024) return QStringLiteral("%1 MB").arg(QString::number(bytes / 1048576.0, 'f', 1));
    return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0));
}

QString Sha512Of(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha512);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

InstalledAddonsDialog::InstalledAddonsDialog(core::ServerConfig server, QWidget* parent)
    : QDialog(parent)
    , server_(std::move(server))
    , client_(new net::ModrinthClient(this))
{
    setWindowTitle(TR("Mod / Plugin đã cài — %1").arg(QString::fromStdWString(server_.name)));
    resize(860, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 14);
    root->setSpacing(12);

    auto* header = new QVBoxLayout();
    const QString mc = DetectMinecraftVersion(server_);
    AddPageHeader(
        header, TR("Mod / Plugin đã cài"),
        TR("Minecraft: %1  ·  Loại server: %2").arg(mc.isEmpty() ? TR("chưa rõ") : mc, QString::fromStdWString(server_.serverType)));
    root->addLayout(header);

    auto* bar = new QHBoxLayout();
    bar->setSpacing(8);
    buttonCheck_ = new QPushButton(TR("Kiểm tra cập nhật"));
    buttonCheck_->setObjectName(QStringLiteral("btnStart"));
    buttonUpdateSelected_ = new QPushButton(TR("Cập nhật mục chọn"));
    buttonUpdateAll_ = new QPushButton(TR("Cập nhật tất cả"));
    buttonDelete_ = new QPushButton(TR("Xoá mục chọn"));
    buttonDelete_->setObjectName(QStringLiteral("btnStop"));
    auto* openButton = new QPushButton(TR("Mở thư mục"));
    auto* rescanButton = new QPushButton(TR("Quét lại"));
    for (QPushButton* b : {buttonCheck_, buttonUpdateSelected_, buttonUpdateAll_}) bar->addWidget(b);
    bar->addStretch(1);
    for (QPushButton* b : {rescanButton, openButton, buttonDelete_}) bar->addWidget(b);
    root->addLayout(bar);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    list_ = new QListWidget(card);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    cardLayout->addWidget(list_);
    root->addWidget(card, 1);

    progress_ = new QProgressBar(this);
    progress_->setTextVisible(false);
    progress_->setVisible(false);
    root->addWidget(progress_);

    auto* footer = new QHBoxLayout();
    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelStatus_->setWordWrap(true);
    footer->addWidget(labelStatus_, 1);
    auto* closeButton = new QPushButton(TR("Đóng"));
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    footer->addWidget(closeButton);
    root->addLayout(footer);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(rescanButton, &QPushButton::clicked, this, [this]() { if (!busy_) Scan(); });
    connect(openButton, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(server_.directory.wstring())));
    });
    connect(buttonCheck_, &QPushButton::clicked, this, &InstalledAddonsDialog::CheckUpdates);
    connect(list_, &QListWidget::itemSelectionChanged, this, &InstalledAddonsDialog::UpdateButtons);
    connect(buttonUpdateSelected_, &QPushButton::clicked, this, [this]() { StartUpdate(SelectedUpdatable()); });
    connect(buttonUpdateAll_, &QPushButton::clicked, this, [this]() {
        QList<int> all;
        for (int i = 0; i < addons_.size(); ++i) if (addons_[i].hasUpdate) all << i;
        StartUpdate(all);
    });
    connect(buttonDelete_, &QPushButton::clicked, this, [this]() {
        const auto items = list_->selectedItems();
        if (items.isEmpty() || busy_) return;
        if (QMessageBox::question(this, TR("Xoá file"), TR("Xoá %1 file đã chọn khỏi server?").arg(items.size()), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
        for (const auto* item : items)
        {
            const int index = item->data(Qt::UserRole).toInt();
            if (index >= 0 && index < addons_.size()) QFile::remove(addons_[index].path);
        }
        Scan();
    });

    Scan();
}

QString InstalledAddonsDialog::BackupFolderFor() const
{
    return backupRoot_;
}

void InstalledAddonsDialog::Scan()
{
    addons_.clear();
    list_->clear();
    const QString serverDir = QString::fromStdWString(server_.directory.wstring());
    for (const QString& folder : {QStringLiteral("mods"), QStringLiteral("plugins")})
    {
        const QDir dir(QDir(serverDir).filePath(folder));
        for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.jar")}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        {
            Addon a;
            a.path = info.absoluteFilePath();
            a.folder = folder;
            a.fileName = info.fileName();
            a.sizeBytes = info.size();
            a.title = info.completeBaseName();
            addons_.push_back(a);
        }
    }
    for (int i = 0; i < addons_.size(); ++i)
    {
        auto* item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(0, 54));
        UpdateItemText(i);
    }
    labelStatus_->setText(addons_.isEmpty() ? TR("Chưa có mod/plugin nào trong thư mục mods/ hoặc plugins/.")
                                            : TR("%1 file. Bấm \"Kiểm tra cập nhật\" để so với Modrinth.").arg(addons_.size()));
    UpdateButtons();
}

void InstalledAddonsDialog::UpdateItemText(int index)
{
    const Addon& a = addons_[index];
    QListWidgetItem* item = list_->item(index);
    QString second;
    if (a.hasUpdate && a.latest.has_value())
    {
        second = TR("⬆ Có bản mới: %1 → %2").arg(a.currentVersion, a.latest->versionNumber);
    }
    else if (a.known)
    {
        second = TR("✓ Mới nhất (%1)").arg(a.currentVersion);
    }
    else if (!a.sha512.isEmpty())
    {
        second = TR("Không có trên Modrinth (không kiểm tra được)");
    }
    else
    {
        second = TR("Chưa kiểm tra");
    }
    item->setText(QStringLiteral("%1   ·   %2 / %3\n%4").arg(a.title, a.folder, a.fileName, second) + QStringLiteral("   ·   ") + HumanSize(a.sizeBytes));

    const FluentColors& c = FluentTheme::Colors();
    item->setForeground(a.hasUpdate ? c.accentText : c.text);
}

void InstalledAddonsDialog::UpdateButtons()
{
    int updatable = 0;
    for (const auto& a : addons_) updatable += a.hasUpdate ? 1 : 0;
    buttonCheck_->setEnabled(!busy_ && !addons_.isEmpty());
    buttonUpdateSelected_->setEnabled(!busy_ && !SelectedUpdatable().isEmpty());
    buttonUpdateAll_->setEnabled(!busy_ && updatable > 0);
    buttonUpdateAll_->setText(updatable > 0 ? TR("Cập nhật tất cả (%1)").arg(updatable) : TR("Cập nhật tất cả"));
    buttonDelete_->setEnabled(!busy_ && !list_->selectedItems().isEmpty());
}

void InstalledAddonsDialog::SetBusy(bool busy)
{
    busy_ = busy;
    progress_->setVisible(busy);
    if (busy) progress_->setRange(0, 0);
    list_->setEnabled(!busy);
    UpdateButtons();
}

QList<int> InstalledAddonsDialog::SelectedUpdatable() const
{
    QList<int> out;
    for (const auto* item : list_->selectedItems())
    {
        const int index = item->data(Qt::UserRole).toInt();
        if (index >= 0 && index < addons_.size() && addons_[index].hasUpdate) out << index;
    }
    return out;
}

// ------------------------------------------------------------------ check

void InstalledAddonsDialog::CheckUpdates()
{
    if (busy_ || addons_.isEmpty()) return;
    const int token = ++checkToken_;
    SetBusy(true);
    labelStatus_->setText(TR("Đang tính mã SHA-512 của các file..."));

    // Hashing many jars can take a moment: do it off the UI thread.
    QList<QString> paths;
    for (const auto& a : addons_) paths << a.path;
    QPointer<InstalledAddonsDialog> self(this);
    std::thread([self, paths, token]() {
        QStringList hashes;
        for (const QString& p : paths) hashes << Sha512Of(p);
        if (self.isNull()) return;
        QMetaObject::invokeMethod(self.data(), [self, hashes, token]() {
            if (self.isNull() || token != self->checkToken_) return;
            self->OnHashesReady(hashes);
        }, Qt::QueuedConnection);
    }).detach();
}

void InstalledAddonsDialog::OnHashesReady(QStringList hashes)
{
    for (int i = 0; i < addons_.size() && i < hashes.size(); ++i)
    {
        addons_[i].sha512 = hashes[i];
        addons_[i].known = false;
        addons_[i].hasUpdate = false;
        addons_[i].latest.reset();
    }
    labelStatus_->setText(TR("Đang nhận diện file trên Modrinth..."));
    const int token = checkToken_;
    QStringList nonEmpty;
    for (const auto& h : hashes) if (!h.isEmpty()) nonEmpty << h;
    client_->IdentifyByHash(nonEmpty, [this, token](QHash<QString, net::ModrinthVersion> versions, QString error) {
        if (token != checkToken_) return;
        if (!error.isEmpty())
        {
            SetBusy(false);
            labelStatus_->setText(TR("Lỗi: %1").arg(error));
            return;
        }
        OnIdentified(versions);
    });
}

void InstalledAddonsDialog::OnIdentified(const QHash<QString, net::ModrinthVersion>& versions)
{
    QStringList projectIds;
    for (auto& a : addons_)
    {
        const auto it = versions.constFind(a.sha512.toLower());
        if (it == versions.constEnd()) continue;
        a.known = true;
        a.projectId = it->projectId;
        a.currentVersion = it->versionNumber;
        a.currentVersionId = it->id;
        a.currentDate = it->datePublished;
        if (!projectIds.contains(a.projectId)) projectIds << a.projectId;
    }

    const int token = checkToken_;
    client_->GetProjectTitles(projectIds.mid(0, 100), [this, token](QHash<QString, QString> titles) {
        if (token != checkToken_) return;
        for (auto& a : addons_)
        {
            if (a.known && titles.contains(a.projectId)) a.title = titles.value(a.projectId);
        }
        labelStatus_->setText(TR("Đang tìm bản mới tương thích với server..."));

        latestPending_ = 0;
        for (const QString& folder : {QStringLiteral("mods"), QStringLiteral("plugins")})
        {
            QStringList hashes;
            for (const auto& a : addons_) if (a.known && a.folder == folder) hashes << a.sha512;
            if (!hashes.isEmpty()) RequestLatest(folder, hashes, &latestPending_);
        }
        if (latestPending_ == 0) FinishCheck();
    });
}

void InstalledAddonsDialog::RequestLatest(const QString& folder, const QStringList& hashes, int* pending)
{
    ++*pending;
    const QString type = folder == QLatin1String("plugins") ? QStringLiteral("plugin") : QStringLiteral("mod");
    QStringList loaders = LoadersForServer(server_, type);
    if (loaders.isEmpty()) loaders = QStringList(); // unknown platform: any loader
    const QString mc = DetectMinecraftVersion(server_);
    const int token = checkToken_;

    client_->LatestByHash(hashes, loaders, mc.isEmpty() ? QStringList() : QStringList{mc},
        [this, token, hashes](QHash<QString, net::ModrinthVersion> latest, QString) {
            if (token != checkToken_) return;
            for (auto& a : addons_)
            {
                const auto it = latest.constFind(a.sha512.toLower());
                if (it == latest.constEnd() || !hashes.contains(a.sha512)) continue;
                a.latest = *it;
                // newer = a different version published later than the installed one
                a.hasUpdate = it->id != a.currentVersionId && it->datePublished > a.currentDate && it->PrimaryFile().has_value();
            }
            if (--latestPending_ == 0) FinishCheck();
        });
}

void InstalledAddonsDialog::FinishCheck()
{
    SetBusy(false);
    int updates = 0, unknown = 0;
    for (int i = 0; i < addons_.size(); ++i)
    {
        UpdateItemText(i);
        updates += addons_[i].hasUpdate ? 1 : 0;
        unknown += addons_[i].known ? 0 : 1;
    }
    labelStatus_->setText(
        updates > 0 ? TR("Có %1 bản cập nhật. %2 file không nhận diện được trên Modrinth.").arg(updates).arg(unknown)
                    : TR("Tất cả đều là bản mới nhất. %1 file không nhận diện được trên Modrinth.").arg(unknown));
    UpdateButtons();
}

// ----------------------------------------------------------------- update

void InstalledAddonsDialog::StartUpdate(const QList<int>& indexes)
{
    if (indexes.isEmpty() || busy_) return;
    if (QMessageBox::question(
            this, TR("Cập nhật"),
            TR("Cập nhật %1 file? Bản cũ được lưu trong addons-backup/. Nên Stop server trước và kiểm tra phụ thuộc mới ở Thư viện.").arg(indexes.size()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    backupRoot_ = QDir(QString::fromStdWString(server_.directory.wstring()))
                      .filePath(QStringLiteral("addons-backup/") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    updateQueue_ = indexes;
    updateIndex_ = 0;
    SetBusy(true);
    UpdateNext();
}

void InstalledAddonsDialog::UpdateNext()
{
    if (updateIndex_ >= updateQueue_.size())
    {
        SetBusy(false);
        for (int i = 0; i < addons_.size(); ++i) UpdateItemText(i);
        labelStatus_->setText(TR("Đã cập nhật xong. Khởi động lại server để áp dụng."));
        UpdateButtons();
        return;
    }

    const int index = updateQueue_[updateIndex_];
    Addon& a = addons_[index];
    const auto file = a.latest->PrimaryFile();
    const QString safe = net::ModrinthClient::SafeFileName(file ? file->filename : QString());
    if (!file.has_value() || safe.isEmpty())
    {
        ++updateIndex_;
        UpdateNext();
        return;
    }

    // Keep the old jar before anything is replaced.
    const QString backupDir = QDir(BackupFolderFor()).filePath(a.folder);
    QDir().mkpath(backupDir);
    QFile::copy(a.path, QDir(backupDir).filePath(a.fileName));

    labelStatus_->setText(TR("Đang cập nhật (%1/%2): %3").arg(updateIndex_ + 1).arg(updateQueue_.size()).arg(a.title));
    const QString destination = QDir(QFileInfo(a.path).absolutePath()).filePath(safe);
    const QString oldPath = a.path;
    const net::ModrinthFile download = *file;

    client_->DownloadFile(
        download, destination,
        [this](qint64 received, qint64 total) {
            if (total > 0)
            {
                progress_->setRange(0, 1000);
                progress_->setValue(static_cast<int>(received * 1000 / total));
            }
        },
        [this, index, oldPath, destination, download](bool ok, QString error) {
            Addon& item = addons_[index];
            if (!ok)
            {
                SetBusy(false);
                labelStatus_->setText(TR("Lỗi khi cập nhật %1: %2 — đã dừng (file cũ vẫn còn).").arg(item.title, error));
                UpdateButtons();
                return;
            }
            if (QFileInfo(oldPath).absoluteFilePath() != QFileInfo(destination).absoluteFilePath())
            {
                QFile::remove(oldPath); // the new jar has a different name: drop the old one
            }
            item.path = destination;
            item.fileName = QFileInfo(destination).fileName();
            item.sha512 = download.sha512;
            item.currentVersion = item.latest->versionNumber;
            item.currentVersionId = item.latest->id;
            item.currentDate = item.latest->datePublished;
            item.hasUpdate = false;
            ++updateIndex_;
            UpdateNext();
        });
}

} // namespace ui
