#include "ui/AddServerDialog.hpp"
#include "ui/I18n.hpp"

#include "javamanager/JavaManager.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMetaObject>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <utility>

namespace ui
{
namespace
{

// Wraps a QLineEdit and a small "..." browse button into one row widget,
// so QFormLayout::addRow() still only sees a single field per row.
QWidget* MakeBrowsableRow(QWidget* parent, QLineEdit* field, QPushButton*& outBrowseButton)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    field->setParent(row);
    layout->addWidget(field, 1);
    outBrowseButton = new QPushButton(QStringLiteral("..."), row);
    outBrowseButton->setObjectName(QStringLiteral("btnNeutral"));
    outBrowseButton->setFixedWidth(36);
    layout->addWidget(outBrowseButton);
    return row;
}

} // namespace

AddServerDialog::AddServerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(TR("Thêm Server Mới"));
    setModal(true);

    auto* form = new QFormLayout();

    editName_ = new QLineEdit(this);
    form->addRow(TR("Tên server:"), editName_);

    editDirectory_ = new QLineEdit(this);
    QPushButton* browseDirectoryButton = nullptr;
    form->addRow(TR("Thư mục server:"), MakeBrowsableRow(this, editDirectory_, browseDirectoryButton));
    connect(browseDirectoryButton, &QPushButton::clicked, this, &AddServerDialog::OnBrowseDirectory);

    // Phase 5 Server Providers: download a runnable server jar directly
    // instead of requiring the user to already have one.
    providerClient_ = new net::ServerProviderClient(this);

    auto* providerRow = new QHBoxLayout();
    providerRow->setSpacing(6);
    comboProvider_ = new QComboBox(this);
    comboProvider_->addItem(TR("— Tự có file jar —"));
    comboProvider_->addItem(QStringLiteral("Vanilla"));
    comboProvider_->addItem(QStringLiteral("Paper"));
    comboProvider_->addItem(QStringLiteral("Fabric"));
    providerRow->addWidget(comboProvider_);

    comboProviderVersion_ = new QComboBox(this);
    comboProviderVersion_->setEnabled(false);
    comboProviderVersion_->setMinimumWidth(110);
    providerRow->addWidget(comboProviderVersion_, 1);

    auto* refreshVersionsButton = new QPushButton(QStringLiteral("⟳"), this);
    refreshVersionsButton->setObjectName(QStringLiteral("btnNeutral"));
    refreshVersionsButton->setFixedWidth(32);
    refreshVersionsButton->setToolTip(TR("Tải lại danh sách phiên bản"));
    providerRow->addWidget(refreshVersionsButton);

    buttonDownloadJar_ = new QPushButton(TR("⬇ Tải"), this);
    buttonDownloadJar_->setObjectName(QStringLiteral("btnSend"));
    buttonDownloadJar_->setEnabled(false);
    providerRow->addWidget(buttonDownloadJar_);

    form->addRow(TR("Tải server tự động:"), providerRow);

    labelProviderStatus_ = new QLabel(this);
    labelProviderStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelProviderStatus_->setWordWrap(true);
    form->addRow(QString(), labelProviderStatus_);

    connect(comboProvider_, &QComboBox::currentIndexChanged, this, &AddServerDialog::OnProviderChanged);
    connect(refreshVersionsButton, &QPushButton::clicked, this, &AddServerDialog::OnRefreshVersionsClicked);
    connect(buttonDownloadJar_, &QPushButton::clicked, this, &AddServerDialog::OnDownloadJarClicked);

    editJar_ = new QLineEdit(this);
    editJar_->setText(QStringLiteral("server.jar"));
    QPushButton* browseJarButton = nullptr;
    form->addRow(TR("Tên file jar:"), MakeBrowsableRow(this, editJar_, browseJarButton));
    connect(browseJarButton, &QPushButton::clicked, this, &AddServerDialog::OnBrowseJar);

    editJavaPath_ = new QLineEdit(this);
    editJavaPath_->setText(QStringLiteral("java.exe"));
    QPushButton* browseJavaButton = nullptr;
    form->addRow(TR("Đường dẫn java.exe:"), MakeBrowsableRow(this, editJavaPath_, browseJavaButton));
    connect(browseJavaButton, &QPushButton::clicked, this, &AddServerDialog::OnBrowseJavaPath);

    // Phase 3 Java Manager: offer a quick-pick of Java installs this app
    // could find on this machine, so most people never need Browse at all.
    comboDetectedJava_ = new QComboBox(this);
    comboDetectedJava_->addItem(TR("— Chọn Java đã phát hiện —"));
    for (const auto& install : javamanager::DetectInstallations())
    {
        const QString path = QString::fromStdWString(install.path.wstring());
        comboDetectedJava_->addItem(path, path); // item data = raw path, kept stable even after the
                                                  // displayed text below gains a "(Java N)" suffix
    }
    comboDetectedJava_->setEnabled(comboDetectedJava_->count() > 1);
    if (!comboDetectedJava_->isEnabled())
    {
        comboDetectedJava_->setToolTip(TR("Không tự phát hiện được Java nào - hãy dùng nút \"...\" ở trên."));
    }
    form->addRow(TR("Java đã cài (tự phát hiện):"), comboDetectedJava_);
    connect(comboDetectedJava_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index <= 0)
        {
            return;
        }
        editJavaPath_->setText(comboDetectedJava_->currentData().toString());
    });

    ProbeDetectedJavaVersions();

    comboServerType_ = new QComboBox(this);
    comboServerType_->addItems({
        QStringLiteral("Vanilla"),
        QStringLiteral("Paper"),
        QStringLiteral("Spigot"),
        QStringLiteral("Purpur"),
        QStringLiteral("Folia"),
        QStringLiteral("Fabric"),
        QStringLiteral("Forge"),
        QStringLiteral("Bukkit"),
        TR("Khác"),
    });
    form->addRow(TR("Loại server:"), comboServerType_);

    auto* tpsHint = new QLabel(
        TR("Chỉ Paper/Spigot/Purpur/Bukkit/Folia mới tự động hiện TPS (server tự trả lời lệnh /tps)."),
        this);
    tpsHint->setObjectName(QStringLiteral("hintLabel"));
    tpsHint->setWordWrap(true);
    form->addRow(QString(), tpsHint);

    editMinMemory_ = new QLineEdit(this);
    editMinMemory_->setText(QStringLiteral("1024"));
    editMinMemory_->setValidator(new QIntValidator(1, 1000000, this));
    form->addRow(TR("RAM tối thiểu (MB):"), editMinMemory_);

    editMaxMemory_ = new QLineEdit(this);
    editMaxMemory_->setText(QStringLiteral("2048"));
    editMaxMemory_->setValidator(new QIntValidator(1, 1000000, this));
    form->addRow(TR("RAM tối đa (MB):"), editMaxMemory_);

    editPort_ = new QLineEdit(this);
    editPort_->setText(QStringLiteral("25565"));
    editPort_->setValidator(new QIntValidator(1, 65535, this));
    form->addRow(QStringLiteral("Port:"), editPort_);

    labelError_ = new QLabel(this);
    labelError_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    labelError_->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("btnStart"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("btnNeutral"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, this, &AddServerDialog::OnSaveClicked);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(labelError_);
    layout->addWidget(buttons);

    resize(500, 500);
}

AddServerDialog::~AddServerDialog()
{
    // javaProbeThread_'s own std::jthread destructor requests a stop
    // (the probe loop checks stop_token between candidates) and joins -
    // guaranteeing it can't still be calling
    // QMetaObject::invokeMethod(this, ...) once this dialog's widgets
    // start being torn down. Same pattern as ui::BackupsDialog.
}

void AddServerDialog::OnBrowseDirectory()
{
    const QString startDir = editDirectory_->text().isEmpty() ? QDir::homePath() : editDirectory_->text();
    const QString chosen = QFileDialog::getExistingDirectory(this, TR("Chọn thư mục server"), startDir);
    if (!chosen.isEmpty())
    {
        editDirectory_->setText(QDir::toNativeSeparators(chosen));
    }
}

void AddServerDialog::OnBrowseJar()
{
    const QString startDir = editDirectory_->text().isEmpty() ? QDir::homePath() : editDirectory_->text();
    const QString chosen = QFileDialog::getOpenFileName(
        this, TR("Chọn file server .jar"), startDir, QStringLiteral("Java Archive (*.jar)"));
    if (chosen.isEmpty())
    {
        return;
    }

    const QFileInfo info(chosen);
    // We only ever store the *filename* here, because MinecraftServer
    // launches java with this as a relative path inside the server's
    // working directory (see MinecraftServer::Start()).
    editJar_->setText(info.fileName());

    // Convenience: if the directory field is still empty, infer it from
    // wherever the chosen jar actually lives.
    if (editDirectory_->text().isEmpty())
    {
        editDirectory_->setText(QDir::toNativeSeparators(info.absolutePath()));
    }
}

void AddServerDialog::OnBrowseJavaPath()
{
    const QString chosen = QFileDialog::getOpenFileName(
        this, TR("Chọn java.exe"), QString(), QStringLiteral("java.exe;;Executable (*.exe)"));
    if (!chosen.isEmpty())
    {
        editJavaPath_->setText(QDir::toNativeSeparators(chosen));
    }
}

void AddServerDialog::ProbeDetectedJavaVersions()
{
    if (comboDetectedJava_->count() <= 1)
    {
        return; // nothing detected - nothing to probe
    }

    // Collect (index, path) pairs first since the probing itself happens
    // on a background thread and must not touch the QComboBox directly.
    QList<std::pair<int, QString>> candidates;
    for (int i = 1; i < comboDetectedJava_->count(); ++i)
    {
        candidates.append({i, comboDetectedJava_->itemData(i).toString()});
    }

    javaProbeThread_ = std::jthread([this, candidates](std::stop_token stopToken) {
        QList<std::pair<int, int>> results; // (index, majorVersion)
        for (const auto& [index, path] : candidates)
        {
            if (stopToken.stop_requested())
            {
                return; // dialog closing - don't bother finishing the batch
            }
            if (auto major = javamanager::ProbeJavaMajorVersion(path.toStdWString()))
            {
                results.append({index, *major});
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, results]() {
                for (const auto& [index, major] : results)
                {
                    if (index < comboDetectedJava_->count())
                    {
                        const QString path = comboDetectedJava_->itemData(index).toString();
                        comboDetectedJava_->setItemText(index, QStringLiteral("%1   (Java %2)").arg(path).arg(major));
                    }
                }
            },
            Qt::QueuedConnection);
    });
}

net::ServerProviderType AddServerDialog::CurrentProviderType() const
{
    switch (comboProvider_->currentIndex())
    {
        case 1: return net::ServerProviderType::Vanilla;
        case 2: return net::ServerProviderType::Paper;
        case 3: return net::ServerProviderType::Fabric;
        default: return net::ServerProviderType::Vanilla; // unreachable while index 0 disables the version combo
    }
}

void AddServerDialog::OnProviderChanged()
{
    const bool manual = comboProvider_->currentIndex() == 0;
    comboProviderVersion_->setEnabled(!manual);
    buttonDownloadJar_->setEnabled(false);
    comboProviderVersion_->clear();
    labelProviderStatus_->setText(QString());

    if (!manual)
    {
        LoadVersionsForCurrentProvider();
    }
}

void AddServerDialog::OnRefreshVersionsClicked()
{
    if (comboProvider_->currentIndex() != 0)
    {
        LoadVersionsForCurrentProvider();
    }
}

void AddServerDialog::LoadVersionsForCurrentProvider()
{
    const net::ServerProviderType type = CurrentProviderType();
    comboProviderVersion_->clear();
    buttonDownloadJar_->setEnabled(false);
    labelProviderStatus_->setText(TR("Đang tải danh sách phiên bản..."));

    providerClient_->ListVersions(type, [this](QStringList versions, QString error) {
        if (!error.isEmpty())
        {
            labelProviderStatus_->setText(TR("Lỗi: %1").arg(error));
            return;
        }
        if (versions.isEmpty())
        {
            labelProviderStatus_->setText(TR("Không tìm thấy phiên bản nào."));
            return;
        }
        comboProviderVersion_->addItems(versions);
        buttonDownloadJar_->setEnabled(true);
        labelProviderStatus_->setText(TR("Chọn phiên bản rồi bấm \"⬇ Tải\"."));
    });
}

void AddServerDialog::OnDownloadJarClicked()
{
    const QString directory = editDirectory_->text().trimmed();
    if (directory.isEmpty())
    {
        labelProviderStatus_->setText(TR("Điền \"Thư mục server\" trước khi tải."));
        return;
    }
    const QString version = comboProviderVersion_->currentText();
    if (version.isEmpty())
    {
        return;
    }

    const net::ServerProviderType type = CurrentProviderType();
    buttonDownloadJar_->setEnabled(false);
    comboProvider_->setEnabled(false);
    comboProviderVersion_->setEnabled(false);
    labelProviderStatus_->setText(TR("Đang lấy thông tin tải xuống..."));

    providerClient_->ResolveServerJar(type, version, [this, directory](std::optional<net::ServerJarInfo> info, QString error) {
        if (!info.has_value())
        {
            labelProviderStatus_->setText(TR("Lỗi: %1").arg(error));
            buttonDownloadJar_->setEnabled(true);
            comboProvider_->setEnabled(true);
            comboProviderVersion_->setEnabled(true);
            return;
        }

        const QString destinationPath = directory + QStringLiteral("/") + info->filename;
        labelProviderStatus_->setText(TR("Đang tải %1...").arg(info->filename));

        providerClient_->DownloadFile(info->downloadUrl, destinationPath, [this, info](bool success, QString downloadError) {
            buttonDownloadJar_->setEnabled(true);
            comboProvider_->setEnabled(true);
            comboProviderVersion_->setEnabled(true);

            if (!success)
            {
                labelProviderStatus_->setText(TR("Lỗi tải file: %1").arg(downloadError));
                return;
            }
            editJar_->setText(info->filename);
            labelProviderStatus_->setText(TR("Đã tải %1 xong.").arg(info->filename));
        });
    });
}

void AddServerDialog::OnSaveClicked()
{
    const QString name = editName_->text().trimmed();
    const QString directory = editDirectory_->text().trimmed();
    const QString jar = editJar_->text().trimmed();
    const QString javaPath = editJavaPath_->text().trimmed();

    if (name.isEmpty() || directory.isEmpty() || jar.isEmpty() || javaPath.isEmpty())
    {
        labelError_->setText(TR("Vui lòng điền đầy đủ Tên, Thư mục, Jar và đường dẫn Java."));
        return;
    }

    bool minOk = false;
    bool maxOk = false;
    bool portOk = false;
    const unsigned long minMemory = editMinMemory_->text().toULong(&minOk);
    const unsigned long maxMemory = editMaxMemory_->text().toULong(&maxOk);
    const unsigned long port = editPort_->text().toULong(&portOk);

    if (!minOk || !maxOk)
    {
        labelError_->setText(TR("RAM tối thiểu/tối đa phải là số nguyên (MB)."));
        return;
    }
    if (!portOk || port == 0 || port > 65535)
    {
        labelError_->setText(TR("Port phải là số từ 1 đến 65535."));
        return;
    }
    if (minMemory > maxMemory)
    {
        labelError_->setText(TR("RAM tối thiểu không được lớn hơn RAM tối đa."));
        return;
    }

    core::ServerConfig config;
    config.name = name.toStdWString();
    config.directory = directory.toStdWString();
    config.serverJar = jar.toStdWString();
    config.javaExecutable = javaPath.toStdWString();
    config.serverType = comboServerType_->currentText().toStdWString();
    config.minMemoryMB = minMemory;
    config.maxMemoryMB = maxMemory;
    config.port = static_cast<std::uint16_t>(port);
    if (comboProvider_->currentIndex() != 0)
    {
        // Remember the Minecraft version picked from the provider - the mod/plugin
        // library uses it to pick compatible files.
        config.minecraftVersion = comboProviderVersion_->currentText().toStdWString();
    }

    result_ = config;
    accept();
}

} // namespace ui
