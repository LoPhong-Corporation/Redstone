#include "config/ConfigManager.hpp"

#include "core/Json.hpp"
#include "core/Logger.hpp"

#include <windows.h>

#include <format>
#include <fstream>
#include <sstream>

namespace config
{
namespace
{

// std::wifstream/wofstream use the active C locale to convert
// bytes<->wchar_t, which on a default "C" locale does NOT decode UTF-8 -
// it just widens each byte 1:1, corrupting anything outside ASCII. To
// avoid that pitfall entirely, the file is read/written as plain UTF-8
// bytes and converted explicitly via the Win32 code-page APIs (the same
// approach process::Process::WriteLine uses for stdin).

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

core::json::JsonValue ToJson(const core::ServerConfig& server)
{
    using core::json::JsonValue;
    JsonValue obj = JsonValue::MakeObject();
    obj.Set(L"id", JsonValue::MakeString(server.id));
    obj.Set(L"name", JsonValue::MakeString(server.name));
    obj.Set(L"directory", JsonValue::MakeString(server.directory.wstring()));
    obj.Set(L"serverJar", JsonValue::MakeString(server.serverJar.wstring()));
    obj.Set(L"javaExecutable", JsonValue::MakeString(server.javaExecutable.wstring()));
    obj.Set(L"serverType", JsonValue::MakeString(server.serverType));
    obj.Set(L"minecraftVersion", JsonValue::MakeString(server.minecraftVersion));
    obj.Set(L"minMemoryMB", JsonValue::MakeNumber(static_cast<double>(server.minMemoryMB)));
    obj.Set(L"maxMemoryMB", JsonValue::MakeNumber(static_cast<double>(server.maxMemoryMB)));
    obj.Set(L"port", JsonValue::MakeNumber(static_cast<double>(server.port)));
    obj.Set(L"autoRestart", JsonValue::MakeBool(server.autoRestart));
    obj.Set(L"autoBackup", JsonValue::MakeBool(server.autoBackup));
    obj.Set(L"autoStartOnAppLaunch", JsonValue::MakeBool(server.autoStartOnAppLaunch));
    obj.Set(L"extraJvmArgs", JsonValue::MakeString(server.extraJvmArgs));
    obj.Set(L"extraServerArgs", JsonValue::MakeString(server.extraServerArgs));
    obj.Set(L"autoBackupIntervalMinutes", JsonValue::MakeNumber(server.autoBackupIntervalMinutes));
    obj.Set(L"backupRetention", JsonValue::MakeNumber(server.backupRetention));
    obj.Set(L"stopTimeoutSeconds", JsonValue::MakeNumber(server.stopTimeoutSeconds));

    JsonValue tunnel = JsonValue::MakeObject();
    tunnel.Set(L"enabled", JsonValue::MakeBool(server.tunnel.enabled));
    tunnel.Set(L"executablePath", JsonValue::MakeString(server.tunnel.executablePath.wstring()));
    tunnel.Set(L"arguments", JsonValue::MakeString(server.tunnel.arguments));
    tunnel.Set(L"autoStartWithServer", JsonValue::MakeBool(server.tunnel.autoStartWithServer));
    obj.Set(L"tunnel", tunnel);

    JsonValue scheduledRestart = JsonValue::MakeObject();
    scheduledRestart.Set(L"enabled", JsonValue::MakeBool(server.scheduledRestart.enabled));
    scheduledRestart.Set(L"intervalHours", JsonValue::MakeNumber(server.scheduledRestart.intervalHours));
    obj.Set(L"scheduledRestart", scheduledRestart);

    return obj;
}

core::ServerConfig FromJson(const core::json::JsonValue& obj)
{
    core::ServerConfig server;
    if (const auto* v = obj.Find(L"id")) server.id = v->AsString();
    if (const auto* v = obj.Find(L"name")) server.name = v->AsString();
    if (const auto* v = obj.Find(L"directory")) server.directory = v->AsString();
    if (const auto* v = obj.Find(L"serverJar")) server.serverJar = v->AsString();
    if (const auto* v = obj.Find(L"javaExecutable")) server.javaExecutable = v->AsString();
    if (const auto* v = obj.Find(L"serverType")) server.serverType = v->AsString(L"Vanilla");
    if (const auto* v = obj.Find(L"minecraftVersion")) server.minecraftVersion = v->AsString();
    if (const auto* v = obj.Find(L"minMemoryMB")) server.minMemoryMB = static_cast<std::uint64_t>(v->AsNumber(1024));
    if (const auto* v = obj.Find(L"maxMemoryMB")) server.maxMemoryMB = static_cast<std::uint64_t>(v->AsNumber(2048));
    if (const auto* v = obj.Find(L"port")) server.port = static_cast<std::uint16_t>(v->AsNumber(25565));
    if (const auto* v = obj.Find(L"autoRestart")) server.autoRestart = v->AsBool();
    if (const auto* v = obj.Find(L"autoBackup")) server.autoBackup = v->AsBool();
    if (const auto* v = obj.Find(L"autoStartOnAppLaunch")) server.autoStartOnAppLaunch = v->AsBool();
    if (const auto* v = obj.Find(L"extraJvmArgs")) server.extraJvmArgs = v->AsString();
    if (const auto* v = obj.Find(L"extraServerArgs")) server.extraServerArgs = v->AsString();
    if (const auto* v = obj.Find(L"autoBackupIntervalMinutes")) server.autoBackupIntervalMinutes = static_cast<int>(v->AsNumber(60));
    if (const auto* v = obj.Find(L"backupRetention")) server.backupRetention = static_cast<int>(v->AsNumber(10));
    if (const auto* v = obj.Find(L"stopTimeoutSeconds")) server.stopTimeoutSeconds = static_cast<int>(v->AsNumber(30));

    if (const auto* tunnel = obj.Find(L"tunnel"))
    {
        if (const auto* v = tunnel->Find(L"enabled")) server.tunnel.enabled = v->AsBool();
        if (const auto* v = tunnel->Find(L"executablePath")) server.tunnel.executablePath = v->AsString();
        if (const auto* v = tunnel->Find(L"arguments")) server.tunnel.arguments = v->AsString();
        if (const auto* v = tunnel->Find(L"autoStartWithServer")) server.tunnel.autoStartWithServer = v->AsBool();
    }

    if (const auto* scheduledRestart = obj.Find(L"scheduledRestart"))
    {
        if (const auto* v = scheduledRestart->Find(L"enabled")) server.scheduledRestart.enabled = v->AsBool();
        if (const auto* v = scheduledRestart->Find(L"intervalHours"))
        {
            server.scheduledRestart.intervalHours = static_cast<int>(v->AsNumber(24));
        }
    }

    return server;
}

} // namespace

ConfigManager::ConfigManager(std::filesystem::path filePath)
    : filePath_(std::move(filePath))
{
}

std::vector<core::ServerConfig> ConfigManager::Load() const
{
    std::vector<core::ServerConfig> result;

    std::ifstream file(filePath_, std::ios::binary);
    if (!file.is_open())
    {
        core::Logger::Instance().Info(
            std::format(L"No existing config at {} (first run?).", filePath_.wstring()));
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::wstring text = Utf8ToWide(buffer.str());

    auto parsed = core::json::Parse(text);
    if (!parsed || parsed->type != core::json::JsonValue::Type::Array)
    {
        core::Logger::Instance().Error(
            std::format(L"Failed to parse {} as a JSON array. Starting with an empty server list.", filePath_.wstring()));
        return result;
    }

    result.reserve(parsed->arrayValue.size());
    for (const auto& item : parsed->arrayValue)
    {
        if (item.type == core::json::JsonValue::Type::Object)
        {
            result.push_back(FromJson(item));
        }
    }
    return result;
}

bool ConfigManager::Save(const std::vector<core::ServerConfig>& servers) const
{
    std::error_code errorCode;
    if (filePath_.has_parent_path())
    {
        std::filesystem::create_directories(filePath_.parent_path(), errorCode);
    }

    core::json::JsonValue array = core::json::JsonValue::MakeArray();
    for (const auto& server : servers)
    {
        array.arrayValue.push_back(ToJson(server));
    }

    std::ofstream file(filePath_, std::ios::trunc | std::ios::binary);
    if (!file.is_open())
    {
        core::Logger::Instance().Error(
            std::format(L"Failed to open {} for writing.", filePath_.wstring()));
        return false;
    }

    const std::string utf8 = WideToUtf8(core::json::Serialize(array));
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return true;
}

} // namespace config
