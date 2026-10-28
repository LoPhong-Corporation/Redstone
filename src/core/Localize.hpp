#pragma once

// Tiny hook so non-UI code (server, backup) can emit translatable text without
// depending on Qt. ui::i18n installs the real translator at startup; until then
// (and for untranslated text) the original literal is returned unchanged.

#include <string>

namespace core
{

using LocalizerFn = std::wstring (*)(const wchar_t* source);
using LocalizerUtf8Fn = std::string (*)(const char* utf8Source);

void SetLocalizer(LocalizerFn fn, LocalizerUtf8Fn fnUtf8 = nullptr);
std::wstring Localize(const wchar_t* source);
std::string LocalizeUtf8(const char* utf8Source);

} // namespace core
