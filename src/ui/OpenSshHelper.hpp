#pragma once

// Detects / installs the OpenSSH SERVER on the machine running Redstone - what
// the remote CLI needs on the far side (`redstonectl --ssh user@host`).
// Windows: installs the optional "OpenSSH.Server" capability, starts the sshd
// service, sets it to Automatic and opens TCP 22 - all in ONE elevated
// PowerShell run (the user sees the normal UAC prompt). Other systems: only
// detection; installing a system package is left to the user's package manager.

#include <QString>

namespace ui::openssh
{

enum class ServerStatus
{
    Running,          // sshd service is running
    InstalledStopped, // installed but not running
    NotInstalled,
    Unknown
};

ServerStatus QueryServerStatus();
QString StatusText(ServerStatus status);

// True on platforms where we can install it for the user (Windows).
bool CanInstallAutomatically();

// Launches the elevated installer and returns immediately (false if it could not be started).
// Poll QueryServerStatus() afterwards.
bool StartElevatedInstall();

} // namespace ui::openssh
