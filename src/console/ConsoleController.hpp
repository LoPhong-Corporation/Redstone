#pragma once

// Reads raw bytes from a pipe HANDLE (a Minecraft process's stdout or
// stderr) on a dedicated std::jthread, reassembles them into text lines,
// and invokes a callback for each complete line. This is what keeps
// ReadFile() off the UI thread (spec section 11 / Rule 3): the UI thread
// never touches the pipe HANDLE directly.
//
// The read loop ends naturally when the child process exits and closes
// its end of the pipe: ReadFile then returns FALSE with
// ERROR_BROKEN_PIPE, which we treat as a normal "stream closed" signal
// rather than an error to report.

#include <functional>
#include <string>
#include <thread>
#include <windows.h>

namespace console
{

class ConsoleController
{
public:
    using LineHandler = std::function<void(const std::wstring&)>;

    ConsoleController() = default;
    ~ConsoleController() { Stop(); }

    ConsoleController(const ConsoleController&) = delete;
    ConsoleController& operator=(const ConsoleController&) = delete;

    // `readHandle` is borrowed, not owned: the caller (MinecraftServer,
    // via Process) keeps ownership and is responsible for its lifetime.
    // Start() is safe to call again after Stop().
    void Start(HANDLE readHandle, LineHandler onLine);

    // Waits for the reader thread to finish. Safe to call even if Start()
    // was never called, or was already stopped.
    void Stop();

private:
    void ReadLoop(HANDLE readHandle, LineHandler onLine);

    std::jthread thread_;
};

} // namespace console
