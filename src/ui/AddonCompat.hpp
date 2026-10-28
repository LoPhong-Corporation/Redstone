#pragma once

// Shared "what runs on this server?" helpers for the mod/plugin screens
// (LibraryDialog and InstalledAddonsDialog).

#include "core/Types.hpp"

#include <QString>
#include <QStringList>

namespace ui
{

// "1.21.4" from the server config, the jar file name, or logs/latest.log; empty if unknown.
QString DetectMinecraftVersion(const core::ServerConfig& cfg);

// Modrinth loader/platform names the server can run for a project type
// ("mod", "plugin", "modpack", "datapack"); empty when not loader-specific.
QStringList LoadersForServer(const core::ServerConfig& cfg, const QString& projectType);

// "mod" for Fabric/Forge/..., "plugin" for Paper/Spigot/...
QString DefaultProjectTypeFor(const core::ServerConfig& cfg);

} // namespace ui
