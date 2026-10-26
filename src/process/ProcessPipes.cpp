#include "process/ProcessPipes.hpp"

#include "core/Logger.hpp"

#include <windows.h>

#include <format>

namespace process
{
namespace
{

// Creates one pipe where `inheritableEnd` becomes inheritable (destined
// for the child) and the other end is explicitly marked non-inheritable
// (kept by the parent). This is the pattern documented by Microsoft for
// "Creating a Child Process with Redirected Input and Output".
bool CreateOnePipe(core::UniqueHandle& inheritableEnd, core::UniqueHandle& privateEnd, bool inheritableIsReadEnd)
{
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = TRUE;
    securityAttributes.lpSecurityDescriptor = nullptr;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &securityAttributes, 0))
    {
        core::Logger::Instance().Error(
            std::format(L"CreatePipe failed. Error: {}", GetLastError()));
        return false;
    }

    HANDLE inheritableRaw = inheritableIsReadEnd ? readHandle : writeHandle;
    HANDLE privateRaw = inheritableIsReadEnd ? writeHandle : readHandle;

    // The parent-side handle must NOT be inherited by the child, otherwise
    // it stays open in the child process too and the pipe never reports
    // EOF/broken-pipe when we close our end (a classic hang-on-shutdown bug).
    if (!SetHandleInformation(privateRaw, HANDLE_FLAG_INHERIT, 0))
    {
        core::Logger::Instance().Error(
            std::format(L"SetHandleInformation failed. Error: {}", GetLastError()));
        CloseHandle(readHandle);
        CloseHandle(writeHandle);
        return false;
    }

    inheritableEnd.Reset(inheritableRaw);
    privateEnd.Reset(privateRaw);
    return true;
}

} // namespace

bool CreateProcessPipes(ProcessPipes& pipes)
{
    // stdin: child reads, parent writes.
    if (!CreateOnePipe(pipes.childStdinRead, pipes.parentStdinWrite, /*inheritableIsReadEnd=*/true))
    {
        return false;
    }
    // stdout: child writes, parent reads.
    if (!CreateOnePipe(pipes.childStdoutWrite, pipes.parentStdoutRead, /*inheritableIsReadEnd=*/false))
    {
        return false;
    }
    // stderr: child writes, parent reads.
    if (!CreateOnePipe(pipes.childStderrWrite, pipes.parentStderrRead, /*inheritableIsReadEnd=*/false))
    {
        return false;
    }
    return true;
}

} // namespace process
