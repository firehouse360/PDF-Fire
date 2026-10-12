// MIT License
//
// Copyright (c) 2026 PDF Fire contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "firehouse360dialog.h"

#include "pdfcertificatemanager.h"
#include "pdfcertificatestore.h"
#include "pdffirecertificateauthority.h"
#include "pdffirefirehouse360.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

namespace pdfplugin
{

namespace
{

/// Wait cursor while the network is used
class WaitCursor
{
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
};

}   // namespace

Firehouse360Dialog::Firehouse360Dialog(QWidget* parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Firehouse 360"));
    setMinimumWidth(560);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* intro = new QLabel(tr("<p>Connect PDF Fire to your department's <b>Firehouse 360</b> (Secure Share). Then every signature you make gets a "
                                  "<b>verify code and QR code</b>, so anyone can check the signed PDF at firehouse360.com/verify - and your "
                                  "department's signing authority is trusted on this computer automatically.</p>"
                                  "<p>Only the verification details are sent (the signature itself: who signed, when, and a fingerprint of the signed "
                                  "file) - <b>never the document</b>. While this is off, PDF Fire does not go online for it.</p>"), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_enabledCheckBox = new QCheckBox(tr("Connect to my department"), this);
    m_enabledCheckBox->setObjectName("firehouse360EnabledCheckBox");
    layout->addWidget(m_enabledCheckBox);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("firehouse360StatusLabel");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_statusLabel->setOpenExternalLinks(true);
    layout->addWidget(m_statusLabel);

    QHBoxLayout* connectionButtons = new QHBoxLayout();
    m_connectButton = new QPushButton(tr("Connect..."), this);
    m_connectButton->setObjectName("firehouse360ConnectButton");
    m_disconnectButton = new QPushButton(tr("Disconnect"), this);
    m_sendButton = new QPushButton(tr("Send Waiting Signatures"), this);
    connectionButtons->addWidget(m_connectButton);
    connectionButtons->addWidget(m_disconnectButton);
    connectionButtons->addWidget(m_sendButton);
    connectionButtons->addStretch(1);
    layout->addLayout(connectionButtons);

    QGroupBox* trustGroup = new QGroupBox(tr("Department authority"), this);
    QVBoxLayout* trustLayout = new QVBoxLayout(trustGroup);
    QLabel* trustText = new QLabel(tr("Downloads your department's authority certificate and revocation list from Firehouse 360 and trusts them on this "
                                      "computer, so the signatures of your members show as trusted (and revoked certificates as revoked)."), trustGroup);
    trustText->setWordWrap(true);
    trustLayout->addWidget(trustText);
    m_trustButton = new QPushButton(tr("Update Department Trust"), trustGroup);
    trustLayout->addWidget(m_trustButton, 0, Qt::AlignLeft);
    layout->addWidget(trustGroup);

    QGroupBox* keyGroup = new QGroupBox(tr("My certificate online"), this);
    QVBoxLayout* keyLayout = new QVBoxLayout(keyGroup);
    QLabel* keyText = new QLabel(tr("Keep an encrypted copy of your signing certificate (.pfx) in Firehouse 360, so you can get it on another computer. It is "
                                    "locked here, with a passphrase only you know - Firehouse 360 can't open it or sign as you. If you forget the passphrase, "
                                    "the online copy can't be recovered."), keyGroup);
    keyText->setWordWrap(true);
    keyLayout->addWidget(keyText);
    QHBoxLayout* keyButtons = new QHBoxLayout();
    m_saveKeyButton = new QPushButton(tr("Save My Certificate Online..."), keyGroup);
    m_getKeyButton = new QPushButton(tr("Get My Certificate..."), keyGroup);
    m_deleteKeyButton = new QPushButton(tr("Delete Online Copy"), keyGroup);
    keyButtons->addWidget(m_saveKeyButton);
    keyButtons->addWidget(m_getKeyButton);
    keyButtons->addWidget(m_deleteKeyButton);
    keyButtons->addStretch(1);
    keyLayout->addLayout(keyButtons);
    layout->addWidget(keyGroup);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_enabledCheckBox, &QCheckBox::toggled, this, &Firehouse360Dialog::onEnabledToggled);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() { connectWithCode(); });
    connect(m_disconnectButton, &QPushButton::clicked, this, &Firehouse360Dialog::onDisconnect);
    connect(m_trustButton, &QPushButton::clicked, this, [this]() { syncTrust(true); });
    connect(m_sendButton, &QPushButton::clicked, this, &Firehouse360Dialog::onSendPending);
    connect(m_saveKeyButton, &QPushButton::clicked, this, &Firehouse360Dialog::onSaveCertificateOnline);
    connect(m_getKeyButton, &QPushButton::clicked, this, &Firehouse360Dialog::onGetCertificateOnline);
    connect(m_deleteKeyButton, &QPushButton::clicked, this, &Firehouse360Dialog::onDeleteCertificateOnline);

    updateStatus(pdf::PDFFireFirehouse360::isConnected());
}

void Firehouse360Dialog::updateStatus(bool fetchFromServer)
{
    m_updating = true;
    const bool isEnabled = pdf::PDFFireFirehouse360::isEnabled();
    m_enabledCheckBox->setChecked(isEnabled);
    m_updating = false;

    QString text;
    QString authorityText;
    if (fetchFromServer && pdf::PDFFireFirehouse360::isConnected())
    {
        WaitCursor waitCursor;
        const pdf::PDFFireFirehouse360::Response response = pdf::PDFFireFirehouse360::fetchStatus();
        if (response.isOk())
        {
            const QJsonObject authority = response.json.value(QStringLiteral("authority")).toObject();
            authorityText = authority.isEmpty() ? tr("Your department has not uploaded its signing authority to Firehouse 360 yet (Secure Share ▸ Settings ▸ PDF Fire signing).")
                                                : tr("Department authority: %1.").arg(authority.value(QStringLiteral("name")).toString().toHtmlEscaped());
            m_hasOnlineCertificate = !response.json.value(QStringLiteral("online_certificate")).toObject().isEmpty();
        }
        else
        {
            authorityText = response.error.toHtmlEscaped();
        }
    }

    const bool isConnected = pdf::PDFFireFirehouse360::isConnected();
    if (isConnected)
    {
        text = tr("<b>Connected</b> to %1 as %2.").arg(pdf::PDFFireFirehouse360::getDepartmentName().toHtmlEscaped(),
                                                        pdf::PDFFireFirehouse360::getMemberName().toHtmlEscaped());
        if (!authorityText.isEmpty())
        {
            text += QStringLiteral("<br>") + authorityText;
        }
        if (m_hasOnlineCertificate)
        {
            text += QStringLiteral("<br>") + tr("A copy of your certificate is saved online.");
        }
    }
    else if (isEnabled)
    {
        text = tr("<b>Not connected yet.</b> Click <b>Connect...</b> and type the code from Firehouse 360 ▸ Secure Share ▸ Connect PDF Fire.");
    }
    else
    {
        text = tr("Off - PDF Fire does not contact Firehouse 360.");
    }

    const int pending = pdf::PDFFireFirehouse360::getPendingCount();
    if (pending > 0)
    {
        text += QStringLiteral("<br>") + tr("%n signature(s) wait to be registered.", nullptr, pending);
    }

    const QString server = pdf::PDFFireFirehouse360::getServerUrl();
    text += QStringLiteral("<br><a href=\"%1/secure-share/pdffire\">%2</a> &nbsp; <a href=\"%1/verify/pdf\">%3</a> &nbsp; <a href=\"%1/help/pdf-fire-department-signing-setup\">%4</a>")
                .arg(server.toHtmlEscaped(), tr("Open Connect PDF Fire on Firehouse 360"), tr("Verify a signed PDF"), tr("Help"));
    m_statusLabel->setText(text);

    m_connectButton->setEnabled(isEnabled && !isConnected);
    m_disconnectButton->setEnabled(isConnected);
    m_sendButton->setEnabled(isConnected && pending > 0);
    m_trustButton->setEnabled(isConnected);
    m_saveKeyButton->setEnabled(isConnected);
    m_getKeyButton->setEnabled(isConnected && m_hasOnlineCertificate);
    m_deleteKeyButton->setEnabled(isConnected && m_hasOnlineCertificate);
}

void Firehouse360Dialog::onEnabledToggled(bool checked)
{
    if (m_updating)
    {
        return;
    }

    if (checked)
    {
        pdf::PDFFireFirehouse360::setEnabled(true);
        if (!pdf::PDFFireFirehouse360::isConnected() && !connectWithCode())
        {
            pdf::PDFFireFirehouse360::setEnabled(false);
        }
    }
    else if (pdf::PDFFireFirehouse360::isConnected())
    {
        if (QMessageBox::question(this, tr("Firehouse 360"), tr("Disconnect this computer from Firehouse 360? New signatures then get no verify code."),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        {
            WaitCursor waitCursor;
            pdf::PDFFireFirehouse360::disconnect();
        }
    }
    else
    {
        pdf::PDFFireFirehouse360::setEnabled(false);
    }
    updateStatus(false);
}

bool Firehouse360Dialog::connectWithCode()
{
    bool isOk = false;
    const QString code = QInputDialog::getText(this, tr("Connect to Firehouse 360"),
                                               tr("Sign in to Firehouse 360, open <b>Secure Share ▸ Connect PDF Fire</b> and click <b>Make a connection code</b>.<br>"
                                                  "Type the 8-character code here (it works once, for 10 minutes):"),
                                               QLineEdit::Normal, QString(), &isOk).trimmed();
    if (!isOk || code.isEmpty())
    {
        updateStatus(false);
        return false;
    }

    pdf::PDFFireFirehouse360::Response response;
    {
        WaitCursor waitCursor;
        response = pdf::PDFFireFirehouse360::pair(code, QSysInfo::machineHostName());
    }
    if (!response.isOk())
    {
        showError(response.error);
        updateStatus(false);
        return false;
    }

    syncTrust(true);
    {
        WaitCursor waitCursor;
        pdf::PDFFireFirehouse360::flushQueue();
    }
    updateStatus(true);
    QMessageBox::information(this, tr("Firehouse 360"), tr("Connected to %1. Your signatures now get a verify code and QR code.")
                                                            .arg(pdf::PDFFireFirehouse360::getDepartmentName()));
    return true;
}

void Firehouse360Dialog::onDisconnect()
{
    if (QMessageBox::question(this, tr("Firehouse 360"), tr("Disconnect this computer from Firehouse 360? New signatures then get no verify code."),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    {
        WaitCursor waitCursor;
        pdf::PDFFireFirehouse360::disconnect();
    }
    m_hasOnlineCertificate = false;
    updateStatus(false);
}

void Firehouse360Dialog::syncTrust(bool interactive)
{
    // Downloaded and checked (SHA-256 against the status); the certificate is kept, so
    // only signatures with the department's certificates get a verify code
    QByteArray certificate;
    QByteArray revocationList;
    pdf::PDFFireFirehouse360::Response response;
    {
        WaitCursor waitCursor;
        response = pdf::PDFFireFirehouse360::updateDepartmentAuthority(&certificate, &revocationList);
    }
    if (!response.isOk() || certificate.isEmpty())
    {
        if (interactive)
        {
            QMessageBox::information(this, tr("Department Authority"), response.error);
        }
        return;
    }

    if (!pdf::PDFFireCertificateAuthority::isAuthorityTrusted(certificate))
    {
        if (!interactive)
        {
            return;
        }
        const QString question = tr("Trust \"%1\" on this computer?\n\nFingerprint:\n%2\n\nIt is the authority your department uploaded to Firehouse 360. "
                                    "Members' signatures then show as trusted here.")
                                     .arg(pdf::PDFFireCertificateAuthority::getCertificateName(certificate), pdf::PDFFireCertificateAuthority::getFingerprint(certificate));
        if (QMessageBox::question(this, tr("Department Authority"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        {
            return;
        }
    }

    QString errorMessage;
    if (!pdf::PDFFireCertificateAuthority::trustAuthority(certificate, revocationList, &errorMessage))
    {
        if (interactive)
        {
            showError(errorMessage);
        }
        return;
    }

    if (interactive)
    {
        QMessageBox::information(this, tr("Department Authority"),
                                 revocationList.isEmpty() ? tr("The department's authority is trusted on this computer. (Its revocation list is not on Firehouse 360 yet.)")
                                                          : tr("The department's authority and its current revocation list are trusted on this computer."));
    }
}

void Firehouse360Dialog::onSendPending()
{
    pdf::PDFFireFirehouse360::FlushResult result;
    {
        WaitCursor waitCursor;
        result = pdf::PDFFireFirehouse360::flushQueue();
    }

    if (!result.reachedServer)
    {
        showError(tr("Firehouse 360 could not be reached. The signatures stay on this computer and are sent later."));
    }
    else if (result.rejected > 0 || !result.messages.isEmpty())
    {
        showError(tr("Registered: %1. Not accepted:\n%2").arg(result.registered).arg(result.messages.join(QLatin1Char('\n'))));
    }
    else
    {
        QMessageBox::information(this, tr("Firehouse 360"), tr("%n signature(s) registered.", nullptr, result.registered));
    }
    updateStatus(false);
}

void Firehouse360Dialog::onSaveCertificateOnline()
{
    // Certificates in PDF Fire's own folder (files), not the system store
    pdf::PDFCertificateEntries entries;
    for (const pdf::PDFCertificateEntry& entry : pdf::PDFCertificateManager::getCertificates())
    {
        if (!entry.pkcs12.isEmpty() && !entry.pkcs12fileName.isEmpty())
        {
            entries.push_back(entry);
        }
    }
    if (entries.empty())
    {
        showError(tr("There is no certificate to save. Import yours first: Sign ▸ My Certificates ▸ Import."));
        return;
    }

    QStringList names;
    for (const pdf::PDFCertificateEntry& entry : entries)
    {
        const QString owner = entry.info.getName(pdf::PDFCertificateInfo::CommonName);
        names << QStringLiteral("%1 (%2)").arg(owner.isEmpty() ? tr("Certificate") : owner, QFileInfo(entry.pkcs12fileName).fileName());
    }

    bool isOk = false;
    const QString chosen = QInputDialog::getItem(this, tr("Save My Certificate Online"), tr("Certificate:"), names, 0, false, &isOk);
    if (!isOk)
    {
        return;
    }
    const pdf::PDFCertificateEntry& entry = entries[size_t(names.indexOf(chosen))];

    const QString passphrase = QInputDialog::getText(this, tr("Save My Certificate Online"),
                                                     tr("Choose a passphrase for the online copy (at least %1 characters - a few unrelated words). "
                                                        "Only you know it; write it down and keep it safe.").arg(pdf::PDFFireFirehouse360::MINIMAL_PASSPHRASE_LENGTH),
                                                     QLineEdit::Password, QString(), &isOk);
    if (!isOk)
    {
        return;
    }
    if (passphrase.size() < pdf::PDFFireFirehouse360::MINIMAL_PASSPHRASE_LENGTH)
    {
        showError(tr("The passphrase must have at least %1 characters.").arg(pdf::PDFFireFirehouse360::MINIMAL_PASSPHRASE_LENGTH));
        return;
    }
    const QString repeated = QInputDialog::getText(this, tr("Save My Certificate Online"), tr("Type the passphrase again:"), QLineEdit::Password, QString(), &isOk);
    if (!isOk)
    {
        return;
    }
    if (repeated != passphrase)
    {
        showError(tr("The passphrases are not the same."));
        return;
    }

    pdf::PDFFireFirehouse360::Response response;
    {
        WaitCursor waitCursor;
        response = pdf::PDFFireFirehouse360::saveCertificateOnline(entry.pkcs12, chosen, entry.pkcs12fileName, passphrase);
    }
    if (!response.isOk())
    {
        showError(response.error);
        return;
    }
    QMessageBox::information(this, tr("Save My Certificate Online"), tr("Saved. On another computer: connect PDF Fire, then Sign ▸ Firehouse 360 ▸ Get My Certificate."));
    updateStatus(true);
}

void Firehouse360Dialog::onGetCertificateOnline()
{
    bool isOk = false;
    const QString passphrase = QInputDialog::getText(this, tr("Get My Certificate"), tr("Passphrase of the online copy:"), QLineEdit::Password, QString(), &isOk);
    if (!isOk || passphrase.isEmpty())
    {
        return;
    }

    QByteArray pkcs12;
    QString fileName;
    pdf::PDFFireFirehouse360::Response response;
    {
        WaitCursor waitCursor;
        response = pdf::PDFFireFirehouse360::fetchCertificateOnline(passphrase, &pkcs12, &fileName);
    }
    if (!response.isOk())
    {
        showError(response.error);
        return;
    }

    // Already here? Then nothing is written twice
    for (const pdf::PDFCertificateEntry& entry : pdf::PDFCertificateManager::getCertificates())
    {
        if (entry.pkcs12 == pkcs12)
        {
            QMessageBox::information(this, tr("Get My Certificate"), tr("This certificate is already on this computer."));
            return;
        }
    }

    QDir().mkpath(pdf::PDFCertificateManager::getCertificateDirectory());
    QString target = fileName.endsWith(QLatin1String(".pfx"), Qt::CaseInsensitive) ? QDir(pdf::PDFCertificateManager::getCertificateDirectory()).filePath(fileName) : QString();
    if (target.isEmpty() || QFile::exists(target))
    {
        target = pdf::PDFCertificateManager::generateCertificateFileName();
    }

    QFile file(target);
    if (!file.open(QFile::WriteOnly) || file.write(pkcs12) != pkcs12.size())
    {
        showError(tr("The certificate could not be saved: %1").arg(file.errorString()));
        return;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.close();
    QMessageBox::information(this, tr("Get My Certificate"),
                             tr("Your certificate is on this computer now. Sign with it as usual (Sign with Certificate) - it still asks for its own password."));
}

void Firehouse360Dialog::onDeleteCertificateOnline()
{
    if (QMessageBox::question(this, tr("Delete Online Copy"), tr("Delete the online copy of your certificate? The copies on your computers are not affected."),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    pdf::PDFFireFirehouse360::Response response;
    {
        WaitCursor waitCursor;
        response = pdf::PDFFireFirehouse360::deleteCertificateOnline();
    }
    if (!response.isOk())
    {
        showError(response.error);
        return;
    }
    m_hasOnlineCertificate = false;
    updateStatus(true);
}

void Firehouse360Dialog::showError(const QString& message)
{
    QMessageBox::critical(this, tr("Firehouse 360"), message);
}

}   // namespace pdfplugin
