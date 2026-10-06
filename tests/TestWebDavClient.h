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

#ifndef KEEPASSXC_TESTWEBDAVCLIENT_H
#define KEEPASSXC_TESTWEBDAVCLIENT_H

#include <QObject>
#include <QScopedPointer>

class QProcess;
class QTemporaryDir;
class WebDavClient;

/**
 * Exercises WebDavClient against the stdlib-only WebDAV server in
 * tests/webdav_test_server.py, so no external service is required.
 */
class TestWebDavClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testRejectsPlainHttpWithoutOptIn();
    void testMetadataReportsMissingFile();
    void testAuthenticationIsRequired();
    void testCreateDownloadRoundTrip();
    void testConditionalUploadDetectsConflict();
    void testConditionalDownloadSkipsUnchangedFile();

private:
    WebDavClient makeClient(const QString& path) const;
    bool pingServer() const;
    static void writeLocalFile(const QString& path, const QByteArray& contents);
    static QByteArray readLocalFile(const QString& path);

    QScopedPointer<QTemporaryDir> m_tempDir;
    QScopedPointer<QProcess> m_server;
    int m_port = 0;
    QString m_dbPath;
    QString m_localPath;
};

#endif // KEEPASSXC_TESTWEBDAVCLIENT_H
