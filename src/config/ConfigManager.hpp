#pragma once

// Persists the list of ServerConfig profiles to data/servers.json using
// the small hand-rolled JSON reader/writer in core::json (spec section 5
// places the file at data/servers.json).

#include "core/Types.hpp"

#include <filesystem>
#include <vector>

namespace config
{

class ConfigManager
{
public:
    explicit ConfigManager(std::filesystem::path filePath = L"data/servers.json");

    // Reads and parses the config file. Returns an empty vector (and logs
    // a warning, not an error - a missing file on first run is normal) if
    // the file does not exist or fails to parse.
    [[nodiscard]] std::vector<core::ServerConfig> Load() const;

    // Overwrites the config file with exactly the given list.
    bool Save(const std::vector<core::ServerConfig>& servers) const;

private:
    std::filesystem::path filePath_;
};

} // namespace config
