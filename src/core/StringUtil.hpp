#pragma once

// Shared UTF-8 <-> UTF-16 (wchar_t) conversion helpers. Extracted here
// because both config::ConfigManager and backup::BackupManager (and now
// this) need the exact same conversion, and std::wifstream/wofstream's
// default locale-based conversion does NOT decode UTF-8 correctly (see
// the longer explanation in ConfigManager.cpp) - so this explicit,
// Win32-based conversion is used everywhere text needs to cross that
// boundary.

#include <string>

namespace core
{

std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

} // namespace core
