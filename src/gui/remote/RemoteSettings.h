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

#ifndef KEEPASSXC_REMOTESETTINGS_H
#define KEEPASSXC_REMOTESETTINGS_H

#include <QHash>
#include <QObject>
#include <QSharedPointer>

class Database;

struct RemoteParams
{
    /** How the remote database is reached. */
    enum class Transport
    {
        Command, // user-supplied download/upload commands (any tool)
        WebDav, // built-in WebDAV client with ETag conflict detection
    };

    QString name;
    Transport transport = Transport::Command;

    // Transport::Command
    QString downloadCommand;
    QString downloadInput;
    int downloadTimeoutMsec;
    QString uploadCommand;
    QString uploadInput;
    int uploadTimeoutMsec;

    // Transport::WebDav
    QString webDavUrl;
    QString webDavUsername;
    int webDavTimeoutMsec = 30000;
};
Q_DECLARE_METATYPE(RemoteParams)

class RemoteSettings : public QObject
{
    Q_OBJECT
public:
    explicit RemoteSettings(const QSharedPointer<Database>& db, QObject* parent = nullptr);
    ~RemoteSettings() override;

    void setDatabase(const QSharedPointer<Database>& db);

    void addRemoteParams(RemoteParams* params);
    void removeRemoteParams(const QString& name);
    RemoteParams* getRemoteParams(const QString& name) const;
    QList<RemoteParams*> getAllRemoteParams() const;

    void loadSettings();
    void saveSettings() const;

    /**
     * WebDAV password for a remote.
     *
     * Deliberately NOT stored in the database: the connection settings live in
     * the (encrypted) kdbx, but the password is kept in the local, unencrypted
     * configuration instead. A shared database then cannot be used to point a
     * victim's client at somebody else's server and harvest the stored
     * credentials, which is the same class of problem as the sync-command
     * injection discussed in upstream issue #12852. The cost is that the
     * password has to be entered again on every machine the database is used on.
     *
     * TODO: move this to the system keyring once KeePassXC has a store for
     * secrets that belong to a remote rather than to a database.
     */
    QString webDavPassword(const RemoteParams* params) const;
    void setWebDavPassword(const RemoteParams* params, const QString& password) const;

private:
    void fromConfig(const QString& data);
    QString toConfig() const;
    static QString credentialKey(const RemoteParams* params);

    QHash<QString, RemoteParams*> m_remoteParams;
    QSharedPointer<Database> m_db;
};

#endif // KEEPASSXC_REMOTESETTINGS_H
