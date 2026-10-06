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

#include "OpenFromWebDavDialog.h"

#include "RemoteDatabaseSession.h"
#include "core/Config.h"
#include "networking/WebDavClient.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
    constexpr int kDownloadTimeoutMsec = 60000;

    /**
     * Local mirror for a WebDAV database.
     *
     * The core only understands local files, so the downloaded copy lives in the
     * application's cache directory (not the system temp directory, which may be
     * cleaned while the database is still open).
     */
    QString mirrorPathFor(const QString& url)
    {
        const auto dir = QDir(QDir::temp().absoluteFilePath(QStringLiteral("keepassxc-webdav")));
        if (!dir.exists()) {
            QDir().mkpath(dir.absolutePath());
        }
        // One mirror per remote, so reopening the same URL reuses the file
        // instead of leaving copies behind.
        const auto name = QString::fromLatin1(
            QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
        return dir.absoluteFilePath(QStringLiteral("%1.kdbx").arg(name));
    }
} // namespace

OpenFromWebDavDialog::OpenFromWebDavDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Open Database from WebDAV"));
    setModal(true);

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(tr("https://cloud.example.com/remote.php/dav/files/user/passwords.kdbx"));

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText(tr("User name (leave empty for anonymous access)"));

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("Password or app password"));

    m_rememberCheck = new QCheckBox(tr("Remember this server and user name on this computer"), this);
    m_rememberCheck->setChecked(true);

    auto* form = new QFormLayout();
    form->addRow(tr("Database URL:"), m_urlEdit);
    form->addRow(tr("User name:"), m_usernameEdit);
    form->addRow(tr("Password:"), m_passwordEdit);

    auto* hint = new QLabel(
        tr("The URL must point directly at the .kdbx file. The database is downloaded, and every save is "
           "uploaded back. If another device changed it in the meantime the upload is refused instead of "
           "overwriting those changes.\n"
           "Only the server address and user name are remembered — the password is never stored."),
        this);
    hint->setWordWrap(true);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    m_openButton = buttons->button(QDialogButtonBox::Open);
    m_openButton->setText(tr("Open"));

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_rememberCheck);
    layout->addWidget(hint);
    layout->addWidget(m_statusLabel);
    layout->addWidget(buttons);
    resize(560, sizeHint().height());

    connect(buttons, &QDialogButtonBox::accepted, this, &OpenFromWebDavDialog::startDownload);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Pre-fill from the last successful connection so the common case is two fields.
    m_urlEdit->setText(config()->get(Config::RemoteWebDavLastUrl).toString());
    m_usernameEdit->setText(config()->get(Config::RemoteWebDavLastUser).toString());
    (m_urlEdit->text().isEmpty() ? m_urlEdit : m_passwordEdit)->setFocus();
}

QString OpenFromWebDavDialog::url() const
{
    return m_urlEdit->text().trimmed();
}

OpenFromWebDavDialog::~OpenFromWebDavDialog() = default;

WebDavConfig OpenFromWebDavDialog::webDavConfig() const
{
    WebDavConfig config;
    config.url = url();
    config.username = m_usernameEdit->text();
    config.password = m_passwordEdit->text();
    config.timeoutMsec = kDownloadTimeoutMsec;
    return config;
}

QString OpenFromWebDavDialog::localFilePath() const
{
    return m_localFilePath;
}

void OpenFromWebDavDialog::setStatus(const QString& message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: palette(bright-text);")
                                         : QStringLiteral("color: palette(text);"));
}

void OpenFromWebDavDialog::startDownload()
{
    if (url().isEmpty()) {
        setStatus(tr("Enter the URL of the database file."), true);
        return;
    }
    if (!url().startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
        && !url().startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) {
        setStatus(tr("The URL must start with https:// (or http:// for a local test server)."), true);
        return;
    }

    const auto mirror = mirrorPathFor(url());
    const auto config = webDavConfig();

    QProgressDialog progress(tr("Downloading the database from the server…"), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents();

    m_localFilePath.clear();
    auto result = WebDavClient(config).download(mirror);
    progress.close();

    if (!result.isSuccess()) {
        if (result.status == WebDavClient::Status::NotFound) {
            setStatus(tr("The server has no file at that URL. Check the path."), true);
        } else if (result.status == WebDavClient::Status::Denied) {
            setStatus(tr("The server rejected the credentials for that URL."), true);
        } else {
            setStatus(tr("Download failed: %1").arg(result.errorMessage), true);
        }
        return;
    }

    // A zero-byte or unreadable mirror must never reach the unlock dialog.
    QFileInfo info(mirror);
    if (!info.isFile() || info.size() == 0) {
        setStatus(tr("The server returned an empty file."), true);
        return;
    }

    m_localFilePath = mirror;
    m_session.reset(new RemoteDatabaseSession(config, mirror, result.etag));

    if (m_rememberCheck->isChecked()) {
        config()->set(Config::RemoteWebDavLastUrl, url());
        config()->set(Config::RemoteWebDavLastUser, m_usernameEdit->text());
    }

    accept();
}

RemoteDatabaseSession* OpenFromWebDavDialog::takeSession()
{
    return m_session.take();
}
