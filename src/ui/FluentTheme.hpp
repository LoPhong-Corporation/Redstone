#pragma once

// Fluent 2 (Windows 11 / Fluent UI) look for the whole Qt Widgets app:
//  - neutral layered surfaces (base -> card -> control), light and dark
//  - one accent colour (selectable) for primary buttons, focus, selection
//  - 4px control radius / 8px card radius, 1px subtle strokes
//  - Segoe UI Variable text, Segoe Fluent Icons glyphs
//  - on Windows 11: dark/light title bar, rounded corners and Mica backdrop
//    via DWM, kept in sync with the chosen theme.

#include "ui/AppSettings.hpp"

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>

class QApplication;

namespace ui
{

// Resolved colour tokens for the active theme (see Apply()).
struct FluentColors
{
    bool dark = true;
    QColor base;          // window background (Mica-like layer)
    QColor card;          // card / panel surface
    QColor cardStroke;
    QColor control;       // standard control fill
    QColor controlHover;
    QColor controlPressed;
    QColor controlStroke;
    QColor text;          // primary
    QColor textSecondary;
    QColor textTertiary;
    QColor textDisabled;
    QColor accent;        // button fill
    QColor accentHover;
    QColor accentPressed;
    QColor accentText;    // accent used as foreground (focus line, selection bar, links)
    QColor accentSubtle;  // translucent accent for selected rows
    QColor danger;
    QColor dangerHover;
    QColor success;
    QColor warning;
    QColor console;       // console background
};

class FluentTheme
{
public:
    // Names for the accent preset combo box.
    static QStringList AccentNames();
    static QColor AccentSwatch(int index);

    // Builds palette + QSS from `settings` and applies it to the app, then
    // re-syncs every open top-level window's title bar. Safe to call again
    // whenever the user changes theme/accent/font scale.
    static void Apply(QApplication& app, const AppSettings& settings);

    // Tokens of the theme applied last (valid after the first Apply()).
    static const FluentColors& Colors();

    // Renders a Segoe Fluent Icons / Segoe MDL2 Assets glyph as an icon in
    // `color` (falls back to a plain text glyph if neither font exists).
    static QIcon Icon(char16_t glyph, const QColor& color, int pixelSize = 16);
    static QIcon Icon(char16_t glyph, int pixelSize = 16); // uses Colors().text

    // Colour for a server state name ("Running", "Crashed", ...).
    static QColor StateColor(const QString& stateName);
};

// Common Segoe Fluent Icons code points used by the app.
namespace glyph
{
inline constexpr char16_t Play = 0xE768;
inline constexpr char16_t Stop = 0xE71A;
inline constexpr char16_t Restart = 0xE72C;
inline constexpr char16_t Add = 0xE710;
inline constexpr char16_t Delete = 0xE74D;
inline constexpr char16_t Settings = 0xE713;
inline constexpr char16_t People = 0xE716;
inline constexpr char16_t Save = 0xE74E;
inline constexpr char16_t Folder = 0xE8B7;
inline constexpr char16_t Download = 0xE896;
inline constexpr char16_t Send = 0xE724;
inline constexpr char16_t Refresh = 0xE72C;
inline constexpr char16_t Edit = 0xE70F;
inline constexpr char16_t Library = 0xE8F1;
inline constexpr char16_t Puzzle = 0xEA86;
inline constexpr char16_t Globe = 0xE774;
inline constexpr char16_t Page = 0xE8A5;
inline constexpr char16_t Color = 0xE790;
inline constexpr char16_t Console = 0xE756;
inline constexpr char16_t Power = 0xE7E8;
inline constexpr char16_t Server = 0xE839;
inline constexpr char16_t Sliders = 0xE9E9;
inline constexpr char16_t Shield = 0xEA18;
inline constexpr char16_t Contact = 0xE77B;
inline constexpr char16_t Block = 0xE733;
inline constexpr char16_t Cancel = 0xE711;
inline constexpr char16_t Accept = 0xE73E;
inline constexpr char16_t ChevronDown = 0xE70D;
inline constexpr char16_t Info = 0xE946;
} // namespace glyph

} // namespace ui
