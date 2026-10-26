#pragma once

// Thin, RAII-safe wrapper around CreateProcessW plus a Windows Job Object
// (spec section 10). Every managed Minecraft server process is placed in
// its own job with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, so if this
// application terminates unexpectedly (crash, forced kill in Task
// Manager) the java.exe child cannot outlive it and keep the world files
// locked / the port bound.
//
// This class deliberately does NOT read stdout/stderr itself - that is
// console::ConsoleController's job, running on its own thread so the UI
// thread is never blocked on ReadFile (spec section 11 / Rule 3).

#include "core/Types.hpp"

#include <windows.h>

#include <filesystem>
#include <string>

namespace process
{

class Process
{
public:
    struct StartOptions
    {
        std::filesystem::path executablePath;
        std::wstring arguments; // everything after the executable name
        std::filesystem::path workingDirectory;
    };

    Process() = default;
    ~Process() = default;

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&&) = default;
    Process& operator=(Process&&) = default;

    // Spawns the process. Returns false (and logs GetLastError) on failure.
    // On success, GetStdoutReadHandle()/GetStderrReadHandle() become valid
    // and can be handed to a ConsoleController.
    [[nodiscard]] bool Start(const StartOptions& options);

    [[nodiscard]] bool IsRunning() const;

    // Blocks the calling thread until the process exits or `timeoutMs`
    // elapses. Callers on the UI thread must never call this with
    // INFINITE - it exists for use on background watcher threads.
    [[nodiscard]] bool Wait(DWORD timeoutMs) const;

    // Hard-kills the process (and, via the Job Object, any children it may
    // have spawned). Used as a last resort when a graceful `stop` command
    // does not make the process exit within a timeout.
    bool Terminate(UINT exitCode) const;

    [[nodiscard]] DWORD GetProcessId() const { return processId_; }

    // Returns false if the process is still running or the exit code
    // could not be retrieved.
    [[nodiscard]] bool GetExitCode(DWORD& exitCode) const;

    // Writes one line (plus a trailing \n) to the process's stdin. This is
    // how console commands like "stop" or "say hello" are sent (spec
    // section 12).
    bool WriteLine(const std::wstring& line) const;

    [[nodiscard]] HANDLE GetStdoutReadHandle() const { return stdoutRead_.Get(); }
    [[nodiscard]] HANDLE GetStderrReadHandle() const { return stderrRead_.Get(); }
    [[nodiscard]] HANDLE GetProcessHandle() const { return processHandle_.Get(); }

private:
    core::UniqueHandle processHandle_;
    core::UniqueHandle threadHandle_;
    core::UniqueHandle jobHandle_;
    core::UniqueHandle stdinWrite_;
    core::UniqueHandle stdoutRead_;
    core::UniqueHandle stderrRead_;
    DWORD processId_ = 0;
};

} // namespace process
