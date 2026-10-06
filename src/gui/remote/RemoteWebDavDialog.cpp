/*
 *  Copyright (C) 2026 KeePassXC Team <team@keepassxc.org>
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

#include "RemoteWebDavDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

RemoteWebDavDialog::RemoteWebDavDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("WebDAV Connection"));
    setModal(true);

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(tr("https://cloud.example.com/remote.php/dav/files/user/passwords.kdbx"));

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(tr("WebDAV user name"));

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("Password or app password"));

    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(5, 600);
    m_timeoutSpin->setSuffix(tr(" seconds"));
    m_timeoutSpin->setValue(30);

    auto* form = new QFormLayout();
    form->addRow(tr("Database URL:"), m_urlEdit);
    form->addRow(tr("User name:"), m_usernameEdit);
    form->addRow(tr("Password:"), m_passwordEdit);
    form->addRow(tr("Timeout:"), m_timeoutSpin);

    auto* hint = new QLabel(
        tr("The URL must point directly at the .kdbx file. The password is stored in this computer's "
           "KeePassXC settings, not inside the database, so sharing the database does not share the "
           "password. Use https:// — unencrypted http:// is refused unless the URL is explicitly allowed."),
        this);
    hint->setWordWrap(true);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* testButton = m_buttons->addButton(tr("Test Connection"), QDialogButtonBox::ActionRole);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(testButton, &QPushButton::clicked, this, &RemoteWebDavDialog::testRequested);

    // Clearing the status when the connection details change avoids showing a
    // stale "connected" message for a URL the user has since edited.
    auto clearStatus = [this]() { m_statusLabel->clear(); };
    connect(m_urlEdit, &QLineEdit::textChanged, clearStatus);
    connect(m_usernameEdit, &QLineEdit::textChanged, clearStatus);
    connect(m_passwordEdit, &QLineEdit::textChanged, clearStatus);

    m_urlEdit->setFocus();
}

QString RemoteWebDavDialog::url() const
{
    return m_urlEdit->text().trimmed();
}

QString RemoteWebDavDialog::username() const
{
    return m_usernameEdit->text();
}

QString RemoteWebDavDialog::password() const
{
    return m_passwordEdit->text();
}

int RemoteWebDavDialog::timeoutSec() const
{
    return m_timeoutSpin->value();
}

void RemoteWebDavDialog::setUrl(const QString& url)
{
    m_urlEdit->setText(url);
}

void RemoteWebDavDialog::setUsername(const QString& username)
{
    m_usernameEdit->setText(username);
}

void RemoteWebDavDialog::setPassword(const QString& password)
{
    m_passwordEdit->setText(password);
}

void RemoteWebDavDialog::setTimeoutSec(int seconds)
{
    if (seconds > 0) {
        m_timeoutSpin->setValue(seconds);
    }
}

void RemoteWebDavDialog::setStatusMessage(const QString& message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: palette(bright-text);")
                                         : QStringLiteral("color: palette(text);"));
}
