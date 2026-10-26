#include "ui/InstalledAddonsDialog.hpp"

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <filesystem>

namespace ui
{
namespace
{
QString FormatSize(qint64 bytes)
{
    constexpr double kKB = 1024.0;
    constexpr double kMB = kKB * 1024.0;
    const auto b = static_cast<double>(bytes);
    if (b >= kMB) return QStringLiteral("%1 MB").arg(QString::number(b / kMB, 'f', 1));
    if (b >= kKB) return QStringLiteral("%1 KB").arg(QString::number(b / kKB, 'f', 1));
    return QStringLiteral("%1 B").arg(bytes);
}
} // namespace

InstalledAddonsDialog::InstalledAddonsDialog(core::ServerConfig server, QWidget* parent)
    : QDialog(parent)
    , server_(std::move(server))
{
    setWindowTitle(QStringLiteral("Plugin/Mod đã cài — %1").arg(QString::fromStdWString(server_.name)));
    resize(560, 440);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        QStringLiteral("Liệt kê file .jar trong thư mục plugins/ và mods/ của server này."), this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    listAddons_ = new QListWidget(this);
    layout->addWidget(listAddons_, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonDelete_ = new QPushButton(QStringLiteral("Xoá"), this);
    buttonDelete_->setObjectName(QStringLiteral("btnStop"));
    buttonDelete_->setEnabled(false);
    buttonOpenFolder_ = new QPushButton(QStringLiteral("📂 Mở thư mục server"), this);
    buttonOpenFolder_->setObjectName(QStringLiteral("btnNeutral"));
    buttonRow->addWidget(buttonDelete_);
    buttonRow->addWidget(buttonOpenFolder_);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelStatus_->setWordWrap(true);
    layout->addWidget(labelStatus_);

    connect(buttonDelete_, &QPushButton::clicked, this, &InstalledAddonsDialog::OnDeleteClicked);
    connect(buttonOpenFolder_, &QPushButton::clicked, this, &InstalledAddonsDialog::OnOpenFolderClicked);
    connect(listAddons_, &QListWidget::itemSelectionChanged, this, &InstalledAddonsDialog::OnSelectionChanged);

    Refresh();
}

void InstalledAddonsDialog::Refresh()
{
    listAddons_->clear();

    for (const wchar_t* subfolder : {L"plugins", L"mods"})
    {
        const auto dir = server_.directory / subfolder;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec))
        {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (!entry.is_regular_file() || entry.path().extension() != L".jar")
            {
                continue;
            }
            std::error_code sizeEc;
            const auto size = static_cast<qint64>(entry.file_size(sizeEc));
            const QString label = QStringLiteral("[%1] %2   ·   %3")
                .arg(QString::fromWCharArray(subfolder))
                .arg(QString::fromStdWString(entry.path().filename().wstring()))
                .arg(FormatSize(size));
            auto* item = new QListWidgetItem(label, listAddons_);
            item->setData(Qt::UserRole, QString::fromStdWString(entry.path().wstring()));
        }
    }

    if (listAddons_->count() == 0)
    {
        labelStatus_->setText(QStringLiteral("Chưa có plugin/mod nào được cài trong plugins/ hoặc mods/."));
    }
    else
    {
        labelStatus_->setText(QString());
    }
    buttonDelete_->setEnabled(false);
}

void InstalledAddonsDialog::OnSelectionChanged()
{
    buttonDelete_->setEnabled(listAddons_->currentItem() != nullptr);
}

void InstalledAddonsDialog::OnDeleteClicked()
{
    auto* item = listAddons_->currentItem();
    if (item == nullptr)
    {
        return;
    }
    const QString fullPath = item->data(Qt::UserRole).toString();

    const auto reply = QMessageBox::question(
        this,
        QStringLiteral("Xác nhận"),
        QStringLiteral("Xoá \"%1\"? Khởi động lại server để áp dụng.").arg(item->text()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    if (QFile::remove(fullPath))
    {
        labelStatus_->setText(QStringLiteral("Đã xoá."));
    }
    else
    {
        labelStatus_->setText(QStringLiteral("Không xoá được file (đang được server dùng?)."));
    }
    Refresh();
}

void InstalledAddonsDialog::OnOpenFolderClicked()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(server_.directory.wstring())));
}

} // namespace ui
