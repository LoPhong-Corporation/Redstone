#pragma once

// Protects small secrets (remote-control tokens) before they are written to disk.
//
// Windows: DPAPI (CryptProtectData, current-user scope) - the stored value can only be decrypted by
// the same Windows account on the same machine, so copying app_settings.ini elsewhere (or a backup
// of it leaking) does not reveal the tokens. Other platforms: no OS facility is assumed, the value
// is stored as is (the files are still created for the current user only).
//
// Stored form: "dpapi:<base64>" when protected, anything else is treated as plain text, so files
// written by older versions still load (and are re-saved protected).

#include <QString>

namespace platform
{

// Returns the string to store. Never fails: if protection is unavailable the input is returned.
QString ProtectSecret(const QString& plain);

// Reverses ProtectSecret(). `ok` (optional) is false when a "dpapi:" value cannot be decrypted
// (different Windows account/machine, or corrupt) - the result is then empty.
QString UnprotectSecret(const QString& stored, bool* ok = nullptr);

bool IsProtectionAvailable();

} // namespace platform
