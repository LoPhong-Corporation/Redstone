#include "ui/BackupsDialog.hpp"
#include "ui/I18n.hpp"

#include "backup/BackupManager.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <filesystem>

namespace ui
{
namespace
{
QString FormatSize(std::uint64_t bytes)
{
    constexpr double kKB = 1024.0;
    constexpr double kMB = kKB * 1024.0;
    constexpr double kGB = kMB * 1024.0;
    const auto b = static_cast<double>(bytes);
    if (b >= kGB) return QStringLiteral("%1 GB").arg(QString::number(b / kGB, 'f', 2));
    if (b >= kMB) return QStringLiteral("%1 MB").arg(QString::number(b / kMB, 'f', 1));
    if (b >= kKB) return QStringLiteral("%1 KB").arg(QString::number(b / kKB, 'f', 1));
    return QStringLiteral("%1 B").arg(bytes);
}
} // namespace

BackupsDialog::BackupsDialog(std::shared_ptr<server::MinecraftServer> server, QWidget* parent)
    : QDialog(parent)
    , server_(std::move(server))
{
    setWindowTitle(QStringLiteral("Backups — %1").arg(QString::fromStdWString(server_->GetConfig().name)));
    resize(580, 480);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        TR("Sao lưu world (world/world_nether/world_the_end) + server.properties thành file .zip. "
                        "Tự động giữ lại %1 backup gần nhất, các bản cũ hơn sẽ bị xoá.")
            .arg(backup::kMaxBackupsToKeep),
        this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    listBackups_ = new QListWidget(this);
    layout->addWidget(listBackups_, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonCreate_ = new QPushButton(TR("+ Tạo backup mới"), this);
    buttonCreate_->setObjectName(QStringLiteral("btnStart"));
    buttonRestore_ = new QPushButton(QStringLiteral("⭯ Restore"), this);
    buttonRestore_->setObjectName(QStringLiteral("btnRestart"));
    buttonRestore_->setEnabled(false);
    buttonDelete_ = new QPushButton(TR("Xoá"), this);
    buttonDelete_->setObjectName(QStringLiteral("btnStop"));
    buttonDelete_->setEnabled(false);
    buttonOpenFolder_ = new QPushButton(TR("📂 Mở thư mục backups"), this);
    buttonOpenFolder_->setObjectName(QStringLiteral("btnNeutral"));
    buttonRow->addWidget(buttonCreate_);
    buttonRow->addWidget(buttonRestore_);
    buttonRow->addWidget(buttonDelete_);
    buttonRow->addWidget(buttonOpenFolder_);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelStatus_->setWordWrap(true);
    layout->addWidget(labelStatus_);

    connect(buttonCreate_, &QPushButton::clicked, this, &BackupsDialog::OnCreateClicked);
    connect(buttonRestore_, &QPushButton::clicked, this, &BackupsDialog::OnRestoreClicked);
    connect(buttonDelete_, &QPushButton::clicked, this, &BackupsDialog::OnDeleteClicked);
    connect(buttonOpenFolder_, &QPushButton::clicked, this, &BackupsDialog::OnOpenFolderClicked);
    connect(listBackups_, &QListWidget::itemSelectionChanged, this, &BackupsDialog::OnSelectionChanged);

    Refresh();
}

BackupsDialog::~BackupsDialog()
{
    // createThread_/restoreThread_'s own std::jthread destructors request
    // a stop and join automatically - but since neither lambda polls a
    // stop_token (there's no safe way to interrupt mid-zip/mid-extract
    // anyway), this just blocks here until any in-flight operation
    // finishes. That is the point: it guarantees neither background
    // thread can still be calling QMetaObject::invokeMethod(this, ...)
    // after this dialog's widgets start being torn down.
}

void BackupsDialog::Refresh()
{
    listBackups_->clear();
    for (const auto& info : backup::ListBackups(server_->GetConfig()))
    {
        const QString label = QStringLiteral("%1   ·   %2")
            .arg(QString::fromStdWString(info.name), FormatSize(info.sizeBytes));
        auto* item = new QListWidgetItem(label, listBackups_);
        item->setData(Qt::UserRole, QString::fromStdWString(info.name));
    }
    buttonDelete_->setEnabled(false);
    buttonRestore_->setEnabled(false);
}

void BackupsDialog::OnSelectionChanged()
{
    const bool hasSelection = listBackups_->currentItem() != nullptr;
    buttonDelete_->setEnabled(hasSelection);
    buttonRestore_->setEnabled(hasSelection);
}

void BackupsDialog::OnCreateClicked()
{
    buttonCreate_->setEnabled(false);
    labelStatus_->setText(TR("Đang tạo backup... (có thể mất một lúc nếu world lớn)"));

    const core::ServerConfig configCopy = server_->GetConfig();
    // server_ is a shared_ptr, so capturing a copy keeps it alive for the thread.
    auto serverRef = server_;
    createThread_ = std::jthread([this, configCopy, serverRef](std::stop_token) {
        // Running server: save-off + save-all flush first, save-on afterwards,
        // so the zip never captures half-written region files.
        serverRef->PrepareForBackup();
        auto result = backup::CreateBackup(configCopy);
        serverRef->FinishBackup();
        QMetaObject::invokeMethod(
            this,
            [this, result]() {
                buttonCreate_->setEnabled(true);
                if (result.has_value())
                {
                    labelStatus_->setText(TR("Đã tạo: %1").arg(QString::fromStdWString(*result)));
                }
                else
                {
                    labelStatus_->setText(
                        TR("Không tạo được backup — server có world chưa (đã Start ít nhất 1 lần chưa)?"));
                }
                Refresh();
            },
            Qt::QueuedConnection);
    });
}

void BackupsDialog::OnRestoreClicked()
{
    auto* item = listBackups_->currentItem();
    if (item == nullptr)
    {
        return;
    }
    const QString name = item->data(Qt::UserRole).toString();

    if (server_->GetState() != core::ServerState::Stopped)
    {
        QMessageBox::warning(
            this,
            QStringLiteral("Restore"),
            TR("Server phải ở trạng thái Stopped mới restore được. Hãy Stop server trước rồi thử lại."));
        return;
    }

    const auto reply = QMessageBox::question(
        this,
        TR("Xác nhận Restore"),
        TR("Restore backup \"%1\"?\n\n"
            "World hiện tại (nếu có) sẽ được DI CHUYỂN (không xoá) sang một thư mục dự phòng trong "
            "backups/ trước khi ghi đè, nên vẫn lấy lại được nếu chọn nhầm bản.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    buttonRestore_->setEnabled(false);
    buttonCreate_->setEnabled(false);
    labelStatus_->setText(TR("Đang restore..."));

    const core::ServerConfig configCopy = server_->GetConfig();
    const std::wstring nameCopy = name.toStdWString();
    restoreThread_ = std::jthread([this, configCopy, nameCopy](std::stop_token) {
        auto result = backup::RestoreBackup(configCopy, nameCopy);
        QMetaObject::invokeMethod(
            this,
            [this, result]() {
                buttonCreate_->setEnabled(true);
                buttonRestore_->setEnabled(listBackups_->currentItem() != nullptr);

                if (!result.success)
                {
                    labelStatus_->setText(TR("Lỗi restore: %1").arg(QString::fromStdWString(result.errorMessage)));
                    return;
                }
                QString message = TR("Đã restore xong. Có thể Start lại server.");
                if (result.safetyBackupPath.has_value())
                {
                    message += TR(" World cũ đã chuyển sang: %1")
                        .arg(QString::fromStdWString(result.safetyBackupPath->wstring()));
                }
                labelStatus_->setText(message);
            },
            Qt::QueuedConnection);
    });
}

void BackupsDialog::OnDeleteClicked()
{
    auto* item = listBackups_->currentItem();
    if (item == nullptr)
    {
        return;
    }
    const QString name = item->data(Qt::UserRole).toString();

    const auto reply = QMessageBox::question(
        this,
        TR("Xác nhận"),
        TR("Xoá backup \"%1\"? Không thể hoàn tác.").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    if (backup::DeleteBackup(server_->GetConfig(), name.toStdWString()))
    {
        labelStatus_->setText(TR("Đã xoá %1.").arg(name));
    }
    else
    {
        labelStatus_->setText(TR("Không xoá được %1.").arg(name));
    }
    Refresh();
}

void BackupsDialog::OnOpenFolderClicked()
{
    const auto dir = backup::BackupsDir(server_->GetConfig());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(dir.wstring())));
}

} // namespace ui
