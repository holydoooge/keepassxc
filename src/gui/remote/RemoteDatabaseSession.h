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

#ifndef KEEPASSXC_REMOTEDATABASESESSION_H
#define KEEPASSXC_REMOTEDATABASESESSION_H

#include "config-keepassx.h"

#ifdef KPXC_FEATURE_NETWORK

#include "networking/WebDavClient.h"

#include <QCoreApplication>
#include <QHash>
#include <QString>

class QWidget;

/**
 * A database that was opened straight from a WebDAV URL.
 *
 * KeePassXC's core works on local files only (Database keeps a file path, a
 * block hash and a file watcher), so "opening from WebDAV" means: download the
 * remote file to a local mirror, open that mirror normally, and push it back to
 * the server whenever the user saves. The mirror is what the rest of the
 * application sees, which is why nothing else in the code base needs to know
 * that the database came from a server.
 *
 * The push back is conditional on the ETag observed at download time. If another
 * device wrote the database in the meantime the server answers 412 and the
 * upload is refused instead of silently replacing the other device's changes;
 * the user is then told to reopen the database so both revisions get merged.
 */
class RemoteDatabaseSession
{
    // Not a QObject: it is a plain value-like session object. This provides the
    // tr() used for the conflict and failure dialogs.
    Q_DECLARE_TR_FUNCTIONS(RemoteDatabaseSession)

public:
    RemoteDatabaseSession(const WebDavConfig& config, const QString& mirrorFilePath, const QString& etag);

    /** Path of the local mirror that was handed to the application. */
    QString mirrorFilePath() const;
    /** URL shown to the user, with any user-info stripped. */
    QString displayUrl() const;

    /**
     * Upload the mirror back to the server.
     *
     * @param parentWidget widget to parent a conflict dialog on, may be null.
     * @return true when the server accepted the new revision.
     */
    bool upload(QWidget* parentWidget = nullptr);

    /** True once the server refused an upload because the remote copy moved on. */
    bool hasUnresolvedConflict() const;

private:
    WebDavConfig m_config;
    QString m_mirrorFilePath;
    // ETag of the revision the mirror was based on; updated after each upload.
    QString m_etag;
    bool m_conflict = false;

    Q_DISABLE_COPY(RemoteDatabaseSession)
};

namespace RemoteDatabaseSessions
{
    /**
     * Tracks WebDAV-backed databases by their local mirror path.
     *
     * A registry rather than a member of DatabaseWidget, because the widget is
     * constructed by the tab widget on the normal open path and has no way to
     * receive the session.
     */
    void add(const QString& mirrorFilePath, RemoteDatabaseSession* session);
    void remove(const QString& mirrorFilePath);
    /** The session for a database opened from WebDAV, or nullptr. */
    RemoteDatabaseSession* find(const QString& mirrorFilePath);
} // namespace RemoteDatabaseSessions

#endif // KPXC_FEATURE_NETWORK

#endif // KEEPASSXC_REMOTEDATABASESESSION_H
