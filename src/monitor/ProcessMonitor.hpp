#pragma once

// Phase 2: lightweight CPU%/RAM sampling for a running java.exe process,
// using only Win32 APIs already available on any Windows 7+ box
// (GetProcessTimes + K32GetProcessMemoryInfo, both exported directly by
// kernel32.dll - no extra .lib to link).
//
// TPS/MSPT is intentionally NOT implemented here: getting it reliably
// requires either RCON or a Paper/Spigot-specific console command, and
// auto-sending an unknown "tps" command into a vanilla server's console
// every few seconds would just spam "Unknown command" for those users.
// That is left for a later iteration once the server-type/RCON story
// exists (see README's "Không nằm trong MVP này").

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

namespace monitor
{

class ProcessMonitor
{
public:
    struct Sample
    {
        double cpuPercent = 0.0;      // normalized 0-100 across all cores
        std::uint64_t memoryBytes = 0; // working set size
    };

    using SampleHandler = std::function<void(const Sample&)>;

    ProcessMonitor() = default;
    ~ProcessMonitor() { Stop(); }

    ProcessMonitor(const ProcessMonitor&) = delete;
    ProcessMonitor& operator=(const ProcessMonitor&) = delete;

    // `processHandle` is borrowed, not owned - same contract as
    // console::ConsoleController. Safe to call again after Stop().
    void Start(HANDLE processHandle, SampleHandler onSample,
        std::chrono::milliseconds interval = std::chrono::seconds(2));

    // Requests the sampling thread to stop and joins it. Because the loop
    // sleeps for `interval` between samples, this can block for up to one
    // interval - acceptable for this app's shutdown path (bounded, not
    // indefinite), same trade-off already made in ConsoleController.
    void Stop();

private:
    void Loop(HANDLE processHandle, SampleHandler onSample, std::chrono::milliseconds interval, std::stop_token stopToken);

    std::jthread thread_;
};

} // namespace monitor
