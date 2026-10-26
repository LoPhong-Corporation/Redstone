#pragma once

// Fork-inspired "locally installed plugins" view: lists every .jar
// sitting in this server's plugins/ and mods/ folders (whichever exist)
// so the user can see and remove what's already installed - the
// complement to ui::LibraryDialog, which only installs new ones.

#include "core/Types.hpp"

#include <QDialog>

class QListWidget;
class QPushButton;
class QLabel;

namespace ui
{

class InstalledAddonsDialog : public QDialog
{
    Q_OBJECT

public:
    InstalledAddonsDialog(core::ServerConfig server, QWidget* parent = nullptr);

private slots:
    void OnDeleteClicked();
    void OnOpenFolderClicked();
    void OnSelectionChanged();

private:
    void Refresh();

    core::ServerConfig server_;

    QListWidget* listAddons_ = nullptr;
    QPushButton* buttonDelete_ = nullptr;
    QPushButton* buttonOpenFolder_ = nullptr;
    QLabel* labelStatus_ = nullptr;
};

} // namespace ui
