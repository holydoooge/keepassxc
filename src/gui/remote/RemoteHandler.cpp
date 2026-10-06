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

#include "RemoteHandler.h"

#include "RemoteProcess.h"
#include "RemoteSettings.h"

#include "core/AsyncTask.h"
#include "core/Database.h"
#include "networking/WebDavClient.h"

#include <QFile>

namespace
{
    QString getTempFileLocation()
    {
        QString uuid = QUuid::createUuid().toString().remove(0, 1);
        uuid.chop(1);
        return QDir::toNativeSeparators(QDir::temp().absoluteFilePath("RemoteDatabase-" + uuid + ".kdbx"));
    }

#ifdef KPXC_FEATURE_NETWORK
    WebDavConfig webDavConfigFor(const RemoteParams* params, const QString& password)
    {
        WebDavConfig config;
        config.url = params->webDavUrl;
        config.username = params->webDavUsername;
        config.password = password;
        config.timeoutMsec = params->webDavTimeoutMsec > 0 ? params->webDavTimeoutMsec : 30000;
        return config;
    }

    RemoteHandler::RemoteResult toRemoteResult(const WebDavClient::Result& result)
    {
        RemoteHandler::RemoteResult out;
        out.success = result.isSuccess();
        out.errorMessage = result.errorMessage;
        out.conflict = result.isConflict();
        out.etag = result.etag;
        return out;
    }
#endif
} // namespace

std::function<QScopedPointer<RemoteProcess>(QObject*)> RemoteHandler::m_createRemoteProcess([](QObject* parent) {
    return QScopedPointer<RemoteProcess>(new RemoteProcess(parent));
});

RemoteHandler::RemoteHandler(QObject* parent)
    : QObject(parent)
{
}

void RemoteHandler::setRemoteProcessFunc(std::function<QScopedPointer<RemoteProcess>(QObject*)> func)
{
    m_createRemoteProcess = std::move(func);
}

bool RemoteHandler::usesWebDav(const RemoteParams* params)
{
    return params && params->transport == RemoteParams::Transport::WebDav;
}

RemoteHandler::RemoteResult RemoteHandler::download(const RemoteParams* params, const QString& webDavPassword)
{
    if (!params) {
        RemoteResult result;
        result.errorMessage = tr("Invalid download parameters provided.");
        return result;
    }

#ifdef KPXC_FEATURE_NETWORK
    if (usesWebDav(params)) {
        return AsyncTask::runAndWaitForFuture([params, webDavPassword] {
            const auto filePath = getTempFileLocation();
            WebDavClient client(webDavConfigFor(params, webDavPassword));
            auto remote = client.download(filePath);

            auto result = toRemoteResult(remote);
            if (result.success) {
                result.filePath = filePath;
            }
            return result;
        });
    }
#else
    if (usesWebDav(params)) {
        RemoteResult result;
        result.errorMessage = tr("This build of KeePassXC was compiled without networking support.");
        return result;
    }
#endif

    return AsyncTask::runAndWaitForFuture([params] {
        RemoteResult result;
        auto filePath = getTempFileLocation();
        auto remoteProcess = m_createRemoteProcess(nullptr); // use nullptr parent, otherwise there is a warning
        remoteProcess->setTempFileLocation(filePath);
        remoteProcess->start(params->downloadCommand);
        if (!params->downloadInput.isEmpty()) {
            remoteProcess->write(params->downloadInput + "\n");
            remoteProcess->waitForBytesWritten();
            remoteProcess->closeWriteChannel();
        }

        bool finished = remoteProcess->waitForFinished(params->downloadTimeoutMsec);
        int statusCode = remoteProcess->exitCode();

        // TODO: For future use
        result.stdOutput = remoteProcess->readOutput();
        result.stdError = remoteProcess->readError();

        if (finished && statusCode == 0) {
            // Check if the file actually downloaded
            QFileInfo fileInfo(filePath);
            if (!fileInfo.exists() || fileInfo.size() == 0) {
                result.success = false;
                result.errorMessage = tr("Command `%1` failed to download database.").arg(params->downloadCommand);
            } else {
                result.success = true;
                result.filePath = filePath;
            }
        } else if (finished) {
            result.success = false;
            result.errorMessage =
                tr("Command `%1` exited with status code: %2").arg(params->downloadCommand).arg(statusCode);
        } else {
            remoteProcess->kill();
            result.success = false;
            result.errorMessage =
                tr("Command `%1` did not finish in time. Process was killed.").arg(params->downloadCommand);
        }

        return result;
    });
}

RemoteHandler::RemoteResult
RemoteHandler::upload(const QString& filePath, const RemoteParams* params, const QString& webDavPassword)
{
    if (!params) {
        RemoteResult result;
        result.errorMessage = tr("Invalid database pointer or upload parameters provided.");
        return result;
    }

#ifdef KPXC_FEATURE_NETWORK
    if (usesWebDav(params)) {
        return AsyncTask::runAndWaitForFuture([filePath, params, webDavPassword] {
            WebDavClient client(webDavConfigFor(params, webDavPassword));

            // Upload conditionally against the validator we can see right now, so
            // a change made by another device since our download is refused with
            // a conflict instead of being silently overwritten.
            const auto metadata = client.metadata();
            switch (metadata.status) {
            case WebDavClient::Status::Success:
                return toRemoteResult(client.upload(filePath, metadata.etag, false));
            case WebDavClient::Status::NotFound:
                // Nothing there yet: create it, but fail if somebody else wins the race.
                return toRemoteResult(client.upload(filePath, QString(), true));
            case WebDavClient::Status::Denied:
            case WebDavClient::Status::Conflict:
            case WebDavClient::Status::Error:
                return toRemoteResult(metadata);
            }
            return toRemoteResult(metadata);
        });
    }
#else
    if (usesWebDav(params)) {
        RemoteResult result;
        result.errorMessage = tr("This build of KeePassXC was compiled without networking support.");
        return result;
    }
#endif

    return AsyncTask::runAndWaitForFuture([filePath, params] {
        RemoteResult result;
        auto remoteProcess = m_createRemoteProcess(nullptr); // use nullptr parent, otherwise there is a warning
        remoteProcess->setTempFileLocation(filePath);
        remoteProcess->start(params->uploadCommand);
        if (!params->uploadInput.isEmpty()) {
            remoteProcess->write(params->uploadInput + "\n");
            remoteProcess->waitForBytesWritten();
            remoteProcess->closeWriteChannel();
        }

        bool finished = remoteProcess->waitForFinished(params->uploadTimeoutMsec);
        int statusCode = remoteProcess->exitCode();

        // TODO: For future use
        result.stdOutput = remoteProcess->readOutput();
        result.stdError = remoteProcess->readError();

        if (finished && statusCode == 0) {
            result.success = true;
        } else if (finished) {
            result.success = false;
            result.errorMessage = tr("Failed to upload merged database. Command `%1` exited with status code: %2")
                                      .arg(params->uploadCommand)
                                      .arg(statusCode);
        } else {
            remoteProcess->kill();
            result.success = false;
            result.errorMessage =
                tr("Failed to upload merged database. Command `%1` did not finish in time. Process was killed.")
                    .arg(params->uploadCommand);
        }

        return result;
    });
}

RemoteHandler::RemoteResult RemoteHandler::testWebDav(const RemoteParams* params, const QString& webDavPassword)
{
    RemoteResult result;
    if (!params) {
        result.errorMessage = tr("Invalid connection parameters provided.");
        return result;
    }

#ifdef KPXC_FEATURE_NETWORK
    return AsyncTask::runAndWaitForFuture([params, webDavPassword] {
        WebDavClient client(webDavConfigFor(params, webDavPassword));
        bool exists = false;
        auto remote = client.testConnection(&exists);
        auto out = toRemoteResult(remote);
        if (out.success && !exists) {
            // A missing file is a normal state for a first upload.
            out.errorMessage = tr("Connected. The remote file does not exist yet and will be created on upload.");
        }
        return out;
    });
#else
    result.errorMessage = tr("This build of KeePassXC was compiled without networking support.");
    return result;
#endif
}
