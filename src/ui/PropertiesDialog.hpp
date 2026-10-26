#pragma once

// Tabbed editor for server.properties (General / Gameplay / Players /
// Network & performance / Resource pack), ~40 of the most useful keys.
// Anything not listed is left untouched on save (see
// config::WriteServerProperties), and a key that is missing from the file
// is only written when the user changes it away from Minecraft's default,
// so saving never bloats a file with defaults.

#include "core/Types.hpp"

#include <QDialog>

#include <map>
#include <string>
#include <vector>

class QLabel;
class QWidget;

namespace ui
{

class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    PropertiesDialog(core::ServerConfig server, QWidget* parent = nullptr);

private slots:
    void OnSaveClicked();
    void OnReloadClicked();

private:
    void LoadFromFile();

    core::ServerConfig server_;
    std::map<std::wstring, std::wstring> loaded_; // snapshot of the file at last load
    std::vector<QWidget*> widgets_;               // parallel to the field table in the .cpp
    QLabel* labelStatus_ = nullptr;
};

} // namespace ui
