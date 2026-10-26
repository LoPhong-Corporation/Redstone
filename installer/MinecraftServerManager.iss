; Minecraft Server Manager - Inno Setup installer script.
;
; Inno Setup is a free, widely-used Windows installer builder:
; https://jrsoftware.org/isinfo.php (download "Inno Setup" - includes the
; ISCC command-line compiler and the Inno Setup Compiler GUI).
;
; ---------------------------------------------------------------------
; HOW TO BUILD THE INSTALLER
; ---------------------------------------------------------------------
;   1. Build the app itself first, Release configuration:
;        cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
;        cmake --build build --config Release
;      windeployqt runs automatically as a CMake POST_BUILD step (see the
;      root CMakeLists.txt), so build\Release\ ends up containing
;      MinecraftServerManager.exe PLUS every Qt DLL/plugin it needs
;      (Qt6Core.dll, Qt6Widgets.dll, Qt6Network.dll, platforms\qwindows.dll,
;      styles\, ...). This script just packages that whole folder.
;
;   2. Compile this script - either:
;        a) Open MinecraftServerManager.iss in the Inno Setup Compiler GUI
;           and press Compile (F9), or
;        b) From a command prompt:
;             "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\MinecraftServerManager.iss
;
;   3. The finished installer appears at installer\Output\
;      MinecraftServerManager-Setup-<version>.exe - hand that one file to
;      anyone; it bundles everything needed, no separate Qt install step.
;
; ---------------------------------------------------------------------
; WHY IT INSTALLS PER-USER (NOT INTO C:\Program Files BY DEFAULT)
; ---------------------------------------------------------------------
; The app stores its own data (data\servers.json, logs\app.log,
; config.json) as PLAIN FILES NEXT TO THE .exe using relative paths (see
; config::ConfigManager / core::Logger) - it does NOT write to
; %AppData% or the registry for its own state. Program Files is normally
; write-protected for standard (non-admin) users, which would silently
; break saving server profiles after install. Installing to
; %LocalAppData%\Programs instead (Setup's "lowest privileges" mode)
; sidesteps that entirely, with no UAC prompt needed to install - the
; same approach VS Code, Discord, and several other modern Windows apps
; use for exactly this reason.
;
; (Each Minecraft SERVER's own files/world live wherever the user points
; that server's "Thư mục server" field to when adding it - never inside
; this app's own install folder - so this only affects the app's small
; own config/log files, not any Minecraft world data.)
;
; One specific in-app feature - "🔓 Mở port trên Firewall" in
; Advanced Settings - still needs the app to be RUN as Administrator to
; succeed (modifying Windows Firewall rules requires elevation
; regardless of where the app is installed); the app already reports
; that clearly if it fails, no installer-level workaround needed for it.

#define MyAppName "Minecraft Server Manager"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "LoPhong Corporation"
#define MyAppExeName "MinecraftServerManager.exe"
; Adjust this if your Release build landed somewhere else.
#define MyBuildDir "..\build\Release"

[Setup]
; Generate your OWN GUID for a real release (Tools > Generate GUID in the
; Inno Setup Compiler) - a stable AppId is what lets a later installer
; version correctly upgrade/replace this one instead of installing side
; by side.
AppId={{B6C1B6A0-6E3D-4E9B-9C7A-9B0B8F5A5C10}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppSupportURL=https://github.com/LoPhong-Corporation-Official/MinecraftServerManager
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=MinecraftServerManager-Setup-{#MyAppVersion}
OutputDir=Output
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
; Uncomment once you have a real .ico (see README.md in this folder):
SetupIconFile=..\resources\redstone.ico
; Uncomment if you add a LICENSE.txt to the project root:
; LicenseFile=..\LICENSE.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Tạo icon ngoài Desktop"; GroupDescription: "Icon bổ sung:"; Flags: unchecked
Name: "startwithwindows"; Description: "Khởi động cùng Windows (chế độ Silent - chỉ hiện icon khay hệ thống)"; GroupDescription: "Tuỳ chọn khởi động:"; Flags: unchecked

[Files]
; Pulls in EVERYTHING windeployqt staged, recursively (exe, every Qt DLL,
; platforms\, styles\, ...) - nothing to list by hand, and nothing to
; forget when Qt's own dependency list changes between versions.
Source: "{#MyBuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Optional command-line remote control client (see cli\redstonectl\README.md)
Source: "..\cli\redstonectl\dist\redstonectl-windows-amd64.exe"; DestDir: "{app}"; DestName: "redstonectl.exe"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Gỡ cài đặt {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Writes the SAME per-user Run key/value the app's own "🚀 Cài đặt ứng
; dụng" > "Khởi động cùng Windows" checkbox manages (see
; platform::StartupManager: HKCU\Software\Microsoft\Windows\
; CurrentVersion\Run, value "MinecraftServerManager"). Using the same
; mechanism (not a Startup-folder shortcut) means the in-app checkbox
; correctly shows this as enabled, toggling it there later just works,
; and the app never launches twice at login. "uninsdeletevalue" removes
; it again on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "MinecraftServerManager"; ValueData: """{app}\{#MyAppExeName}"" --silent"; Flags: uninsdeletevalue; Tasks: startwithwindows

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Chạy {#MyAppName} ngay"; Flags: nowait postinstall skipifsilent

; Deliberately NOT deleting data\ or logs\ on uninstall - those hold the
; user's server PROFILES (name/paths/ports/etc, not the actual Minecraft
; worlds, which live elsewhere) and are small enough to leave behind by
; default, the same way most Windows apps leave user data in place unless
; the user explicitly asks for a "clean" removal. Add an [UninstallDelete]
; section here yourself if you'd rather it always wipe them.
