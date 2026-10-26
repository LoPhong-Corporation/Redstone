#include "core/StringUtil.hpp"

#include <windows.h>

namespace core
{

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return L"";
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength <= 0)
    {
        return L"";
    }
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideLength);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
    {
        return "";
    }
    const int narrowLength = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (narrowLength <= 0)
    {
        return "";
    }
    std::string narrow(static_cast<size_t>(narrowLength), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), narrowLength, nullptr, nullptr);
    return narrow;
}

} // namespace core
