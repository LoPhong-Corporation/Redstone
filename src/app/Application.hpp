#pragma once

#include "config/ConfigManager.hpp"
#include "core/EventDispatcher.hpp"
#include "server/ServerManager.hpp"
#include "ui/MainWindow.hpp"

#include <memory>

namespace app
{

class Application
{
public:
    Application() = default;

    // Creates QApplication, wires ServerManager/ConfigManager/EventDispatcher,
    // shows MainWindow, runs the Qt event loop until quit, then shuts down
    // cleanly. Returns the process exit code.
    int Run(int argc, char** argv);

private:
    void ShutdownServers();
    void AutoStartFlaggedServers(); // servers with autoStartOnAppLaunch = true

    std::shared_ptr<core::EventDispatcher> events_;
    std::shared_ptr<config::ConfigManager> configManager_;
    std::shared_ptr<server::ServerManager> serverManager_;
    std::unique_ptr<ui::MainWindow> mainWindow_;
};

} // namespace app
