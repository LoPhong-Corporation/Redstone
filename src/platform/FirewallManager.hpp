#pragma once

// Phase 6 remainder: opens a TCP port on Windows Firewall for a server,
// via `netsh advfirewall firewall add rule` (run through the same
// process::Process wrapper used everywhere else in this app - no direct
// Windows Firewall COM API, keeping this consistent with the rest of the
// codebase and avoiding pulling in netfw.h/COM interop for one command).
//
// This is a MANUAL, one-off action (a button in
// ui::AdvancedSettingsDialog), not something Start() calls automatically
// every time - calling `netsh ... add rule` repeatedly would pile up
// duplicate rules with the same name, since netsh does not de-duplicate.
//
// Modifying Windows Firewall rules normally requires the process to be
// running elevated (as Administrator). If this app is not running
// elevated, AddRule() will fail (`netsh` exits non-zero) - the caller is
// expected to surface that to the user rather than silently doing
// nothing (see AdvancedSettingsDialog's button handler).

#include <cstdint>
#include <string>

namespace platform
{

// `ruleName` should be unique per server (callers use the server's name)
// so RemoveRule() can target the right one and re-running AddRule() for a
// different server doesn't collide.
bool AddFirewallRule(const std::wstring& ruleName, std::uint16_t port);
bool RemoveFirewallRule(const std::wstring& ruleName);

} // namespace platform
