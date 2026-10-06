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

#include "TestWebDavClient.h"

#include "config-keepassx-tests.h"
#include "networking/WebDavClient.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

namespace
{
    constexpr int kServerStartTimeoutMsec = 15000;
    const char* kUsername = "kpxc-test";
    const char* kPassword = "test-password";
} // namespace

TestWebDavClient::TestWebDavClient(QObject* parent)
    : QObject(parent)
{
}

void TestWebDavClient::initTestCase()
{
    m_tempDir.reset(new QTemporaryDir());
    QVERIFY2(m_tempDir->isValid(), "Unable to create a temporary directory for the WebDAV test root");

    QString python = QStringLiteral(KEEPASSXC_TEST_PYTHON);
    QString script = QStringLiteral(KEEPASSXC_TEST_WEBDAV_SERVER);

    if (python.isEmpty() || !QFile::exists(script)) {
        QSKIP("No Python interpreter or no webdav_test_server.py configured; skipping WebDAV tests");
    }

    // Port 0 lets the OS pick a free port; the server prints the chosen one.
    m_server.reset(new QProcess(this));
    m_server->setProcessChannelMode(QProcess::MergedChannels);
    m_server->start(python,
                    {script,
                     m_tempDir->path(),
                     QStringLiteral("0"),
                     QStringLiteral("--user"),
                     QString::fromLatin1(kUsername),
                     QStringLiteral("--password"),
                     QString::fromLatin1(kPassword)});

    if (!m_server->waitForStarted(kServerStartTimeoutMsec)) {
        QSKIP("Unable to start the WebDAV test server");
    }

    // Read stdout until the "PORT <n>" banner arrives.
    QByteArray banner;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kServerStartTimeoutMsec) {
        if (!m_server->waitForReadyRead(1000)) {
            continue;
        }
        banner.append(m_server->readAll());
        const auto match = QRegularExpression(QStringLiteral("PORT (\\d+)")).match(QString::fromLatin1(banner));
        if (match.hasMatch()) {
            m_port = match.captured(1).toInt();
            break;
        }
    }

    if (m_port == 0) {
        QSKIP("The WebDAV test server did not report a port");
    }

    m_dbPath = m_tempDir->path() + QStringLiteral("/remote.kdbx");
    m_localPath = m_tempDir->path() + QStringLiteral("/local.kdbx");

    QVERIFY2(pingServer(), "The WebDAV test server is not answering");
}

void TestWebDavClient::cleanupTestCase()
{
    if (m_server) {
        m_server->terminate();
        if (!m_server->waitForFinished(5000)) {
            m_server->kill();
            m_server->waitForFinished(2000);
        }
    }
    m_tempDir.reset();
}

WebDavClient TestWebDavClient::makeClient(const QString& path) const
{
    WebDavConfig config;
    config.url = QStringLiteral("http://127.0.0.1:%1/%2").arg(m_port).arg(path);
    config.username = QString::fromLatin1(kUsername);
    config.password = QString::fromLatin1(kPassword);
    config.timeoutMsec = 10000;
    // The test server is plain HTTP on loopback; opting in explicitly is what
    // the production code requires of the user as well.
    config.allowInsecureHttp = true;
    return WebDavClient(config);
}

bool TestWebDavClient::pingServer() const
{
    auto client = makeClient(QStringLiteral("remote.kdbx"));
    bool exists = false;
    const auto result = client.testConnection(&exists);
    return result.status != WebDavClient::Status::Error;
}

void TestWebDavClient::writeLocalFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(contents), qint64(contents.size()));
    file.close();
}

QByteArray TestWebDavClient::readLocalFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void TestWebDavClient::testRejectsPlainHttpWithoutOptIn()
{
    WebDavConfig config;
    config.url = QStringLiteral("http://127.0.0.1:%1/remote.kdbx").arg(m_port);
    config.username = QString::fromLatin1(kUsername);
    config.password = QString::fromLatin1(kPassword);

    WebDavClient client(config);
    const auto result = client.metadata();
    QCOMPARE(result.status, WebDavClient::Status::Error);
    QVERIFY(result.errorMessage.contains(QStringLiteral("http://"), Qt::CaseInsensitive));
}

void TestWebDavClient::testMetadataReportsMissingFile()
{
    auto client = makeClient(QStringLiteral("does-not-exist.kdbx"));
    const auto result = client.metadata();
    QCOMPARE(result.status, WebDavClient::Status::NotFound);
}

void TestWebDavClient::testAuthenticationIsRequired()
{
    WebDavConfig config;
    config.url = QStringLiteral("http://127.0.0.1:%1/remote.kdbx").arg(m_port);
    config.username = QStringLiteral("kpxc-test");
    config.password = QStringLiteral("wrong-password");
    config.allowInsecureHttp = true;

    WebDavClient client(config);
    writeLocalFile(m_localPath, "payload");
    const auto result = client.upload(m_localPath, QString(), true);
    QCOMPARE(result.status, WebDavClient::Status::Denied);
}

void TestWebDavClient::testCreateDownloadRoundTrip()
{
    const QByteArray original("first version of the database");
    writeLocalFile(m_localPath, original);

    auto client = makeClient(QStringLiteral("roundtrip.kdbx"));

    // Creating a file that does not exist yet must use If-None-Match: *.
    auto upload = client.upload(m_localPath, QString(), true);
    QCOMPARE(upload.status, WebDavClient::Status::Success);

    // A second unconditional-create must be refused: the file now exists.
    writeLocalFile(m_localPath, "should not overwrite");
    upload = client.upload(m_localPath, QString(), true);
    QCOMPARE(upload.status, WebDavClient::Status::Conflict);

    auto metadata = client.metadata();
    QCOMPARE(metadata.status, WebDavClient::Status::Success);
    QVERIFY2(!metadata.etag.isEmpty(), "PROPFIND must yield an ETag for conditional uploads to work");

    const QString downloadPath = m_tempDir->path() + QStringLiteral("/downloaded.kdbx");
    QFile::remove(downloadPath);
    const auto download = client.download(downloadPath);
    QCOMPARE(download.status, WebDavClient::Status::Success);
    QCOMPARE(readLocalFile(downloadPath), original);
    QVERIFY2(!download.etag.isEmpty(), "GET must return the ETag of the downloaded revision");
}

void TestWebDavClient::testConditionalUploadDetectsConflict()
{
    const QByteArray first("revision one");
    writeLocalFile(m_localPath, first);

    auto client = makeClient(QStringLiteral("conflict.kdbx"));
    auto upload = client.upload(m_localPath, QString(), true);
    QCOMPARE(upload.status, WebDavClient::Status::Success);

    // The revision we believe is current.
    auto metadata = client.metadata();
    QCOMPARE(metadata.status, WebDavClient::Status::Success);
    const QString observedEtag = metadata.etag;
    QVERIFY(!observedEtag.isEmpty());

    // Somebody else writes the remote file in the meantime.
    writeLocalFile(m_localPath, "written by the other device");
    const auto foreign = client.upload(m_localPath, observedEtag);
    QCOMPARE(foreign.status, WebDavClient::Status::Success);

    // Our upload with the now outdated ETag must fail loudly, not overwrite.
    writeLocalFile(m_localPath, "our local edit");
    const auto ours = client.upload(m_localPath, observedEtag);
    QCOMPARE(ours.status, WebDavClient::Status::Conflict);
    QCOMPARE(ours.expectedEtag, observedEtag);
    QVERIFY(!ours.errorMessage.isEmpty());

    // Nothing was written: the remote file still holds the other device's text.
    const QString verifyPath = m_tempDir->path() + QStringLiteral("/verify.kdbx");
    QFile::remove(verifyPath);
    const auto download = client.download(verifyPath);
    QCOMPARE(download.status, WebDavClient::Status::Success);
    QCOMPARE(readLocalFile(verifyPath), QByteArray("written by the other device"));

    // Re-reading the metadata yields the new ETag, so a retry can proceed.
    const auto refreshed = client.metadata();
    QCOMPARE(refreshed.status, WebDavClient::Status::Success);
    QVERIFY(refreshed.etag != observedEtag);
    QCOMPARE(client.upload(m_localPath, refreshed.etag).status, WebDavClient::Status::Success);
}

void TestWebDavClient::testConditionalDownloadSkipsUnchangedFile()
{
    writeLocalFile(m_localPath, "unchanged body");

    auto client = makeClient(QStringLiteral("conditional-get.kdbx"));
    QCOMPARE(client.upload(m_localPath, QString(), true).status, WebDavClient::Status::Success);

    auto metadata = client.metadata();
    QCOMPARE(metadata.status, WebDavClient::Status::Success);
    QVERIFY(!metadata.etag.isEmpty());

    const QString downloadPath = m_tempDir->path() + QStringLiteral("/conditional.kdbx");
    QFile::remove(downloadPath);

    // With the current ETag the server answers 304, which the client reports as
    // a conflict so callers know their copy is still the newest revision.
    const auto skipped = client.download(downloadPath, metadata.etag);
    QCOMPARE(skipped.status, WebDavClient::Status::Conflict);
    QVERIFY2(!QFile::exists(downloadPath), "A skipped download must not produce a local file");

    // With a stale ETag the body is transferred normally.
    const auto fetched = client.download(downloadPath, QStringLiteral("\"stale-etag\""));
    QCOMPARE(fetched.status, WebDavClient::Status::Success);
    QCOMPARE(readLocalFile(downloadPath), QByteArray("unchanged body"));
}

QTEST_GUILESS_MAIN(TestWebDavClient)
