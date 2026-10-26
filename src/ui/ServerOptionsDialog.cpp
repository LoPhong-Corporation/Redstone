#include "ui/ServerOptionsDialog.hpp"

#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ui
{
namespace
{

// JVM flag presets. Index 0 = "no preset" (keeps whatever the user typed).
struct JvmPreset
{
    const char* label;
    const char* flags;
};

constexpr JvmPreset kJvmPresets[] = {
    {"Tuỳ chỉnh / không dùng preset", ""},
    {"Aikar's flags (G1GC — khuyên dùng cho Paper/Spigot, RAM ≥ 4GB)",
     "-XX:+UseG1GC -XX:+ParallelRefProcEnabled -XX:MaxGCPauseMillis=200 -XX:+UnlockExperimentalVMOptions "
     "-XX:+DisableExplicitGC -XX:+AlwaysPreTouch -XX:G1NewSizePercent=30 -XX:G1MaxNewSizePercent=40 "
     "-XX:G1HeapRegionSize=8M -XX:G1ReservePercent=20 -XX:G1HeapWastePercent=5 -XX:G1MixedGCCountTarget=4 "
     "-XX:InitiatingHeapOccupancyPercent=15 -XX:G1MixedGCLiveThresholdPercent=90 "
     "-XX:G1RSetUpdatingPauseTimePercent=5 -XX:SurvivorRatio=32 -XX:+PerfDisableSharedMem "
     "-XX:MaxTenuringThreshold=1 -Dusing.aikars.flags=https://mcflags.emc.gg -Daikars.new.flags=true"},
    {"ZGC thế hệ (chỉ Java 21+, server lớn)", "-XX:+UseZGC -XX:+ZGenerational"},
    {"Serial GC (server rất nhỏ, ít RAM)", "-XX:+UseSerialGC"},
};

QWidget* WithBrowse(QLineEdit* edit, QPushButton*& browseOut)
{
    auto* box = new QWidget();
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    browseOut = new QPushButton(QStringLiteral("Chọn..."), box);
    layout->addWidget(edit, 1);
    layout->addWidget(browseOut);
    box->setMinimumWidth(360);
    return box;
}

} // namespace

ServerOptionsDialog::ServerOptionsDialog(
    std::shared_ptr<server::MinecraftServer> server,
    std::shared_ptr<server::ServerManager> serverManager,
    std::shared_ptr<config::ConfigManager> configManager,
    QWidget* parent)
    : QDialog(parent)
    , server_(std::move(server))
    , serverManager_(std::move(serverManager))
    , configManager_(std::move(configManager))
{
    setWindowTitle(QStringLiteral("Cài đặt server — %1").arg(QString::fromStdWString(server_->GetConfig().name)));
    resize(760, 640);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 12, 0);
    layout->setSpacing(8);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    AddPageHeader(
        layout,
        QStringLiteral("Cài đặt server"),
        QStringLiteral("Áp dụng ở lần Start tiếp theo. Cài đặt gameplay (độ khó, PvP, whitelist...) nằm ở \"server.properties\"."));
    layout->addSpacing(6);

    // ---- Cơ bản ----
    layout->addWidget(MakeSectionLabel(QStringLiteral("CƠ BẢN")));
    editName_ = new QLineEdit();
    editName_->setMinimumWidth(260);
    layout->addWidget(MakeSettingRow(QStringLiteral("Tên hiển thị"), QString(), editName_));

    spinPort_ = new QSpinBox();
    spinPort_->setRange(1, 65535);
    spinPort_->setMinimumWidth(120);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Port"), QStringLiteral("Port này dùng cho nút mở Firewall/Tunnel. Port thật của server nằm ở server.properties."), spinPort_));

    spinMinMemory_ = new QSpinBox();
    spinMinMemory_->setRange(256, 262144);
    spinMinMemory_->setSingleStep(256);
    spinMinMemory_->setSuffix(QStringLiteral(" MB"));
    spinMinMemory_->setMinimumWidth(140);
    layout->addWidget(MakeSettingRow(QStringLiteral("RAM tối thiểu (-Xms)"), QString(), spinMinMemory_));

    spinMaxMemory_ = new QSpinBox();
    spinMaxMemory_->setRange(256, 262144);
    spinMaxMemory_->setSingleStep(256);
    spinMaxMemory_->setSuffix(QStringLiteral(" MB"));
    spinMaxMemory_->setMinimumWidth(140);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("RAM tối đa (-Xmx)"), QStringLiteral("Với Aikar's flags nên đặt bằng RAM tối thiểu."), spinMaxMemory_));

    editJava_ = new QLineEdit();
    QPushButton* browseJava = nullptr;
    layout->addWidget(MakeSettingRow(QStringLiteral("Java (java.exe)"), QString(), WithBrowse(editJava_, browseJava)));
    editJar_ = new QLineEdit();
    QPushButton* browseJar = nullptr;
    layout->addWidget(MakeSettingRow(QStringLiteral("File jar của server"), QString(), WithBrowse(editJar_, browseJar)));

    editMinecraftVersion_ = new QLineEdit();
    editMinecraftVersion_->setPlaceholderText(QStringLiteral("vd: 1.21.4"));
    editMinecraftVersion_->setMinimumWidth(140);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Phiên bản Minecraft"),
        QStringLiteral("Thư viện Mod/Plugin dùng để chọn file tương thích. Để trống = tự dò từ log/tên jar."),
        editMinecraftVersion_));

    // ---- Chạy ----
    layout->addSpacing(10);
    layout->addWidget(MakeSectionLabel(QStringLiteral("THAM SỐ KHỞI CHẠY")));

    comboJvmPreset_ = new QComboBox();
    for (const auto& preset : kJvmPresets)
    {
        comboJvmPreset_->addItem(QString::fromUtf8(preset.label));
    }
    comboJvmPreset_->setMinimumWidth(360);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Preset JVM flags"), QStringLiteral("Chọn để điền sẵn vào ô tham số JVM bên dưới."), comboJvmPreset_));

    editJvmArgs_ = new QLineEdit();
    editJvmArgs_->setPlaceholderText(QStringLiteral("vd: -XX:+UseG1GC -Dfile.encoding=UTF-8"));
    editJvmArgs_->setMinimumWidth(360);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Tham số JVM bổ sung"), QStringLiteral("Chèn trước -jar (sau -Xms/-Xmx)."), editJvmArgs_));

    editServerArgs_ = new QLineEdit();
    editServerArgs_->setPlaceholderText(QStringLiteral("vd: --forceUpgrade --eraseCache"));
    editServerArgs_->setMinimumWidth(360);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Tham số server bổ sung"), QStringLiteral("Thêm sau --nogui."), editServerArgs_));

    layout->addWidget(MakeToggleRow(
        QStringLiteral("Tự khởi động lại khi crash"), QStringLiteral("Tối đa 5 lần trong 10 phút, rồi dừng hẳn."), toggleAutoRestart_));

    spinStopTimeout_ = new QSpinBox();
    spinStopTimeout_->setRange(5, 600);
    spinStopTimeout_->setSuffix(QStringLiteral(" giây"));
    spinStopTimeout_->setMinimumWidth(120);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Thời gian chờ khi Stop"),
        QStringLiteral("Chờ server lưu world và tự thoát; quá thời gian này sẽ bị buộc dừng. World lớn nên đặt cao hơn."),
        spinStopTimeout_));

    // ---- Backup ----
    layout->addSpacing(10);
    layout->addWidget(MakeSectionLabel(QStringLiteral("BACKUP TỰ ĐỘNG")));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Tự backup định kỳ khi server chạy"),
        QStringLiteral("Dùng save-off + save-all flush trước khi nén nên bản backup luôn nhất quán."),
        toggleAutoBackup_));

    spinBackupInterval_ = new QSpinBox();
    spinBackupInterval_->setRange(5, 10080);
    spinBackupInterval_->setSuffix(QStringLiteral(" phút"));
    spinBackupInterval_->setMinimumWidth(130);
    layout->addWidget(MakeSettingRow(QStringLiteral("Chu kỳ backup"), QString(), spinBackupInterval_));

    spinBackupRetention_ = new QSpinBox();
    spinBackupRetention_->setRange(1, 200);
    spinBackupRetention_->setSuffix(QStringLiteral(" bản"));
    spinBackupRetention_->setMinimumWidth(120);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Số bản backup giữ lại"), QStringLiteral("Bản cũ hơn tự bị xoá sau mỗi lần backup."), spinBackupRetention_));

    layout->addStretch(1);

    // ---- Footer ----
    auto* footer = new QHBoxLayout();
    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelStatus_->setWordWrap(true);
    footer->addWidget(labelStatus_, 1);
    auto* saveButton = new QPushButton(QStringLiteral("Lưu"), this);
    saveButton->setObjectName(QStringLiteral("btnStart"));
    auto* closeButton = new QPushButton(QStringLiteral("Đóng"), this);
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    footer->addWidget(saveButton);
    footer->addWidget(closeButton);
    root->addLayout(footer);

    connect(saveButton, &QPushButton::clicked, this, &ServerOptionsDialog::OnSaveClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(browseJava, &QPushButton::clicked, this, &ServerOptionsDialog::OnBrowseJava);
    connect(browseJar, &QPushButton::clicked, this, &ServerOptionsDialog::OnBrowseJar);
    connect(comboJvmPreset_, &QComboBox::currentIndexChanged, this, &ServerOptionsDialog::OnJvmPresetChanged);

    LoadFromConfig();
}

void ServerOptionsDialog::LoadFromConfig()
{
    const core::ServerConfig& cfg = server_->GetConfig();
    editName_->setText(QString::fromStdWString(cfg.name));
    spinPort_->setValue(cfg.port);
    spinMinMemory_->setValue(static_cast<int>(cfg.minMemoryMB));
    spinMaxMemory_->setValue(static_cast<int>(cfg.maxMemoryMB));
    editJava_->setText(QString::fromStdWString(cfg.javaExecutable.wstring()));
    editJar_->setText(QString::fromStdWString(cfg.serverJar.wstring()));
    editMinecraftVersion_->setText(QString::fromStdWString(cfg.minecraftVersion));
    editJvmArgs_->setText(QString::fromStdWString(cfg.extraJvmArgs));
    editServerArgs_->setText(QString::fromStdWString(cfg.extraServerArgs));
    toggleAutoRestart_->setChecked(cfg.autoRestart);
    spinStopTimeout_->setValue(cfg.stopTimeoutSeconds);
    toggleAutoBackup_->setChecked(cfg.autoBackup);
    spinBackupInterval_->setValue(cfg.autoBackupIntervalMinutes);
    spinBackupRetention_->setValue(cfg.backupRetention);

    // Select the preset that matches the stored flags exactly, if any.
    comboJvmPreset_->blockSignals(true);
    int match = 0;
    for (int i = 1; i < static_cast<int>(sizeof(kJvmPresets) / sizeof(kJvmPresets[0])); ++i)
    {
        if (editJvmArgs_->text() == QString::fromUtf8(kJvmPresets[i].flags))
        {
            match = i;
        }
    }
    comboJvmPreset_->setCurrentIndex(match);
    comboJvmPreset_->blockSignals(false);

    if (server_->GetState() != core::ServerState::Stopped)
    {
        labelStatus_->setText(QStringLiteral("Server đang chạy — thay đổi sẽ có hiệu lực sau khi Restart."));
    }
}

void ServerOptionsDialog::OnJvmPresetChanged(int index)
{
    if (index > 0)
    {
        editJvmArgs_->setText(QString::fromUtf8(kJvmPresets[index].flags));
    }
}

void ServerOptionsDialog::OnBrowseJava()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Chọn java.exe"), editJava_->text(), QStringLiteral("java.exe (java.exe);;Tất cả (*.*)"));
    if (!path.isEmpty())
    {
        editJava_->setText(QDir::toNativeSeparators(path));
    }
}

void ServerOptionsDialog::OnBrowseJar()
{
    const QString start = QString::fromStdWString(server_->GetConfig().directory.wstring());
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Chọn file jar"), start, QStringLiteral("Jar (*.jar);;Tất cả (*.*)"));
    if (!path.isEmpty())
    {
        editJar_->setText(QDir::toNativeSeparators(path));
    }
}

void ServerOptionsDialog::OnSaveClicked()
{
    if (editName_->text().trimmed().isEmpty())
    {
        labelStatus_->setText(QStringLiteral("Tên server không được để trống."));
        return;
    }
    if (spinMinMemory_->value() > spinMaxMemory_->value())
    {
        labelStatus_->setText(QStringLiteral("RAM tối thiểu không được lớn hơn RAM tối đa."));
        return;
    }

    core::ServerConfig cfg = server_->GetConfig();
    cfg.name = editName_->text().trimmed().toStdWString();
    cfg.port = static_cast<std::uint16_t>(spinPort_->value());
    cfg.minMemoryMB = static_cast<std::uint64_t>(spinMinMemory_->value());
    cfg.maxMemoryMB = static_cast<std::uint64_t>(spinMaxMemory_->value());
    cfg.javaExecutable = editJava_->text().trimmed().toStdWString();
    cfg.serverJar = editJar_->text().trimmed().toStdWString();
    cfg.minecraftVersion = editMinecraftVersion_->text().trimmed().toStdWString();
    cfg.extraJvmArgs = editJvmArgs_->text().trimmed().toStdWString();
    cfg.extraServerArgs = editServerArgs_->text().trimmed().toStdWString();
    cfg.autoRestart = toggleAutoRestart_->isChecked();
    cfg.stopTimeoutSeconds = spinStopTimeout_->value();
    cfg.autoBackup = toggleAutoBackup_->isChecked();
    cfg.autoBackupIntervalMinutes = spinBackupInterval_->value();
    cfg.backupRetention = spinBackupRetention_->value();

    server_->UpdateConfig(cfg);
    serverManager_->SaveAll(*configManager_);
    labelStatus_->setText(QStringLiteral("Đã lưu. Khởi động lại server để áp dụng."));
}

} // namespace ui
