#include "platform/StartupManager.hpp"

#include "core/Logger.hpp"

#include <windows.h>

#include <format>
#include <vector>

namespace platform
{
namespace
{
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"MinecraftServerManager";

std::wstring GetOwnExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0)
        {
            return L"";
        }
        if (written < buffer.size())
        {
            return std::wstring(buffer.data(), written);
        }
        // Path didn't fit - grow and retry (extremely long install paths).
        buffer.resize(buffer.size() * 2);
    }
}
} // namespace

bool IsStartOnBootEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    const LSTATUS status = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool SetStartOnBoot(bool enable, bool silent)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    {
        core::Logger::Instance().Error(L"StartupManager: failed to open the Run registry key.");
        return false;
    }

    bool success = true;
    if (!enable)
    {
        const LSTATUS status = RegDeleteValueW(key, kValueName);
        // ERROR_FILE_NOT_FOUND just means it was already disabled - not a failure from the caller's point of view.
        success = (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    }
    else
    {
        const std::wstring exePath = GetOwnExecutablePath();
        if (exePath.empty())
        {
            core::Logger::Instance().Error(L"StartupManager: could not determine the running executable's path.");
            RegCloseKey(key);
            return false;
        }

        std::wstring command = L"\"" + exePath + L"\"";
        if (silent)
        {
            command += L" --silent";
        }

        const LSTATUS status = RegSetValueExW(
            key,
            kValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        success = status == ERROR_SUCCESS;
    }

    RegCloseKey(key);
    if (!success)
    {
        core::Logger::Instance().Error(L"StartupManager: failed to update the Run registry value.");
    }
    return success;
}

} // namespace platform
