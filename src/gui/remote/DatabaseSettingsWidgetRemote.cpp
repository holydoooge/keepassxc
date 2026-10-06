/*
 *  Copyright (C) 2025 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "DatabaseSettingsWidgetRemote.h"
#include "ui_DatabaseSettingsWidgetRemote.h"

#include "core/Global.h"
#include "core/Metadata.h"

#include "RemoteHandler.h"
#include "RemoteSettings.h"
#ifdef KPXC_FEATURE_NETWORK
#include "RemoteWebDavDialog.h"
#endif
#include "gui/MessageBox.h"

#include <QFile>

DatabaseSettingsWidgetRemote::DatabaseSettingsWidgetRemote(QWidget* parent)
    : DatabaseSettingsWidget(parent)
    , m_remoteSettings(new RemoteSettings(nullptr, this))
    , m_ui(new Ui::DatabaseSettingsWidgetRemote())
{
    m_ui->setupUi(this);
    m_ui->messageWidget->setHidden(true);

    connect(m_ui->saveSettingsButton, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemote::saveCurrentSettings);
    connect(
        m_ui->removeSettingsButton, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemote::removeCurrentSettings);
    connect(m_ui->settingsListWidget,
            &QListWidget::itemSelectionChanged,
            this,
            &DatabaseSettingsWidgetRemote::editCurrentSettings);
    connect(m_ui->testDownloadCommandButton, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemote::testDownload);
    connect(m_ui->webDavConfigureButton, &QPushButton::clicked, this, &DatabaseSettingsWidgetRemote::configureWebDav);

    connect(m_ui->transportComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                m_webDavMode = (index == 1);
                m_modified = true;
                updateWebDavSummary();
            });

    auto setModified = [this]() { m_modified = true; };
    connect(m_ui->nameLineEdit, &QLineEdit::textChanged, setModified);
    connect(m_ui->downloadCommand, &QLineEdit::textChanged, setModified);
    connect(m_ui->inputForDownload, &QPlainTextEdit::textChanged, setModified);
    connect(m_ui->downloadTimeoutSec, QOverload<int>::of(&QSpinBox::valueChanged), setModified);
    connect(m_ui->uploadCommand, &QLineEdit::textChanged, setModified);
    connect(m_ui->inputForUpload, &QPlainTextEdit::textChanged, setModified);
    connect(m_ui->uploadTimeoutSec, QOverload<int>::of(&QSpinBox::valueChanged), setModified);

    updateWebDavSummary();
}

DatabaseSettingsWidgetRemote::~DatabaseSettingsWidgetRemote() = default;

void DatabaseSettingsWidgetRemote::initialize()
{
    clearFields();
    m_remoteSettings->setDatabase(m_db);
    updateSettingsList();
    if (m_ui->settingsListWidget->count() > 0) {
        m_ui->settingsListWidget->setCurrentRow(0);
        m_ui->removeSettingsButton->setEnabled(true);
    } else {
        m_ui->removeSettingsButton->setDisabled(true);
    }
}

void DatabaseSettingsWidgetRemote::uninitialize()
{
}

bool DatabaseSettingsWidgetRemote::saveSettings()
{
    if (m_modified) {
        auto ans = MessageBox::question(this,
                                        tr("Save Remote Settings"),
                                        tr("You have unsaved changes. Do you want to save them?"),
                                        MessageBox::Save | MessageBox::Discard | MessageBox::Cancel,
                                        MessageBox::Save);
        if (ans == MessageBox::Save) {
            saveCurrentSettings();
        } else if (ans == MessageBox::Cancel) {
            return false;
        }
    }

    m_remoteSettings->saveSettings();
    return true;
}

void DatabaseSettingsWidgetRemote::saveCurrentSettings()
{
    QString name = m_ui->nameLineEdit->text();

    // Picking WebDAV and pressing Save without naming the remote is the obvious
    // first thing a user tries; give it a name instead of doing nothing.
    if (name.isEmpty() && m_webDavMode) {
        name = QStringLiteral("WebDAV");
        m_ui->nameLineEdit->setText(name);
    }

    if (name.isEmpty()) {
        m_ui->messageWidget->showMessage(tr("Name cannot be empty."), MessageWidget::Warning);
        return;
    }

    if (m_webDavMode && m_webDavUrl.isEmpty()) {
        m_ui->messageWidget->showMessage(
            tr("Configure the WebDAV server first: press “Configure WebDAV…” and enter the database URL."),
            MessageWidget::Warning);
        return;
    }

    auto* params = new RemoteParams();
    params->name = name;
    params->transport = m_webDavMode ? RemoteParams::Transport::WebDav : RemoteParams::Transport::Command;
    params->downloadCommand = m_ui->downloadCommand->text();
    params->downloadInput = m_ui->inputForDownload->toPlainText();
    params->downloadTimeoutMsec = m_ui->downloadTimeoutSec->value() * 1000;
    params->uploadCommand = m_ui->uploadCommand->text();
    params->uploadInput = m_ui->inputForUpload->toPlainText();
    params->uploadTimeoutMsec = m_ui->uploadTimeoutSec->value() * 1000;
    params->webDavUrl = m_webDavUrl;
    params->webDavUsername = m_webDavUsername;
    params->webDavTimeoutMsec = m_ui->downloadTimeoutSec->value() * 1000;

    // The password never enters the database; it is stored per-machine and keyed
    // by the exact remote it belongs to (see RemoteSettings::webDavPassword).
    m_remoteSettings->setWebDavPassword(params, m_webDavPassword);

    m_remoteSettings->addRemoteParams(params);
    updateSettingsList();

    auto item = findItemByName(name);
    m_ui->settingsListWidget->setCurrentItem(item);
    m_ui->removeSettingsButton->setEnabled(true);
    m_modified = false;
}

QListWidgetItem* DatabaseSettingsWidgetRemote::findItemByName(const QString& name)
{
    return m_ui->settingsListWidget->findItems(name, Qt::MatchExactly).first();
}

void DatabaseSettingsWidgetRemote::removeCurrentSettings()
{
    m_remoteSettings->removeRemoteParams(m_ui->nameLineEdit->text());
    updateSettingsList();
    if (!m_remoteSettings->getAllRemoteParams().empty()) {
        m_ui->settingsListWidget->setCurrentRow(0);
        m_ui->removeSettingsButton->setEnabled(true);
    } else {
        clearFields();
        m_ui->removeSettingsButton->setDisabled(true);
    }
}

void DatabaseSettingsWidgetRemote::editCurrentSettings()
{
    if (!m_ui->settingsListWidget->currentItem()) {
        return;
    }

    QString name = m_ui->settingsListWidget->currentItem()->text();
    auto* params = m_remoteSettings->getRemoteParams(name);
    if (!params) {
        return;
    }

    m_ui->nameLineEdit->setText(params->name);
    m_ui->downloadCommand->setText(params->downloadCommand);
    m_ui->inputForDownload->setPlainText(params->downloadInput);
    m_ui->downloadTimeoutSec->setValue(params->downloadTimeoutMsec / 1000);
    m_ui->uploadCommand->setText(params->uploadCommand);
    m_ui->inputForUpload->setPlainText(params->uploadInput);
    m_ui->uploadTimeoutSec->setValue(params->uploadTimeoutMsec / 1000);

    m_webDavMode = (params->transport == RemoteParams::Transport::WebDav);
    m_webDavUrl = params->webDavUrl;
    m_webDavUsername = params->webDavUsername;
    // Re-read the locally stored secret; it is absent on a machine that has
    // never used this remote, in which case the user is asked for it again.
    m_webDavPassword = m_remoteSettings->webDavPassword(params);
    if (params->webDavTimeoutMsec > 0) {
        m_ui->downloadTimeoutSec->setValue(params->webDavTimeoutMsec / 1000);
    }

    // setCurrentIndex triggers the change handler, which is harmless here.
    m_ui->transportComboBox->setCurrentIndex(m_webDavMode ? 1 : 0);
    updateWebDavSummary();
    m_modified = false;
}

void DatabaseSettingsWidgetRemote::configureWebDav()
{
#ifdef KPXC_FEATURE_NETWORK
    RemoteWebDavDialog dialog(this);
    dialog.setUrl(m_webDavUrl);
    dialog.setUsername(m_webDavUsername);
    dialog.setPassword(m_webDavPassword);
    dialog.setTimeoutSec(m_ui->downloadTimeoutSec->value());

    connect(&dialog, &RemoteWebDavDialog::testRequested, this, [this, &dialog] {
        RemoteParams params;
        params.transport = RemoteParams::Transport::WebDav;
        params.webDavUrl = dialog.url();
        params.webDavUsername = dialog.username();
        params.webDavTimeoutMsec = dialog.timeoutSec() * 1000;

        RemoteHandler handler(this);
        const auto result = handler.testWebDav(&params, dialog.password());
        if (result.success) {
            dialog.setStatusMessage(result.errorMessage.isEmpty()
                                        ? tr("Connected successfully.")
                                        : result.errorMessage,
                                    false);
        } else {
            dialog.setStatusMessage(tr("Connection failed: %1").arg(result.errorMessage), true);
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_webDavUrl = dialog.url();
    m_webDavUsername = dialog.username();
    m_webDavPassword = dialog.password();

    if (m_webDavUrl.isEmpty()) {
        m_ui->messageWidget->showMessage(tr("The WebDAV URL cannot be empty."), MessageWidget::Warning);
        return;
    }

    m_webDavMode = true;
    m_ui->transportComboBox->setCurrentIndex(1);
    m_ui->downloadTimeoutSec->setValue(dialog.timeoutSec());
    updateWebDavSummary();
    m_modified = true;
#else
    m_ui->messageWidget->showMessage(tr("This build of KeePassXC was compiled without networking support."),
                                     MessageWidget::Warning);
#endif
}

void DatabaseSettingsWidgetRemote::updateWebDavSummary()
{
    if (m_webDavUrl.isEmpty()) {
        m_ui->webDavSummaryLabel->setText(
            m_webDavMode
                ? tr("No WebDAV server configured yet — press “Configure WebDAV…”, then Save.")
                : tr("Select “WebDAV (built-in client)” and press “Configure WebDAV…” to set up a server."));
    } else {
        auto summary = tr("WebDAV: %1 (user %2)").arg(m_webDavUrl, m_webDavUsername.isEmpty()
                                                                      ? tr("none")
                                                                      : m_webDavUsername);
        if (m_webDavPassword.isEmpty()) {
            summary += QLatin1Char(' ') + tr("— no password stored on this computer yet.");
        }
        m_ui->webDavSummaryLabel->setText(summary);
    }

    // The command fields are meaningless in WebDAV mode.
    m_ui->commandTabWidget->setEnabled(!m_webDavMode);
    // Deliberately always enabled: this button is how a NEW remote is switched
    // to WebDAV, so disabling it outside WebDAV mode made the feature
    // unreachable (the mode could only be changed by the combo box, which had no
    // visible effect on the fields it enables).
    m_ui->webDavConfigureButton->setEnabled(true);
}

void DatabaseSettingsWidgetRemote::updateSettingsList()
{
    m_ui->settingsListWidget->clear();
    for (auto params : m_remoteSettings->getAllRemoteParams()) {
        auto* item = new QListWidgetItem(m_ui->settingsListWidget);
        item->setText(params->name);
        m_ui->settingsListWidget->addItem(item);
    }
}

void DatabaseSettingsWidgetRemote::clearFields()
{
    m_ui->nameLineEdit->setText("");
    m_ui->downloadCommand->setText("");
    m_ui->inputForDownload->setPlainText("");
    m_ui->downloadTimeoutSec->setValue(10);
    m_ui->uploadCommand->setText("");
    m_ui->inputForUpload->setPlainText("");
    m_ui->uploadTimeoutSec->setValue(10);

    m_webDavMode = false;
    m_webDavUrl.clear();
    m_webDavUsername.clear();
    m_webDavPassword.clear();
    m_ui->transportComboBox->setCurrentIndex(0);
    updateWebDavSummary();

    m_modified = false;
}

void DatabaseSettingsWidgetRemote::testDownload()
{
    RemoteParams params;
    params.name = m_ui->nameLineEdit->text();
    params.downloadCommand = m_ui->downloadCommand->text();
    params.downloadInput = m_ui->inputForDownload->toPlainText();
    params.downloadTimeoutMsec = m_ui->downloadTimeoutSec->value() * 1000;

    QScopedPointer<RemoteHandler> remoteHandler(new RemoteHandler(this));
    if (params.downloadCommand.isEmpty()) {
        m_ui->messageWidget->showMessage(tr("Download command cannot be empty."), MessageWidget::Warning);
        return;
    }

    RemoteHandler::RemoteResult result = remoteHandler->download(&params);
    if (!result.success) {
        m_ui->messageWidget->showMessage(tr("Download failed with error: %1").arg(result.errorMessage),
                                         MessageWidget::Error);
        return;
    }

    if (!QFile::exists(result.filePath)) {
        m_ui->messageWidget->showMessage(tr("Download finished, but file %1 could not be found.").arg(result.filePath),
                                         MessageWidget::Error);
        return;
    }

    m_ui->messageWidget->showMessage(tr("Download successful."), MessageWidget::Positive);
}
