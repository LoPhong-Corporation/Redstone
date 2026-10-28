#include "app/Application.hpp"

#include "core/Logger.hpp"
#include "ui/AppSettings.hpp"
#include "ui/FluentTheme.hpp"
#include "ui/I18n.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QProcess>
#include <QStyleFactory>

#include <chrono>
#include <string_view>
#include <thread>

namespace app
{
namespace
{

} // namespace

int Application::Run(int argc, char** argv)
{
    // Window scale + high-DPI handling must be decided BEFORE QApplication exists.
    // PassThrough keeps fractional Windows scaling (125%/150%) crisp instead of
    // rounding it to a whole number, which made text look blurry or too small.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    {
        const QString exeDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
        const int uiScale = ui::AppSettings::PeekUiScalePercent(exeDir);
        if (uiScale > 0 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        {
            qputenv("QT_SCALE_FACTOR", QByteArray::number(uiScale / 100.0, 'f', 2));
        }
    }

    QApplication qtApp(argc, argv);
    QApplication::setApplicationName("Minecraft Server Manager");
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/redstone.png"))); // every dialog inherits it

    // data/servers.json and logs/app.log are opened via RELATIVE paths
    // (see config::ConfigManager / core::Logger), i.e. relative to the
    // process's current working directory - which is NOT necessarily the
    // folder the .exe lives in. Launched from a Start Menu shortcut it
    // usually is, but launched by Windows at login through the Registry
    // Run key (platform::StartupManager) the working directory is
    // typically System32, so the app would come up with an empty server
    // list and no way to save. Pinning the working directory to the
    // executable's own folder up front makes every relative path resolve
    // the same way regardless of how the app was started. Must happen
    // before anything touches those paths (first Logger::Instance() /
    // ConfigManager::Load() call).
    QDir::setCurrent(QCoreApplication::applicationDirPath());
    i18n::SetLanguage(ui::CurrentSettings().language); // before any widget text is created
    // Keeps the app (and its running servers) alive with only the tray
    // icon left when the main window is closed/hidden - otherwise Qt
    // would quit the whole process as soon as the last visible window
    // closes, which defeats the point of "Silent" startup mode / minimize
    // to tray.
    QApplication::setQuitOnLastWindowClosed(false);
    // Fusion renders custom QSS colours consistently across Windows
    // versions; the native "windowsvista" style ignores several of the
    // rules above (list/scrollbar colours in particular).
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    ui::FluentTheme::Apply(qtApp, ui::CurrentSettings()); // Fluent palette + QSS (must precede MainWindow)

    // "Silent" startup mode: no visible window at launch, just the tray
    // icon - meant for "start with Windows" (see platform::StartupManager)
    // so the app doesn't pop a window in the user's face on every login.
    // The window can still be opened anytime from the tray icon.
    bool silent = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--silent" || arg == "-silent")
        {
            silent = true;
        }
    }

    events_ = std::make_shared<core::EventDispatcher>();
    configManager_ = std::make_shared<config::ConfigManager>();
    serverManager_ = std::make_shared<server::ServerManager>(events_);
    serverManager_->LoadFromConfig(*configManager_);

    mainWindow_ = std::make_unique<ui::MainWindow>(serverManager_, configManager_, events_);
    if (!silent)
    {
        mainWindow_->show();
    }

    AutoStartFlaggedServers();

    const int exitCode = qtApp.exec();

    // Stop every running server BEFORE mainWindow_/events_ are destroyed
    // (which happens right after Run() returns, when Application itself
    // goes out of scope in main()). MinecraftServer's watcher/console/
    // stats threads call events_->Publish(), whose subscriber calls
    // QMetaObject::invokeMethod(mainWindow_.get(), ...) - so as long as no
    // server is left running, no background thread can touch a MainWindow
    // that is about to be destroyed.
    ShutdownServers();

    if (ui::RestartRequested().load())
    {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
    }

    return exitCode;
}

void Application::AutoStartFlaggedServers()
{
    // Off the UI thread, same reasoning as MainWindow's own Start button:
    // CreateProcessW is normally fast but should never risk stalling
    // startup (antivirus scanning java.exe, a slow disk, etc.).
    for (const auto& srv : serverManager_->GetAll())
    {
        if (srv->GetConfig().autoStartOnAppLaunch)
        {
            std::thread([srv]() { srv->Start(); }).detach();
        }
    }
}

void Application::ShutdownServers()
{
    for (const auto& srv : serverManager_->GetAll())
    {
        if (srv->GetState() == core::ServerState::Running || srv->GetState() == core::ServerState::Starting)
        {
            srv->Stop(std::chrono::seconds(20));
        }
    }
}

} // namespace app
