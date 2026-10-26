#pragma once

#include "core/Types.hpp"

#include <cstdint>
#include <string>

namespace core
{

enum class EventType
{
    ServerStateChanged,
    ConsoleLine,
    ServerCrashed,
    StatsUpdated, // Phase 2: periodic CPU%/RAM sample from monitor::ProcessMonitor
    EulaRequired, // Phase 3: server exited because eula.txt says eula=false
};

// A single application event. Kept as one flat struct (rather than a
// polymorphic hierarchy) because every event type here is small and the
// UI layer only ever needs to switch on `type` - no virtual dispatch
// needed, no extra allocations for simple cases.
struct AppEvent
{
    EventType type;
    std::wstring serverId;
    std::wstring message;              // console line text, or a human-readable detail
    ServerState state = ServerState::Stopped; // meaningful when type == ServerStateChanged
    // --- StatsUpdated fields ---
    // A StatsUpdated event carries EITHER a fresh cpu/memory sample OR a
    // freshly-parsed TPS reply, never necessarily both at once (they come
    // from two independent sources ticking at different times). cpuPercent
    // < 0 is the sentinel meaning "this event has no new cpu/memory data -
    // only look at tpsText". Real samples are always >= 0.
    double cpuPercent = -1.0;
    std::uint64_t memoryBytes = 0;
    std::wstring tpsText;               // non-empty only when a "/tps" reply was just parsed
};

} // namespace core

