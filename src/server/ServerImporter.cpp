#include "server/ServerImporter.hpp"

#include "config/ServerProperties.hpp"
#include "core/Types.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>

namespace server
{
namespace
{

std::wstring ToLowerCopy(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

// Best-effort guess from the jar's filename. Not authoritative - the user
// can always correct it afterwards (it only affects the auto-TPS-query
// safety check from Phase 2).
std::wstring GuessServerType(const std::wstring& filenameLower)
{
    struct Rule
    {
        const wchar_t* needle;
        const wchar_t* type;
    };
    static constexpr Rule rules[] = {
        {L"paper", L"Paper"},
        {L"purpur", L"Purpur"},
        {L"folia", L"Folia"},
        {L"spigot", L"Spigot"},
        {L"bukkit", L"Bukkit"},
        {L"fabric", L"Fabric"},
        {L"forge", L"Forge"},
        {L"quilt", L"Fabric"}, // Quilt servers are Fabric-API-compatible; treat like Fabric for our TPS-safety list
    };
    for (const auto& rule : rules)
    {
        if (filenameLower.find(rule.needle) != std::wstring::npos)
        {
            return rule.type;
        }
    }
    return L"Vanilla";
}

} // namespace

std::optional<ImportedServerInfo> ScanServerDirectory(const std::filesystem::path& directory)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec))
    {
        return std::nullopt;
    }

    std::wstring chosenJar;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (entry.path().extension() != L".jar")
        {
            continue;
        }
        const std::wstring nameLower = ToLowerCopy(entry.path().filename().wstring());
        // Skip obvious non-server jars sitting in the root by mistake.
        if (nameLower.find(L"installer") != std::wstring::npos)
        {
            continue;
        }
        // Prefer the first match; if the user has more than one jar in
        // their server root, they can still correct the "Tên file jar"
        // field by hand afterwards.
        chosenJar = entry.path().filename().wstring();
        break;
    }

    if (chosenJar.empty())
    {
        return std::nullopt;
    }

    ImportedServerInfo info;
    info.jarFilename = chosenJar;
    info.guessedType = GuessServerType(ToLowerCopy(chosenJar));

    // If server.properties already exists (the server has been run
    // before), pull the real port out of it instead of assuming 25565.
    core::ServerConfig probe;
    probe.directory = directory;
    const auto props = config::ReadServerProperties(probe);
    if (const auto it = props.find(L"server-port"); it != props.end())
    {
        const int parsed = _wtoi(it->second.c_str());
        if (parsed > 0 && parsed <= 65535)
        {
            info.port = static_cast<std::uint16_t>(parsed);
        }
    }

    return info;
}

} // namespace server
