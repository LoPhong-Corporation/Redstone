#pragma once

// A modal dialog for entering a new server profile's fields, built on
// Qt's QDialog. Call exec(); if it returns QDialog::Accepted, GetResult()
// holds the new core::ServerConfig (id is left empty - ServerManager::
// AddServer() assigns it).

#include "core/Types.hpp"
#include "net/ServerProviderClient.hpp"

#include <QDialog>

#include <optional>
#include <thread>

class QLineEdit;
class QLabel;
class QComboBox;
class QPushButton;

namespace ui
{

class AddServerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddServerDialog(QWidget* parent = nullptr);
    ~AddServerDialog() override;

    [[nodiscard]] std::optional<core::ServerConfig> GetResult() const { return result_; }

private slots:
    void OnSaveClicked();
    void OnBrowseDirectory();
    void OnBrowseJar();
    void OnBrowseJavaPath();
    void OnProviderChanged();     // Phase 5
    void OnRefreshVersionsClicked();
    void OnDownloadJarClicked();

private:
    net::ServerProviderClient* providerClient_;
    [[nodiscard]] net::ServerProviderType CurrentProviderType() const;
    void LoadVersionsForCurrentProvider();
    void ProbeDetectedJavaVersions(); // background: appends "(Java N)" to each combo entry once known

    QLineEdit* editName_ = nullptr;
    QLineEdit* editDirectory_ = nullptr;
    QLineEdit* editJar_ = nullptr;
    QLineEdit* editJavaPath_ = nullptr;
    QComboBox* comboServerType_ = nullptr;
    QComboBox* comboDetectedJava_ = nullptr;
    QLineEdit* editMinMemory_ = nullptr;
    QLineEdit* editMaxMemory_ = nullptr;
    QLineEdit* editPort_ = nullptr;
    QLabel* labelError_ = nullptr;

    // Phase 5: Server Providers (download a server jar directly).
    QComboBox* comboProvider_ = nullptr;
    QComboBox* comboProviderVersion_ = nullptr;
    QPushButton* buttonDownloadJar_ = nullptr;
    QLabel* labelProviderStatus_ = nullptr;

    // Joinable (not detached) so the destructor can guarantee it isn't
    // still calling QMetaObject::invokeMethod(this, ...) once this
    // dialog's widgets start being torn down - same pattern used
    // throughout the rest of the UI layer (see ui::BackupsDialog for the
    // fullest explanation).
    std::jthread javaProbeThread_;

    std::optional<core::ServerConfig> result_;
};

} // namespace ui
