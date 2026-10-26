#include "process/Process.hpp"

#include "core/Logger.hpp"
#include "process/ProcessPipes.hpp"

#include <format>
#include <vector>

namespace process
{
namespace
{

// CreateProcessW takes a mutable command-line buffer (it may rewrite the
// argument separators in place), so we cannot pass a std::wstring's
// internal buffer or a string literal directly - both are effectively
// read-only for this purpose.
std::vector<wchar_t> BuildMutableCommandLine(const std::wstring& commandLine)
{
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    return buffer;
}

std::wstring QuoteIfNeeded(const std::wstring& value)
{
    if (value.find(L' ') == std::wstring::npos)
    {
        return value;
    }
    return L"\"" + value + L"\"";
}

} // namespace

bool Process::Start(const StartOptions& options)
{
    ProcessPipes pipes;
    if (!CreateProcessPipes(pipes))
    {
        return false; // CreateProcessPipes already logged the specific failure.
    }

    std::wstring commandLine =
        QuoteIfNeeded(options.executablePath.wstring()) + L" " + options.arguments;
    std::vector<wchar_t> mutableCommandLine = BuildMutableCommandLine(commandLine);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = pipes.childStdinRead.Get();
    startupInfo.hStdOutput = pipes.childStdoutWrite.Get();
    startupInfo.hStdError = pipes.childStderrWrite.Get();

    PROCESS_INFORMATION processInfo{};

    const std::wstring workingDirectory = options.workingDirectory.wstring();

    // Not using system()/popen()/ShellExecute() per spec section 8: only
    // CreateProcessW gives us the handles, exit code and inheritance
    // control this class relies on.
    const BOOL started = CreateProcessW(
        nullptr,                 // application name: taken from the command line instead
        mutableCommandLine.data(),
        nullptr,                 // default process security attributes
        nullptr,                 // default thread security attributes
        /*bInheritHandles=*/TRUE, // required so the child inherits the pipe ends
        CREATE_NO_WINDOW,         // Minecraft's own console window is not needed; ours replaces it
        nullptr,                  // inherit the parent's environment
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startupInfo,
        &processInfo);

    if (!started)
    {
        core::Logger::Instance().Error(
            std::format(L"CreateProcessW failed. Error: {}", GetLastError()));
        return false;
    }

    processHandle_.Reset(processInfo.hProcess);
    threadHandle_.Reset(processInfo.hThread);
    processId_ = processInfo.dwProcessId;

    // Now that the child has inherited the childStdin*/childStdout*/childStderr*
    // handles, our copies of them must be closed. Keeping them open would
    // mean the pipe never sees EOF once the child exits (the write end
    // would still be "open" as far as Windows is concerned - our handle).
    pipes.childStdinRead.Reset();
    pipes.childStdoutWrite.Reset();
    pipes.childStderrWrite.Reset();

    stdinWrite_ = std::move(pipes.parentStdinWrite);
    stdoutRead_ = std::move(pipes.parentStdoutRead);
    stderrRead_ = std::move(pipes.parentStderrRead);

    // Job Object (spec section 10): ensures java.exe cannot outlive this
    // application. JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE means Windows itself
    // terminates every process in the job as soon as the last handle to
    // the job is closed - including on an ungraceful crash of this app,
    // where our own cleanup code would never get to run.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr)
    {
        core::Logger::Instance().Error(
            std::format(L"CreateJobObjectW failed. Error: {}", GetLastError()));
        // Not fatal to starting the server - we simply lose the "cannot
        // outlive the manager" guarantee for this one process.
    }
    else
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limitInfo{};
        limitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limitInfo, sizeof(limitInfo)))
        {
            core::Logger::Instance().Error(
                std::format(L"SetInformationJobObject failed. Error: {}", GetLastError()));
        }
        if (!AssignProcessToJobObject(job, processHandle_.Get()))
        {
            core::Logger::Instance().Error(
                std::format(L"AssignProcessToJobObject failed. Error: {}", GetLastError()));
        }
        jobHandle_.Reset(job);
    }

    return true;
}

bool Process::IsRunning() const
{
    if (!processHandle_)
    {
        return false;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(processHandle_.Get(), &exitCode))
    {
        return false;
    }
    return exitCode == STILL_ACTIVE;
}

bool Process::Wait(DWORD timeoutMs) const
{
    if (!processHandle_)
    {
        return true;
    }
    return WaitForSingleObject(processHandle_.Get(), timeoutMs) == WAIT_OBJECT_0;
}

bool Process::Terminate(UINT exitCode) const
{
    if (!processHandle_)
    {
        return true;
    }
    if (!TerminateProcess(processHandle_.Get(), exitCode))
    {
        core::Logger::Instance().Error(
            std::format(L"TerminateProcess failed. Error: {}", GetLastError()));
        return false;
    }
    return true;
}

bool Process::GetExitCode(DWORD& exitCode) const
{
    if (!processHandle_)
    {
        return false;
    }
    if (!GetExitCodeProcess(processHandle_.Get(), &exitCode))
    {
        core::Logger::Instance().Error(
            std::format(L"GetExitCodeProcess failed. Error: {}", GetLastError()));
        return false;
    }
    return exitCode != STILL_ACTIVE;
}

bool Process::WriteLine(const std::wstring& line) const
{
    if (!stdinWrite_)
    {
        return false;
    }
    // Minecraft's console reads narrow (ANSI/UTF-8-ish) text from stdin,
    // not UTF-16, so commands are converted before writing (spec section
    // 12 shows plain ASCII commands; this also covers non-ASCII player
    // names/messages via UTF-8).
    const int narrowLength = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (narrowLength <= 0)
    {
        return false;
    }
    std::string narrowLine(static_cast<size_t>(narrowLength) - 1, '\0'); // exclude the null terminator
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, narrowLine.data(), narrowLength, nullptr, nullptr);
    narrowLine += "\n";

    DWORD bytesWritten = 0;
    if (!WriteFile(stdinWrite_.Get(), narrowLine.data(), static_cast<DWORD>(narrowLine.size()), &bytesWritten, nullptr))
    {
        core::Logger::Instance().Error(
            std::format(L"WriteFile to child stdin failed. Error: {}", GetLastError()));
        return false;
    }
    return true;
}

} // namespace process
