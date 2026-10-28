#include "ui/OpenSshHelper.hpp"

#include "ui/I18n.hpp"

#include <QProcess>
#include <QStandardPaths>

namespace ui::openssh
{

ServerStatus QueryServerStatus()
{
#ifdef Q_OS_WIN
    QProcess sc;
    sc.start(QStringLiteral("sc.exe"), {QStringLiteral("query"), QStringLiteral("sshd")});
    if (!sc.waitForFinished(4000))
    {
        sc.kill();
        return ServerStatus::Unknown;
    }
    if (sc.exitCode() == 1060) // ERROR_SERVICE_DOES_NOT_EXIST
    {
        return ServerStatus::NotInstalled;
    }
    const QString out = QString::fromLocal8Bit(sc.readAllStandardOutput());
    if (out.contains(QStringLiteral("RUNNING")))
    {
        return ServerStatus::Running;
    }
    return out.contains(QStringLiteral("STATE")) ? ServerStatus::InstalledStopped : ServerStatus::Unknown;
#else
    const QString sshd = QStandardPaths::findExecutable(
        QStringLiteral("sshd"), {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"), QStringLiteral("/sbin"), QStringLiteral("/usr/local/sbin")});
    if (sshd.isEmpty())
    {
        return ServerStatus::NotInstalled;
    }
    QProcess pg;
    pg.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("sshd")});
    if (pg.waitForFinished(2000))
    {
        return pg.exitCode() == 0 ? ServerStatus::Running : ServerStatus::InstalledStopped;
    }
    return ServerStatus::Unknown;
#endif
}

QString StatusText(ServerStatus status)
{
    switch (status)
    {
        case ServerStatus::Running: return TR("Đã cài và đang chạy");
        case ServerStatus::InstalledStopped: return TR("Đã cài nhưng chưa chạy");
        case ServerStatus::NotInstalled: return TR("Chưa cài");
        default: return TR("Không xác định");
    }
}

bool CanInstallAutomatically()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool StartElevatedInstall()
{
#ifdef Q_OS_WIN
    // One elevated PowerShell run does everything, so there is a single UAC prompt.
    const QString script = QStringLiteral(
        "Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0; "
        "Set-Service -Name sshd -StartupType Automatic; "
        "Start-Service sshd; "
        "if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) { "
        "New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (sshd)' -Enabled True "
        "-Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null }");
    // Start-Process -Verb RunAs triggers the UAC prompt; the inner command is passed as one quoted argument.
    QString inner = script;
    inner.replace(QLatin1Char('\''), QStringLiteral("''"));
    const QString outer = QStringLiteral(
        "Start-Process powershell -Verb RunAs -WindowStyle Normal -ArgumentList "
        "'-NoProfile','-ExecutionPolicy','Bypass','-Command','%1'").arg(inner.replace(QLatin1Char('"'), QStringLiteral("\\\"")));
    return QProcess::startDetached(
        QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-Command"), outer});
#else
    return false;
#endif
}

} // namespace ui::openssh
