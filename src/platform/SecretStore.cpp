#include "platform/SecretStore.hpp"

#include <QByteArray>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace platform
{
namespace
{
constexpr const char* kPrefix = "dpapi:";

#ifdef _WIN32
// Not a secret: it only binds the blob to this application so another program running as the same
// user cannot trivially reuse it with a bare CryptUnprotectData call.
const BYTE kEntropy[] = {'R', 'e', 'd', 's', 't', 'o', 'n', 'e', '-', 'r', 'e', 'm', 'o', 't', 'e'};
#endif
} // namespace

bool IsProtectionAvailable()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

QString ProtectSecret(const QString& plain)
{
#ifdef _WIN32
    if (plain.isEmpty())
    {
        return plain;
    }
    QByteArray bytes = plain.toUtf8();
    DATA_BLOB in{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)), const_cast<BYTE*>(kEntropy)};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Redstone", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    {
        return plain; // keep working rather than lose the secret
    }
    const QByteArray cipher(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return QString::fromLatin1(kPrefix) + QString::fromLatin1(cipher.toBase64());
#else
    return plain;
#endif
}

QString UnprotectSecret(const QString& stored, bool* ok)
{
    if (ok != nullptr)
    {
        *ok = true;
    }
    if (!stored.startsWith(QLatin1String(kPrefix)))
    {
        return stored; // plain text (older file, or a platform without protection)
    }
#ifdef _WIN32
    QByteArray cipher = QByteArray::fromBase64(stored.mid(static_cast<int>(sizeof("dpapi:") - 1)).toLatin1());
    DATA_BLOB in{static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE*>(cipher.data())};
    DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy)), const_cast<BYTE*>(kEntropy)};
    DATA_BLOB out{};
    if (CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    {
        const QString plain = QString::fromUtf8(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return plain;
    }
#endif
    if (ok != nullptr)
    {
        *ok = false;
    }
    return QString();
}

} // namespace platform
