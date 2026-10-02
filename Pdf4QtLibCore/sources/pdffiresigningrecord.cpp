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

#include "pdffiresigningrecord.h"
#include "pdfdocumentbuilder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

namespace pdf
{

/// Service, which answers with the public address of the caller (plain text)
static constexpr const char* PUBLIC_ADDRESS_SERVICE = "https://api.ipify.org";

QString PDFFireSigningRecord::getConsentText()
{
    return QCoreApplication::translate("PDFFireSigningRecord",
                                       "I intend to sign this document, and I agree that my electronic signature "
                                       "is the legal equivalent of my handwritten signature.");
}

void PDFFireSigningRecord::collectComputerInformation(bool operatingSystem, bool computer, bool localAddresses)
{
    applicationVersion = QString("%1 (PDF4QT %2)").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion());
    this->operatingSystem = operatingSystem ? QString("%1 (%2 %3, %4)").arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(),
                                                                         QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture())
                                            : QString();
    hostName = computer ? QSysInfo::machineHostName() : QString();
    userAccount = computer ? qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME")) : QString();

    this->localAddresses.clear();
    if (!localAddresses)
    {
        return;
    }

    // The IPv4 addresses, and one global IPv6 address. Loopback, link-local and
    // unique-local addresses say nothing about the computer, and the temporary
    // IPv6 addresses (privacy extensions) change every few hours.
    QString globalIPv6Address;
    for (const QHostAddress& address : QNetworkInterface::allAddresses())
    {
        if (address.isLoopback() || address.isLinkLocal() || address.isUniqueLocalUnicast())
        {
            continue;
        }

        if (address.protocol() == QAbstractSocket::IPv4Protocol)
        {
            this->localAddresses << address.toString();
        }
        else if (globalIPv6Address.isEmpty() && address.isGlobal())
        {
            QHostAddress plainAddress = address;
            plainAddress.setScopeId(QString());
            globalIPv6Address = plainAddress.toString();
        }
    }
    if (!globalIPv6Address.isEmpty())
    {
        this->localAddresses << globalIPv6Address;
    }
}

void PDFFireSigningRecord::lookupPublicAddress(int timeoutMilliseconds)
{
    QNetworkAccessManager networkAccessManager;
    QNetworkRequest request{ QUrl(QString::fromLatin1(PUBLIC_ADDRESS_SERVICE)) };
    request.setTransferTimeout(timeoutMilliseconds);
    QNetworkReply* reply = networkAccessManager.get(request);

    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    QTimer deadlineTimer;
    deadlineTimer.setSingleShot(true);
    QObject::connect(&deadlineTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadlineTimer.start(timeoutMilliseconds);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);

    const QString host = QUrl(QString::fromLatin1(PUBLIC_ADDRESS_SERVICE)).host();
    if (reply->error() == QNetworkReply::NoError)
    {
        // Only a valid address is accepted - the answer is written into the document
        const QString answer = QString::fromLatin1(reply->read(64)).trimmed();
        QHostAddress address;
        if (address.setAddress(answer))
        {
            publicAddress = QString("%1 (reported by %2)").arg(address.toString(), host);
        }
        else
        {
            publicAddress = QString("not known (%1 gave no valid address)").arg(host);
        }
    }
    else
    {
        publicAddress = QString("not known (%1 could not be reached: %2)").arg(host, reply->errorString());
    }
    reply->deleteLater();
}

QString PDFFireSigningRecord::toText() const
{
    QStringList lines;
    auto addLine = [&lines](const char* caption, const QString& value)
    {
        if (!value.isEmpty())
        {
            // A value is one line, so a line feed in it can't fake another entry
            QString singleLine = value;
            singleLine.replace(QChar('\r'), QChar(' ')).replace(QChar('\n'), QChar(' '));
            lines << QString("%1: %2").arg(QString::fromLatin1(caption), singleLine);
        }
    };

    lines << QStringLiteral("PDF Fire signing record");
    addLine("Signer (certificate)", signerName);
    addLine("Signer e-mail (certificate)", signerEmail);
    addLine("Certificate issued by", certificateIssuer.isEmpty() ? QStringLiteral("the signer (self-made certificate)") : certificateIssuer);
    addLine("Name typed by the signer", typedName);
    addLine("Signature type", signatureType);
    addLine("Timestamp authority", timestampAuthority);
    addLine("Certification", certification);
    if (signingTime.isValid())
    {
        addLine("Signed (local time)", signingTime.toOffsetFromUtc(signingTime.offsetFromUtc()).toString(Qt::ISODate));
        addLine("Signed (UTC)", signingTime.toUTC().toString(Qt::ISODate));
    }
    addLine("Reason", reason);
    addLine("Contact", contactInfo);
    addLine("Document", documentName);
    addLine("File SHA-256 before signing", documentFingerprint);
    addLine("Signature on page", pageNumber > 0 ? QString::number(pageNumber) : QStringLiteral("invisible signature"));
    addLine("Consent to sign electronically", consentAccepted ? QString("accepted - \"%1\"").arg(getConsentText()) : QStringLiteral("not recorded"));
    addLine("Operating system", operatingSystem);
    addLine("Computer name", hostName);
    addLine("User account", userAccount);
    addLine("IP addresses of this computer", localAddresses.join(QStringLiteral(", ")));
    addLine("Public IP address", publicAddress);
    if (!recordingSetBy.isEmpty())
    {
        addLine("Recorded details chosen by", isRecordingLocked ? QString("%1 (required)").arg(recordingSetBy) : recordingSetBy);
    }
    addLine("Application", applicationVersion);
    return lines.join(QChar('\n'));
}

void PDFFireSigningRecord::writeTo(PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary) const
{
    PDFObjectFactory factory;
    factory.beginDictionary();

    factory.beginDictionaryItem(DICTIONARY_KEY);
    factory << toText();
    factory.endDictionaryItem();

    // The host name is what the Location entry is meant for ("CPU host name or
    // physical location of the signing")
    if (!hostName.isEmpty())
    {
        factory.beginDictionaryItem("Location");
        factory << hostName;
        factory.endDictionaryItem();
    }

    // Signature build properties (Adobe: Digital Signature Build Dictionary) -
    // the application and the operating system, shown by other readers too
    factory.beginDictionaryItem("Prop_Build");
    factory.beginDictionary();
    factory.beginDictionaryItem("App");
    factory.beginDictionary();
    factory.beginDictionaryItem("Name");
    factory << WrapName("PDF_Fire");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("REx");
    factory << QCoreApplication::applicationVersion();
    factory.endDictionaryItem();
    if (!operatingSystem.isEmpty())
    {
        factory.beginDictionaryItem("OS");
        factory.beginArray();
        factory << WrapName(QSysInfo::kernelType().toLatin1());
        factory.endArray();
        factory.endDictionaryItem();
    }
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();

    factory.endDictionary();
    builder.mergeTo(signatureDictionary, factory.takeObject());
}

QString PDFFireSigningRecord::getLogFileName()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("signing-log.txt"));
}

QString PDFFireSigningRecord::getFingerprint(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString PDFFireSigningRecord::appendToLog(const QString& savedFileName, const QByteArray& savedFileData) const
{
    const QString logFileName = getLogFileName();
    if (logFileName.isEmpty() || !QDir().mkpath(QFileInfo(logFileName).path()))
    {
        return QString();
    }

    QFile file(logFileName);
    if (!file.open(QFile::WriteOnly | QFile::Append | QFile::Text))
    {
        return QString();
    }

    QString text = toText();
    text += QString("\nSaved as: %1").arg(QFileInfo(savedFileName).absoluteFilePath());
    text += QString("\nSigned file SHA-256: %1").arg(getFingerprint(savedFileData));
    text += QStringLiteral("\n\n");
    file.write(text.toUtf8());
    file.close();

    // The log is personal (addresses, file names) - only the user can read it
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return logFileName;
}

}   // namespace pdf
