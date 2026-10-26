#include "core/Logger.hpp"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <cwchar>
#include <filesystem>

namespace core
{

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
{
    std::error_code errorCode;
    std::filesystem::create_directories(L"logs", errorCode);
    // errorCode is intentionally ignored here: if we cannot create the log
    // directory we still want the application to run, just without a log
    // file (Write() below tolerates fileHandle_ being null).

    FILE* file = nullptr;
    if (_wfopen_s(&file, L"logs/app.log", L"a, ccs=UTF-16LE") != 0)
    {
        file = nullptr;
    }
    fileHandle_ = file;
}

Logger::~Logger()
{
    if (fileHandle_ != nullptr)
    {
        std::fclose(static_cast<FILE*>(fileHandle_));
    }
}

void Logger::Info(const std::wstring& message)
{
    Write(L"INFO", message);
}

void Logger::Warning(const std::wstring& message)
{
    Write(L"WARN", message);
}

void Logger::Error(const std::wstring& message)
{
    Write(L"ERROR", message);
}

void Logger::Write(const wchar_t* level, const std::wstring& message)
{
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &now);

    wchar_t timestamp[32];
    std::wcsftime(timestamp, sizeof(timestamp) / sizeof(wchar_t), L"%Y-%m-%d %H:%M:%S", &localTime);

    std::wstring line = std::wstring(L"[") + timestamp + L"] [" + level + L"] " + message + L"\n";

    std::scoped_lock lock(mutex_);

    // Always visible in a debugger (Visual Studio's Output window) even if
    // the log file could not be opened, e.g. due to permissions.
    OutputDebugStringW(line.c_str());

    if (fileHandle_ != nullptr)
    {
        std::fputws(line.c_str(), static_cast<FILE*>(fileHandle_));
        std::fflush(static_cast<FILE*>(fileHandle_));
    }
}

} // namespace core
