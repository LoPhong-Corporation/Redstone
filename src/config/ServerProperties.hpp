#pragma once

// Reads/writes the Minecraft server's own server.properties file (plain
// Java Properties format: "key=value" per line, "#" comments) - distinct
// from config::ConfigManager, which manages this app's own
// data/servers.json. Used by ui::PropertiesDialog.

#include "core/Types.hpp"

#include <map>
#include <string>

namespace config
{

// Returns every key=value pair found. Comments and blank lines are
// dropped (writing preserves them separately - see WriteServerProperties).
// Returns an empty map if the file does not exist yet (the server has
// never been run) rather than treating that as an error.
std::map<std::wstring, std::wstring> ReadServerProperties(const core::ServerConfig& server);

// Merges `updates` into the existing file: known keys are updated in
// place (comments and line order preserved), unrecognized existing lines
// are left untouched, and any key in `updates` not already present is
// appended at the end. Returns false if the file could not be written.
bool WriteServerProperties(const core::ServerConfig& server, const std::map<std::wstring, std::wstring>& updates);

} // namespace config
