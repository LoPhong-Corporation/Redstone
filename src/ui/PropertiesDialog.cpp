#include "ui/PropertiesDialog.hpp"

#include "config/ServerProperties.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/FluentWidgets.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ui
{
namespace
{

enum class Kind { Text, Password, Int, Bool, Combo };

struct Field
{
    int tab;
    const wchar_t* key;
    Kind kind;
    const char* title;
    const char* description;
    const char* defaultValue;
    int min;              // Int only
    int max;              // Int only
    const char* options;  // Combo only: '|' separated
};

constexpr const char* kTabNames[] = {"Chung", "Gameplay", "Người chơi", "Mạng & hiệu năng", "Resource pack"};

// clang-format off
const Field kFields[] = {
    // ---- 0: Chung ----
    {0, L"motd", Kind::Text, "MOTD (mô tả server)", "Hiện dưới tên server trong danh sách. Hỗ trợ mã màu §.", "A Minecraft Server", 0, 0, nullptr},
    {0, L"level-name", Kind::Text, "Tên thư mục world", "Đổi tên = dùng/tạo world khác trong thư mục server.", "world", 0, 0, nullptr},
    {0, L"level-seed", Kind::Text, "Seed", "Để trống = ngẫu nhiên. Chỉ có tác dụng khi world mới được tạo.", "", 0, 0, nullptr},
    {0, L"level-type", Kind::Combo, "Kiểu world", "Chỉ áp dụng khi tạo world mới.", "minecraft\\:normal", 0, 0,
        "minecraft\\:normal|minecraft\\:flat|minecraft\\:large_biomes|minecraft\\:amplified|minecraft\\:single_biome_surface"},
    {0, L"generate-structures", Kind::Bool, "Sinh công trình", "Làng, pháo đài, đền... khi tạo chunk mới.", "true", 0, 0, nullptr},
    {0, L"max-world-size", Kind::Int, "Bán kính world tối đa", "Đơn vị block. Giá trị nhỏ giúp giới hạn dung lượng world.", "29999984", 1, 29999984, nullptr},
    {0, L"enable-status", Kind::Bool, "Hiện trong danh sách server", "Tắt = server không trả lời ping (hiện \"Can't connect\" trong danh sách).", "true", 0, 0, nullptr},

    // ---- 1: Gameplay ----
    {1, L"difficulty", Kind::Combo, "Độ khó", "", "easy", 0, 0, "peaceful|easy|normal|hard"},
    {1, L"gamemode", Kind::Combo, "Chế độ chơi mặc định", "", "survival", 0, 0, "survival|creative|adventure|spectator"},
    {1, L"force-gamemode", Kind::Bool, "Ép về chế độ mặc định", "Mỗi lần vào server người chơi bị đặt lại về chế độ mặc định.", "false", 0, 0, nullptr},
    {1, L"hardcore", Kind::Bool, "Hardcore", "Chết là bị ban khỏi world; độ khó bị khoá ở hard.", "false", 0, 0, nullptr},
    {1, L"pvp", Kind::Bool, "Cho phép PvP", "", "true", 0, 0, nullptr},
    {1, L"allow-flight", Kind::Bool, "Cho phép bay (survival)", "Cần bật nếu dùng mod/plugin cho bay, nếu không bị kick \"flying\".", "false", 0, 0, nullptr},
    {1, L"allow-nether", Kind::Bool, "Cho phép Nether", "", "true", 0, 0, nullptr},
    {1, L"spawn-monsters", Kind::Bool, "Sinh quái vật", "", "true", 0, 0, nullptr},
    {1, L"spawn-animals", Kind::Bool, "Sinh động vật", "", "true", 0, 0, nullptr},
    {1, L"spawn-npcs", Kind::Bool, "Sinh dân làng (NPC)", "", "true", 0, 0, nullptr},
    {1, L"enable-command-block", Kind::Bool, "Bật command block", "", "false", 0, 0, nullptr},
    {1, L"spawn-protection", Kind::Int, "Bán kính bảo vệ spawn", "Người không phải OP không phá/đặt block trong bán kính này. 0 = tắt.", "16", 0, 29999984, nullptr},

    // ---- 2: Người chơi ----
    {2, L"max-players", Kind::Int, "Số người chơi tối đa", "", "20", 1, 100000, nullptr},
    {2, L"white-list", Kind::Bool, "Bật whitelist", "Chỉ người trong whitelist mới vào được. Quản lý danh sách ở \"Người chơi\".", "false", 0, 0, nullptr},
    {2, L"enforce-whitelist", Kind::Bool, "Thi hành whitelist ngay", "Khi bật whitelist, người đang online mà không có trong danh sách bị kick.", "false", 0, 0, nullptr},
    {2, L"online-mode", Kind::Bool, "Online mode (xác thực tài khoản)", "Tắt cho phép bản crack nhưng kém an toàn — ai cũng giả được tên OP.", "true", 0, 0, nullptr},
    {2, L"enforce-secure-profile", Kind::Bool, "Bắt buộc chat có chữ ký", "Chỉ có tác dụng khi online-mode bật.", "true", 0, 0, nullptr},
    {2, L"prevent-proxy-connections", Kind::Bool, "Chặn kết nối qua proxy/VPN", "", "false", 0, 0, nullptr},
    {2, L"hide-online-players", Kind::Bool, "Ẩn danh sách người chơi online", "Không lộ tên người chơi trong server list.", "false", 0, 0, nullptr},
    {2, L"player-idle-timeout", Kind::Int, "Kick khi AFK (phút)", "0 = không kick.", "0", 0, 100000, nullptr},
    {2, L"op-permission-level", Kind::Int, "Cấp quyền mặc định của OP", "1 = bypass spawn protection, 2 = lệnh cơ bản, 3 = quản lý người chơi, 4 = toàn quyền.", "4", 1, 4, nullptr},

    // ---- 3: Mạng & hiệu năng ----
    {3, L"server-ip", Kind::Text, "Địa chỉ lắng nghe", "Để trống = mọi địa chỉ (khuyên dùng).", "", 0, 0, nullptr},
    {3, L"server-port", Kind::Int, "Port server", "Nhớ mở port này ở Firewall / router.", "25565", 1, 65535, nullptr},
    {3, L"view-distance", Kind::Int, "View distance (chunk)", "Giảm giá trị này là cách nhanh nhất để giảm lag.", "10", 3, 32, nullptr},
    {3, L"simulation-distance", Kind::Int, "Simulation distance (chunk)", "Phạm vi mob/cây trồng/redstone còn hoạt động.", "10", 3, 32, nullptr},
    {3, L"entity-broadcast-range-percentage", Kind::Int, "Tầm gửi thực thể (%)", "Giảm để đỡ tải mạng khi có nhiều entity.", "100", 10, 500, nullptr},
    {3, L"max-tick-time", Kind::Int, "Watchdog max-tick-time (ms)", "Server bị tắt nếu một tick kéo dài quá mức này. -1 = tắt watchdog.", "60000", -1, 2000000000, nullptr},
    {3, L"network-compression-threshold", Kind::Int, "Ngưỡng nén gói tin (byte)", "-1 = không nén (hợp với tunnel/LAN), 0 = nén tất cả.", "256", -1, 65535, nullptr},
    {3, L"sync-chunk-writes", Kind::Bool, "Ghi chunk đồng bộ", "Tắt có thể nhanh hơn nhưng dễ hỏng world nếu mất điện.", "true", 0, 0, nullptr},
    {3, L"use-native-transport", Kind::Bool, "Native transport (Linux)", "", "true", 0, 0, nullptr},
    {3, L"enable-query", Kind::Bool, "Bật GameSpy query", "", "false", 0, 0, nullptr},
    {3, L"enable-rcon", Kind::Bool, "Bật RCON", "Điều khiển từ xa — chỉ bật khi đặt mật khẩu mạnh và không mở port RCON ra Internet.", "false", 0, 0, nullptr},
    {3, L"rcon.port", Kind::Int, "RCON port", "", "25575", 1, 65535, nullptr},
    {3, L"rcon.password", Kind::Password, "RCON password", "", "", 0, 0, nullptr},

    // ---- 4: Resource pack ----
    {4, L"resource-pack", Kind::Text, "URL resource pack", "Link tải trực tiếp tới file .zip.", "", 0, 0, nullptr},
    {4, L"resource-pack-sha1", Kind::Text, "SHA-1 của resource pack", "Giúp client cache đúng bản.", "", 0, 0, nullptr},
    {4, L"require-resource-pack", Kind::Bool, "Bắt buộc dùng resource pack", "Người từ chối tải sẽ bị kick.", "false", 0, 0, nullptr},
};
// clang-format on

constexpr int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
constexpr int kTabCount = static_cast<int>(sizeof(kTabNames) / sizeof(kTabNames[0]));

} // namespace

PropertiesDialog::PropertiesDialog(core::ServerConfig server, QWidget* parent)
    : QDialog(parent)
    , server_(std::move(server))
{
    setWindowTitle(QStringLiteral("server.properties — %1").arg(QString::fromStdWString(server_.name)));
    resize(780, 640);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(10);

    auto* header = new QVBoxLayout();
    AddPageHeader(
        header,
        QStringLiteral("Cài đặt gameplay & mạng"),
        QStringLiteral("Chỉnh server.properties bằng giao diện. Các dòng không có ở đây được giữ nguyên khi lưu."));
    root->addLayout(header);

    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    std::vector<QVBoxLayout*> tabLayouts;
    for (int t = 0; t < kTabCount; ++t)
    {
        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
        auto* content = new QWidget();
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(0, 12, 10, 8);
        layout->setSpacing(8);
        scroll->setWidget(content);
        tabs->addTab(scroll, QString::fromUtf8(kTabNames[t]).replace(QLatin1Char('&'), QStringLiteral("&&"))); // "&&" = literal & (no mnemonic)
        tabLayouts.push_back(layout);
    }

    widgets_.resize(kFieldCount, nullptr);
    for (int i = 0; i < kFieldCount; ++i)
    {
        const Field& f = kFields[i];
        QWidget* control = nullptr;
        switch (f.kind)
        {
            case Kind::Text:
            case Kind::Password:
            {
                auto* edit = new QLineEdit();
                edit->setMinimumWidth(280);
                if (f.kind == Kind::Password)
                {
                    edit->setEchoMode(QLineEdit::Password);
                }
                control = edit;
                break;
            }
            case Kind::Int:
            {
                auto* spin = new QSpinBox();
                spin->setRange(f.min, f.max);
                spin->setMinimumWidth(130);
                control = spin;
                break;
            }
            case Kind::Bool:
                control = new ToggleSwitch();
                break;
            case Kind::Combo:
            {
                auto* combo = new QComboBox();
                combo->addItems(QString::fromUtf8(f.options).split(QLatin1Char('|')));
                combo->setMinimumWidth(240);
                control = combo;
                break;
            }
        }
        widgets_[static_cast<size_t>(i)] = control;
        tabLayouts[static_cast<size_t>(f.tab)]->addWidget(
            MakeSettingRow(QString::fromUtf8(f.title), QString::fromUtf8(f.description), control));
    }
    for (auto* layout : tabLayouts)
    {
        layout->addStretch(1);
    }

    auto* footer = new QHBoxLayout();
    labelStatus_ = new QLabel(this);
    labelStatus_->setObjectName(QStringLiteral("hintLabel"));
    labelStatus_->setWordWrap(true);
    footer->addWidget(labelStatus_, 1);
    auto* reloadButton = new QPushButton(QStringLiteral("Tải lại"), this);
    reloadButton->setObjectName(QStringLiteral("btnNeutral"));
    auto* saveButton = new QPushButton(QStringLiteral("Lưu"), this);
    saveButton->setObjectName(QStringLiteral("btnStart"));
    auto* closeButton = new QPushButton(QStringLiteral("Đóng"), this);
    closeButton->setObjectName(QStringLiteral("btnNeutral"));
    footer->addWidget(reloadButton);
    footer->addWidget(saveButton);
    footer->addWidget(closeButton);
    root->addLayout(footer);

    connect(reloadButton, &QPushButton::clicked, this, &PropertiesDialog::OnReloadClicked);
    connect(saveButton, &QPushButton::clicked, this, &PropertiesDialog::OnSaveClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    LoadFromFile();
}

void PropertiesDialog::LoadFromFile()
{
    loaded_ = config::ReadServerProperties(server_);
    labelStatus_->setText(loaded_.empty()
        ? QStringLiteral("Chưa tìm thấy server.properties — hãy Start server ít nhất 1 lần để Minecraft tự tạo file này. "
                         "Bạn vẫn có thể lưu: file sẽ được tạo mới.")
        : QString());

    for (int i = 0; i < kFieldCount; ++i)
    {
        const Field& f = kFields[i];
        const auto it = loaded_.find(f.key);
        const QString value = it != loaded_.end() ? QString::fromStdWString(it->second) : QString::fromUtf8(f.defaultValue);
        QWidget* w = widgets_[static_cast<size_t>(i)];

        switch (f.kind)
        {
            case Kind::Text:
            case Kind::Password:
                static_cast<QLineEdit*>(w)->setText(value);
                break;
            case Kind::Int:
            {
                bool ok = false;
                const int number = value.toInt(&ok);
                static_cast<QSpinBox*>(w)->setValue(ok ? number : QString::fromUtf8(f.defaultValue).toInt());
                break;
            }
            case Kind::Bool:
                static_cast<ToggleSwitch*>(w)->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
                break;
            case Kind::Combo:
            {
                auto* combo = static_cast<QComboBox*>(w);
                int index = combo->findText(value);
                if (index < 0)
                {
                    combo->addItem(value); // keep unknown/legacy values (e.g. level-type=default) intact
                    index = combo->count() - 1;
                }
                combo->setCurrentIndex(index);
                break;
            }
        }
    }
}

void PropertiesDialog::OnReloadClicked()
{
    LoadFromFile();
}

void PropertiesDialog::OnSaveClicked()
{
    std::map<std::wstring, std::wstring> updates;
    for (int i = 0; i < kFieldCount; ++i)
    {
        const Field& f = kFields[i];
        QWidget* w = widgets_[static_cast<size_t>(i)];
        QString value;
        switch (f.kind)
        {
            case Kind::Text:
            case Kind::Password: value = static_cast<QLineEdit*>(w)->text(); break;
            case Kind::Int: value = QString::number(static_cast<QSpinBox*>(w)->value()); break;
            case Kind::Bool: value = static_cast<ToggleSwitch*>(w)->isChecked() ? QStringLiteral("true") : QStringLiteral("false"); break;
            case Kind::Combo: value = static_cast<QComboBox*>(w)->currentText(); break;
        }

        // Only touch keys that already exist in the file, or that the user
        // moved away from the default - never pad the file with defaults.
        const bool existed = loaded_.find(f.key) != loaded_.end();
        if (existed || value != QString::fromUtf8(f.defaultValue))
        {
            updates[f.key] = value.toStdWString();
        }
    }

    if (config::WriteServerProperties(server_, updates))
    {
        loaded_ = config::ReadServerProperties(server_);
        labelStatus_->setText(QStringLiteral("Đã lưu. Khởi động lại server để áp dụng."));
    }
    else
    {
        labelStatus_->setText(QStringLiteral("Lỗi: không ghi được server.properties."));
    }
}

} // namespace ui
