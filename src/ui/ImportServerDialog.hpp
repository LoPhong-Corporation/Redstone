#pragma once

// Fork-inspired "Import Server": point this at a folder that already has
// a Minecraft server jar in it (moved from another manager, or set up by
// hand) and it scans the folder (server::ScanServerDirectory) to guess
// the jar filename, server type, and port, instead of making the user
// fill in an entirely blank AddServerDialog for a server that already
// exists on disk.

#include "core/Types.hpp"

#include <QDialog>

#include <optional>

class QLineEdit;
class QLabel;
class QPushButton;

namespace ui
{

class ImportServerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportServerDialog(QWidget* parent = nullptr);

    [[nodiscard]] std::optional<core::ServerConfig> GetResult() const { return result_; }

private slots:
    void OnBrowseDirectory();
    void OnBrowseJavaPath();
    void OnImportClicked();

private:
    void RescanDirectory();

    QLineEdit* editName_ = nullptr;
    QLineEdit* editDirectory_ = nullptr;
    QLabel* labelDetected_ = nullptr;
    QLineEdit* editJavaPath_ = nullptr;
    QLineEdit* editMinMemory_ = nullptr;
    QLineEdit* editMaxMemory_ = nullptr;
    QLabel* labelError_ = nullptr;

    std::wstring detectedJarFilename_;
    std::wstring detectedType_;
    std::uint16_t detectedPort_ = 25565;
    bool hasScanResult_ = false;

    std::optional<core::ServerConfig> result_;
};

} // namespace ui
