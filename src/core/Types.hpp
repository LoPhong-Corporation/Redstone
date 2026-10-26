#pragma once

// Shared primitive types used across the whole application.
//
// Design notes (see spec Rule 5/6/8):
// - UniqueHandle owns exactly one Windows HANDLE and closes it in its
//   destructor (RAII). It is move-only so ownership is always explicit
//   and a HANDLE can never be leaked by forgetting a CloseHandle() call.
// - ServerConfig is a plain data type shared by ConfigManager, ServerManager
//   and the UI layer.

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace core {

// RAII wrapper around a Windows HANDLE. Treats both nullptr and
// INVALID_HANDLE_VALUE as "no handle" so callers don't have to remember
// which sentinel a given API uses.
class UniqueHandle
{
public:
    UniqueHandle() noexcept = default;

    explicit UniqueHandle(HANDLE handle) noexcept
        : handle_(handle)
    {
    }

    ~UniqueHandle()
    {
        Reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Closes the currently owned handle (if any) and takes ownership of a
    // new one. Calling Reset() with no argument just releases ownership.
    void Reset(HANDLE handle = nullptr) noexcept
    {
        if (IsValid())
        {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

    // Gives up ownership without closing the handle. Used when a handle is
    // handed off to Windows (e.g. into a STARTUPINFOW) and Windows itself,
    // not us, is responsible for its lifetime from that point on.
    [[nodiscard]] HANDLE Release() noexcept
    {
        HANDLE result = handle_;
        handle_ = nullptr;
        return result;
    }

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    explicit operator bool() const noexcept { return IsValid(); }

private:
    HANDLE handle_ = nullptr;
};

// Mirrors the state machine described in the spec (section 7).
enum class ServerState
{
    Stopped,
    Starting,
    Running,
    Stopping,
    Crashed,
    Restarting
};

[[nodiscard]] inline const wchar_t* ToString(ServerState state) noexcept
{
    switch (state)
    {
        case ServerState::Stopped: return L"Stopped";
        case ServerState::Starting: return L"Starting";
        case ServerState::Running: return L"Running";
        case ServerState::Stopping: return L"Stopping";
        case ServerState::Crashed: return L"Crashed";
        case ServerState::Restarting: return L"Restarting";
    }
    return L"Unknown";
}

// One managed Minecraft server profile (spec section 6). Deliberately a
// plain struct with no behaviour - lifecycle logic lives in MinecraftServer.
struct ServerConfig
{
    std::wstring id;
    std::wstring name;

    std::filesystem::path directory;
    std::filesystem::path serverJar;
    std::filesystem::path javaExecutable;

    std::wstring serverType = L"Vanilla";
    std::wstring minecraftVersion;

    std::uint64_t minMemoryMB = 1024;
    std::uint64_t maxMemoryMB = 2048;

    std::uint16_t port = 25565;

    bool autoRestart = false;
    bool autoBackup = false;
    bool autoStartOnAppLaunch = false; // start this server automatically when the app itself starts

    // Per-server run options (edited in ui::ServerOptionsDialog).
    std::wstring extraJvmArgs;      // inserted before -jar, e.g. "-XX:+UseG1GC -XX:+ParallelRefProcEnabled"
    std::wstring extraServerArgs;   // appended after --nogui, e.g. "--forceUpgrade"
    int autoBackupIntervalMinutes = 60; // used when autoBackup == true
    int backupRetention = 10;           // how many backups to keep (older ones are deleted)
    int stopTimeoutSeconds = 30;        // graceful "stop" wait before the process is killed

    // Phase 6: an optional companion process (Playit.gg agent, Cloudflare
    // Tunnel's cloudflared, or any other executable) managed alongside the
    // Minecraft server itself. This app does not implement (or reimplement)
    // either service's tunneling protocol - it only starts/stops whichever
    // executable the user already has, the same way it starts/stops
    // java.exe, and folds its console output into the same console view
    // (prefixed "[tunnel] ") so the public address it prints is visible
    // without a separate terminal window.
    struct TunnelConfig
    {
        bool enabled = false;
        std::filesystem::path executablePath;
        std::wstring arguments;
        bool autoStartWithServer = false;
    } tunnel;

    // Fork-inspired: restart on a fixed schedule (not just on crash), with
    // a single "restarting in 60s" in-game warning first.
    struct ScheduledRestartConfig
    {
        bool enabled = false;
        int intervalHours = 24;
    } scheduledRestart;
};

} // namespace core
