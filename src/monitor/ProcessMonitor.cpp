#include "monitor/ProcessMonitor.hpp"

#include <psapi.h>

namespace monitor
{
namespace
{

ULARGE_INTEGER ToULargeInteger(const FILETIME& fileTime)
{
    ULARGE_INTEGER result;
    result.LowPart = fileTime.dwLowDateTime;
    result.HighPart = fileTime.dwHighDateTime;
    return result;
}

DWORD GetLogicalProcessorCount()
{
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    return systemInfo.dwNumberOfProcessors > 0 ? systemInfo.dwNumberOfProcessors : 1;
}

} // namespace

void ProcessMonitor::Start(HANDLE processHandle, SampleHandler onSample, std::chrono::milliseconds interval)
{
    Stop();
    thread_ = std::jthread([this, processHandle, onSample = std::move(onSample), interval](std::stop_token stopToken) {
        Loop(processHandle, onSample, interval, stopToken);
    });
}

void ProcessMonitor::Stop()
{
    if (thread_.joinable())
    {
        thread_.request_stop();
        thread_.join();
    }
}

void ProcessMonitor::Loop(HANDLE processHandle, SampleHandler onSample, std::chrono::milliseconds interval, std::stop_token stopToken)
{
    const DWORD numProcessors = GetLogicalProcessorCount();

    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetProcessTimes(processHandle, &creationTime, &exitTime, &kernelTime, &userTime))
    {
        return; // process already gone / handle invalid - nothing to sample
    }

    ULARGE_INTEGER lastKernel = ToULargeInteger(kernelTime);
    ULARGE_INTEGER lastUser = ToULargeInteger(userTime);
    auto lastSampleTime = std::chrono::steady_clock::now();

    while (!stopToken.stop_requested())
    {
        std::this_thread::sleep_for(interval);
        if (stopToken.stop_requested())
        {
            break;
        }

        // The process handle stays valid (and GetProcessTimes keeps
        // succeeding, returning the final CPU time) even after the
        // process has exited, until every handle to it is closed - so we
        // must check "has it exited?" explicitly rather than relying on
        // GetProcessTimes failing.
        if (WaitForSingleObject(processHandle, 0) == WAIT_OBJECT_0)
        {
            break;
        }

        FILETIME creationNow{};
        FILETIME exitNow{};
        FILETIME kernelNow{};
        FILETIME userNow{};
        if (!GetProcessTimes(processHandle, &creationNow, &exitNow, &kernelNow, &userNow))
        {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const ULARGE_INTEGER kernelValue = ToULargeInteger(kernelNow);
        const ULARGE_INTEGER userValue = ToULargeInteger(userNow);

        const std::uint64_t cpuDelta100ns =
            (kernelValue.QuadPart - lastKernel.QuadPart) + (userValue.QuadPart - lastUser.QuadPart);
        const double wallSeconds = std::chrono::duration<double>(now - lastSampleTime).count();

        double cpuPercent = 0.0;
        if (wallSeconds > 0.0)
        {
            const double cpuSeconds = static_cast<double>(cpuDelta100ns) / 1.0e7; // 100ns units -> seconds
            cpuPercent = (cpuSeconds / wallSeconds / static_cast<double>(numProcessors)) * 100.0;
        }

        PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
        memoryCounters.cb = sizeof(memoryCounters);
        std::uint64_t memoryBytes = 0;
        if (K32GetProcessMemoryInfo(processHandle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memoryCounters), sizeof(memoryCounters)))
        {
            memoryBytes = memoryCounters.WorkingSetSize;
        }

        Sample sample;
        sample.cpuPercent = cpuPercent;
        sample.memoryBytes = memoryBytes;
        onSample(sample);

        lastKernel = kernelValue;
        lastUser = userValue;
        lastSampleTime = now;
    }
}

} // namespace monitor
