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

#ifndef KEEPASSXC_OPENFROMWEBDAVDIALOG_H
#define KEEPASSXC_OPENFROMWEBDAVDIALOG_H

#include "config-keepassx.h"

#ifdef KPXC_FEATURE_NETWORK

#include "networking/WebDavClient.h"

#include <QDialog>
#include <QScopedPointer>
#include <QString>

class RemoteDatabaseSession;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

/**
 * Asks for a WebDAV URL and credentials, then downloads the database.
 *
 * On acceptance the caller takes ownership of a RemoteDatabaseSession and opens
 * localFilePath() through the normal "open database" path; the session handles
 * uploading the file back on every save.
 */
class OpenFromWebDavDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpenFromWebDavDialog(QWidget* parent = nullptr);
    // Defined in the .cpp so QScopedPointer's deleter sees the complete type.
    ~OpenFromWebDavDialog() override;

    /** Absolute path of the downloaded mirror; only valid after acceptance. */
    QString localFilePath() const;
    /** Session for the downloaded database; ownership passes to the caller. */
    RemoteDatabaseSession* takeSession();

private slots:
    void startDownload();

private:
    QString url() const;
    WebDavConfig webDavConfig() const;
    void setStatus(const QString& message, bool isError);

    QLineEdit* m_urlEdit = nullptr;
    QLineEdit* m_usernameEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QCheckBox* m_rememberCheck = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_openButton = nullptr;

    QString m_localFilePath;
    QScopedPointer<RemoteDatabaseSession> m_session;
};

#endif // KPXC_FEATURE_NETWORK

#endif // KEEPASSXC_OPENFROMWEBDAVDIALOG_H
