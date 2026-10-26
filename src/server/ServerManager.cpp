#include "server/ServerManager.hpp"

#include "config/ConfigManager.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>

namespace server
{

ServerManager::ServerManager(std::shared_ptr<core::EventDispatcher> events)
    : events_(std::move(events))
{
}

std::wstring ServerManager::GenerateServerId() const
{
    // A GUID would also work here, but pulling in COM (CoCreateGuid) for
    // an id that only ever needs to be unique within this app's config
    // file is unnecessary weight. Wall-clock time + a monotonic counter
    // is unique enough for that purpose and has zero extra dependencies.
    static std::atomic<std::uint64_t> counter{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return std::format(L"srv-{}-{}", now, counter.fetch_add(1));
}

std::wstring ServerManager::AddServer(core::ServerConfig config, config::ConfigManager& configManager)
{
    config.id = GenerateServerId();

    auto instance = std::make_shared<MinecraftServer>(config, events_);
    {
        std::scoped_lock lock(mutex_);
        servers_.push_back(instance);
    }

    // Persist immediately so a crash right after adding a server does not
    // lose the profile.
    SaveAll(configManager);

    core::Logger::Instance().Info(std::format(L"Added server profile '{}' ({})", config.name, config.id));
    return config.id;
}

bool ServerManager::RemoveServer(const std::wstring& id, config::ConfigManager& configManager)
{
    std::shared_ptr<MinecraftServer> toRemove;
    {
        std::scoped_lock lock(mutex_);
        auto it = std::find_if(servers_.begin(), servers_.end(), [&](const auto& s) { return s->GetConfig().id == id; });
        if (it == servers_.end())
        {
            return false;
        }
        toRemove = *it;
        servers_.erase(it);
    }

    if (toRemove && toRemove->GetState() == core::ServerState::Running)
    {
        toRemove->Stop();
    }

    SaveAll(configManager);
    return true;
}

std::shared_ptr<MinecraftServer> ServerManager::Get(const std::wstring& id) const
{
    std::scoped_lock lock(mutex_);
    auto it = std::find_if(servers_.begin(), servers_.end(), [&](const auto& s) { return s->GetConfig().id == id; });
    return it == servers_.end() ? nullptr : *it;
}

std::vector<std::shared_ptr<MinecraftServer>> ServerManager::GetAll() const
{
    std::scoped_lock lock(mutex_);
    return servers_;
}

void ServerManager::LoadFromConfig(config::ConfigManager& configManager)
{
    auto configs = configManager.Load();
    std::scoped_lock lock(mutex_);
    servers_.clear();
    servers_.reserve(configs.size());
    for (auto& cfg : configs)
    {
        servers_.push_back(std::make_shared<MinecraftServer>(cfg, events_));
    }
    core::Logger::Instance().Info(std::format(L"Loaded {} server profile(s).", servers_.size()));
}

void ServerManager::SaveAll(config::ConfigManager& configManager) const
{
    std::vector<core::ServerConfig> allConfigs;
    for (const auto& server : GetAll())
    {
        allConfigs.push_back(server->GetConfig());
    }
    configManager.Save(allConfigs);
}

} // namespace server
