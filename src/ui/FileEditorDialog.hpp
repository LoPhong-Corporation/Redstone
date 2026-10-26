#pragma once

// Fork-inspired "in-app code editor": a plain text editor for any file
// inside a server's directory (bukkit.yml, spigot.yml, a plugin's own
// config.yml, ...), not just server.properties (which has its own
// friendlier form-based ui::PropertiesDialog). No syntax highlighting in
// this pass - see README for why that was left out.

#include <QDialog>

#include <filesystem>

class QPlainTextEdit;
class QLabel;

namespace ui
{

class FileEditorDialog : public QDialog
{
    Q_OBJECT

public:
    FileEditorDialog(std::filesystem::path filePath, QWidget* parent = nullptr);

private slots:
    void OnSaveClicked();
    void OnReloadClicked();

private:
    void LoadFile();

    std::filesystem::path filePath_;
    QPlainTextEdit* editor_ = nullptr;
    QLabel* labelStatus_ = nullptr;
};

} // namespace ui
