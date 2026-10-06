/*
 *  Copyright (C) 2023 KeePassXC Team <team@keepassxc.org>
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

#include "RemoteSettings.h"

#include "core/Database.h"
#include "core/Metadata.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

RemoteSettings::RemoteSettings(const QSharedPointer<Database>& db, QObject* parent)
    : QObject(parent)
{
    setDatabase(db);
}

RemoteSettings::~RemoteSettings() = default;

void RemoteSettings::setDatabase(const QSharedPointer<Database>& db)
{
    m_remoteParams.clear();
    m_db = db;
    loadSettings();
}

void RemoteSettings::addRemoteParams(RemoteParams* params)
{
    if (params->name.isEmpty()) {
        qWarning() << "RemoteSettings::addRemoteParams: Remote parameters name is empty";
        return;
    }
    m_remoteParams.insert(params->name, params);
}

void RemoteSettings::removeRemoteParams(const QString& name)
{
    m_remoteParams.remove(name);
}

RemoteParams* RemoteSettings::getRemoteParams(const QString& name) const
{
    if (m_remoteParams.contains(name)) {
        return m_remoteParams.value(name);
    }
    return nullptr;
}

QList<RemoteParams*> RemoteSettings::getAllRemoteParams() const
{
    return m_remoteParams.values();
}

void RemoteSettings::loadSettings()
{
    if (m_db) {
        fromConfig(m_db->metadata()->customData()->value(CustomData::RemoteProgramSettings));
    }
}

void RemoteSettings::saveSettings() const
{
    if (m_db) {
        m_db->metadata()->customData()->set(CustomData::RemoteProgramSettings, toConfig());
    }
}

QString RemoteSettings::toConfig() const
{
    QJsonArray config;
    for (const auto params : m_remoteParams.values()) {
        QJsonObject object;
        object["name"] = params->name;
        object["transport"] = params->transport == RemoteParams::Transport::WebDav ? "webdav" : "command";
        object["downloadCommand"] = params->downloadCommand;
        object["downloadCommandInput"] = params->downloadInput;
        object["downloadTimeoutMsec"] = params->downloadTimeoutMsec;
        object["uploadCommand"] = params->uploadCommand;
        object["uploadCommandInput"] = params->uploadInput;
        object["uploadTimeoutMsec"] = params->uploadTimeoutMsec;
        object["webDavUrl"] = params->webDavUrl;
        object["webDavUsername"] = params->webDavUsername;
        object["webDavTimeoutMsec"] = params->webDavTimeoutMsec;
        config << object;
    }
    QJsonDocument doc(config);
    return doc.toJson(QJsonDocument::Compact);
}

void RemoteSettings::fromConfig(const QString& data)
{
    m_remoteParams.clear();

    QJsonDocument json = QJsonDocument::fromJson(data.toUtf8());
    for (const auto& item : json.array().toVariantList()) {
        auto itemMap = item.toMap();
        auto* params = new RemoteParams();
        params->name = itemMap["name"].toString();
        params->transport = itemMap.value("transport", "command").toString() == "webdav"
                                ? RemoteParams::Transport::WebDav
                                : RemoteParams::Transport::Command;
        params->downloadCommand = itemMap["downloadCommand"].toString();
        params->downloadInput = itemMap["downloadCommandInput"].toString();
        params->downloadTimeoutMsec = itemMap.value("downloadTimeoutMsec", 10000).toInt();
        params->uploadCommand = itemMap["uploadCommand"].toString();
        params->uploadInput = itemMap["uploadCommandInput"].toString();
        params->uploadTimeoutMsec = itemMap.value("uploadTimeoutMsec", 10000).toInt();
        params->webDavUrl = itemMap["webDavUrl"].toString();
        params->webDavUsername = itemMap["webDavUsername"].toString();
        params->webDavTimeoutMsec = itemMap.value("webDavTimeoutMsec", 30000).toInt();

        m_remoteParams.insert(params->name, params);
    }
}

QString RemoteSettings::credentialKey(const RemoteParams* params)
{
    if (!params) {
        return {};
    }
    // Scope the stored secret to the exact remote it belongs to, so that
    // editing the URL (in the database, possibly by somebody else) cannot make
    // the client hand the saved password to a different server.
    const auto identity = params->webDavUrl.trimmed() + QLatin1Char('\n') + params->webDavUsername;
    return QString::fromLatin1(
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

QString RemoteSettings::webDavPassword(const RemoteParams* params) const
{
    const auto key = credentialKey(params);
    if (key.isEmpty()) {
        return {};
    }
    const auto store = config()->get(Config::RemoteWebDavCredentials).toMap();
    return store.value(key).toString();
}

void RemoteSettings::setWebDavPassword(const RemoteParams* params, const QString& password) const
{
    const auto key = credentialKey(params);
    if (key.isEmpty()) {
        return;
    }
    auto store = config()->get(Config::RemoteWebDavCredentials).toMap();
    if (password.isEmpty()) {
        store.remove(key);
    } else {
        store.insert(key, password);
    }
    config()->set(Config::RemoteWebDavCredentials, store);
}
