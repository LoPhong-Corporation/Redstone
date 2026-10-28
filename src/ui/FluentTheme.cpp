#include "ui/FluentTheme.hpp"
#include "ui/I18n.hpp"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QMainWindow>
#include <QDialog>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>

namespace ui
{
namespace
{

struct AccentPreset
{
    const char* name;
    const char* hex;
};

constexpr AccentPreset kAccents[] = {
    {"Xanh dương", "#0F6CBD"},
    {"Xanh ngọc", "#038387"},
    {"Xanh lá", "#107C10"},
    {"Tím", "#6B4FBB"},
    {"Hồng", "#C4314B"},
    {"Cam", "#CA5010"},
};
constexpr int kAccentCount = static_cast<int>(sizeof(kAccents) / sizeof(kAccents[0]));

FluentColors g_colors;

QString Css(const QColor& c)
{
    if (c.alpha() == 255)
    {
        return c.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

bool SystemPrefersDark()
{
    // Windows stores the app theme choice here (0 = dark). On other
    // platforms the key does not exist and the default (light) is used.
    QSettings reg(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return reg.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
}

FluentColors BuildColors(const AppSettings& s)
{
    FluentColors c;
    switch (s.theme)
    {
        case AppSettings::ThemeMode::Dark: c.dark = true; break;
        case AppSettings::ThemeMode::Light: c.dark = false; break;
        case AppSettings::ThemeMode::System: c.dark = SystemPrefersDark(); break;
    }

    const QColor accentBase(QString::fromLatin1(kAccents[std::clamp(s.accentIndex, 0, kAccentCount - 1)].hex));

    if (c.dark)
    {
        c.base = QColor("#202020");
        c.card = QColor("#2B2B2B");
        c.cardStroke = QColor("#383838");
        c.control = QColor("#353535");
        c.controlHover = QColor("#3C3C3C");
        c.controlPressed = QColor("#2E2E2E");
        c.controlStroke = QColor("#474747");
        c.text = QColor("#FFFFFF");
        c.textSecondary = QColor("#D0D0D0");
        c.textTertiary = QColor("#9E9E9E");
        c.textDisabled = QColor("#6B6B6B");
        c.accent = accentBase;
        c.accentHover = accentBase.lighter(112);
        c.accentPressed = accentBase.darker(120);
        c.accentText = accentBase.lighter(165);
        c.danger = QColor("#C42B1C");
        c.dangerHover = QColor("#D13B2C");
        c.success = QColor("#6CCB5F");
        c.warning = QColor("#F9C74F");
        c.console = QColor("#181818");
    }
    else
    {
        c.base = QColor("#F3F3F3");
        c.card = QColor("#FFFFFF");
        c.cardStroke = QColor("#E5E5E5");
        c.control = QColor("#FFFFFF");
        c.controlHover = QColor("#F5F5F5");
        c.controlPressed = QColor("#EDEDED");
        c.controlStroke = QColor("#D1D1D1");
        c.text = QColor("#1A1A1A");
        c.textSecondary = QColor("#5C5C5C");
        c.textTertiary = QColor("#767676");
        c.textDisabled = QColor("#ADADAD");
        c.accent = accentBase;
        c.accentHover = accentBase.darker(110);
        c.accentPressed = accentBase.darker(130);
        c.accentText = accentBase;
        c.danger = QColor("#C42B1C");
        c.dangerHover = QColor("#A4262C");
        c.success = QColor("#107C10");
        c.warning = QColor("#BC4B09");
        c.console = QColor("#FBFBFB");
    }
    c.accentSubtle = c.accentText;
    c.accentSubtle.setAlpha(c.dark ? 70 : 38);
    return c;
}

// Small PNG glyphs referenced from the QSS (Qt style sheets cannot draw a
// check mark or a chevron by themselves). Regenerated on every Apply().
QString WriteGlyphImage(const QString& name, int kind, const QColor& color)
{
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    dir.mkpath(QStringLiteral("MinecraftServerManager"));
    const QString path = dir.filePath(QStringLiteral("MinecraftServerManager/") + name);

    constexpr int kSize = 32; // rendered at 2x for sharpness; QSS scales via width/height
    QImage img(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    QPainterPath path2;
    if (kind == 0) // check
    {
        path2.moveTo(7.5, 17);
        path2.lineTo(13.5, 23);
        path2.lineTo(24.5, 10);
    }
    else if (kind == 1) // chevron down
    {
        path2.moveTo(8, 12);
        path2.lineTo(16, 20);
        path2.lineTo(24, 12);
    }
    else // chevron up
    {
        path2.moveTo(8, 20);
        path2.lineTo(16, 12);
        path2.lineTo(24, 20);
    }
    p.drawPath(path2);
    p.end();
    img.save(path, "PNG");
    return path;
}

QString BuildStyleSheet(const FluentColors& c, const AppSettings& s)
{
    const double fs = 10.5 * s.fontScalePercent / 100.0; // 10.5pt base: easier to read than the old 10pt
    const QString checkImg = WriteGlyphImage(QStringLiteral("check.png"), 0, QColor("#FFFFFF"));
    const QString chevDown = WriteGlyphImage(
        c.dark ? QStringLiteral("chev_down_dark.png") : QStringLiteral("chev_down_light.png"), 1, c.textSecondary);
    const QString chevUp = WriteGlyphImage(
        c.dark ? QStringLiteral("chev_up_dark.png") : QStringLiteral("chev_up_light.png"), 2, c.textSecondary);

    QString qss = QStringLiteral(R"QSS(
* { outline: 0; }
QWidget {
    color: @text@;
    font-family: "Segoe UI Variable Text", "Segoe UI", sans-serif;
    font-size: @fs@pt;
}
QMainWindow, QDialog { background-color: @base@; }
QLabel { background: transparent; }
QLabel:disabled { color: @textDis@; }
QLabel#titleLabel { font-size: @fsTitle@pt; font-weight: 600; }
QLabel#pageTitle { font-size: @fsPage@pt; font-weight: 600; }
QLabel#subtitleLabel { color: @text2@; }
QLabel#sectionLabel { color: @text2@; font-size: @fsSmall@pt; font-weight: 600; }
QLabel#statsLabel { color: @text2@; font-family: "Cascadia Mono", Consolas, monospace; font-size: @fsSmall@pt; }
QLabel#hintLabel { color: @text3@; font-size: @fsSmall@pt; }
QLabel#rowTitle { font-weight: 600; }
QLabel#rowDesc { color: @text2@; font-size: @fsSmall@pt; }
QLabel#statusLabel { font-weight: 600; font-size: @fsSub@pt; }
QLabel#glyph { font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; color: @accentText@; }

QFrame#card {
    background-color: @card@;
    border: 1px solid @cardStroke@;
    border-radius: 8px;
}
QFrame#settingRow {
    background-color: @card@;
    border: 1px solid @cardStroke@;
    border-radius: 6px;
}
QFrame#separator { background-color: @cardStroke@; border: none; max-height: 1px; min-height: 1px; }

QGroupBox {
    border: 1px solid @cardStroke@;
    border-radius: 8px;
    margin-top: 14px;
    padding: 18px 12px 12px 12px;
    font-weight: 600;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: @text@; }

/* ---- Buttons ---- */
QPushButton {
    background-color: @control@;
    border: 1px solid @controlStroke@;
    border-radius: 4px;
    padding: 6px 16px;
    min-height: 20px;
    color: @text@;
}
/* hover lifts the label 1px, press pushes it down 1px (total height unchanged) */
QPushButton:hover { background-color: @controlHover@; padding-top: 5px; padding-bottom: 7px; }
QPushButton:pressed { background-color: @controlPressed@; color: @text2@; padding-top: 7px; padding-bottom: 5px; }
QPushButton:focus { border: 1px solid @accentText@; }
QPushButton:disabled { background-color: transparent; border: 1px solid @cardStroke@; color: @textDis@; }

QPushButton#btnStart, QPushButton#btnSend, QPushButton[accent="true"] {
    background-color: @accent@;
    border: 1px solid @accent@;
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton#btnStart:hover, QPushButton#btnSend:hover, QPushButton[accent="true"]:hover {
    background-color: @accentHover@; border-color: @accentHover@;
}
QPushButton#btnStart:pressed, QPushButton#btnSend:pressed, QPushButton[accent="true"]:pressed {
    background-color: @accentPressed@; border-color: @accentPressed@; color: rgba(255,255,255,200);
}
QPushButton#btnStart:disabled, QPushButton#btnSend:disabled, QPushButton[accent="true"]:disabled {
    background-color: transparent; border: 1px solid @cardStroke@; color: @textDis@; font-weight: 400;
}
QPushButton#btnStop, QPushButton[danger="true"] {
    background-color: @danger@; border: 1px solid @danger@; color: #FFFFFF; font-weight: 600;
}
QPushButton#btnStop:hover, QPushButton[danger="true"]:hover { background-color: @dangerHover@; border-color: @dangerHover@; }
QPushButton#btnStop:pressed, QPushButton[danger="true"]:pressed { background-color: @danger@; color: rgba(255,255,255,200); }
QPushButton#btnStop:disabled, QPushButton[danger="true"]:disabled {
    background-color: transparent; border: 1px solid @cardStroke@; color: @textDis@; font-weight: 400;
}
QPushButton[subtle="true"] { background-color: transparent; border: 1px solid transparent; }
QPushButton[subtle="true"]:hover { background-color: @controlHover@; }
QPushButton[subtle="true"]:pressed { background-color: @controlPressed@; }

/* ---- Inputs ---- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background-color: @control@;
    border: 1px solid @controlStroke@;
    border-bottom: 1px solid @text3@;
    border-radius: 4px;
    padding: 5px 10px;
    selection-background-color: @accent@;
    selection-color: #FFFFFF;
}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { min-height: 20px; }
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { background-color: @controlHover@; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
    background-color: @card@;
    border-bottom: 2px solid @accentText@;
    padding-bottom: 4px;
}
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    background-color: transparent; color: @textDis@; border: 1px solid @cardStroke@;
}
QLineEdit:read-only { color: @text2@; }

QComboBox { padding-right: 30px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 28px; border: none; }
QComboBox::down-arrow { image: url("@chevDown@"); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background-color: @card@;
    border: 1px solid @cardStroke@;
    padding: 4px;
    selection-background-color: @accentSubtle@;
    selection-color: @text@;
    outline: 0;
}
QComboBox QAbstractItemView::item { padding: 6px 10px; min-height: 22px; border-radius: 4px; }

QSpinBox, QDoubleSpinBox { padding-right: 26px; }
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border; subcontrol-position: top right; width: 22px; border: none; margin-top: 2px; margin-right: 2px;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border; subcontrol-position: bottom right; width: 22px; border: none; margin-bottom: 2px; margin-right: 2px;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url("@chevUp@"); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url("@chevDown@"); width: 10px; height: 10px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover,
QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background-color: @controlPressed@; border-radius: 3px; }

QPlainTextEdit#consoleView {
    background-color: @console@;
    border: 1px solid @cardStroke@;
    border-radius: 6px;
    padding: 8px;
}
QPlainTextEdit#consoleView:focus { border: 1px solid @cardStroke@; padding: 8px; }

/* ---- Check / radio ---- */
QCheckBox, QRadioButton { spacing: 10px; background: transparent; }
QCheckBox:disabled, QRadioButton:disabled { color: @textDis@; }
QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid @text3@; border-radius: 4px; background-color: @control@; }
QCheckBox::indicator:hover { border-color: @text2@; background-color: @controlHover@; }
QCheckBox::indicator:checked { background-color: @accent@; border: 1px solid @accent@; image: url("@check@"); }
QCheckBox::indicator:checked:hover { background-color: @accentHover@; border-color: @accentHover@; }
QCheckBox::indicator:disabled { border-color: @textDis@; background-color: transparent; }
QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid @text3@; border-radius: 10px; background-color: @control@; }
QRadioButton::indicator:hover { border-color: @text2@; }
QRadioButton::indicator:checked { border: 6px solid @accent@; background-color: #FFFFFF; }

/* ---- Lists / tables ---- */
QListWidget, QListView {
    background-color: transparent; border: none; padding: 0; outline: 0;
}
QListWidget::item, QListView::item {
    padding: 9px 12px; border-radius: 4px; margin: 1px 0; color: @text@;
}
QListWidget::item:hover, QListView::item:hover { background-color: @controlHover@; }
QListWidget::item:selected, QListView::item:selected { background-color: @accentSubtle@; color: @text@; }

QTableWidget, QTableView, QTreeWidget, QTreeView {
    background-color: @card@;
    alternate-background-color: @card@;
    border: 1px solid @cardStroke@;
    border-radius: 6px;
    gridline-color: @cardStroke@;
    selection-background-color: @accentSubtle@;
    selection-color: @text@;
    outline: 0;
}
QTableWidget::item, QTableView::item, QTreeWidget::item, QTreeView::item { padding: 4px 8px; border: none; }
QTableWidget::item:selected, QTableView::item:selected, QTreeWidget::item:selected, QTreeView::item:selected {
    background-color: @accentSubtle@; color: @text@;
}
QHeaderView { background-color: transparent; }
QHeaderView::section {
    background-color: @card@; color: @text2@; border: none; border-bottom: 1px solid @cardStroke@;
    padding: 7px 10px; font-weight: 600;
}
QTableCornerButton::section { background-color: @card@; border: none; }

/* ---- Tabs ---- */
QTabWidget::pane { border: none; top: 0px; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: @text2@; padding: 8px 14px; margin-right: 2px;
    border: none; border-bottom: 2px solid transparent;
}
QTabBar::tab:hover { color: @text@; background-color: @controlHover@; border-radius: 4px; }
QTabBar::tab:selected { color: @text@; font-weight: 600; border-bottom: 2px solid @accentText@; }

/* ---- Toolbar / command bar ---- */
QToolBar { background-color: transparent; border: none; padding: 6px 20px 0px 20px; spacing: 2px; }
QToolBar::separator { background-color: @cardStroke@; width: 1px; margin: 8px 8px; }
QToolButton {
    background-color: transparent; border: 1px solid transparent; border-radius: 4px;
    padding: 6px 10px; color: @text@;
}
QToolButton:hover { background-color: @controlHover@; padding-top: 5px; padding-bottom: 7px; }
QToolButton:pressed, QToolButton:checked { background-color: @controlPressed@; padding-top: 7px; padding-bottom: 5px; }
QToolButton:disabled { color: @textDis@; }
QToolButton::menu-indicator { image: url("@chevDown@"); width: 10px; height: 10px; subcontrol-position: right center; subcontrol-origin: padding; right: 4px; }
QToolButton[popupMode="2"] { padding-right: 20px; }

/* ---- Menus / tooltips ---- */
QMenu { background-color: @card@; border: 1px solid @cardStroke@; padding: 4px; }
QMenu::item { padding: 7px 28px 7px 12px; margin: 1px 2px; border-radius: 4px; }
QMenu::item:selected { background-color: @controlHover@; }
QMenu::item:disabled { color: @textDis@; }
QMenu::separator { height: 1px; background: @cardStroke@; margin: 4px 8px; }
QMenu::icon { padding-left: 8px; }
QToolTip { background-color: @card@; color: @text@; border: 1px solid @cardStroke@; padding: 6px 8px; }

/* ---- Scrollbars ---- */
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: @scroll@; border-radius: 3px; min-height: 28px; margin: 0 3px; }
QScrollBar::handle:vertical:hover { background: @scrollHover@; margin: 0 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:horizontal { background: @scroll@; border-radius: 3px; min-width: 28px; margin: 3px 0; }
QScrollBar::handle:horizontal:hover { background: @scrollHover@; margin: 2px 0; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QProgressBar { background-color: @control@; border: none; border-radius: 3px; min-height: 6px; max-height: 6px; text-align: center; color: transparent; }
QProgressBar::chunk { background-color: @accentText@; border-radius: 3px; }

QSplitter::handle { background-color: transparent; }
QDialogButtonBox QPushButton { min-width: 88px; }
QMessageBox { background-color: @base@; }
)QSS");

    const QColor scroll = c.dark ? QColor(255, 255, 255, 90) : QColor(0, 0, 0, 80);
    const QColor scrollHover = c.dark ? QColor(255, 255, 255, 140) : QColor(0, 0, 0, 130);

    const QList<QPair<QString, QString>> tokens = {
        {QStringLiteral("@text2@"), Css(c.textSecondary)},
        {QStringLiteral("@text3@"), Css(c.textTertiary)},
        {QStringLiteral("@textDis@"), Css(c.textDisabled)},
        {QStringLiteral("@text@"), Css(c.text)},
        {QStringLiteral("@base@"), Css(c.base)},
        {QStringLiteral("@cardStroke@"), Css(c.cardStroke)},
        {QStringLiteral("@card@"), Css(c.card)},
        {QStringLiteral("@controlHover@"), Css(c.controlHover)},
        {QStringLiteral("@controlPressed@"), Css(c.controlPressed)},
        {QStringLiteral("@controlStroke@"), Css(c.controlStroke)},
        {QStringLiteral("@control@"), Css(c.control)},
        {QStringLiteral("@accentHover@"), Css(c.accentHover)},
        {QStringLiteral("@accentPressed@"), Css(c.accentPressed)},
        {QStringLiteral("@accentText@"), Css(c.accentText)},
        {QStringLiteral("@accentSubtle@"), Css(c.accentSubtle)},
        {QStringLiteral("@accent@"), Css(c.accent)},
        {QStringLiteral("@dangerHover@"), Css(c.dangerHover)},
        {QStringLiteral("@danger@"), Css(c.danger)},
        {QStringLiteral("@console@"), Css(c.console)},
        {QStringLiteral("@scrollHover@"), Css(scrollHover)},
        {QStringLiteral("@scroll@"), Css(scroll)},
        {QStringLiteral("@fsTitle@"), QString::number(fs * 1.5, 'f', 1)},
        {QStringLiteral("@fsPage@"), QString::number(fs * 2.0, 'f', 1)},
        {QStringLiteral("@fsSub@"), QString::number(fs * 1.1, 'f', 1)},
        {QStringLiteral("@fsSmall@"), QString::number(fs * 0.9, 'f', 1)},
        {QStringLiteral("@fs@"), QString::number(fs, 'f', 1)},
        {QStringLiteral("@check@"), checkImg},
        {QStringLiteral("@chevDown@"), chevDown},
        {QStringLiteral("@chevUp@"), chevUp},
    };
    for (const auto& [key, value] : tokens)
    {
        qss.replace(key, value);
    }
    return qss;
}

QPalette BuildPalette(const FluentColors& c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.base);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.control);
    p.setColor(QPalette::AlternateBase, c.card);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.control);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, c.card);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.textTertiary);
    p.setColor(QPalette::Link, c.accentText);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::Text, c.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDisabled);
    return p;
}

#ifdef _WIN32
void SyncTitleBar(QWidget* window)
{
    if (window == nullptr || !window->isWindow())
    {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr)
    {
        return;
    }
    const BOOL dark = g_colors.dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 20H1+/Win11); 19 = same on
    // older Win10 builds. Unsupported attributes just return an error code.
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    const int roundedCorners = 2;   // DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &roundedCorners, sizeof(roundedCorners));
    const int mica = 2;             // DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW (Mica), Win11 22H2+
    DwmSetWindowAttribute(hwnd, 38, &mica, sizeof(mica));
}
#else
void SyncTitleBar(QWidget*) {}
#endif

// Applies SyncTitleBar to every dialog/main window the moment it is shown,
// so windows opened later (dialogs) match the theme without extra wiring.
class TitleBarFilter : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show)
        {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget != nullptr && widget->isWindow() &&
                (qobject_cast<QMainWindow*>(widget) != nullptr || qobject_cast<QDialog*>(widget) != nullptr))
            {
                SyncTitleBar(widget);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

QStringList FluentTheme::AccentNames()
{
    QStringList names;
    for (const auto& preset : kAccents)
    {
        names << i18n::Tr(QString::fromUtf8(preset.name));
    }
    return names;
}

QColor FluentTheme::AccentSwatch(int index)
{
    return QColor(QString::fromLatin1(kAccents[std::clamp(index, 0, kAccentCount - 1)].hex));
}

void FluentTheme::Apply(QApplication& app, const AppSettings& settings)
{
    g_colors = BuildColors(settings);
    app.setPalette(BuildPalette(g_colors));
    app.setStyleSheet(BuildStyleSheet(g_colors, settings));

    static TitleBarFilter* filter = nullptr;
    if (filter == nullptr)
    {
        filter = new TitleBarFilter(&app);
        app.installEventFilter(filter);
    }
    for (QWidget* top : QApplication::topLevelWidgets())
    {
        if (qobject_cast<QMainWindow*>(top) != nullptr || qobject_cast<QDialog*>(top) != nullptr)
        {
            SyncTitleBar(top);
        }
    }
}

const FluentColors& FluentTheme::Colors()
{
    return g_colors;
}

QIcon FluentTheme::Icon(char16_t glyphCode, const QColor& color, int pixelSize)
{
    constexpr int kScale = 2;
    QPixmap pixmap(pixelSize * kScale, pixelSize * kScale);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(kScale);

    QFont font;
    font.setFamilies({QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")});
    font.setPixelSize(pixelSize);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRect(0, 0, pixelSize, pixelSize), Qt::AlignCenter, QString(QChar(glyphCode)));
    painter.end();
    return QIcon(pixmap);
}

QIcon FluentTheme::Icon(char16_t glyphCode, int pixelSize)
{
    return Icon(glyphCode, g_colors.text, pixelSize);
}

QColor FluentTheme::StateColor(const QString& stateName)
{
    if (stateName == QLatin1String("Running"))
    {
        return g_colors.success;
    }
    if (stateName == QLatin1String("Crashed"))
    {
        return g_colors.dark ? QColor("#FF99A4") : g_colors.danger;
    }
    if (stateName == QLatin1String("Stopped"))
    {
        return g_colors.textTertiary;
    }
    return g_colors.warning; // Starting / Stopping / Restarting
}

} // namespace ui
