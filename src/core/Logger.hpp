#pragma once

// Process-wide logger. A singleton is used deliberately here (the one
// piece of "global mutable state" the spec's Rule 4 tolerates) because
// every layer of the app - UI, process management, config - needs to be
// able to report errors, and threading a Logger& through every
// constructor would add noise without real benefit at this scale.

#include <mutex>
#include <string>

namespace core
{

class Logger
{
public:
    static Logger& Instance();

    void Info(const std::wstring& message);
    void Warning(const std::wstring& message);
    void Error(const std::wstring& message);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    void Write(const wchar_t* level, const std::wstring& message);

    std::mutex mutex_;
    void* fileHandle_ = nullptr; // FILE*, opaque here to avoid pulling <cstdio> into every includer
};

} // namespace core
