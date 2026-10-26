#pragma once

// Fork-inspired "import an existing server" support: scans a folder the
// user already has a Minecraft server in (e.g. moved from another
// manager, or set up by hand before installing this app) and pulls out
// enough information to pre-fill AddServerDialog/ImportServerDialog,
// instead of requiring the user to already know their jar's exact
// filename, type, and port.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace server
{

struct ImportedServerInfo
{
    std::wstring jarFilename; // just the filename, relative to `directory`
    std::wstring guessedType; // "Vanilla" | "Paper" | "Spigot" | "Purpur" | "Fabric" | "Forge" | "Bukkit" | "Folia" | "Khác"
    std::uint16_t port = 25565;
};

// Returns std::nullopt if no .jar file is found directly inside
// `directory` (sub-folders are not searched - a server's own jar always
// lives at its root).
std::optional<ImportedServerInfo> ScanServerDirectory(const std::filesystem::path& directory);

} // namespace server
