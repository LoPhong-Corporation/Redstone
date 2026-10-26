#pragma once

// Registers/unregisters this app to launch when the current Windows user
// logs in, via the standard per-user Registry Run key
// (HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run) -
// the same mechanism most consumer apps use (no admin rights required,
// unlike a Scheduled Task or an HKLM Run key). Combined with "Silent"
// startup mode (see main.cpp/Application) and each server's own "auto-
// start with app" setting, this is what makes "start my server when the
// PC turns on" actually work end to end: Windows starts the app -> the
// app runs with --silent (window hidden, tray icon only) -> the app
// starts every server flagged autoStartOnAppLaunch.

#include <string>

namespace platform
{

[[nodiscard]] bool IsStartOnBootEnabled();

// `silent` controls whether "--silent" is appended to the registered
// command line (see main.cpp for what that flag does). Returns false if
// the Registry key could not be written/deleted (e.g. write-protected
// HKCU, which is unusual but not impossible on locked-down machines).
bool SetStartOnBoot(bool enable, bool silent);

} // namespace platform
