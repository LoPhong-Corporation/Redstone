#include "ui/AdvancedSettingsDialog.hpp"
#include "ui/I18n.hpp"

#include "platform/FirewallManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <thread>

namespace ui
{

AdvancedSettingsDialog::AdvancedSettingsDialog(
    std::shared_ptr<server::MinecraftServer> server,
    std::shared_ptr<server::ServerManager> serverManager,
    std::shared_ptr<config::ConfigManager> configManager,
    QWidget* parent)
    : QDialog(parent)
    , server_(std::move(server))
    , serverManager_(std::move(serverManager))
    , configManager_(std::move(configManager))
{
    setWindowTitle(TR("Cài đặt nâng cao — %1").arg(QString::fromStdWString(server_->GetConfig().name)));
    resize(520, 480);

    auto* layout = new QVBoxLayout(this);

    // --- Tunnel group (Phase 6) --------------------------------------
    auto* tunnelGroup = new QGroupBox(QStringLiteral("Network Tunnel (Playit.gg / Cloudflare Tunnel)"), this);
    auto* tunnelForm = new QFormLayout(tunnelGroup);

    comboPreset_ = new QComboBox(tunnelGroup);
    comboPreset_->addItems({TR("Tuỳ chỉnh"), QStringLiteral("Playit.gg"), QStringLiteral("Cloudflare Tunnel")});
    tunnelForm->addRow(TR("Loại:"), comboPreset_);

    labelPresetHint_ = new QLabel(tunnelGroup);
    labelPresetHint_->setObjectName(QStringLiteral("hintLabel"));
    labelPresetHint_->setWordWrap(true);
    tunnelForm->addRow(QString(), labelPresetHint_);

    auto* execRow = new QWidget(tunnelGroup);
    auto* execLayout = new QHBoxLayout(execRow);
    execLayout->setContentsMargins(0, 0, 0, 0);
    execLayout->setSpacing(6);
    editTunnelExecutable_ = new QLineEdit(execRow);
    execLayout->addWidget(editTunnelExecutable_, 1);
    auto* browseButton = new QPushButton(QStringLiteral("..."), execRow);
    browseButton->setObjectName(QStringLiteral("btnNeutral"));
    browseButton->setFixedWidth(36);
    execLayout->addWidget(browseButton);
    tunnelForm->addRow(TR("File thực thi:"), execRow);
    connect(browseButton, &QPushButton::clicked, this, &AdvancedSettingsDialog::OnBrowseTunnelExecutable);

    editTunnelArguments_ = new QLineEdit(tunnelGroup);
    tunnelForm->addRow(TR("Tham số dòng lệnh:"), editTunnelArguments_);

    checkTunnelEnabled_ = new QCheckBox(TR("Bật tunnel cho server này"), tunnelGroup);
    tunnelForm->addRow(QString(), checkTunnelEnabled_);

    checkTunnelAutoStart_ = new QCheckBox(TR("Tự khởi động tunnel cùng lúc Start server"), tunnelGroup);
    tunnelForm->addRow(QString(), checkTunnelAutoStart_);

    auto* tunnelButtonRow = new QWidget(tunnelGroup);
    auto* tunnelButtonLayout = new QHBoxLayout(tunnelButtonRow);
    tunnelButtonLayout->setContentsMargins(0, 0, 0, 0);
    buttonStartTunnel_ = new QPushButton(TR("▶ Start tunnel ngay"), tunnelButtonRow);
    buttonStartTunnel_->setObjectName(QStringLiteral("btnStart"));
    buttonStopTunnel_ = new QPushButton(QStringLiteral("■ Stop tunnel"), tunnelButtonRow);
    buttonStopTunnel_->setObjectName(QStringLiteral("btnStop"));
    tunnelButtonLayout->addWidget(buttonStartTunnel_);
    tunnelButtonLayout->addWidget(buttonStopTunnel_);
    tunnelForm->addRow(QString(), tunnelButtonRow);
    connect(buttonStartTunnel_, &QPushButton::clicked, this, &AdvancedSettingsDialog::OnStartTunnelClicked);
    connect(buttonStopTunnel_, &QPushButton::clicked, this, &AdvancedSettingsDialog::OnStopTunnelClicked);

    labelTunnelStatus_ = new QLabel(tunnelGroup);
    labelTunnelStatus_->setObjectName(QStringLiteral("hintLabel"));
    tunnelForm->addRow(QString(), labelTunnelStatus_);

    layout->addWidget(tunnelGroup);

    // --- Scheduled restart group --------------------------------------
    auto* restartGroup = new QGroupBox(TR("Tự động Restart định kỳ"), this);
    auto* restartForm = new QFormLayout(restartGroup);

    checkScheduledRestartEnabled_ = new QCheckBox(TR("Bật restart định kỳ"), restartGroup);
    restartForm->addRow(QString(), checkScheduledRestartEnabled_);

    spinRestartIntervalHours_ = new QSpinBox(restartGroup);
    spinRestartIntervalHours_->setRange(1, 168);
    spinRestartIntervalHours_->setSuffix(TR(" giờ"));
    restartForm->addRow(TR("Chu kỳ:"), spinRestartIntervalHours_);

    auto* restartHint = new QLabel(
        TR("Sẽ gửi \"say [Manager] Server sẽ tự khởi động lại sau 60 giây.\" vào chat 60 giây trước khi restart."),
        restartGroup);
    restartHint->setObjectName(QStringLiteral("hintLabel"));
    restartHint->setWordWrap(true);
    restartForm->addRow(QString(), restartHint);

    layout->addWidget(restartGroup);

    // --- Startup group ------------------------------------------------
    auto* startupGroup = new QGroupBox(TR("Khởi động"), this);
    auto* startupForm = new QFormLayout(startupGroup);

    checkAutoStartWithApp_ = new QCheckBox(TR("Tự động Start server này khi mở app"), startupGroup);
    startupForm->addRow(QString(), checkAutoStartWithApp_);

    auto* startupHint = new QLabel(
        TR("Kết hợp với \"Khởi động cùng Windows\" (🚀 Cài đặt ứng dụng ở toolbar chính) để server này "
                        "tự chạy ngay khi bật máy, không cần mở tay."),
        startupGroup);
    startupHint->setObjectName(QStringLiteral("hintLabel"));
    startupHint->setWordWrap(true);
    startupForm->addRow(QString(), startupHint);

    layout->addWidget(startupGroup);

    // --- Windows Firewall group ---------------------------------------
    auto* firewallGroup = new QGroupBox(QStringLiteral("Windows Firewall"), this);
    auto* firewallLayout = new QVBoxLayout(firewallGroup);

    auto* firewallHint = new QLabel(
        TR("Mở port TCP %1 trên Windows Firewall để người khác kết nối được từ ngoài mạng LAN. "
                        "Cần app đang chạy với quyền Administrator, nếu không sẽ báo lỗi.")
            .arg(server_->GetConfig().port),
        firewallGroup);
    firewallHint->setObjectName(QStringLiteral("hintLabel"));
    firewallHint->setWordWrap(true);
    firewallLayout->addWidget(firewallHint);

    buttonOpenFirewallPort_ = new QPushButton(TR("🔓 Mở port trên Firewall"), firewallGroup);
    buttonOpenFirewallPort_->setObjectName(QStringLiteral("btnRestart"));
    firewallLayout->addWidget(buttonOpenFirewallPort_);
    connect(buttonOpenFirewallPort_, &QPushButton::clicked, this, &AdvancedSettingsDialog::OnOpenFirewallPortClicked);

    labelFirewallStatus_ = new QLabel(firewallGroup);
    labelFirewallStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelFirewallStatus_->setWordWrap(true);
    firewallLayout->addWidget(labelFirewallStatus_);

    layout->addWidget(firewallGroup);

    labelSaveStatus_ = new QLabel(this);
    labelSaveStatus_->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(labelSaveStatus_);

    auto* buttons = new QDialogButtonBox(this);
    auto* saveButton = buttons->addButton(TR("Lưu"), QDialogButtonBox::AcceptRole);
    saveButton->setObjectName(QStringLiteral("btnStart"));
    auto* closeButton = buttons->addButton(TR("Đóng"), QDialogButtonBox::RejectRole);
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    connect(saveButton, &QPushButton::clicked, this, &AdvancedSettingsDialog::OnSaveClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(comboPreset_, &QComboBox::currentIndexChanged, this, &AdvancedSettingsDialog::OnPresetChanged);

    // Load current values.
    const auto& config = server_->GetConfig();
    editTunnelExecutable_->setText(QString::fromStdWString(config.tunnel.executablePath.wstring()));
    editTunnelArguments_->setText(QString::fromStdWString(config.tunnel.arguments));
    checkTunnelEnabled_->setChecked(config.tunnel.enabled);
    checkTunnelAutoStart_->setChecked(config.tunnel.autoStartWithServer);
    checkScheduledRestartEnabled_->setChecked(config.scheduledRestart.enabled);
    spinRestartIntervalHours_->setValue(config.scheduledRestart.intervalHours > 0 ? config.scheduledRestart.intervalHours : 24);
    checkAutoStartWithApp_->setChecked(config.autoStartOnAppLaunch);

    OnPresetChanged();
    RefreshTunnelStatus();
}

AdvancedSettingsDialog::~AdvancedSettingsDialog()
{
    // tunnelStartThread_/tunnelStopThread_/firewallThread_'s own std::jthread destructors
    // request a stop and join automatically - this just blocks here until
    // any in-flight start/stop finishes, guaranteeing neither can still
    // be calling QMetaObject::invokeMethod(this, ...) once this dialog's
    // widgets start being torn down. Same reasoning as ui::BackupsDialog.
}

void AdvancedSettingsDialog::OnPresetChanged()
{
    switch (comboPreset_->currentIndex())
    {
        case 1: // Playit.gg
            labelPresetHint_->setText(
                TR("Tải playit.exe từ playit.gg, chọn file đó ở dưới. Không cần tham số. "
                                "Lần đầu chạy sẽ in ra link để liên kết tài khoản trong console."));
            break;
        case 2: // Cloudflare Tunnel
            labelPresetHint_->setText(
                TR("Cần đã tạo Tunnel + route DNS trên Cloudflare Dashboard trước. "
                                "Tham số ví dụ: tunnel run <tên-tunnel-của-bạn>"));
            break;
        default:
            labelPresetHint_->setText(TR("Chọn 1 file thực thi bất kỳ và tham số dòng lệnh tương ứng."));
            break;
    }
}

void AdvancedSettingsDialog::OnBrowseTunnelExecutable()
{
    const QString chosen = QFileDialog::getOpenFileName(
        this, TR("Chọn file thực thi tunnel"), QString(), QStringLiteral("Executable (*.exe)"));
    if (!chosen.isEmpty())
    {
        editTunnelExecutable_->setText(chosen);
    }
}

void AdvancedSettingsDialog::RefreshTunnelStatus()
{
    labelTunnelStatus_->setText(server_->IsTunnelRunning()
        ? TR("Trạng thái: đang chạy")
        : TR("Trạng thái: đã dừng"));
}

void AdvancedSettingsDialog::SetTunnelButtonsEnabled(bool enabled)
{
    buttonStartTunnel_->setEnabled(enabled);
    buttonStopTunnel_->setEnabled(enabled);
}

void AdvancedSettingsDialog::OnStartTunnelClicked()
{
    // Apply + persist whatever is currently typed in the form FIRST -
    // otherwise StartTunnel() reads server_'s already-saved config, which
    // may still have an empty/stale executable path if the user typed a
    // new one but hasn't clicked "Lưu" yet, and the tunnel would fail to
    // start with no visible error (just stuck on "đã dừng").
    ApplyFieldsToConfig();

    SetTunnelButtonsEnabled(false);
    labelTunnelStatus_->setText(TR("Đang khởi động..."));

    auto srv = server_;
    tunnelStartThread_ = std::jthread([this, srv](std::stop_token) {
        const bool started = srv->StartTunnel();
        // Hop back to the UI thread to reflect the real outcome - without
        // this, the label above is the last thing ever shown, even once
        // the process is actually up and running (or failed to start).
        QMetaObject::invokeMethod(
            this,
            [this, started]() {
                SetTunnelButtonsEnabled(true);
                RefreshTunnelStatus();
                if (!started)
                {
                    labelTunnelStatus_->setText(
                        labelTunnelStatus_->text() +
                        TR("  (không khởi động được - xem lý do cụ thể ở tab Console của server này)"));
                }
            },
            Qt::QueuedConnection);
    });
}

void AdvancedSettingsDialog::OnStopTunnelClicked()
{
    SetTunnelButtonsEnabled(false);
    labelTunnelStatus_->setText(TR("Đang dừng..."));

    auto srv = server_;
    tunnelStopThread_ = std::jthread([this, srv](std::stop_token) {
        srv->StopTunnel();
        QMetaObject::invokeMethod(
            this,
            [this]() {
                SetTunnelButtonsEnabled(true);
                RefreshTunnelStatus();
            },
            Qt::QueuedConnection);
    });
}

void AdvancedSettingsDialog::OnOpenFirewallPortClicked()
{
    buttonOpenFirewallPort_->setEnabled(false);
    labelFirewallStatus_->setText(TR("Đang thêm rule..."));

    const std::wstring ruleName = L"MinecraftServerManager - " + server_->GetConfig().name;
    const std::uint16_t port = server_->GetConfig().port;

    firewallThread_ = std::jthread([this, ruleName, port](std::stop_token) {
        const bool ok = platform::AddFirewallRule(ruleName, port);
        QMetaObject::invokeMethod(
            this,
            [this, ok, port]() {
                buttonOpenFirewallPort_->setEnabled(true);
                labelFirewallStatus_->setText(ok
                    ? TR("Đã mở port %1. Kiểm tra thêm ở router nếu chơi ngoài mạng LAN (port forwarding).").arg(port)
                    : TR("Thất bại - hãy chạy app này với quyền Administrator rồi thử lại."));
            },
            Qt::QueuedConnection);
    });
}

void AdvancedSettingsDialog::ApplyFieldsToConfig()
{
    core::ServerConfig config = server_->GetConfig();
    config.tunnel.executablePath = editTunnelExecutable_->text().toStdWString();
    config.tunnel.arguments = editTunnelArguments_->text().toStdWString();
    config.tunnel.enabled = checkTunnelEnabled_->isChecked();
    config.tunnel.autoStartWithServer = checkTunnelAutoStart_->isChecked();
    config.scheduledRestart.enabled = checkScheduledRestartEnabled_->isChecked();
    config.scheduledRestart.intervalHours = spinRestartIntervalHours_->value();
    config.autoStartOnAppLaunch = checkAutoStartWithApp_->isChecked();

    server_->UpdateConfig(config);
    serverManager_->SaveAll(*configManager_);
}

void AdvancedSettingsDialog::OnSaveClicked()
{
    ApplyFieldsToConfig();
    labelSaveStatus_->setText(TR("Đã lưu."));
}

} // namespace ui
