cmake_minimum_required(VERSION 3.21)

project(MinecraftServerManager
    LANGUAGES CXX
    DESCRIPTION "Windows Minecraft Server Management Application (Qt6 UI + native Win32 process backend)"
)

if (NOT WIN32)
    message(FATAL_ERROR
        "MinecraftServerManager's process backend uses the native Win32 API "
        "(CreateProcessW, Job Objects) and cannot be built for this platform.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt's moc needs to process any header containing Q_OBJECT (MainWindow.hpp,
# AddServerDialog.hpp). AUTOMOC finds those automatically from the sources
# listed below - no manual moc_*.cpp bookkeeping needed.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(MSM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/CompilerOptions.cmake)

# If CMake cannot find Qt6, point it at your install, e.g.:
#   cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
# Network is required for the Modrinth mods/plugins/modpacks library
# (ui::LibraryDialog / net::ModrinthClient).
# Qt location: pass -DCMAKE_PREFIX_PATH=... or -DQt6_DIR=... on the command
# line, or set the QT_DIR environment variable (e.g. C:/Qt/6.7.0/msvc2022_64).
# Nothing machine-specific is hardcoded here any more.
if (NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH AND DEFINED ENV{QT_DIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
endif()
set(Qt6_DIR
    "C:/Qt/6.11.2/msvc2022_64/lib/cmake/Qt6"
)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

if (WIN32)
    enable_language(RC) # for resources/app.rc (exe icon + version info)
endif()

add_executable(MinecraftServerManager WIN32
    src/main.cpp

    resources/resources.qrc
    $<$<BOOL:${WIN32}>:resources/app.rc>

    src/app/Application.hpp
    src/app/Application.cpp

    src/core/Types.hpp
    src/core/Event.hpp
    src/core/EventDispatcher.hpp
    src/core/EventDispatcher.cpp
    src/core/Localize.hpp
    src/core/Localize.cpp
    src/core/Logger.hpp
    src/core/Logger.cpp
    src/core/Json.hpp
    src/core/Json.cpp
    src/core/StringUtil.hpp
    src/core/StringUtil.cpp

    src/process/Process.hpp
    src/process/Process.cpp
    src/process/ProcessPipes.hpp
    src/process/ProcessPipes.cpp

    src/monitor/ProcessMonitor.hpp
    src/monitor/ProcessMonitor.cpp

    src/javamanager/JavaManager.hpp
    src/javamanager/JavaManager.cpp

    src/backup/ZipWriter.hpp
    src/backup/ZipWriter.cpp
    src/backup/ZipReader.hpp
    src/backup/ZipReader.cpp
    src/backup/BackupManager.hpp
    src/backup/BackupManager.cpp

    src/console/ConsoleBuffer.hpp
    src/console/ConsoleController.hpp
    src/console/ConsoleController.cpp

    src/server/MinecraftServer.hpp
    src/server/MinecraftServer.cpp
    src/server/ServerManager.hpp
    src/server/ServerManager.cpp
    src/server/ServerImporter.hpp
    src/server/ServerImporter.cpp

    src/config/ConfigManager.hpp
    src/config/ConfigManager.cpp
    src/config/ServerProperties.hpp
    src/config/ServerProperties.cpp

    src/net/ModrinthClient.hpp
    src/net/ModrinthClient.cpp
    src/net/ServerProviderClient.hpp
    src/net/ServerProviderClient.cpp

    src/platform/StartupManager.hpp
    src/platform/StartupManager.cpp
    src/platform/FirewallManager.hpp
    src/platform/FirewallManager.cpp

    src/ui/MainWindow.hpp
    src/ui/MainWindow.cpp
    src/ui/AddServerDialog.hpp
    src/ui/AddServerDialog.cpp
    src/ui/LibraryDialog.hpp
    src/ui/LibraryDialog.cpp
    src/ui/PropertiesDialog.hpp
    src/ui/PropertiesDialog.cpp
    src/ui/PlayersDialog.hpp
    src/ui/PlayersDialog.cpp
    src/ui/BackupsDialog.hpp
    src/ui/BackupsDialog.cpp
    src/ui/ImportServerDialog.hpp
    src/ui/ImportServerDialog.cpp
    src/ui/InstalledAddonsDialog.hpp
    src/ui/InstalledAddonsDialog.cpp
    src/ui/AdvancedSettingsDialog.hpp
    src/ui/AdvancedSettingsDialog.cpp
    src/archive/ZipArchive.hpp
    src/archive/ZipArchive.cpp
    src/net/ModpackInstaller.hpp
    src/net/ModpackInstaller.cpp
    src/ui/I18n.hpp
    src/ui/I18n.cpp
    src/ui/AddonCompat.hpp
    src/ui/AddonCompat.cpp
    src/ui/OpenSshHelper.hpp
    src/ui/OpenSshHelper.cpp
    src/remote/RemoteControlServer.hpp
    src/remote/RemoteControlServer.cpp
    src/ui/AppSettings.hpp
    src/ui/AppSettings.cpp
    src/ui/FluentTheme.hpp
    src/ui/FluentTheme.cpp
    src/ui/FluentWidgets.hpp
    src/ui/FluentWidgets.cpp
    src/ui/ServerOptionsDialog.hpp
    src/ui/ServerOptionsDialog.cpp
    src/ui/AppSettingsDialog.hpp
    src/ui/AppSettingsDialog.cpp
    src/ui/FileEditorDialog.hpp
    src/ui/FileEditorDialog.cpp
)

target_include_directories(MinecraftServerManager PRIVATE src)

# UNICODE / _UNICODE select the *W (wide) Win32 API overloads used by the
# process/console/config backend (CreateProcessW, etc.) - unrelated to Qt,
# which always talks to Windows in UTF-16 regardless of this macro.
# NOMINMAX avoids <windows.h>'s min/max macros clashing with std::min/max.
target_compile_definitions(MinecraftServerManager PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(MinecraftServerManager PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)
if (WIN32)
    target_link_libraries(MinecraftServerManager PRIVATE dwmapi) # dark title bar / Mica via DWM
endif()

msm_apply_compiler_options(MinecraftServerManager)

set_target_properties(MinecraftServerManager PROPERTIES
    VS_DPI_AWARE "PerMonitor"
)

# Copies the Qt DLLs (Qt6Core.dll, Qt6Widgets.dll, platforms/qwindows.dll,
# ...) next to the .exe after each build, so it runs without manually
# fiddling with PATH. Requires windeployqt.exe to be reachable, which it
# is automatically when Qt6 was found via find_package() above.
find_program(MSM_WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin" "$ENV{QT_DIR}/bin")
if (MSM_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET MinecraftServerManager POST_BUILD
        COMMAND "${MSM_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:MinecraftServerManager>"
        COMMENT "Running windeployqt to stage Qt runtime DLLs next to the .exe"
    )
endif()

# --- Tests (spec section 46) -------------------------------------------
# Kept minimal for the Phase 1 MVP: a hand-rolled smoke-test executable for
# the pieces that do not require a live java.exe/Windows process/Qt event
# loop to be meaningful (JSON round-trip, config load/save).
option(MSM_BUILD_TESTS "Build the smoke-test executable" OFF)
if (MSM_BUILD_TESTS)
    add_executable(MinecraftServerManagerTests
        tests/ConfigTests.cpp

        src/core/Json.cpp
        src/core/Logger.cpp
        src/config/ConfigManager.cpp
    )
    target_include_directories(MinecraftServerManagerTests PRIVATE src)
    target_compile_definitions(MinecraftServerManagerTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    msm_apply_compiler_options(MinecraftServerManagerTests)

    enable_testing()
    add_test(NAME ConfigSmokeTests COMMAND MinecraftServerManagerTests)
endif()
