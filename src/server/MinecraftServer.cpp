#include "server/MinecraftServer.hpp"

#include "backup/BackupManager.hpp"
#include "core/Localize.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <fstream>
#include <optional>
#include <string_view>

namespace server
{
namespace
{
constexpr int kMaxAutoRestarts = 5;
constexpr auto kAutoRestartWindow = std::chrono::minutes(10);
constexpr auto kAutoRestartDelay = std::chrono::seconds(2);

// Every 5th stats sample (~10s at the default 2s sampling interval) we
// auto-send "/tps" for server types known to implement it.
constexpr int kTpsQueryEveryNTicks = 5;

// Server-type names (case-insensitive) known to answer a console "/tps"
// command. Auto-sending it to anything else (vanilla, Forge, Fabric...)
// would just spam "Unknown command" into the user's console.
constexpr std::array<const wchar_t*, 5> kTpsCapableServerTypes = {
    L"paper", L"spigot", L"purpur", L"bukkit", L"folia"
};

std::wstring ToLower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

// Paper/Spigot/Purpur reply to "/tps" with a line like:
//   "TPS from last 1m, 5m, 15m: 20.0, 20.0, 20.0"
// often prefixed with Minecraft colour codes (a section sign U+00A7
// followed by one format character), which are stripped before matching.
std::optional<std::wstring> TryParseTpsLine(const std::wstring& line)
{
    std::wstring clean;
    clean.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == static_cast<wchar_t>(0x00A7) && i + 1 < line.size())
        {
            ++i; // skip the section sign AND the format character after it
            continue;
        }
        clean += line[i];
    }

    constexpr const wchar_t* marker = L"TPS from last";
    const auto pos = clean.find(marker);
    if (pos == std::wstring::npos)
    {
        return std::nullopt;
    }
    return clean.substr(pos);
}

} // namespace

MinecraftServer::MinecraftServer(core::ServerConfig config, std::shared_ptr<core::EventDispatcher> events)
    : config_(std::move(config))
    , events_(std::move(events))
{
}

MinecraftServer::~MinecraftServer()
{
    if (GetState() == core::ServerState::Running || GetState() == core::ServerState::Starting)
    {
        // Best-effort graceful shutdown; the destructor cannot report
        // failures, but Stop() still logs internally via process::Process.
        Stop(std::chrono::seconds(10));
    }
}

bool MinecraftServer::Start()
{
    if (GetState() == core::ServerState::Running || GetState() == core::ServerState::Starting)
    {
        core::Logger::Instance().Warning(
            std::format(L"Start() ignored for '{}': already {}", config_.name, ToString(GetState())));
        return false;
    }

    ClearOnlinePlayers();
    SetState(core::ServerState::Starting);
    stopRequested_ = false;
    statsTickCounter_ = 0;

    process::Process::StartOptions options;
    options.executablePath = config_.javaExecutable;
    options.workingDirectory = config_.directory;
    options.arguments = std::format(L"-Xms{}M -Xmx{}M", config_.minMemoryMB, config_.maxMemoryMB);
    if (!config_.extraJvmArgs.empty())
    {
        options.arguments += L" " + config_.extraJvmArgs;
    }
    options.arguments += std::format(L" -jar \"{}\" --nogui", config_.serverJar.wstring());
    if (!config_.extraServerArgs.empty())
    {
        options.arguments += L" " + config_.extraServerArgs;
    }

    if (!process_.Start(options))
    {
        SetState(core::ServerState::Crashed);
        return false;
    }

    consoleBuffer_.Append(core::Localize(L"[manager] Starting server process..."));

    stdoutController_.Start(process_.GetStdoutReadHandle(), [this](const std::wstring& line) {
        OnConsoleLine(line);
    });
    stderrController_.Start(process_.GetStderrReadHandle(), [this](const std::wstring& line) {
        OnConsoleLine(line);
    });

    // Phase 2: CPU%/RAM sampling every 2s while the process is running.
    processMonitor_.Start(process_.GetProcessHandle(), [this](const monitor::ProcessMonitor::Sample& sample) {
        OnStatsSample(sample);
    });

    // Phase 6: optionally bring the tunnel companion process up alongside
    // the Minecraft server itself.
    if (config_.tunnel.enabled && config_.tunnel.autoStartWithServer)
    {
        StartTunnel();
    }

    // Fork-inspired scheduled restart: only armed if configured, and only
    // for this run - WatchForExit() stops this thread once the process
    // exits for any reason, and a fresh one is created on the next Start().
    if (config_.scheduledRestart.enabled && config_.scheduledRestart.intervalHours > 0)
    {
        scheduledRestartThread_ = std::jthread([this](std::stop_token stopToken) { ScheduledRestartLoop(stopToken); });
    }

    if (config_.autoBackup && config_.autoBackupIntervalMinutes > 0)
    {
        autoBackupThread_ = std::jthread([this](std::stop_token stopToken) { AutoBackupLoop(stopToken); });
    }

    // Publish Running BEFORE the watcher thread exists. If java.exe dies
    // instantly (missing EULA, bad jar...), WatchForExit() sets Crashed -
    // doing SetState(Running) after launching the watcher let that late
    // "Running" overwrite the real "Crashed" state and leave the UI wrong.
    SetState(core::ServerState::Running);

    watcherThread_ = std::jthread([this](std::stop_token) { WatchForExit(); });
    return true;
}

bool MinecraftServer::Stop(std::chrono::milliseconds timeout)
{
    const auto current = GetState();
    if (current != core::ServerState::Running && current != core::ServerState::Starting)
    {
        return false;
    }

    if (timeout.count() <= 0)
    {
        timeout = std::chrono::seconds(std::max(5, config_.stopTimeoutSeconds));
    }

    SetState(core::ServerState::Stopping);
    stopRequested_ = true;

    process_.WriteLine(L"stop");

    const auto timeoutMs = static_cast<DWORD>(timeout.count());
    if (!process_.Wait(timeoutMs))
    {
        consoleBuffer_.Append(core::Localize(L"[manager] Graceful stop timed out, killing process..."));
        process_.Terminate(1);
        process_.Wait(5000); // bounded wait for the watcher thread to observe the exit
    }

    // WatchForExit (running on watcherThread_) is responsible for the
    // final state transition to Stopped once GetExitCodeProcess()
    // confirms the process has actually exited, so we don't set state
    // here - doing so from two places would risk racing with it.
    return true;
}

bool MinecraftServer::Restart()
{
    consoleBuffer_.Append(core::Localize(L"[manager] Restarting server..."));
    if (GetState() == core::ServerState::Running || GetState() == core::ServerState::Stopping)
    {
        Stop();
    }
    return Start();
}

bool MinecraftServer::PrepareForBackup(std::chrono::seconds timeout)
{
    if (GetState() != core::ServerState::Running)
    {
        return true; // stopped server: files on disk are already consistent
    }

    // Copying region files while Minecraft is still writing them can give a
    // corrupt backup. Standard fix: disable auto-save, force a full flush,
    // wait for the server to confirm, then copy.
    flushConfirmed_ = false;
    awaitingFlush_ = true;
    process_.WriteLine(L"save-off");
    process_.WriteLine(L"save-all flush");

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flushConfirmed_ && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    awaitingFlush_ = false;

    if (!flushConfirmed_)
    {
        consoleBuffer_.Append(core::Localize(L"[manager] Không nhận được xác nhận 'save-all' trong thời gian chờ, vẫn tiếp tục backup."));
    }
    return flushConfirmed_;
}

void MinecraftServer::FinishBackup()
{
    if (GetState() == core::ServerState::Running)
    {
        process_.WriteLine(L"save-on");
    }
}

bool MinecraftServer::SendCommand(const std::wstring& command)
{
    if (GetState() != core::ServerState::Running)
    {
        return false;
    }
    consoleBuffer_.Append(L"> " + command);
    return process_.WriteLine(command);
}

void MinecraftServer::SetState(core::ServerState newState)
{
    state_.store(newState);
    if (events_)
    {
        core::AppEvent event;
        event.type = core::EventType::ServerStateChanged;
        event.serverId = config_.id;
        event.message = ToString(newState);
        event.state = newState;
        events_->Publish(event);
    }
}

void MinecraftServer::OnConsoleLine(const std::wstring& line)
{
    consoleBuffer_.Append(line);

    TrackPlayerLine(line);

    // Vanilla/Paper confirm "save-all flush" with "Saved the game" (older
    // versions: "Saved the world"). Used by PrepareForBackup().
    if (awaitingFlush_.load() &&
        (line.find(L"Saved the game") != std::wstring::npos || line.find(L"Saved the world") != std::wstring::npos))
    {
        flushConfirmed_ = true;
    }

    if (events_)
    {
        core::AppEvent event;
        event.type = core::EventType::ConsoleLine;
        event.serverId = config_.id;
        event.message = line;
        events_->Publish(event);
    }

    // Phase 2: opportunistically pick up a TPS reply if one just came
    // through, regardless of whether we were the one who asked for it
    // (an admin typing "/tps" by hand in the console still gets picked up
    // and reflected in the UI).
    if (events_)
    {
        if (auto tpsText = TryParseTpsLine(line))
        {
            core::AppEvent statsEvent;
            statsEvent.type = core::EventType::StatsUpdated;
            statsEvent.serverId = config_.id;
            statsEvent.cpuPercent = -1.0; // sentinel: no cpu/mem data in this event
            statsEvent.tpsText = *tpsText;
            events_->Publish(statsEvent);
        }
    }
}

void MinecraftServer::OnStatsSample(const monitor::ProcessMonitor::Sample& sample)
{
    if (events_)
    {
        core::AppEvent event;
        event.type = core::EventType::StatsUpdated;
        event.serverId = config_.id;
        event.cpuPercent = sample.cpuPercent;
        event.memoryBytes = sample.memoryBytes;
        events_->Publish(event);
    }

    if (SupportsTpsQuery())
    {
        const int tick = statsTickCounter_.fetch_add(1) + 1;
        if (tick % kTpsQueryEveryNTicks == 0)
        {
            process_.WriteLine(L"tps");
        }
    }
}

bool MinecraftServer::SupportsTpsQuery() const
{
    const std::wstring lowered = ToLower(config_.serverType);
    return std::any_of(kTpsCapableServerTypes.begin(), kTpsCapableServerTypes.end(), [&](const wchar_t* type) {
        return lowered == type;
    });
}

void MinecraftServer::WatchForExit()
{
    // Blocks this dedicated thread (never the UI thread) until the child
    // process exits, however that happens: graceful "stop", a crash, or
    // being killed after a timed-out graceful stop.
    process_.Wait(INFINITE);

    DWORD exitCode = 0;
    process_.GetExitCode(exitCode);

    // Whatever happens next (Stopped, Crashed, or an imminent
    // auto-restart), the scheduled-restart loop for THIS run is done -
    // stop it here rather than leaving it sleeping for up to
    // `intervalHours` against a server that is no longer running. Safe to
    // call from here (watcherThread_) since it's a different thread
    // object than scheduledRestartThread_ itself - no self-join hazard.
    // Start() creates a fresh one if/when the server runs again.
    if (scheduledRestartThread_.joinable())
    {
        scheduledRestartThread_.request_stop();
        scheduledRestartThread_.join();
    }

    if (autoBackupThread_.joinable())
    {
        autoBackupThread_.request_stop();
        autoBackupThread_.join();
    }
    ClearOnlinePlayers();

    const bool wasStopRequested = stopRequested_.load();

    if (wasStopRequested)
    {
        consoleBuffer_.Append(core::Localize(L"[manager] Server process stopped."));
        SetState(core::ServerState::Stopped);
        if (IsTunnelRunning())
        {
            StopTunnel(); // don't leave the tunnel up once the server itself is down
        }
    }
    else
    {
        consoleBuffer_.Append(
            std::vformat(core::Localize(L"[manager] Server process exited unexpectedly (exit code {})."), std::make_wformat_args(exitCode)));
        HandleUnexpectedExit();
    }
}

void MinecraftServer::HandleUnexpectedExit()
{
    SetState(core::ServerState::Crashed);
    if (events_)
    {
        core::AppEvent event;
        event.type = core::EventType::ServerCrashed;
        event.serverId = config_.id;
        event.message = L"Server process exited unexpectedly.";
        events_->Publish(event);
    }

    // Phase 3 EULA helper: Minecraft's own first-run behaviour is to
    // write a fresh eula.txt (with eula=false) and exit immediately -
    // this is the single most common reason a brand-new server "crashes"
    // on its very first start. Detect that specific, well-known pattern
    // and tell the user clearly what to do, instead of just labelling it
    // a generic crash and (if autoRestart is on) looping forever trying
    // to relaunch a server that will refuse to start every time.
    if (IsEulaNotAccepted())
    {
        consoleBuffer_.Append(
            core::Localize(L"[manager] Server dừng vì chưa chấp nhận Minecraft EULA. Dùng nút \"📜 Chấp nhận EULA\" trên toolbar rồi Start lại."));
        if (events_)
        {
            core::AppEvent eulaEvent;
            eulaEvent.type = core::EventType::EulaRequired;
            eulaEvent.serverId = config_.id;
            events_->Publish(eulaEvent);
        }
        return; // do not auto-restart into the same dead end
    }

    if (!config_.autoRestart || !ShouldAutoRestart())
    {
        return;
    }

    SetState(core::ServerState::Restarting);
    consoleBuffer_.Append(core::Localize(L"[manager] Auto-restart scheduled..."));

    // Restarting must NOT happen synchronously here: Start() will replace
    // watcherThread_ with a new std::jthread, and reassigning a jthread
    // joins its previous thread first. Since we ARE that previous thread
    // right now, joining ourselves would be undefined behaviour. Handing
    // the restart off to a separate, short-lived thread avoids that.
    std::thread([this]() {
        std::this_thread::sleep_for(kAutoRestartDelay);
        Start();
    }).detach();
}

bool MinecraftServer::IsEulaNotAccepted() const
{
    // eula.txt is a plain Java Properties file, always ASCII in practice
    // ("eula=false"/"eula=true" plus a comment line), so a narrow
    // std::ifstream is fine here - no UTF-8 decoding concerns like the
    // JSON config file has.
    std::ifstream file(config_.directory / L"eula.txt");
    if (!file.is_open())
    {
        return false; // no eula.txt yet is not itself evidence of anything
    }
    std::string line;
    while (std::getline(file, line))
    {
        if (line.find("eula=false") != std::string::npos)
        {
            return true;
        }
        if (line.find("eula=true") != std::string::npos)
        {
            return false;
        }
    }
    return false;
}

bool MinecraftServer::AcceptEula() const
{
    const auto eulaPath = config_.directory / L"eula.txt";

    std::vector<std::string> lines;
    bool foundEulaLine = false;
    {
        std::ifstream in(eulaPath);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("eula=", 0) == 0) // starts with "eula="
            {
                lines.push_back("eula=true");
                foundEulaLine = true;
            }
            else
            {
                lines.push_back(line);
            }
        }
    }
    if (!foundEulaLine)
    {
        lines.push_back("eula=true");
    }

    std::error_code ec;
    std::filesystem::create_directories(config_.directory, ec);

    std::ofstream out(eulaPath, std::ios::trunc);
    if (!out.is_open())
    {
        core::Logger::Instance().Error(std::format(L"Failed to write {}", eulaPath.wstring()));
        return false;
    }
    for (const auto& line : lines)
    {
        out << line << "\n";
    }
    return true;
}

bool MinecraftServer::ShouldAutoRestart()
{
    std::scoped_lock lock(restartMutex_);
    const auto now = std::chrono::steady_clock::now();

    // Drop restart timestamps older than the rolling window before
    // counting, so this is "N restarts in the last 10 minutes", not
    // "N restarts ever".
    std::erase_if(recentRestarts_, [&](const auto& timestamp) {
        return now - timestamp > kAutoRestartWindow;
    });

    if (static_cast<int>(recentRestarts_.size()) >= kMaxAutoRestarts)
    {
        consoleBuffer_.Append(core::Localize(L"[manager] Auto-restart limit reached (5 within 10 minutes). Giving up."));
        return false;
    }

    recentRestarts_.push_back(now);
    return true;
}

bool MinecraftServer::StartTunnel()
{
    if (config_.tunnel.executablePath.empty())
    {
        consoleBuffer_.Append(core::Localize(L"[manager] Chưa cấu hình đường dẫn cho tunnel (Playit.gg/Cloudflare Tunnel/...)."));
        return false;
    }
    if (tunnelProcess_.IsRunning())
    {
        return false; // already running
    }

    process::Process::StartOptions options;
    options.executablePath = config_.tunnel.executablePath;
    options.workingDirectory = config_.tunnel.executablePath.parent_path();
    options.arguments = config_.tunnel.arguments;

    if (!tunnelProcess_.Start(options))
    {
        consoleBuffer_.Append(core::Localize(L"[manager] Không khởi động được tunnel process."));
        return false;
    }

    tunnelStdoutController_.Start(tunnelProcess_.GetStdoutReadHandle(), [this](const std::wstring& line) {
        OnConsoleLine(L"[tunnel] " + line);
    });
    tunnelStderrController_.Start(tunnelProcess_.GetStderrReadHandle(), [this](const std::wstring& line) {
        OnConsoleLine(L"[tunnel] " + line);
    });

    consoleBuffer_.Append(core::Localize(L"[manager] Tunnel process started."));
    return true;
}

bool MinecraftServer::StopTunnel()
{
    if (!tunnelProcess_.IsRunning())
    {
        return false;
    }
    // Tunnel agents (playit.exe, cloudflared.exe, ...) don't share a
    // universal graceful "stop" console command the way Minecraft does,
    // so this goes straight to a clean process termination via the same
    // Job-Object-backed Process class the Minecraft server itself uses.
    tunnelProcess_.Terminate(0);
    tunnelStdoutController_.Stop();
    tunnelStderrController_.Stop();
    consoleBuffer_.Append(core::Localize(L"[manager] Tunnel process stopped."));
    return true;
}

namespace
{
// Sleeps in small steps so a stop_token can interrupt a long wait
// promptly instead of only being checked once every multi-hour interval.
void InterruptibleSleep(std::chrono::seconds duration, std::stop_token stopToken)
{
    constexpr auto kStep = std::chrono::seconds(2);
    auto remaining = duration;
    while (remaining.count() > 0 && !stopToken.stop_requested())
    {
        const auto step = std::min(remaining, kStep);
        std::this_thread::sleep_for(step);
        remaining -= step;
    }
}
} // namespace

void MinecraftServer::ScheduledRestartLoop(std::stop_token stopToken)
{
    const auto fullInterval = std::chrono::seconds(static_cast<long long>(config_.scheduledRestart.intervalHours) * 3600);
    constexpr auto kWarningLeadTime = std::chrono::seconds(60);
    const auto beforeWarning = fullInterval > kWarningLeadTime ? fullInterval - kWarningLeadTime : std::chrono::seconds(0);

    while (!stopToken.stop_requested())
    {
        InterruptibleSleep(beforeWarning, stopToken);
        if (stopToken.stop_requested())
        {
            return;
        }

        if (GetState() == core::ServerState::Running)
        {
            process_.WriteLine(core::Localize(L"say [Manager] Server sẽ tự khởi động lại sau 60 giây."));
        }

        InterruptibleSleep(kWarningLeadTime, stopToken);
        if (stopToken.stop_requested())
        {
            return;
        }

        if (GetState() == core::ServerState::Running)
        {
            // Restart() itself calls Stop()+Start(), and Start() will
            // create a brand-new scheduledRestartThread_ - reassigning a
            // jthread joins its previous holder first. Since THIS
            // function runs on scheduledRestartThread_, calling Restart()
            // directly here would mean that join() targets its own
            // thread once Start() runs (undefined behaviour), same
            // hazard already solved for the crash-triggered auto-restart
            // path. Handing it to a short-lived helper thread avoids it.
            std::thread([this]() { Restart(); }).detach();
            return; // this run's job is done either way
        }
    }
}

std::vector<std::wstring> MinecraftServer::GetOnlinePlayers() const
{
    std::scoped_lock lock(playersMutex_);
    return onlinePlayers_;
}

void MinecraftServer::ClearOnlinePlayers()
{
    std::scoped_lock lock(playersMutex_);
    onlinePlayers_.clear();
}

void MinecraftServer::TrackPlayerLine(const std::wstring& line)
{
    // Typical line: "[12:00:00] [Server thread/INFO]: Steve joined the game"
    constexpr std::wstring_view kJoined = L" joined the game";
    constexpr std::wstring_view kLeft = L" left the game";

    const bool joined = line.find(kJoined) != std::wstring::npos;
    const auto marker = joined ? line.find(kJoined) : line.find(kLeft);
    if (marker == std::wstring::npos)
    {
        return;
    }
    const auto prefix = line.rfind(L"]: ", marker);
    if (prefix == std::wstring::npos)
    {
        return;
    }
    const std::wstring name = line.substr(prefix + 3, marker - (prefix + 3));
    // Reject chat messages like "<Steve> he joined the game" and anything odd.
    if (name.empty() || name.size() > 16 || name.find_first_of(L" <>[]") != std::wstring::npos)
    {
        return;
    }

    std::scoped_lock lock(playersMutex_);
    const auto it = std::find(onlinePlayers_.begin(), onlinePlayers_.end(), name);
    if (joined)
    {
        if (it == onlinePlayers_.end())
        {
            onlinePlayers_.push_back(name);
        }
    }
    else if (it != onlinePlayers_.end())
    {
        onlinePlayers_.erase(it);
    }
}

void MinecraftServer::AutoBackupLoop(std::stop_token stopToken)
{
    const auto interval = std::chrono::seconds(static_cast<long long>(config_.autoBackupIntervalMinutes) * 60);
    while (!stopToken.stop_requested())
    {
        InterruptibleSleep(interval, stopToken);
        if (stopToken.stop_requested())
        {
            return;
        }
        if (GetState() != core::ServerState::Running)
        {
            continue;
        }
        consoleBuffer_.Append(core::Localize(L"[manager] Auto backup..."));
        PrepareForBackup();
        const auto result = backup::CreateBackup(config_);
        FinishBackup();
        consoleBuffer_.Append(result.has_value()
            ? core::Localize(L"[manager] Auto backup xong: ") + *result
            : core::Localize(L"[manager] Auto backup thất bại (chưa có world?)."));
    }
}

} // namespace server
