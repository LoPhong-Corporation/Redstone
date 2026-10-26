#include "platform/FirewallManager.hpp"

#include "core/Logger.hpp"
#include "process/Process.hpp"

#include <format>

namespace platform
{
namespace
{

bool RunNetsh(const std::wstring& arguments)
{
    process::Process proc;
    process::Process::StartOptions options;
    options.executablePath = L"netsh.exe";
    options.arguments = arguments;

    if (!proc.Start(options))
    {
        core::Logger::Instance().Error(L"FirewallManager: failed to launch netsh.exe.");
        return false;
    }

    proc.Wait(10000); // netsh is normally instant; 10s is a generous upper bound
    DWORD exitCode = 1;
    proc.GetExitCode(exitCode);

    if (exitCode != 0)
    {
        core::Logger::Instance().Warning(std::format(
            L"FirewallManager: netsh exited with code {} - likely needs Administrator privileges.", exitCode));
    }
    return exitCode == 0;
}

} // namespace

bool AddFirewallRule(const std::wstring& ruleName, std::uint16_t port)
{
    const std::wstring arguments = L"advfirewall firewall add rule name=\"" + ruleName +
        L"\" dir=in action=allow protocol=TCP localport=" + std::to_wstring(port);
    return RunNetsh(arguments);
}

bool RemoveFirewallRule(const std::wstring& ruleName)
{
    const std::wstring arguments = L"advfirewall firewall delete rule name=\"" + ruleName + L"\"";
    return RunNetsh(arguments);
}

} // namespace platform
