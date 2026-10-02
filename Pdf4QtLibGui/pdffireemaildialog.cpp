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

#include "pdffireemaildialog.h"
#include "pdfwidgetutils.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "pdfdbgheap.h"

namespace pdfviewer
{

namespace
{

/// The password of the mail server, remembered until the application is closed
QString s_sessionPassword;

QSettings createSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
}

}   // namespace

PDFFireMailServerDialog::PDFFireMailServerDialog(QWidget* parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Mail Server"));

    const pdf::PDFSmtpClient::Settings settings = loadSettings();

    m_hostEdit = new QLineEdit(settings.host, this);
    m_hostEdit->setPlaceholderText(tr("for example smtp.gmail.com"));
    m_portEdit = new QSpinBox(this);
    m_portEdit->setRange(1, 65535);
    m_portEdit->setValue(settings.port);
    m_securityCombo = new QComboBox(this);
    m_securityCombo->addItem(tr("STARTTLS (usually port 587)"), int(pdf::PDFSmtpClient::Security::StartTls));
    m_securityCombo->addItem(tr("SSL/TLS (usually port 465)"), int(pdf::PDFSmtpClient::Security::SslTls));
    m_securityCombo->addItem(tr("None - no password, trusted network only"), int(pdf::PDFSmtpClient::Security::None));
    m_securityCombo->setCurrentIndex(m_securityCombo->findData(int(settings.security)));
    m_userNameEdit = new QLineEdit(settings.userName, this);
    m_userNameEdit->setPlaceholderText(tr("usually your e-mail address"));
    m_fromAddressEdit = new QLineEdit(settings.fromAddress, this);
    m_fromAddressEdit->setPlaceholderText(tr("you@example.com"));
    m_fromNameEdit = new QLineEdit(settings.fromName, this);

    // The usual port follows the security, unless the user has typed another one
    connect(m_securityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        const auto security = static_cast<pdf::PDFSmtpClient::Security>(m_securityCombo->currentData().toInt());
        const int port = m_portEdit->value();
        if (port == 25 || port == 465 || port == 587)
        {
            m_portEdit->setValue(security == pdf::PDFSmtpClient::Security::SslTls ? 465 : 587);
        }
    });

    QFormLayout* formLayout = new QFormLayout();
    formLayout->addRow(tr("Server:"), m_hostEdit);
    formLayout->addRow(tr("Port:"), m_portEdit);
    formLayout->addRow(tr("Security:"), m_securityCombo);
    formLayout->addRow(tr("User name:"), m_userNameEdit);
    formLayout->addRow(tr("From address:"), m_fromAddressEdit);
    formLayout->addRow(tr("From name:"), m_fromNameEdit);

    QLabel* hintLabel = new QLabel(tr("The password is not saved. It is asked for, when a document is sent first, and it is "
                                      "remembered only until PDF Fire is closed.\n\n"
                                      "Gmail: smtp.gmail.com, 587, STARTTLS, and an app password (Google Account > Security > App passwords).\n"
                                      "Outlook / Microsoft 365: smtp.office365.com, 587, STARTTLS.\n"
                                      "Proton Mail: an SMTP token (Settings > IMAP/SMTP), smtp.protonmail.ch, 587, STARTTLS."), this);
    hintLabel->setWordWrap(true);
    hintLabel->setObjectName("mailServerHint");

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(hintLabel);
    layout->addWidget(buttonBox);

    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 520));
}

pdf::PDFSmtpClient::Settings PDFFireMailServerDialog::loadSettings()
{
    QSettings settings = createSettings();
    settings.beginGroup("PDFFireMailServer");

    pdf::PDFSmtpClient::Settings result;
    result.host = settings.value("host").toString();
    result.port = settings.value("port", 587).toInt();
    result.security = static_cast<pdf::PDFSmtpClient::Security>(settings.value("security", int(pdf::PDFSmtpClient::Security::StartTls)).toInt());
    result.userName = settings.value("userName").toString();
    result.fromAddress = settings.value("fromAddress").toString();
    result.fromName = settings.value("fromName").toString();
    result.password = s_sessionPassword;

    settings.endGroup();
    return result;
}

void PDFFireMailServerDialog::saveSettings(const pdf::PDFSmtpClient::Settings& smtpSettings)
{
    QSettings settings = createSettings();
    settings.beginGroup("PDFFireMailServer");
    settings.setValue("host", smtpSettings.host);
    settings.setValue("port", smtpSettings.port);
    settings.setValue("security", int(smtpSettings.security));
    settings.setValue("userName", smtpSettings.userName);
    settings.setValue("fromAddress", smtpSettings.fromAddress);
    settings.setValue("fromName", smtpSettings.fromName);
    settings.endGroup();
}

bool PDFFireMailServerDialog::isConfigured()
{
    const pdf::PDFSmtpClient::Settings settings = loadSettings();
    return !settings.host.isEmpty() && pdf::PDFSmtpClient::isValidAddress(settings.fromAddress);
}

void PDFFireMailServerDialog::accept()
{
    pdf::PDFSmtpClient::Settings settings;
    settings.host = m_hostEdit->text().trimmed();
    settings.port = m_portEdit->value();
    settings.security = static_cast<pdf::PDFSmtpClient::Security>(m_securityCombo->currentData().toInt());
    settings.userName = m_userNameEdit->text().trimmed();
    settings.fromAddress = m_fromAddressEdit->text().trimmed();
    settings.fromName = m_fromNameEdit->text().trimmed();

    if (settings.host.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("Enter the address of the mail server."));
        return;
    }

    if (!pdf::PDFSmtpClient::isValidAddress(settings.fromAddress))
    {
        QMessageBox::warning(this, windowTitle(), tr("Enter a valid sender address."));
        return;
    }

    if (settings.security == pdf::PDFSmtpClient::Security::None && !settings.userName.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("A server with a user name needs an encrypted connection (STARTTLS or SSL/TLS) - the password would be readable on the network."));
        return;
    }

    // A changed server or user needs the password again
    const pdf::PDFSmtpClient::Settings oldSettings = loadSettings();
    if (oldSettings.host != settings.host || oldSettings.userName != settings.userName)
    {
        s_sessionPassword.clear();
    }

    saveSettings(settings);
    QDialog::accept();
}

PDFFireEmailDialog::PDFFireEmailDialog(QByteArray attachment, QString attachmentFileName, QString subject, QWidget* parent) :
    QDialog(parent),
    m_attachment(qMove(attachment)),
    m_attachmentFileName(qMove(attachmentFileName))
{
    setWindowTitle(tr("Send by E-Mail"));

    m_toEdit = new QLineEdit(this);
    m_toEdit->setPlaceholderText(tr("name@example.com, another@example.com"));
    m_subjectEdit = new QLineEdit(subject, this);
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlaceholderText(tr("Message (optional)"));

    QLabel* attachmentLabel = new QLabel(tr("%1 (%2)").arg(m_attachmentFileName, QLocale().formattedDataSize(m_attachment.size())), this);
    m_serverLabel = new QLabel(this);
    QPushButton* serverButton = new QPushButton(tr("Mail Server..."), this);
    connect(serverButton, &QPushButton::clicked, this, [this]()
    {
        PDFFireMailServerDialog dialog(this);
        dialog.exec();
        updateServerLabel();
    });

    QHBoxLayout* serverLayout = new QHBoxLayout();
    serverLayout->addWidget(m_serverLabel, 1);
    serverLayout->addWidget(serverButton);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->addRow(tr("To:"), m_toEdit);
    formLayout->addRow(tr("Subject:"), m_subjectEdit);
    formLayout->addRow(tr("Attachment:"), attachmentLabel);
    formLayout->addRow(tr("Send through:"), serverLayout);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttonBox->addButton(tr("Send"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(m_textEdit, 1);
    layout->addWidget(buttonBox);

    updateServerLabel();
    resize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(560, 420)));
}

void PDFFireEmailDialog::updateServerLabel()
{
    const pdf::PDFSmtpClient::Settings settings = PDFFireMailServerDialog::loadSettings();
    m_serverLabel->setText(PDFFireMailServerDialog::isConfigured() ? tr("%1 (from %2)").arg(settings.host, settings.fromAddress)
                                                                   : tr("No mail server is set yet."));
}

void PDFFireEmailDialog::accept()
{
    pdf::PDFSmtpClient::Message message;
    message.subject = m_subjectEdit->text();
    message.text = m_textEdit->toPlainText();
    message.attachment = m_attachment;
    message.attachmentFileName = m_attachmentFileName;

    const QStringList recipients = m_toEdit->text().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    for (const QString& recipient : recipients)
    {
        if (!pdf::PDFSmtpClient::isValidAddress(recipient))
        {
            QMessageBox::warning(this, windowTitle(), tr("'%1' is not a valid e-mail address.").arg(recipient));
            return;
        }
    }
    if (recipients.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("Enter the address of the recipient."));
        return;
    }
    message.to = recipients;

    if (!PDFFireMailServerDialog::isConfigured())
    {
        PDFFireMailServerDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
        updateServerLabel();
    }

    pdf::PDFSmtpClient::Settings settings = PDFFireMailServerDialog::loadSettings();
    if (!settings.userName.isEmpty() && s_sessionPassword.isEmpty())
    {
        bool ok = false;
        const QString password = QInputDialog::getText(this, tr("Mail Server Password"),
                                                       tr("Password for %1 at %2:\n(It is not saved, only remembered until PDF Fire is closed.)").arg(settings.userName, settings.host),
                                                       QLineEdit::Password, QString(), &ok);
        if (!ok)
        {
            return;
        }
        s_sessionPassword = password;
    }
    settings.password = s_sessionPassword;

    // The conversation with the server runs in the background, the window stays responsive
    QProgressDialog progressDialog(tr("Sending the e-mail..."), QString(), 0, 0, this);
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setMinimumDuration(0);

    QFutureWatcher<pdf::PDFOperationResult> watcher;
    connect(&watcher, &QFutureWatcher<pdf::PDFOperationResult>::finished, &progressDialog, &QProgressDialog::close);
    watcher.setFuture(QtConcurrent::run([settings, message]() { return pdf::PDFSmtpClient::send(settings, message); }));
    progressDialog.exec();
    watcher.waitForFinished();

    const pdf::PDFOperationResult result = watcher.result();
    if (!result)
    {
        // A wrong password is asked for again next time
        if (result.getErrorMessage().contains("AUTH"))
        {
            s_sessionPassword.clear();
        }
        QMessageBox::critical(this, tr("E-Mail Not Sent"), result.getErrorMessage());
        return;
    }

    QMessageBox::information(this, tr("E-Mail Sent"), tr("The document was sent to %1.").arg(recipients.join(", ")));
    QDialog::accept();
}

}   // namespace pdfviewer
