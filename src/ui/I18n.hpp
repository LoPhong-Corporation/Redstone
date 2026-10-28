#pragma once

// Runtime translation. The SOURCE strings in the code are Vietnamese (the
// original UI language); every other language is a JSON dictionary
// { "_name": "English", "<source string>": "<translation>", ... }.
//
// Dictionaries come from two places, merged in this order:
//   1. built into the exe:   resources/i18n/<code>.json   (qrc ":/i18n/<code>.json")
//   2. next to the exe:      translations/<code>.json      (add a language without rebuilding)
// A missing key simply shows the source text, so a partial translation is fine.
// Placeholders (%1, %2, {}) must be kept as in the source string.

#include <QList>
#include <QString>

#include <string>

namespace i18n
{

struct LanguageInfo
{
    QString code; // "vi", "en", ...
    QString name; // native name shown in the picker
};

// "auto" -> follows the system language (Vietnamese if the locale is vi, else English).
// Returns false if the dictionary for `code` could not be found (falls back to the source text).
bool SetLanguage(const QString& code);
QString CurrentLanguage();                  // resolved code, never "auto"
QString ResolveAuto();
QList<LanguageInfo> AvailableLanguages();   // always includes the source language "vi"

QString Tr(const QString& source);
std::wstring TrW(const wchar_t* source);

} // namespace i18n

// Marks and translates a source (Vietnamese) string literal.
#define TR(text) ::i18n::Tr(QStringLiteral(text))
