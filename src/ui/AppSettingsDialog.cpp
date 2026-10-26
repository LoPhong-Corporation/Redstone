#include "ui/AppSettingsDialog.hpp"

#include "platform/StartupManager.hpp"
#include "remote/RemoteControlServer.hpp"
#include "ui/AppSettings.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QLineEdit>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ui
{
namespace
{

QIcon SwatchIcon(const QColor& color)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(1, 1, 14, 14), 4, 4);
    p.end();
    return QIcon(pm);
}

// Wraps a page's content in a scroll area so tall pages still fit small screens.
QWidget* WrapScrollable(QWidget* content)
{
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
    scroll->setWidget(content);
    return scroll;
}

QWidget* NewPage(QVBoxLayout*& layoutOut, const QString& title, const QString& subtitle)
{
    auto* page = new QWidget();
    layoutOut = new QVBoxLayout(page);
    layoutOut->setContentsMargins(0, 0, 12, 0);
    layoutOut->setSpacing(8);
    AddPageHeader(layoutOut, title, subtitle);
    layoutOut->addSpacing(8);
    return page;
}

} // namespace

AppSettingsDialog::AppSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Cài đặt"));
    resize(820, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(14);

    auto* body = new QHBoxLayout();
    body->setSpacing(24);

    categories_ = new QListWidget(this);
    categories_->setFixedWidth(190);
    categories_->setIconSize(QSize(18, 18));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Color), QStringLiteral("Giao diện")));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Console), QStringLiteral("Console")));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Settings), QStringLiteral("Hệ thống")));
    categories_->addItem(new QListWidgetItem(FluentTheme::Icon(glyph::Globe), QStringLiteral("Điều khiển từ xa")));
    body->addWidget(categories_);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(WrapScrollable(BuildAppearancePage()));
    pages_->addWidget(WrapScrollable(BuildConsolePage()));
    pages_->addWidget(WrapScrollable(BuildSystemPage()));
    pages_->addWidget(WrapScrollable(BuildRemotePage()));
    body->addWidget(pages_, 1);
    root->addLayout(body, 1);

    connect(categories_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    categories_->setCurrentRow(0);

    auto* footer = new QHBoxLayout();
    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    footer->addWidget(labelStatus_, 1);
    auto* applyButton = new QPushButton(QStringLiteral("Áp dụng"), this);
    applyButton->setObjectName(QStringLiteral("btnStart"));
    auto* closeButton = new QPushButton(QStringLiteral("Đóng"), this);
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    footer->addWidget(applyButton);
    footer->addWidget(closeButton);
    root->addLayout(footer);

    connect(applyButton, &QPushButton::clicked, this, &AppSettingsDialog::OnApplyClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    LoadFromSettings();
}

QWidget* AppSettingsDialog::BuildAppearancePage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(layout, QStringLiteral("Giao diện"), QStringLiteral("Phong cách Fluent — chủ đề, màu nhấn và cỡ chữ."));

    comboTheme_ = new QComboBox();
    comboTheme_->addItems({QStringLiteral("Theo hệ thống"), QStringLiteral("Tối"), QStringLiteral("Sáng")});
    comboTheme_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Chủ đề"), QStringLiteral("Chế độ \"Theo hệ thống\" đọc cài đặt app sáng/tối của Windows lúc áp dụng."), comboTheme_));

    comboAccent_ = new QComboBox();
    const QStringList names = FluentTheme::AccentNames();
    for (int i = 0; i < names.size(); ++i)
    {
        comboAccent_->addItem(SwatchIcon(FluentTheme::AccentSwatch(i)), names[i]);
    }
    comboAccent_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Màu nhấn"), QStringLiteral("Dùng cho nút chính, thanh chọn, viền focus và công tắc."), comboAccent_));

    comboFontScale_ = new QComboBox();
    comboFontScale_->addItem(QStringLiteral("Nhỏ (90%)"), 90);
    comboFontScale_->addItem(QStringLiteral("Vừa (100%)"), 100);
    comboFontScale_->addItem(QStringLiteral("Lớn (115%)"), 115);
    comboFontScale_->addItem(QStringLiteral("Rất lớn (130%)"), 130);
    comboFontScale_->setMinimumWidth(180);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Cỡ chữ giao diện"), QStringLiteral("Phóng to/thu nhỏ toàn bộ chữ trong ứng dụng."), comboFontScale_));

    layout->addStretch(1);
    return page;
}

QWidget* AppSettingsDialog::BuildConsolePage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(layout, QStringLiteral("Console"), QStringLiteral("Cách hiển thị log của server."));

    comboConsoleFont_ = new QFontComboBox();
    comboConsoleFont_->setFontFilters(QFontComboBox::MonospacedFonts);
    comboConsoleFont_->setMinimumWidth(200);
    layout->addWidget(MakeSettingRow(QStringLiteral("Font"), QStringLiteral("Chỉ liệt kê font đơn cách (monospace)."), comboConsoleFont_));

    spinConsoleFontSize_ = new QSpinBox();
    spinConsoleFontSize_->setRange(7, 28);
    spinConsoleFontSize_->setSuffix(QStringLiteral(" pt"));
    spinConsoleFontSize_->setMinimumWidth(110);
    layout->addWidget(MakeSettingRow(QStringLiteral("Cỡ chữ console"), QString(), spinConsoleFontSize_));

    spinConsoleMaxLines_ = new QSpinBox();
    spinConsoleMaxLines_->setRange(500, 50000);
    spinConsoleMaxLines_->setSingleStep(500);
    spinConsoleMaxLines_->setMinimumWidth(110);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Số dòng tối đa hiển thị"), QStringLiteral("Dòng cũ hơn bị bỏ khỏi khung hiển thị để app luôn mượt."), spinConsoleMaxLines_));

    layout->addWidget(MakeToggleRow(QStringLiteral("Tự xuống dòng"), QStringLiteral("Dòng dài được bẻ gọn thay vì cuộn ngang."), toggleWrap_));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Tô màu theo mức log"), QStringLiteral("WARN màu vàng, ERROR/lỗi màu đỏ, dòng [manager]/[tunnel] màu nhấn."), toggleColorize_));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Hiện giờ nhận dòng"), QStringLiteral("Thêm [HH:mm:ss] đầu mỗi dòng mới (không áp dụng cho log cũ đã có)."), toggleTimestamps_));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Tự cuộn xuống cuối"), QStringLiteral("Luôn bám theo dòng mới nhất khi đang ở cuối khung."), toggleAutoScroll_));

    layout->addStretch(1);
    return page;
}

QWidget* AppSettingsDialog::BuildSystemPage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(layout, QStringLiteral("Hệ thống"), QStringLiteral("Khởi động, khay hệ thống và xác nhận thao tác."));

    layout->addWidget(MakeToggleRow(
        QStringLiteral("Khởi động cùng Windows"), QStringLiteral("Ghi vào Registry Run của user hiện tại (không cần quyền Admin)."), toggleStartWithWindows_));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Chạy ẩn (Silent) khi khởi động cùng Windows"),
        QStringLiteral("Chỉ hiện icon khay hệ thống, không bật cửa sổ. Server đã đánh dấu \"tự Start khi mở app\" vẫn tự chạy."),
        toggleSilent_));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Nút X thu xuống khay"), QStringLiteral("Tắt = bấm X sẽ thoát hẳn app (và dừng các server đang chạy)."), toggleMinimizeToTray_));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Thông báo khay hệ thống"), QStringLiteral("Báo khi server khởi động xong, dừng hoặc bị crash."), toggleTrayNotifications_));
    layout->addWidget(MakeToggleRow(
        QStringLiteral("Hỏi xác nhận trước khi Stop/Restart"), QStringLiteral("Tránh lỡ tay làm gián đoạn người chơi đang online."), toggleConfirmStop_));

    layout->addStretch(1);
    return page;
}

QWidget* AppSettingsDialog::BuildRemotePage()
{
    QVBoxLayout* layout = nullptr;
    QWidget* page = NewPage(
        layout, QStringLiteral("Điều khiển từ xa"),
        QStringLiteral("Cho phép công cụ dòng lệnh redstonectl điều khiển các server từ terminal."));

    layout->addWidget(MakeToggleRow(
        QStringLiteral("Bật điều khiển từ xa"),
        QStringLiteral("Mặc định TẮT. Khi bật, app chỉ lắng nghe ở 127.0.0.1 (chính máy này), không mở ra mạng."),
        toggleRemote_));

    spinRemotePort_ = new QSpinBox();
    spinRemotePort_->setRange(1024, 65535);
    spinRemotePort_->setMinimumWidth(120);
    layout->addWidget(MakeSettingRow(QStringLiteral("Port"), QStringLiteral("Port trên 127.0.0.1 mà redstonectl kết nối tới."), spinRemotePort_));

    editRemoteToken_ = new QLineEdit();
    editRemoteToken_->setEchoMode(QLineEdit::Password);
    editRemoteToken_->setReadOnly(true);
    editRemoteToken_->setMinimumWidth(300);
    auto* showButton = new QPushButton(QStringLiteral("Hiện"));
    auto* copyButton = new QPushButton(QStringLiteral("Sao chép"));
    auto* newButton = new QPushButton(QStringLiteral("Tạo mới"));
    auto* tokenBox = new QWidget();
    auto* tokenLayout = new QHBoxLayout(tokenBox);
    tokenLayout->setContentsMargins(0, 0, 0, 0);
    tokenLayout->setSpacing(8);
    tokenLayout->addWidget(editRemoteToken_, 1);
    tokenLayout->addWidget(showButton);
    tokenLayout->addWidget(copyButton);
    tokenLayout->addWidget(newButton);
    layout->addWidget(MakeSettingRow(
        QStringLiteral("Token truy cập"),
        QStringLiteral("Mật khẩu dùng chung. Token không bao giờ được gửi qua kết nối, chỉ một mã HMAC của nó. Tạo mới sẽ ngắt mọi phiên cũ."),
        tokenBox));

    auto* hint = new QLabel(QStringLiteral(
        "<b>Dùng từ máy khác qua SSH</b> (cần OpenSSH Server trên máy này):<br>"
        "<code>redstonectl --ssh bạn@may-chu --token &lt;token&gt; list</code><br><br>"
        "Cùng máy: <code>redstonectl --token &lt;token&gt; list</code><br><br>"
        "Chỉ các thao tác có trong app được hỗ trợ: xem/Start/Stop/Restart server, console, người chơi, backup, "
        "server.properties. Không thể thêm/xoá server, khôi phục backup hay đổi cài đặt app từ xa."));
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    layout->addWidget(hint);
    layout->addStretch(1);

    connect(showButton, &QPushButton::clicked, this, [this, showButton]() {
        const bool hidden = editRemoteToken_->echoMode() == QLineEdit::Password;
        editRemoteToken_->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        showButton->setText(hidden ? QStringLiteral("Ẩn") : QStringLiteral("Hiện"));
    });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        if (!editRemoteToken_->text().isEmpty())
        {
            QGuiApplication::clipboard()->setText(editRemoteToken_->text());
            labelStatus_->setText(QStringLiteral("Đã sao chép token vào clipboard."));
        }
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        editRemoteToken_->setText(remote::RemoteControlServer::GenerateToken());
        labelStatus_->setText(QStringLiteral("Đã tạo token mới - bấm Áp dụng để dùng."));
    });
    return page;
}

void AppSettingsDialog::LoadFromSettings()
{
    const AppSettings& s = CurrentSettings();
    comboTheme_->setCurrentIndex(static_cast<int>(s.theme));
    comboAccent_->setCurrentIndex(std::min(s.accentIndex, comboAccent_->count() - 1));
    const int scaleIndex = comboFontScale_->findData(s.fontScalePercent);
    comboFontScale_->setCurrentIndex(scaleIndex >= 0 ? scaleIndex : 1);

    comboConsoleFont_->setCurrentFont(QFont(s.consoleFontFamily));
    spinConsoleFontSize_->setValue(s.consoleFontSize);
    spinConsoleMaxLines_->setValue(s.consoleMaxLines);
    toggleWrap_->setChecked(s.consoleWrap);
    toggleColorize_->setChecked(s.consoleColorize);
    toggleTimestamps_->setChecked(s.consoleTimestamps);
    toggleAutoScroll_->setChecked(s.consoleAutoScroll);

    toggleStartWithWindows_->setChecked(platform::IsStartOnBootEnabled());
    toggleSilent_->setChecked(true);
    toggleMinimizeToTray_->setChecked(s.minimizeToTrayOnClose);
    toggleTrayNotifications_->setChecked(s.trayNotifications);
    toggleConfirmStop_->setChecked(s.confirmStopRestart);

    toggleRemote_->setChecked(s.remoteEnabled);
    spinRemotePort_->setValue(s.remotePort);
    editRemoteToken_->setText(s.remoteToken);
}

void AppSettingsDialog::OnApplyClicked()
{
    AppSettings s = CurrentSettings();
    s.theme = static_cast<AppSettings::ThemeMode>(comboTheme_->currentIndex());
    s.accentIndex = comboAccent_->currentIndex();
    s.fontScalePercent = comboFontScale_->currentData().toInt();

    s.consoleFontFamily = comboConsoleFont_->currentFont().family();
    s.consoleFontSize = spinConsoleFontSize_->value();
    s.consoleMaxLines = spinConsoleMaxLines_->value();
    s.consoleWrap = toggleWrap_->isChecked();
    s.consoleColorize = toggleColorize_->isChecked();
    s.consoleTimestamps = toggleTimestamps_->isChecked();
    s.consoleAutoScroll = toggleAutoScroll_->isChecked();

    s.minimizeToTrayOnClose = toggleMinimizeToTray_->isChecked();
    s.trayNotifications = toggleTrayNotifications_->isChecked();
    s.confirmStopRestart = toggleConfirmStop_->isChecked();

    s.remoteEnabled = toggleRemote_->isChecked();
    s.remotePort = spinRemotePort_->value();
    if (s.remoteEnabled && editRemoteToken_->text().isEmpty())
    {
        editRemoteToken_->setText(remote::RemoteControlServer::GenerateToken()); // never run with an empty secret
    }
    s.remoteToken = editRemoteToken_->text();

    CurrentSettings() = s;
    s.Save();

    const bool startupOk = platform::SetStartOnBoot(toggleStartWithWindows_->isChecked(), toggleSilent_->isChecked());

    FluentTheme::Apply(*qApp, s);
    emit settingsApplied();

    labelStatus_->setText(startupOk
        ? QStringLiteral("Đã áp dụng.")
        : QStringLiteral("Đã áp dụng, nhưng không ghi được mục khởi động cùng Windows vào Registry."));
}

} // namespace ui
