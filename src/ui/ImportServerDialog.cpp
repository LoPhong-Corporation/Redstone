#include "ui/ImportServerDialog.hpp"

#include "server/ServerImporter.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui
{

ImportServerDialog::ImportServerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Import Server Có Sẵn"));
    setModal(true);
    resize(480, 380);

    auto* form = new QFormLayout();

    editName_ = new QLineEdit(this);
    form->addRow(QStringLiteral("Tên server:"), editName_);

    auto* dirRow = new QWidget(this);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->setSpacing(6);
    editDirectory_ = new QLineEdit(dirRow);
    dirLayout->addWidget(editDirectory_, 1);
    auto* browseDirButton = new QPushButton(QStringLiteral("..."), dirRow);
    browseDirButton->setObjectName(QStringLiteral("btnNeutral"));
    browseDirButton->setFixedWidth(36);
    dirLayout->addWidget(browseDirButton);
    form->addRow(QStringLiteral("Thư mục server có sẵn:"), dirRow);
    connect(browseDirButton, &QPushButton::clicked, this, &ImportServerDialog::OnBrowseDirectory);

    labelDetected_ = new QLabel(
        QStringLiteral("Chọn thư mục chứa server đã có sẵn (đã có file .jar) để quét tự động."), this);
    labelDetected_->setObjectName(QStringLiteral("hintLabel"));
    labelDetected_->setWordWrap(true);
    form->addRow(QString(), labelDetected_);

    auto* javaRow = new QWidget(this);
    auto* javaLayout = new QHBoxLayout(javaRow);
    javaLayout->setContentsMargins(0, 0, 0, 0);
    javaLayout->setSpacing(6);
    editJavaPath_ = new QLineEdit(javaRow);
    editJavaPath_->setText(QStringLiteral("java.exe"));
    javaLayout->addWidget(editJavaPath_, 1);
    auto* browseJavaButton = new QPushButton(QStringLiteral("..."), javaRow);
    browseJavaButton->setObjectName(QStringLiteral("btnNeutral"));
    browseJavaButton->setFixedWidth(36);
    javaLayout->addWidget(browseJavaButton);
    form->addRow(QStringLiteral("Đường dẫn java.exe:"), javaRow);
    connect(browseJavaButton, &QPushButton::clicked, this, &ImportServerDialog::OnBrowseJavaPath);

    editMinMemory_ = new QLineEdit(this);
    editMinMemory_->setText(QStringLiteral("1024"));
    editMinMemory_->setValidator(new QIntValidator(1, 1000000, this));
    form->addRow(QStringLiteral("RAM tối thiểu (MB):"), editMinMemory_);

    editMaxMemory_ = new QLineEdit(this);
    editMaxMemory_->setText(QStringLiteral("2048"));
    editMaxMemory_->setValidator(new QIntValidator(1, 1000000, this));
    form->addRow(QStringLiteral("RAM tối đa (MB):"), editMaxMemory_);

    labelError_ = new QLabel(this);
    labelError_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    labelError_->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(this);
    auto* importButton = buttons->addButton(QStringLiteral("Import"), QDialogButtonBox::AcceptRole);
    importButton->setObjectName(QStringLiteral("btnStart"));
    auto* cancelButton = buttons->addButton(QStringLiteral("Hủy"), QDialogButtonBox::RejectRole);
    cancelButton->setObjectName(QStringLiteral("btnNeutral"));
    connect(importButton, &QPushButton::clicked, this, &ImportServerDialog::OnImportClicked);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(labelError_);
    layout->addWidget(buttons);
}

void ImportServerDialog::OnBrowseDirectory()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Chọn thư mục server có sẵn"), QDir::homePath());
    if (!chosen.isEmpty())
    {
        editDirectory_->setText(QDir::toNativeSeparators(chosen));
        RescanDirectory();
    }
}

void ImportServerDialog::OnBrowseJavaPath()
{
    const QString chosen = QFileDialog::getOpenFileName(
        this, QStringLiteral("Chọn java.exe"), QString(), QStringLiteral("java.exe;;Executable (*.exe)"));
    if (!chosen.isEmpty())
    {
        editJavaPath_->setText(QDir::toNativeSeparators(chosen));
    }
}

void ImportServerDialog::RescanDirectory()
{
    hasScanResult_ = false;
    const auto directory = editDirectory_->text().toStdWString();
    if (directory.empty())
    {
        return;
    }

    const auto scanned = server::ScanServerDirectory(directory);
    if (!scanned.has_value())
    {
        labelDetected_->setText(
            QStringLiteral("Không tìm thấy file .jar nào trong thư mục này. Kiểm tra lại đường dẫn."));
        return;
    }

    detectedJarFilename_ = scanned->jarFilename;
    detectedType_ = scanned->guessedType;
    detectedPort_ = scanned->port;
    hasScanResult_ = true;

    labelDetected_->setText(QStringLiteral("Tìm thấy: %1  ·  Loại đoán: %2  ·  Port: %3")
        .arg(QString::fromStdWString(detectedJarFilename_), QString::fromStdWString(detectedType_))
        .arg(detectedPort_));

    if (editName_->text().isEmpty())
    {
        // A friendly default: the folder's own name.
        editName_->setText(QDir(editDirectory_->text()).dirName());
    }
}

void ImportServerDialog::OnImportClicked()
{
    const QString name = editName_->text().trimmed();
    const QString directory = editDirectory_->text().trimmed();
    const QString javaPath = editJavaPath_->text().trimmed();

    if (name.isEmpty() || directory.isEmpty() || javaPath.isEmpty())
    {
        labelError_->setText(QStringLiteral("Vui lòng điền đầy đủ Tên, Thư mục và đường dẫn Java."));
        return;
    }
    if (!hasScanResult_)
    {
        labelError_->setText(QStringLiteral("Chưa quét được file jar nào - kiểm tra lại Thư mục server."));
        return;
    }

    bool minOk = false;
    bool maxOk = false;
    const unsigned long minMemory = editMinMemory_->text().toULong(&minOk);
    const unsigned long maxMemory = editMaxMemory_->text().toULong(&maxOk);
    if (!minOk || !maxOk || minMemory > maxMemory)
    {
        labelError_->setText(QStringLiteral("RAM tối thiểu/tối đa không hợp lệ."));
        return;
    }

    core::ServerConfig config;
    config.name = name.toStdWString();
    config.directory = directory.toStdWString();
    config.serverJar = detectedJarFilename_;
    config.javaExecutable = javaPath.toStdWString();
    config.serverType = detectedType_;
    config.port = detectedPort_;
    config.minMemoryMB = minMemory;
    config.maxMemoryMB = maxMemory;

    result_ = config;
    accept();
}

} // namespace ui
