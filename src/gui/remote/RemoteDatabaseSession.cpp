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

#include "RemoteDatabaseSession.h"

#ifdef KPXC_FEATURE_NETWORK

#include "core/AsyncTask.h"
#include "gui/MessageBox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QUrl>
#include <QUuid>

RemoteDatabaseSession::RemoteDatabaseSession(const WebDavConfig& config,
                                            const QString& mirrorFilePath,
                                            const QString& etag)
    : m_config(config)
    , m_mirrorFilePath(mirrorFilePath)
    , m_etag(etag)
{
}

QString RemoteDatabaseSession::mirrorFilePath() const
{
    return m_mirrorFilePath;
}

QString RemoteDatabaseSession::displayUrl() const
{
    QUrl url(m_config.url);
    // Credentials never belong in the UI, even if the user typed them into the URL.
    url.setUserInfo(QString());
    return url.toString();
}

bool RemoteDatabaseSession::hasUnresolvedConflict() const
{
    return m_conflict;
}

bool RemoteDatabaseSession::upload(QWidget* parentWidget)
{
    const auto result = AsyncTask::runAndWaitForFuture([this] {
        WebDavClient client(m_config);
        // Conditional on the revision this mirror is based on: a 412 means
        // somebody else wrote the database since we downloaded it.
        return client.upload(m_mirrorFilePath, m_etag);
    });

    if (result.isSuccess()) {
        m_conflict = false;
        if (!result.etag.isEmpty()) {
            m_etag = result.etag;
        }
        return true;
    }

    if (result.isConflict()) {
        m_conflict = true;
        if (parentWidget) {
            MessageBox::warning(parentWidget,
                                tr("WebDAV Upload Conflict"),
                                tr("The database on the server was changed by another device while you had it open, "
                                   "so your changes were NOT uploaded.\n\n"
                                   "Your local changes are safe in this window. To combine them with the server "
                                   "version, save the database to a file and use Database → Merge From…, or reopen "
                                   "the database from WebDAV (your unsaved changes would be lost then).\n\n"
                                   "Server: %1")
                                    .arg(displayUrl()),
                                MessageBox::Ok);
        }
        return false;
    }

    if (parentWidget) {
        MessageBox::warning(parentWidget,
                            tr("WebDAV Upload Failed"),
                            tr("The database was saved locally but could not be uploaded to %1:\n\n%2")
                                .arg(displayUrl(), result.errorMessage),
                            MessageBox::Ok);
    }
    return false;
}

namespace RemoteDatabaseSessions
{
    namespace
    {
        // Keyed by canonical mirror path so lookups from the save path match.
        QHash<QString, RemoteDatabaseSession*>& registry()
        {
            static QHash<QString, RemoteDatabaseSession*> sessions;
            return sessions;
        }

        QString keyFor(const QString& path)
        {
            if (path.isEmpty()) {
                return {};
            }
            QFileInfo info(path);
            // The file may already be gone by the time we look it up.
            return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
        }
    } // namespace

    void add(const QString& mirrorFilePath, RemoteDatabaseSession* session)
    {
        const auto key = keyFor(mirrorFilePath);
        if (key.isEmpty() || !session) {
            delete session;
            return;
        }
        // Replacing an entry means a database was reopened; drop the old session.
        delete registry().take(key);
        registry().insert(key, session);
    }

    void remove(const QString& mirrorFilePath)
    {
        delete registry().take(keyFor(mirrorFilePath));
    }

    RemoteDatabaseSession* find(const QString& mirrorFilePath)
    {
        return registry().value(keyFor(mirrorFilePath), nullptr);
    }
} // namespace RemoteDatabaseSessions

#endif // KPXC_FEATURE_NETWORK
