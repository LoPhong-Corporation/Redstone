#pragma once

// Owns every configured Minecraft server profile for the lifetime of the
// application. This is the object the UI layer talks to; it never touches
// process::Process or console::ConsoleController directly (spec Rule 2:
// no business logic in UI classes).

#include "core/EventDispatcher.hpp"
#include "core/Types.hpp"
#include "server/MinecraftServer.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace config
{
class ConfigManager; // fwd decl - avoids a header dependency cycle
}

namespace server
{

class ServerManager
{
public:
    explicit ServerManager(std::shared_ptr<core::EventDispatcher> events);

    // Adds a new server profile, generates its id, persists it via
    // ConfigManager, and returns the id. `config.id` is overwritten.
    std::wstring AddServer(core::ServerConfig config, config::ConfigManager& configManager);

    // Removes a profile. Stops the server first if it is running.
    // Returns false if no profile with that id exists.
    bool RemoveServer(const std::wstring& id, config::ConfigManager& configManager);

    [[nodiscard]] std::shared_ptr<MinecraftServer> Get(const std::wstring& id) const;
    [[nodiscard]] std::vector<std::shared_ptr<MinecraftServer>> GetAll() const;

    // Loads every profile from disk via ConfigManager and constructs a
    // MinecraftServer for each. Call once at startup.
    void LoadFromConfig(config::ConfigManager& configManager);

    // Persists every server's *current* config (as returned by each
    // MinecraftServer::GetConfig()) to disk. Call after mutating a
    // server's config in place via MinecraftServer::UpdateConfig() (e.g.
    // from ui::AdvancedSettingsDialog) so the change survives a restart.
    void SaveAll(config::ConfigManager& configManager) const;

private:
    [[nodiscard]] std::wstring GenerateServerId() const;

    std::shared_ptr<core::EventDispatcher> events_;

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<MinecraftServer>> servers_;
};

} // namespace server
