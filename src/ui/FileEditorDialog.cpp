#include "ui/FileEditorDialog.hpp"

#include "core/StringUtil.hpp"

#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <fstream>
#include <sstream>

namespace ui
{

FileEditorDialog::FileEditorDialog(std::filesystem::path filePath, QWidget* parent)
    : QDialog(parent)
    , filePath_(std::move(filePath))
{
    setWindowTitle(QStringLiteral("Sửa file — %1").arg(QString::fromStdWString(filePath_.filename().wstring())));
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);

    auto* pathLabel = new QLabel(QString::fromStdWString(filePath_.wstring()), this);
    pathLabel->setObjectName(QStringLiteral("hintLabel"));
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);

    editor_ = new QPlainTextEdit(this);
    QFont monoFont(QStringLiteral("Consolas"), 10);
    monoFont.setStyleHint(QFont::Monospace);
    editor_->setFont(monoFont);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(editor_, 1);

    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(labelStatus_);

    auto* buttons = new QDialogButtonBox(this);
    auto* reloadButton = buttons->addButton(QStringLiteral("Tải lại"), QDialogButtonBox::ResetRole);
    reloadButton->setObjectName(QStringLiteral("btnNeutral"));
    auto* saveButton = buttons->addButton(QStringLiteral("Lưu"), QDialogButtonBox::AcceptRole);
    saveButton->setObjectName(QStringLiteral("btnStart"));
    auto* closeButton = buttons->addButton(QStringLiteral("Đóng"), QDialogButtonBox::RejectRole);
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    connect(reloadButton, &QPushButton::clicked, this, &FileEditorDialog::OnReloadClicked);
    connect(saveButton, &QPushButton::clicked, this, &FileEditorDialog::OnSaveClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);

    LoadFile();
}

void FileEditorDialog::LoadFile()
{
    std::ifstream file(filePath_, std::ios::binary);
    if (!file.is_open())
    {
        labelStatus_->setText(QStringLiteral("Không mở được file (chưa tồn tại?). Gõ nội dung rồi bấm Lưu để tạo mới."));
        editor_->setPlainText(QString());
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    editor_->setPlainText(QString::fromStdWString(core::Utf8ToWide(buffer.str())));
    labelStatus_->setText(QString());
}

void FileEditorDialog::OnReloadClicked()
{
    LoadFile();
}

void FileEditorDialog::OnSaveClicked()
{
    const std::string utf8 = core::WideToUtf8(editor_->toPlainText().toStdWString());

    std::error_code ec;
    if (filePath_.has_parent_path())
    {
        std::filesystem::create_directories(filePath_.parent_path(), ec);
    }

    std::ofstream out(filePath_, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        labelStatus_->setText(QStringLiteral("Lỗi: không ghi được file."));
        return;
    }
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    labelStatus_->setText(QStringLiteral("Đã lưu. Có thể cần khởi động lại server/plugin để áp dụng."));
}

} // namespace ui
