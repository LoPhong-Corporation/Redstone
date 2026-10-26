#include "console/ConsoleController.hpp"

namespace console
{
namespace
{

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return L"";
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength <= 0)
    {
        return L"";
    }
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideLength);
    return wide;
}

} // namespace

void ConsoleController::Start(HANDLE readHandle, LineHandler onLine)
{
    Stop();
    thread_ = std::jthread([this, readHandle, onLine = std::move(onLine)](std::stop_token) {
        ReadLoop(readHandle, onLine);
    });
}

void ConsoleController::Stop()
{
    if (thread_.joinable())
    {
        thread_.request_stop();
        // We do not close the read handle here (we do not own it), so the
        // most reliable way for the loop to end promptly is for the
        // process itself to exit or for the caller to close the handle.
        // request_stop() is still issued so the thread can react between
        // reads if it is later restructured; join() waits for actual exit.
        thread_.join();
    }
}

void ConsoleController::ReadLoop(HANDLE readHandle, LineHandler onLine)
{
    std::string pending; // bytes read so far that do not yet form a full line
    char rawBuffer[4096];

    while (true)
    {
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(readHandle, rawBuffer, sizeof(rawBuffer), &bytesRead, nullptr);
        if (!ok || bytesRead == 0)
        {
            // ERROR_BROKEN_PIPE (or any other failure) means the child
            // closed its end - i.e. the process exited. This is the
            // expected way for this loop to end.
            break;
        }

        pending.append(rawBuffer, bytesRead);

        size_t newlinePos;
        while ((newlinePos = pending.find('\n')) != std::string::npos)
        {
            std::string rawLine = pending.substr(0, newlinePos);
            pending.erase(0, newlinePos + 1);

            // Minecraft's console output typically ends lines with \r\n;
            // strip a trailing \r left over after splitting on \n.
            if (!rawLine.empty() && rawLine.back() == '\r')
            {
                rawLine.pop_back();
            }

            onLine(Utf8ToWide(rawLine));
        }
    }

    // Flush any trailing partial line (no final newline before the
    // process exited) so it is not silently lost.
    if (!pending.empty())
    {
        onLine(Utf8ToWide(pending));
    }
}

} // namespace console
