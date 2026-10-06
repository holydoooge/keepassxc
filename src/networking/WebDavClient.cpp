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

#include "WebDavClient.h"

#include "NetworkManager.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

namespace
{
    /**
     * Pull the ETag out of a WebDAV multistatus response.
     *
     * PROPFIND answers carry the validator inside the XML body (servers do not
     * set an ETag header on 207 responses), so the header path alone is not
     * enough. Namespaces are ignored: the prefix varies between servers.
     */
    QString etagFromPropfind(const QByteArray& xml)
    {
        QXmlStreamReader reader(xml);
        bool insideEtag = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) {
                if (reader.name() == QLatin1String("getetag")) {
                    insideEtag = true;
                }
            } else if (reader.isEndElement()) {
                if (reader.name() == QLatin1String("getetag")) {
                    insideEtag = false;
                }
            } else if (reader.isCharacters() && insideEtag) {
                const auto text = reader.text().toString().trimmed();
                if (!text.isEmpty()) {
                    return text;
                }
            }
        }
        return {};
    }

    /**
     * ETags are opaque quoted strings per RFC 9110. Compare them without the
     * surrounding quotes and without the weak-validator prefix, because servers
     * are inconsistent about echoing both back.
     */
    QString normalizeEtag(const QString& etag)
    {
        return etag.trimmed().remove(QRegularExpression(QStringLiteral("^(W/)?\"|\"$")));
    }

    QString describeHttpStatus(int status)
    {
        switch (status) {
        case 301:
        case 302:
        case 307:
        case 308:
            return QObject::tr("The server redirected the request. Check that the URL points directly at the "
                               "database file.");
        case 405:
            return QObject::tr("The server does not allow this operation on the resource.");
        case 409:
            return QObject::tr("The parent collection does not exist on the server.");
        case 423:
            return QObject::tr("The resource is locked on the server.");
        case 507:
            return QObject::tr("The server is out of storage space.");
        default:
            return QObject::tr("The server returned HTTP status %1.").arg(status);
        }
    }
} // namespace

WebDavClient::WebDavClient(const WebDavConfig& config, QNetworkAccessManager* netMgr)
    : m_config(config)
    , m_netMgr(netMgr ? netMgr : getNetMgr())
{
    // Note: this configures the shared application-wide network manager, so the
    // timeout applies to other network users (icon downloads, breach checks)
    // too. That is intentional for now: those requests all want a finite
    // timeout, and a per-request timeout would need QNetworkRequest plumbing
    // that does not exist on all supported Qt versions.
    if (m_config.timeoutMsec > 0) {
        m_netMgr->setTransferTimeout(m_config.timeoutMsec);
    }
}

WebDavClient::~WebDavClient() = default;

bool WebDavClient::isInsecureConfig() const
{
    return m_config.url.trimmed().startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
           && !m_config.allowInsecureHttp;
}

QNetworkRequest WebDavClient::makeRequest() const
{
    QNetworkRequest request{QUrl(m_config.url.trimmed())};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (!m_config.username.isEmpty()) {
        // Pre-emptive Basic auth: without it the first request always costs an
        // extra round trip and some servers answer PROPFIND with 401 only.
        const auto credentials = m_config.username + QLatin1Char(':') + m_config.password;
        request.setRawHeader("Authorization", "Basic " + credentials.toUtf8().toBase64());
    }
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KeePassXC"));
    return request;
}

WebDavClient::Result WebDavClient::failure(Status status, const QString& message, const QString& expectedEtag) const
{
    Result result;
    result.status = status;
    result.errorMessage = message;
    result.expectedEtag = expectedEtag;
    return result;
}

WebDavClient::Result WebDavClient::execute(QNetworkReply* reply, const QString& operation, bool* resourceExists)
{
    if (!reply) {
        return failure(Status::Error, tr("%1 failed: the request could not be created.").arg(operation));
    }

    // Wait for the reply without blocking the calling thread's event loop.
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray etagHeader = reply->rawHeader("ETag");
    const auto networkError = reply->error();
    const QString networkErrorText = reply->errorString();

    if (resourceExists) {
        *resourceExists = (httpStatus != 404);
    }

    Result result;
    result.etag = normalizeEtag(QString::fromLatin1(etagHeader));

    // A redirect must not be followed silently: Qt turns the PUT into a GET for
    // 301/302 and the upload would appear to succeed without writing anything.
    if (httpStatus == 301 || httpStatus == 302 || httpStatus == 303 || httpStatus == 307 || httpStatus == 308) {
        return failure(Status::Error, tr("%1 failed: %2").arg(operation, describeHttpStatus(httpStatus)));
    }

    // Prefer the HTTP status over the Qt error code: Qt reports 412 and the other
    // 4xx codes as generic ProtocolInvalidOperation/Content errors, and the status
    // is what actually carries the meaning here.
    if (httpStatus == 412) {
        return failure(Status::Conflict,
                       tr("%1 failed: the database changed on the server since it was downloaded.").arg(operation));
    }
    if (httpStatus == 404) {
        return failure(Status::NotFound, tr("%1 failed: the remote file does not exist.").arg(operation));
    }
    if (httpStatus == 401 || httpStatus == 403) {
        return failure(Status::Denied, tr("%1 failed: the server rejected the credentials.").arg(operation));
    }

    if (networkError != QNetworkReply::NoError) {
        if (httpStatus >= 400) {
            return failure(Status::Error, tr("%1 failed: %2").arg(operation, describeHttpStatus(httpStatus)));
        }
        return failure(Status::Error, tr("%1 failed: %2").arg(operation, networkErrorText));
    }

    if (httpStatus >= 400) {
        return failure(Status::Error, tr("%1 failed: %2").arg(operation, describeHttpStatus(httpStatus)));
    }

    result.status = Status::Success;
    return result;
}

WebDavClient::Result WebDavClient::metadata()
{
    if (isInsecureConfig()) {
        return failure(Status::Error, tr("Refusing to send credentials over an unencrypted http:// connection."));
    }

    // Depth: 0 keeps the answer to the resource itself instead of the whole tree.
    QNetworkRequest request = makeRequest();
    request.setRawHeader("Depth", "0");
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/xml; charset=utf-8"));
    static const QByteArray propfindBody =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\"><D:prop>"
        "<D:getetag/><D:getcontentlength/><D:resourcetype/>"
        "</D:prop></D:propfind>";

    auto* reply = m_netMgr->sendCustomRequest(request, "PROPFIND");
    bool exists = true;
    auto result = execute(reply, tr("Reading remote file metadata"), &exists);
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (result.isSuccess() && !exists) {
        return failure(Status::NotFound, tr("The remote file does not exist."));
    }
    // PROPFIND answers put the validator in the XML body, not in a header.
    if (result.isSuccess() && result.etag.isEmpty()) {
        result.etag = normalizeEtag(etagFromPropfind(body));
    }
    // Some servers omit the ETag on PROPFIND; fall back to a HEAD probe so
    // callers can still get a validator for their conditional upload.
    if (result.isSuccess() && result.etag.isEmpty()) {
        auto* headReply = m_netMgr->head(makeRequest());
        auto headResult = execute(headReply, tr("Reading remote file metadata"));
        headReply->deleteLater();
        if (headResult.isSuccess()) {
            result.etag = headResult.etag;
        }
    }
    return result;
}

WebDavClient::Result WebDavClient::download(const QString& localFilePath, const QString& ifNoneMatchEtag)
{
    if (isInsecureConfig()) {
        return failure(Status::Error, tr("Refusing to send credentials over an unencrypted http:// connection."));
    }

    QNetworkRequest request = makeRequest();
    if (!ifNoneMatchEtag.isEmpty()) {
        request.setRawHeader("If-None-Match", '"' + normalizeEtag(ifNoneMatchEtag).toUtf8() + '"');
    }

    auto* reply = m_netMgr->get(request);
    // connect() below is satisfied by the QNetworkReply::finished signal, so the
    // body is fully buffered by the time execute() returns.
    auto result = execute(reply, tr("Downloading the remote database"));
    if (!result.isSuccess()) {
        reply->deleteLater();
        return result;
    }

    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (payload.isEmpty()) {
        return failure(Status::Error, tr("The server returned an empty response for the remote database."));
    }

    // Write through a temporary file so a failed transfer cannot leave a
    // truncated database behind for the caller to open.
    const auto tempPath = localFilePath + QStringLiteral(".part");
    QFile out(tempPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return failure(Status::Error, tr("Unable to write the downloaded database to %1.").arg(localFilePath));
    }
    if (out.write(payload) != payload.size()) {
        out.close();
        QFile::remove(tempPath);
        return failure(Status::Error, tr("Writing the downloaded database to %1 failed.").arg(localFilePath));
    }
    out.close();

    QFile::remove(localFilePath);
    if (!QFile::rename(tempPath, localFilePath)) {
        QFile::remove(tempPath);
        return failure(Status::Error, tr("Unable to replace %1 with the downloaded database.").arg(localFilePath));
    }

    return result;
}

WebDavClient::Result WebDavClient::upload(const QString& localFilePath, const QString& ifMatchEtag, bool ifNoneMatchAny)
{
    if (isInsecureConfig()) {
        return failure(Status::Error,
                       tr("Refusing to send credentials over an unencrypted http:// connection."));
    }

    QFile file(localFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(Status::Error, tr("Unable to read %1 for upload.").arg(localFilePath));
    }

    QNetworkRequest request = makeRequest();
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    if (ifNoneMatchAny) {
        request.setRawHeader("If-None-Match", "*");
    } else if (!ifMatchEtag.isEmpty()) {
        request.setRawHeader("If-Match", '"' + normalizeEtag(ifMatchEtag).toUtf8() + '"');
    }

    auto* reply = m_netMgr->put(request, &file);
    auto result = execute(reply, tr("Uploading the database"), nullptr);
    reply->deleteLater();

    if (result.isConflict()) {
        result.expectedEtag = ifMatchEtag;
    }
    return result;
}

WebDavClient::Result WebDavClient::testConnection(bool* exists)
{
    if (isInsecureConfig()) {
        return failure(Status::Error,
                       tr("Refusing to send credentials over an unencrypted http:// connection."));
    }

    bool resourceExists = true;
    auto result = execute(m_netMgr->sendCustomRequest(makeRequest(), "OPTIONS"), tr("Connecting to the server"));
    if (result.isSuccess()) {
        // OPTIONS does not report existence, so ask for the metadata as well.
        result = metadata();
        resourceExists = !(result.status == Status::NotFound);
        if (result.status == Status::NotFound) {
            // A missing file is not a connection failure; report it as usable.
            result.status = Status::Success;
            result.errorMessage.clear();
        }
    }

    if (exists) {
        *exists = resourceExists;
    }
    return result;
}
