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

#include "departmentauthoritydialog.h"

#include "pdfcertificatemanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace pdfplugin
{

namespace
{

constexpr const char* SETTINGS_AUTHORITY_DIRECTORY = "SignaturePlugin/AuthorityDirectory";

QString formatDate(const QDateTime& dateTime)
{
    return dateTime.isValid() ? QLocale::system().toString(dateTime.toLocalTime().date(), QLocale::ShortFormat) : QString();
}

/// A line edit with a Browse button, which chooses a folder or a file to be saved
QWidget* createPathEdit(QWidget* parent, QLineEdit** edit, std::function<QString(const QString&)> browse)
{
    QWidget* widget = new QWidget(parent);
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    *edit = new QLineEdit(widget);
    QPushButton* button = new QPushButton(QObject::tr("Browse..."), widget);
    layout->addWidget(*edit, 1);
    layout->addWidget(button);
    QLineEdit* lineEdit = *edit;
    QObject::connect(button, &QPushButton::clicked, widget, [lineEdit, browse]()
    {
        const QString path = browse(lineEdit->text());
        if (!path.isEmpty())
        {
            lineEdit->setText(path);
        }
    });
    return widget;
}

}   // namespace

DepartmentAuthorityDialog::DepartmentAuthorityDialog(QWidget* parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Department Certificates"));
    setMinimumSize(760, 520);

    QVBoxLayout* layout = new QVBoxLayout(this);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::RichText);
    layout->addWidget(m_statusLabel);

    QHBoxLayout* authorityButtons = new QHBoxLayout();
    QPushButton* createButton = new QPushButton(tr("Create Authority..."), this);
    QPushButton* openButton = new QPushButton(tr("Open Authority..."), this);
    createButton->setToolTip(tr("Create the certificate authority of the department - done once"));
    openButton->setToolTip(tr("Open the authority folder to issue or revoke certificates (needs the passphrase)"));
    authorityButtons->addWidget(createButton);
    authorityButtons->addWidget(openButton);
    authorityButtons->addStretch(1);
    layout->addLayout(authorityButtons);

    m_certificatesWidget = new QTreeWidget(this);
    m_certificatesWidget->setObjectName("certificatesWidget");
    m_certificatesWidget->setRootIsDecorated(false);
    m_certificatesWidget->setHeaderLabels({ tr("Member"), tr("Rank / Title"), tr("E-mail"), tr("Issued"), tr("Expires"), tr("Status") });
    m_certificatesWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_certificatesWidget, 1);

    QHBoxLayout* certificateButtons = new QHBoxLayout();
    m_issueButton = new QPushButton(tr("Issue Certificate..."), this);
    m_revokeButton = new QPushButton(tr("Revoke..."), this);
    m_trustButton = new QPushButton(tr("Trust on This Computer"), this);
    m_shareButton = new QPushButton(tr("Share Authority Certificate..."), this);
    m_publishButton = new QPushButton(tr("Renew Revocation List"), this);
    m_shareButton->setToolTip(tr("Save a copy of the authority certificate (authority.cer) - give it to anybody, who should trust the signatures of the members"));
    m_publishButton->setToolTip(tr("Write the revocation list again (it is valid for a year), then upload it to its web address"));
    certificateButtons->addWidget(m_issueButton);
    certificateButtons->addWidget(m_revokeButton);
    certificateButtons->addStretch(1);
    certificateButtons->addWidget(m_trustButton);
    certificateButtons->addWidget(m_shareButton);
    certificateButtons->addWidget(m_publishButton);
    layout->addLayout(certificateButtons);

    // The departments, whose members' signatures are trusted here - the part every
    // member and every reader of the documents uses
    QGroupBox* trustedGroup = new QGroupBox(tr("Departments trusted on this computer"), this);
    QVBoxLayout* trustedLayout = new QVBoxLayout(trustedGroup);
    QLabel* trustedLabel = new QLabel(tr("Signatures of members of these departments show as trusted. Signatures of other "
                                         "certificates are still checked for changes, but their signer is not verified."), trustedGroup);
    trustedLabel->setWordWrap(true);
    trustedLayout->addWidget(trustedLabel);
    m_trustedWidget = new QTreeWidget(trustedGroup);
    m_trustedWidget->setObjectName("trustedWidget");
    m_trustedWidget->setRootIsDecorated(false);
    m_trustedWidget->setHeaderLabels({ tr("Department authority"), tr("Fingerprint"), tr("Revocation list valid until"), tr("Trusted for") });
    m_trustedWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_trustedWidget->setMaximumHeight(140);
    trustedLayout->addWidget(m_trustedWidget);
    QHBoxLayout* trustedButtons = new QHBoxLayout();
    QPushButton* trustFileButton = new QPushButton(tr("Trust an Authority..."), trustedGroup);
    trustFileButton->setToolTip(tr("Trust the signatures of a department on this computer - choose its authority.cer file"));
    m_removeTrustedButton = new QPushButton(tr("Stop Trusting"), trustedGroup);
    trustedButtons->addWidget(trustFileButton);
    trustedButtons->addWidget(m_removeTrustedButton);
    trustedButtons->addStretch(1);
    trustedLayout->addLayout(trustedButtons);
    layout->addWidget(trustedGroup);
    connect(m_removeTrustedButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onRemoveTrustedAuthority);
    connect(m_trustedWidget, &QTreeWidget::currentItemChanged, this, [this]()
    {
        const QTreeWidgetItem* item = m_trustedWidget->currentItem();
        m_removeTrustedButton->setEnabled(item && !item->data(0, Qt::UserRole + 1).toBool());
    });

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(createButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onCreateAuthority);
    connect(openButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onOpenAuthority);
    connect(trustFileButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onTrustAuthorityFile);
    connect(m_issueButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onIssueCertificate);
    connect(m_revokeButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onRevokeCertificate);
    connect(m_trustButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onTrustOpenAuthority);
    connect(m_shareButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onShareCertificate);
    connect(m_publishButton, &QPushButton::clicked, this, &DepartmentAuthorityDialog::onPublishRevocationList);
    connect(m_certificatesWidget, &QTreeWidget::currentItemChanged, this, &DepartmentAuthorityDialog::updateButtons);

    updateUi();
}

void DepartmentAuthorityDialog::showError(const QString& message)
{
    QMessageBox::critical(this, tr("Department Certificates"), message);
}

void DepartmentAuthorityDialog::updateUi()
{
    const bool isOpen = m_authority.isOpen();
    if (!isOpen)
    {
        m_statusLabel->setText(tr("<p><b>No authority is open.</b></p>"
                                  "<p>The department authority issues the signing certificates of the members. Whoever trusts the "
                                  "authority (once) sees the signatures of all members as trusted. <b>Create Authority</b> is done "
                                  "once; then <b>Open Authority</b> (with its passphrase) issues and revokes certificates.</p>"
                                  "<p>Members and others only need the list at the bottom: <b>Trust an Authority</b> with the department's <i>authority.cer</i> file.</p>"));
    }
    else
    {
        const bool isTrusted = pdf::PDFFireCertificateAuthority::isAuthorityTrusted(m_authority.getCertificate());
        const QDateTime nextUpdate = m_authority.getCrlNextUpdate();
        QString revocationText = m_authority.getCrlUrl().isEmpty()
                                     ? tr("No web address for the revocation list.")
                                     : tr("Revocation list: <i>%1</i>, valid until %2.").arg(m_authority.getCrlUrl().toHtmlEscaped(), formatDate(nextUpdate));
        if (nextUpdate.isValid() && nextUpdate < QDateTime::currentDateTimeUtc().addDays(30))
        {
            revocationText += tr(" <b>Renew it soon.</b>");
        }
        m_statusLabel->setText(tr("<p><b>%1</b> &nbsp; (%2)</p><p>Fingerprint: <tt>%5</tt><br>%3<br>%4</p>")
                                   .arg(m_authority.getName().toHtmlEscaped(), QDir::toNativeSeparators(m_authority.getDirectory()).toHtmlEscaped(), revocationText,
                                        isTrusted ? tr("Trusted on this computer.") : tr("<b>Not trusted on this computer yet</b> - the signatures of the members show as not trusted here."),
                                        pdf::PDFFireCertificateAuthority::getFingerprint(m_authority.getCertificate())));
        m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    // The list is filled again - the selected certificate stays selected
    const QString selectedSerialNumber = m_certificatesWidget->currentItem() ? m_certificatesWidget->currentItem()->data(0, Qt::UserRole).toString() : QString();
    const QSignalBlocker blocker(m_certificatesWidget);
    m_certificatesWidget->clear();
    for (const pdf::PDFFireCertificateAuthority::IssuedCertificate& issued : m_authority.getIssuedCertificates())
    {
        QString status = tr("Valid");
        if (issued.isRevoked())
        {
            status = tr("Revoked %1").arg(formatDate(issued.revoked));
        }
        else if (issued.expires < QDateTime::currentDateTimeUtc())
        {
            status = tr("Expired");
        }

        QTreeWidgetItem* item = new QTreeWidgetItem(m_certificatesWidget, { issued.name, issued.title, issued.email, formatDate(issued.issued), formatDate(issued.expires), status });
        item->setData(0, Qt::UserRole, issued.serialNumber);
        item->setToolTip(0, tr("Serial number %1").arg(issued.serialNumber));
        if (issued.serialNumber == selectedSerialNumber)
        {
            m_certificatesWidget->setCurrentItem(item);
        }
    }

    updateButtons();
    updateTrustedAuthorities();
}

void DepartmentAuthorityDialog::updateTrustedAuthorities()
{
    m_trustedWidget->clear();
    for (const pdf::PDFFireCertificateAuthority::TrustedAuthority& authority : pdf::PDFFireCertificateAuthority::getTrustedAuthorities())
    {
        QString crlText = tr("no list");
        if (authority.crlNextUpdate.isValid())
        {
            crlText = formatDate(authority.crlNextUpdate);
            if (authority.crlNextUpdate < QDateTime::currentDateTimeUtc())
            {
                crlText += tr(" (expired)");
            }
        }

        QTreeWidgetItem* item = new QTreeWidgetItem(m_trustedWidget, { authority.name, authority.fingerprint.left(19) + QStringLiteral(" ..."), crlText,
                                                                        authority.isForAllUsers ? tr("All users (administrator)") : tr("You") });
        item->setToolTip(1, authority.fingerprint);
        item->setData(0, Qt::UserRole, authority.fileName);
        item->setData(0, Qt::UserRole + 1, authority.isForAllUsers);
    }
    m_removeTrustedButton->setEnabled(false);
}

void DepartmentAuthorityDialog::onRemoveTrustedAuthority()
{
    QTreeWidgetItem* item = m_trustedWidget->currentItem();
    if (!item ||
        QMessageBox::question(this, tr("Stop Trusting"),
                              tr("Stop trusting %1? The signatures of its members will show as not verified on this computer.").arg(item->text(0)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!pdf::PDFFireCertificateAuthority::removeTrustedAuthority(item->data(0, Qt::UserRole).toString(), &errorMessage))
    {
        showError(errorMessage);
    }
    updateUi();
}

void DepartmentAuthorityDialog::updateButtons()
{
    const bool isOpen = m_authority.isOpen();
    const QTreeWidgetItem* current = m_certificatesWidget->currentItem();
    bool isCurrentRevoked = true;
    if (current)
    {
        const QString serialNumber = current->data(0, Qt::UserRole).toString();
        for (const auto& issued : m_authority.getIssuedCertificates())
        {
            if (issued.serialNumber == serialNumber)
            {
                isCurrentRevoked = issued.isRevoked();
            }
        }
    }

    m_issueButton->setEnabled(isOpen);
    m_revokeButton->setEnabled(isOpen && current && !isCurrentRevoked);
    m_trustButton->setEnabled(isOpen && !pdf::PDFFireCertificateAuthority::isAuthorityTrusted(m_authority.getCertificate()));
    m_shareButton->setEnabled(isOpen);
    m_publishButton->setEnabled(isOpen);
}

void DepartmentAuthorityDialog::onCreateAuthority()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create Department Authority"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* intro = new QLabel(tr("The authority is created once. Its folder holds the key, which issues the certificates of the "
                                  "members - keep the folder in Proton Drive (online only, not synced to computers) with a copy on an "
                                  "encrypted USB stick in the station safe. Keep the passphrase on paper in the safe, not in the same "
                                  "place as the folder. Anybody with both can issue certificates in the name of the department."), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QFormLayout* form = new QFormLayout();
    // Nothing is filled in - every department creates its own authority, under its own name
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    nameEdit->setObjectName("authorityNameEdit");
    nameEdit->setPlaceholderText(tr("e.g. Prospect VFD Signing Authority"));
    QLineEdit* organizationEdit = new QLineEdit(&dialog);
    organizationEdit->setObjectName("organizationEdit");
    organizationEdit->setPlaceholderText(tr("e.g. Prospect VFD"));
    QLineEdit* crlUrlEdit = new QLineEdit(&dialog);
    crlUrlEdit->setObjectName("crlUrlEdit");
    crlUrlEdit->setPlaceholderText(tr("e.g. https://prospectfd.com/pki/authority.crl"));
    crlUrlEdit->setToolTip(tr("The revocation list (authority.crl) is uploaded to this address. It is written into every "
                              "certificate and can't be changed later. Leave it empty to publish no list."));
    QLineEdit* directoryEdit = nullptr;
    QWidget* directoryWidget = createPathEdit(&dialog, &directoryEdit, [&dialog](const QString& current)
    {
        return QFileDialog::getExistingDirectory(&dialog, tr("Folder of the Authority"), current);
    });
    directoryEdit->setObjectName("directoryEdit");
    directoryEdit->setText(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(tr("Signing Authority")));
    connect(nameEdit, &QLineEdit::textEdited, &dialog, [documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), directoryEdit](const QString& name)
    {
        // The folder follows the name of the authority
        QString safeName = name.trimmed();
        safeName.replace(QRegularExpression(QStringLiteral("[^\\w .-]")), QStringLiteral("_"));
        directoryEdit->setText(QDir(documents).filePath(safeName.isEmpty() ? QStringLiteral("Signing Authority") : safeName));
    });
    QLineEdit* passphraseEdit = new QLineEdit(&dialog);
    passphraseEdit->setObjectName("passphraseEdit");
    passphraseEdit->setEchoMode(QLineEdit::Password);
    passphraseEdit->setPlaceholderText(tr("At least %1 characters - a few unrelated words").arg(pdf::PDFFireCertificateAuthority::MINIMAL_PASSPHRASE_LENGTH));
    QLineEdit* repeatEdit = new QLineEdit(&dialog);
    repeatEdit->setObjectName("repeatEdit");
    repeatEdit->setEchoMode(QLineEdit::Password);

    form->addRow(tr("Name:"), nameEdit);
    form->addRow(tr("Organization:"), organizationEdit);
    form->addRow(tr("Revocation list address:"), crlUrlEdit);
    form->addRow(tr("Folder:"), directoryWidget);
    form->addRow(tr("Passphrase:"), passphraseEdit);
    form->addRow(tr("Repeat passphrase:"), repeatEdit);
    layout->addLayout(form);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
    {
        if (passphraseEdit->text() != repeatEdit->text())
        {
            QMessageBox::critical(&dialog, dialog.windowTitle(), tr("The passphrases are not the same."));
            return;
        }

        pdf::PDFFireCertificateAuthority::AuthorityInfo info;
        info.name = nameEdit->text().trimmed();
        info.organization = organizationEdit->text().trimmed();
        info.crlUrl = crlUrlEdit->text().trimmed();

        QString errorMessage;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool isCreated = m_authority.create(directoryEdit->text().trimmed(), info, passphraseEdit->text(), &errorMessage);
        QApplication::restoreOverrideCursor();
        if (!isCreated)
        {
            QMessageBox::critical(&dialog, dialog.windowTitle(), errorMessage);
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    QSettings().setValue(SETTINGS_AUTHORITY_DIRECTORY, m_authority.getDirectory());
    updateUi();

    if (QMessageBox::question(this, tr("Authority Created"),
                              tr("The authority was created in:\n%1\n\nTrust it on this computer now?").arg(QDir::toNativeSeparators(m_authority.getDirectory())),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
    {
        onTrustOpenAuthority();
    }
}

void DepartmentAuthorityDialog::onOpenAuthority()
{
    const QString lastDirectory = QSettings().value(SETTINGS_AUTHORITY_DIRECTORY).toString();
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Open the Folder of the Authority"), lastDirectory);
    if (directory.isEmpty())
    {
        return;
    }

    bool isOk = false;
    const QString passphrase = QInputDialog::getText(this, tr("Open Authority"), tr("Passphrase of the authority:"), QLineEdit::Password, QString(), &isOk);
    if (!isOk)
    {
        return;
    }

    QString errorMessage;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool isOpened = m_authority.open(directory, passphrase, &errorMessage);
    QApplication::restoreOverrideCursor();
    if (!isOpened)
    {
        showError(errorMessage);
        updateUi();
        return;
    }

    QSettings().setValue(SETTINGS_AUTHORITY_DIRECTORY, m_authority.getDirectory());
    updateUi();
}

void DepartmentAuthorityDialog::onTrustAuthorityFile()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Trust an Authority"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                          tr("Authority certificate (*.cer *.crt *.pem *.der);;All files (*.*)"));
    if (fileName.isEmpty())
    {
        return;
    }

    QFile file(fileName);
    if (!file.open(QFile::ReadOnly))
    {
        showError(tr("The file '%1' can't be read.").arg(fileName));
        return;
    }
    const QByteArray certificate = file.readAll();

    // The revocation list goes with the certificate, when it is next to it
    QByteArray revocationList;
    QFile crlFile(QFileInfo(fileName).dir().filePath(pdf::PDFFireCertificateAuthority::CRL_FILE_NAME));
    if (crlFile.open(QFile::ReadOnly))
    {
        revocationList = crlFile.readAll();
    }

    const QString fingerprint = pdf::PDFFireCertificateAuthority::getFingerprint(certificate);
    if (fingerprint.isEmpty())
    {
        showError(tr("The file '%1' is not a certificate.").arg(fileName));
        return;
    }

    if (QMessageBox::question(this, tr("Trust an Authority"),
                              tr("Authority: %1\nFingerprint:\n%2\n\nEvery certificate issued by this authority will be trusted on this computer. "
                                 "Trust only the authority certificate of your own department, received from a person you know - "
                                 "compare the fingerprint with theirs (the Department window shows it).\n\nTrust it?")
                                  .arg(pdf::PDFFireCertificateAuthority::getCertificateName(certificate), fingerprint),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!pdf::PDFFireCertificateAuthority::trustAuthority(certificate, revocationList, &errorMessage))
    {
        showError(errorMessage);
        return;
    }
    QMessageBox::information(this, tr("Trust an Authority"), tr("The authority is trusted. Documents opened from now on show the signatures of its members as trusted."));
    updateUi();
}

void DepartmentAuthorityDialog::onIssueCertificate()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Issue a Member Certificate"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* intro = new QLabel(tr("Issue a certificate only to a member you have identified in person."), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QFormLayout* form = new QFormLayout();
    QLineEdit* nameEdit = new QLineEdit(&dialog);
    nameEdit->setObjectName("memberNameEdit");
    nameEdit->setPlaceholderText(tr("Full name, as it is shown in the signatures"));
    QLineEdit* titleEdit = new QLineEdit(&dialog);
    titleEdit->setObjectName("memberTitleEdit");
    titleEdit->setPlaceholderText(tr("e.g. Lieutenant, Secretary (optional)"));
    QLineEdit* emailEdit = new QLineEdit(&dialog);
    emailEdit->setObjectName("memberEmailEdit");
    emailEdit->setPlaceholderText(tr("(optional)"));
    QSpinBox* yearsSpin = new QSpinBox(&dialog);
    yearsSpin->setObjectName("memberYearsSpin");
    yearsSpin->setRange(1, 5);
    yearsSpin->setValue(2);
    yearsSpin->setSuffix(tr(" years"));
    QLineEdit* fileEdit = nullptr;
    QWidget* fileWidget = createPathEdit(&dialog, &fileEdit, [&dialog](const QString& current)
    {
        return QFileDialog::getSaveFileName(&dialog, tr("Save the Certificate of the Member"), current, tr("Certificate (*.pfx)"));
    });
    fileEdit->setObjectName("memberFileEdit");

    form->addRow(tr("Name:"), nameEdit);
    form->addRow(tr("Rank / title:"), titleEdit);
    form->addRow(tr("E-mail:"), emailEdit);
    form->addRow(tr("Valid for:"), yearsSpin);
    form->addRow(tr("Save as:"), fileWidget);
    layout->addLayout(form);

    // The file name follows the name, until the user changes it
    const QString documentsDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    auto defaultFileName = [documentsDirectory](const QString& name)
    {
        QString safeName = name.trimmed();
        safeName.replace(QRegularExpression(QStringLiteral("[^\\w .-]")), QStringLiteral("_"));
        return QDir(documentsDirectory).filePath(QString("%1 - signing certificate.pfx").arg(safeName.isEmpty() ? QStringLiteral("Member") : safeName));
    };
    fileEdit->setText(defaultFileName(QString()));
    connect(nameEdit, &QLineEdit::textChanged, &dialog, [=](const QString& name)
    {
        if (QFileInfo(fileEdit->text()).fileName().endsWith(QStringLiteral(" - signing certificate.pfx")))
        {
            fileEdit->setText(defaultFileName(name));
        }
    });

    QGroupBox* policyGroup = new QGroupBox(tr("Recorded when this member signs"), &dialog);
    QVBoxLayout* policyLayout = new QVBoxLayout(policyGroup);
    QCheckBox* operatingSystemCheckBox = new QCheckBox(tr("Operating system"), policyGroup);
    QCheckBox* computerCheckBox = new QCheckBox(tr("Computer name and user account"), policyGroup);
    QCheckBox* localAddressCheckBox = new QCheckBox(tr("IP addresses of the computer"), policyGroup);
    QCheckBox* publicAddressCheckBox = new QCheckBox(tr("Public IP address (looked up online)"), policyGroup);
    QCheckBox* lockedCheckBox = new QCheckBox(tr("The member can't turn these off"), policyGroup);
    lockedCheckBox->setObjectName("memberLockedCheckBox");
    for (QCheckBox* checkBox : { operatingSystemCheckBox, computerCheckBox, localAddressCheckBox, publicAddressCheckBox })
    {
        checkBox->setChecked(true);
        policyLayout->addWidget(checkBox);
    }
    policyLayout->addWidget(lockedCheckBox);
    QLabel* alwaysLabel = new QLabel(tr("The name, the date and time, the fingerprint of the document and the consent are always recorded."), policyGroup);
    alwaysLabel->setWordWrap(true);
    policyLayout->addWidget(alwaysLabel);
    layout->addWidget(policyGroup);

    QCheckBox* installCheckBox = new QCheckBox(tr("Also install it on this computer (your own certificate)"), &dialog);
    installCheckBox->setObjectName("memberInstallCheckBox");
    layout->addWidget(installCheckBox);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Issue"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QString password;
    QString savedFileName;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
    {
        pdf::PDFFireCertificateAuthority::MemberInfo member;
        member.name = nameEdit->text().trimmed();
        member.title = titleEdit->text().trimmed();
        member.email = emailEdit->text().trimmed();
        member.validityYears = yearsSpin->value();
        member.policy.recordOperatingSystem = operatingSystemCheckBox->isChecked();
        member.policy.recordComputer = computerCheckBox->isChecked();
        member.policy.recordLocalAddresses = localAddressCheckBox->isChecked();
        member.policy.recordPublicAddress = publicAddressCheckBox->isChecked();
        member.policy.isLocked = lockedCheckBox->isChecked();

        savedFileName = fileEdit->text().trimmed();
        if (!savedFileName.endsWith(QStringLiteral(".pfx"), Qt::CaseInsensitive))
        {
            savedFileName += QStringLiteral(".pfx");
        }
        if (QFile::exists(savedFileName) &&
            QMessageBox::question(&dialog, dialog.windowTitle(), tr("The file '%1' exists. Replace it?").arg(savedFileName)) != QMessageBox::Yes)
        {
            return;
        }

        password = pdf::PDFFireCertificateAuthority::generatePassword();
        QByteArray pkcs12;
        QString errorMessage;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool isIssued = m_authority.issue(member, password, &pkcs12, nullptr, &errorMessage);
        QApplication::restoreOverrideCursor();
        if (!isIssued)
        {
            QMessageBox::critical(&dialog, dialog.windowTitle(), errorMessage);
            return;
        }

        // The file holds the (encrypted) key of the member - only the owner may read it
        QFile::remove(savedFileName);
        QFile file(savedFileName);
        if (!file.open(QFile::WriteOnly | QFile::NewOnly, QFile::ReadOwner | QFile::WriteOwner) || file.write(pkcs12) != pkcs12.size())
        {
            QMessageBox::critical(&dialog, dialog.windowTitle(), tr("The certificate was issued, but the file '%1' can't be written. Revoke it and issue it again.").arg(savedFileName));
            dialog.accept();
            return;
        }
        file.close();

        if (installCheckBox->isChecked())
        {
            QDir().mkpath(pdf::PDFCertificateManager::getCertificateDirectory());
            const QString installedFileName = pdf::PDFCertificateManager::generateCertificateFileName();
            QFile installedFile(installedFileName);
            if (installedFile.open(QFile::WriteOnly | QFile::NewOnly, QFile::ReadOwner | QFile::WriteOwner))
            {
                installedFile.write(pkcs12);
                installedFile.close();
            }
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    updateUi();
    if (password.isEmpty())
    {
        return;
    }

    // The password is shown once - it is not stored anywhere
    QDialog passwordDialog(this);
    passwordDialog.setWindowTitle(tr("Certificate Issued"));
    QVBoxLayout* passwordLayout = new QVBoxLayout(&passwordDialog);
    QLabel* text = new QLabel(tr("<p>The certificate was saved as:<br><i>%1</i></p>"
                                 "<p>Give the member the file and this password (write it down - it is not stored anywhere and "
                                 "can't be shown again):</p>").arg(QDir::toNativeSeparators(savedFileName).toHtmlEscaped()), &passwordDialog);
    text->setWordWrap(true);
    passwordLayout->addWidget(text);
    QLineEdit* passwordEdit = new QLineEdit(password, &passwordDialog);
    passwordEdit->setObjectName("issuedPasswordEdit");
    passwordEdit->setReadOnly(true);
    QFont passwordFont = passwordEdit->font();
    passwordFont.setFamily(QStringLiteral("Monospace"));
    passwordFont.setPointSizeF(passwordFont.pointSizeF() * 1.4);
    passwordEdit->setFont(passwordFont);
    passwordLayout->addWidget(passwordEdit);
    QLabel* howTo = new QLabel(tr("<p>The member installs it in PDF Fire: <b>Sign ▸ My Certificates ▸ Import</b>, and trusts the "
                                  "department: <b>Sign ▸ Department ▸ Trust an Authority</b> with <i>authority.cer</i>.</p>"), &passwordDialog);
    howTo->setWordWrap(true);
    passwordLayout->addWidget(howTo);
    QDialogButtonBox* passwordButtons = new QDialogButtonBox(QDialogButtonBox::Ok, &passwordDialog);
    QPushButton* copyButton = passwordButtons->addButton(tr("Copy Password"), QDialogButtonBox::ActionRole);
    connect(copyButton, &QPushButton::clicked, &passwordDialog, [password]() { QApplication::clipboard()->setText(password); });
    connect(passwordButtons, &QDialogButtonBox::accepted, &passwordDialog, &QDialog::accept);
    passwordLayout->addWidget(passwordButtons);
    passwordDialog.exec();
}

void DepartmentAuthorityDialog::onRevokeCertificate()
{
    QTreeWidgetItem* item = m_certificatesWidget->currentItem();
    if (!item)
    {
        return;
    }

    const QStringList reasons = { tr("The member left the department"),
                                  tr("Lost or stolen computer, or the password is known by others"),
                                  tr("Replaced by a new certificate"),
                                  tr("Other reason") };
    bool isOk = false;
    const QString reason = QInputDialog::getItem(this, tr("Revoke Certificate"),
                                                 tr("Revoke the certificate of %1? It can't be undone.\n\nReason:").arg(item->text(0)),
                                                 reasons, 0, false, &isOk);
    if (!isOk)
    {
        return;
    }

    pdf::PDFFireCertificateAuthority::RevocationReason revocationReason = pdf::PDFFireCertificateAuthority::RevocationReason::Unspecified;
    switch (reasons.indexOf(reason))
    {
        case 0: revocationReason = pdf::PDFFireCertificateAuthority::RevocationReason::CessationOfOperation; break;
        case 1: revocationReason = pdf::PDFFireCertificateAuthority::RevocationReason::KeyCompromise; break;
        case 2: revocationReason = pdf::PDFFireCertificateAuthority::RevocationReason::Superseded; break;
        default: break;
    }

    QString errorMessage;
    if (!m_authority.revoke(item->data(0, Qt::UserRole).toString(), revocationReason, &errorMessage))
    {
        showError(errorMessage);
        return;
    }
    updateUi();
    onPublishRevocationList();
}

void DepartmentAuthorityDialog::onTrustOpenAuthority()
{
    QFile crlFile(QDir(m_authority.getDirectory()).filePath(pdf::PDFFireCertificateAuthority::CRL_FILE_NAME));
    QByteArray revocationList;
    if (crlFile.open(QFile::ReadOnly))
    {
        revocationList = crlFile.readAll();
    }

    QString errorMessage;
    if (!pdf::PDFFireCertificateAuthority::trustAuthority(m_authority.getCertificate(), revocationList, &errorMessage))
    {
        showError(errorMessage);
    }
    updateUi();
}

void DepartmentAuthorityDialog::onShareCertificate()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Share Authority Certificate"),
                                                          QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("authority.cer")),
                                                          tr("Certificate (*.cer)"));
    if (fileName.isEmpty())
    {
        return;
    }

    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate) || file.write(m_authority.getCertificate()) != m_authority.getCertificate().size())
    {
        showError(tr("The file '%1' can't be written.").arg(fileName));
        return;
    }
    file.close();
    QMessageBox::information(this, tr("Share Authority Certificate"),
                             tr("Saved. Its fingerprint is:\n%1\n\nThe certificate holds no secret - e-mail it, post it on the department website, or copy it to a "
                                "USB stick. In Adobe Acrobat or Reader it is trusted in: Preferences ▸ Signatures ▸ Identities & Trusted "
                                "Certificates ▸ Trusted Certificates ▸ Import, then Edit Trust ▸ \"Use this certificate as a trusted root\".")
                                 .arg(pdf::PDFFireCertificateAuthority::getFingerprint(m_authority.getCertificate())));
}

void DepartmentAuthorityDialog::onPublishRevocationList()
{
    QString errorMessage;
    if (!m_authority.writeRevocationList(&errorMessage))
    {
        showError(errorMessage);
        return;
    }
    updateUi();

    const QString crlFileName = QDir::toNativeSeparators(QDir(m_authority.getDirectory()).filePath(pdf::PDFFireCertificateAuthority::CRL_FILE_NAME));
    if (m_authority.getCrlUrl().isEmpty())
    {
        QMessageBox::information(this, tr("Revocation List"), tr("The revocation list was written:\n%1").arg(crlFileName));
    }
    else
    {
        QMessageBox::information(this, tr("Revocation List"),
                                 tr("The revocation list was written:\n%1\n\nUpload it to:\n%2\n\nUntil it is uploaded, other computers and Adobe "
                                    "don't know about the change.").arg(crlFileName, m_authority.getCrlUrl()));
    }
}

}   // namespace pdfplugin
