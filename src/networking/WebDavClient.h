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

#ifndef KEEPASSXC_WEBDAVCLIENT_H
#define KEEPASSXC_WEBDAVCLIENT_H

#include "config-keepassx.h"

#ifdef KPXC_FEATURE_NETWORK

#include <QByteArray>
#include <QCoreApplication>
#include <QNetworkRequest>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

/**
 * Connection details for one WebDAV resource (a single remote database file).
 *
 * KeePassXC talks to the remote file directly as a byte stream; the file itself
 * is never mounted, so the local database stays authoritative while a remote
 * copy is kept in sync (see RemoteHandler).
 */
struct WebDavConfig
{
    QString url;
    QString username;
    QString password;
    int timeoutMsec = 30000;
    // Refuse to talk to plain http:// URLs unless the user explicitly opts in.
    bool allowInsecureHttp = false;
};

/**
 * Blocking WebDAV file-transfer client.
 *
 * WebDAV is plain HTTP plus a few extra verbs, so this deliberately implements
 * only what is needed to move a database file safely, using Qt's network stack
 * (and therefore the user's proxy and TLS configuration):
 *
 *  - PROPFIND with Depth: 0 for metadata, specifically the ETag of a resource
 *  - GET for the file body
 *  - conditional PUT (If-Match / If-None-Match) for the file body
 *  - OPTIONS as a cheap connectivity and capability probe
 *
 * The conditional PUT is what makes concurrent edits safe rather than silently
 * lossy: a PUT carrying the ETag observed at download time is rejected with
 * 412 Precondition Failed if anybody else wrote the resource in the meantime,
 * which the caller must resolve by merging again (WebDAV itself defines no way
 * to merge two versions of a file). Explicit LOCK/UNLOCK is deliberately not
 * implemented yet: a lock only helps while it is held, and a crashed client
 * leaves a stale lock that blocks the user out of their own database.
 */
class WebDavClient
{
    // WebDavClient is intentionally not a QObject: it runs synchronously on the
    // calling thread. This macro provides the tr() used for user-facing errors.
    Q_DECLARE_TR_FUNCTIONS(WebDavClient)

public:
    enum class Status
    {
        Success, // the operation completed
        Conflict, // the remote copy changed underneath us (HTTP 412)
        NotFound, // the remote file does not exist (HTTP 404)
        Denied, // authentication or authorization failed (HTTP 401/403)
        Error, // anything else: transport failure, TLS error, timeout, 5xx
    };

    struct Result
    {
        Status status = Status::Error;
        QString errorMessage;
        // ETag observed by the operation that just ran, when the server sent one.
        QString etag;
        // ETag the caller expected to see, echoed back on a conflict.
        QString expectedEtag;

        bool isSuccess() const
        {
            return status == Status::Success;
        }
        bool isConflict() const
        {
            return status == Status::Conflict;
        }
    };

    explicit WebDavClient(const WebDavConfig& config, QNetworkAccessManager* netMgr = nullptr);
    ~WebDavClient();

    /** Metadata for the remote resource; fills Result::etag when the server provides one. */
    Result metadata();

    /**
     * Download the remote file into a local file.
     *
     * When @p ifNoneMatchEtag is non-empty the request is conditional, so the
     * caller can skip a transfer it already has and receive Status::Conflict.
     */
    Result download(const QString& localFilePath, const QString& ifNoneMatchEtag = QString());

    /**
     * Upload a local file to the remote resource.
     *
     * @param ifMatchEtag    proceed only if the remote ETag still equals this
     *                       value; use the ETag from metadata()/download().
     * @param ifNoneMatchAny proceed only if the resource does not exist yet
     *                       ("If-None-Match: *"), for first-time uploads.
     */
    Result upload(const QString& localFilePath,
                  const QString& ifMatchEtag = QString(),
                  bool ifNoneMatchAny = false);

    /** Connectivity and auth probe. Reports whether the resource exists. */
    Result testConnection(bool* exists = nullptr);

private:
    Result execute(QNetworkReply* reply, const QString& operation, bool* resourceExists = nullptr);
    Result failure(Status status, const QString& message, const QString& expectedEtag = QString()) const;
    QNetworkRequest makeRequest() const;
    bool isInsecureConfig() const;

    WebDavConfig m_config;
    QNetworkAccessManager* m_netMgr;

    Q_DISABLE_COPY(WebDavClient)
};

#endif // KPXC_FEATURE_NETWORK

#endif // KEEPASSXC_WEBDAVCLIENT_H
