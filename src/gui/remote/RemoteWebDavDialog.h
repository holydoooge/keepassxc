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

#ifndef KEEPASSXC_REMOTEWEBDAVDIALOG_H
#define KEEPASSXC_REMOTEWEBDAVDIALOG_H

#include <QDialog>
#include <QString>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

/**
 * Collects the connection details of a single WebDAV-hosted database file.
 *
 * The password is held by the caller and stored locally, never in the database:
 * see RemoteSettings::webDavPassword for the reasoning.
 */
class RemoteWebDavDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoteWebDavDialog(QWidget* parent = nullptr);

    QString url() const;
    QString username() const;
    QString password() const;
    int timeoutSec() const;

    void setUrl(const QString& url);
    void setUsername(const QString& username);
    void setPassword(const QString& password);
    void setTimeoutSec(int seconds);
    void setStatusMessage(const QString& message, bool isError);

signals:
    /** Emitted instead of accepting, so the caller can verify the connection. */
    void testRequested();

private:
    QLineEdit* m_urlEdit = nullptr;
    QLineEdit* m_usernameEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QLabel* m_statusLabel = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

#endif // KEEPASSXC_REMOTEWEBDAVDIALOG_H
