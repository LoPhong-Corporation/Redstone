#pragma once

// Owns everything needed to run ONE Minecraft server: its configuration,
// the underlying OS process, the two console readers (stdout/stderr) and
// the state machine described in spec section 7. ServerManager owns a
// collection of these; the UI talks to a specific server through this
// class, never through Process/ConsoleController directly.

#include "console/ConsoleBuffer.hpp"
#include "console/ConsoleController.hpp"
#include "core/EventDispatcher.hpp"
#include "core/Types.hpp"
#include "monitor/ProcessMonitor.hpp"
#include "process/Process.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace server
{

class MinecraftServer
{
public:
    MinecraftServer(core::ServerConfig config, std::shared_ptr<core::EventDispatcher> events);
    ~MinecraftServer();

    MinecraftServer(const MinecraftServer&) = delete;
    MinecraftServer& operator=(const MinecraftServer&) = delete;

    [[nodiscard]] bool Start();

    // Sends "stop" over stdin and waits up to `timeout` for the process to
    // exit gracefully before hard-killing it. Runs synchronously on
    // whichever thread calls it - the UI wraps this call in its own
    // worker thread so the window stays responsive (spec Rule 3).
    // A zero/negative `timeout` (the default) means "use the per-server
    // stopTimeoutSeconds setting".
    bool Stop(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    bool Restart();

    bool SendCommand(const std::wstring& command);

    // Hot backup support: when the server is running, disables auto-save and
    // flushes the world to disk (waits for the server's confirmation up to
    // `timeout`) so a zip taken right after is consistent. Pair every call
    // with FinishBackup(), which re-enables auto-save. No-ops for a stopped
    // server. Returns true if the flush was confirmed (or nothing to flush).
    bool PrepareForBackup(std::chrono::seconds timeout = std::chrono::seconds(30));
    void FinishBackup();

    // Phase 3 EULA helper: rewrites eula.txt with eula=true (creating the
    // file if it does not exist yet), preserving any other lines already
    // in it. Does NOT start the server - the caller decides whether/when
    // to call Start() afterwards. This never runs silently on its own:
    // it is only ever invoked in direct response to the user explicitly
    // agreeing to the EULA in the UI (see ui::MainWindow's EULA prompt).
    bool AcceptEula() const;

    // Phase 6: starts/stops the optional companion process (Playit.gg
    // agent, cloudflared, or any executable) configured in
    // config_.tunnel. Its console output is folded into this server's own
    // console (prefixed "[tunnel] "), so no separate UI plumbing is
    // needed to see it. Independent of the Minecraft process itself -
    // callable whether or not the server is currently running.
    bool StartTunnel();
    bool StopTunnel();
    [[nodiscard]] bool IsTunnelRunning() const { return tunnelProcess_.IsRunning(); }

    [[nodiscard]] core::ServerState GetState() const { return state_.load(); }
    [[nodiscard]] const core::ServerConfig& GetConfig() const { return config_; }
    void UpdateConfig(core::ServerConfig config) { config_ = std::move(config); }

    [[nodiscard]] std::vector<std::wstring> GetConsoleLines() const { return consoleBuffer_.GetAll(); }

    // Players currently connected, tracked from "<name> joined/left the
    // game" console lines (works for Vanilla/Paper/Spigot/Fabric/Forge).
    // Cleared whenever the server (re)starts or stops. Thread-safe.
    [[nodiscard]] std::vector<std::wstring> GetOnlinePlayers() const;

private:
    void SetState(core::ServerState newState);
    void OnConsoleLine(const std::wstring& line);
    void OnStatsSample(const monitor::ProcessMonitor::Sample& sample); // Phase 2
    [[nodiscard]] bool SupportsTpsQuery() const; // Phase 2: Paper/Spigot/Purpur/... only
    void WatchForExit();       // runs on watcherThread_
    void HandleUnexpectedExit();
    [[nodiscard]] bool ShouldAutoRestart();
    [[nodiscard]] bool IsEulaNotAccepted() const; // Phase 3
    void ScheduledRestartLoop(std::stop_token stopToken); // runs on scheduledRestartThread_
    void AutoBackupLoop(std::stop_token stopToken);       // runs on autoBackupThread_
    void TrackPlayerLine(const std::wstring& line);
    void ClearOnlinePlayers();

    core::ServerConfig config_;
    std::shared_ptr<core::EventDispatcher> events_;

    process::Process process_;
    monitor::ProcessMonitor processMonitor_; // declared right after process_ so it is
                                              // destroyed (and its thread stopped) BEFORE
                                              // process_ closes the handle it borrows
    console::ConsoleController stdoutController_;
    console::ConsoleController stderrController_;
    console::ConsoleBuffer consoleBuffer_;

    // Phase 6: the optional tunnel companion process. Declared alongside
    // process_/stdoutController_ so it follows the same "controllers
    // destroyed before the Process they read from" ordering rule.
    process::Process tunnelProcess_;
    console::ConsoleController tunnelStdoutController_;
    console::ConsoleController tunnelStderrController_;

    std::atomic<core::ServerState> state_{core::ServerState::Stopped};
    std::atomic<bool> stopRequested_{false};
    mutable std::mutex playersMutex_;
    std::vector<std::wstring> onlinePlayers_;

    std::atomic<bool> awaitingFlush_{false};  // PrepareForBackup() is waiting for "Saved the game"
    std::atomic<bool> flushConfirmed_{false};
    std::atomic<int> statsTickCounter_{0}; // Phase 2: throttles auto "/tps" queries

    std::jthread watcherThread_;

    // Auto-restart bookkeeping (spec section "Maximum automatic restarts:
    // 5 within 10 minutes").
    std::mutex restartMutex_;
    std::vector<std::chrono::steady_clock::time_point> recentRestarts_;

    // Declared last so it is destroyed FIRST (before process_ and
    // everything else it might touch via GetState()/WriteLine()/Restart()).
    std::jthread scheduledRestartThread_;
    std::jthread autoBackupThread_;
};

} // namespace server
