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

#include "pdffiresmtpclient.h"

#include <QDateTime>
#include <QHostInfo>
#include <QLocale>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSslSocket>
#include <QUuid>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

/// Removes the line breaks, so a text cannot add its own headers to the message
QString sanitizeHeaderText(QString text)
{
    text.replace(QChar('\r'), QChar(' '));
    text.replace(QChar('\n'), QChar(' '));
    return text.trimmed();
}

/// Encodes a header value (RFC 2047), if it is not plain ASCII
QByteArray encodeHeaderText(const QString& text)
{
    const QString sanitized = sanitizeHeaderText(text);
    bool isAscii = true;
    for (const QChar character : sanitized)
    {
        if (character.unicode() > 126 || character.unicode() < 32)
        {
            isAscii = false;
            break;
        }
    }

    if (isAscii)
    {
        return sanitized.toLatin1();
    }

    return "=?UTF-8?B?" + sanitized.toUtf8().toBase64() + "?=";
}

/// Base64 in lines of 76 characters, as MIME requires
QByteArray base64Lines(const QByteArray& data)
{
    const QByteArray encoded = data.toBase64();
    QByteArray result;
    result.reserve(encoded.size() + encoded.size() / 76 * 2 + 2);
    for (qsizetype i = 0; i < encoded.size(); i += 76)
    {
        result += encoded.mid(i, 76);
        result += "\r\n";
    }
    return result;
}

class SmtpConversation
{
public:
    explicit SmtpConversation(const PDFSmtpClient::Settings& settings) : m_settings(settings) { }

    PDFOperationResult run(const QByteArray& sender, const QList<QByteArray>& recipients, const QByteArray& message);

private:
    /// Reads the reply of the server, returns its code (or -1 on a timeout / an error)
    int readReply();

    /// Sends a command and checks the code of the reply
    bool command(const QByteArray& line, std::initializer_list<int> expectedCodes, const QString& logLine = QString());

    bool sayHello();
    bool authenticate();

    const PDFSmtpClient::Settings& m_settings;
    QSslSocket m_socket;
    QByteArray m_lastReply;
    QString m_error;
    QStringList m_capabilities;
};

int SmtpConversation::readReply()
{
    m_lastReply.clear();
    m_capabilities.clear();

    while (true)
    {
        while (!m_socket.canReadLine())
        {
            if (!m_socket.waitForReadyRead(m_settings.timeout))
            {
                m_error = PDFTranslationContext::tr("The mail server did not answer in time (%1).").arg(m_socket.errorString());
                return -1;
            }
        }

        const QByteArray line = m_socket.readLine().trimmed();
        m_lastReply += line + "\n";

        if (line.size() < 3)
        {
            m_error = PDFTranslationContext::tr("The mail server sent an invalid reply.");
            return -1;
        }

        // A reply can have several lines "250-...", the last one is "250 ..."
        if (line.size() > 4)
        {
            m_capabilities << QString::fromLatin1(line.mid(4)).toUpper();
        }

        if (line.size() == 3 || line[3] == ' ')
        {
            bool ok = false;
            const int code = line.left(3).toInt(&ok);
            return ok ? code : -1;
        }
    }
}

bool SmtpConversation::command(const QByteArray& line, std::initializer_list<int> expectedCodes, const QString& logLine)
{
    m_socket.write(line + "\r\n");
    if (!m_socket.waitForBytesWritten(m_settings.timeout))
    {
        m_error = PDFTranslationContext::tr("Sending to the mail server failed (%1).").arg(m_socket.errorString());
        return false;
    }

    const int code = readReply();
    if (code == -1)
    {
        return false;
    }

    if (std::find(expectedCodes.begin(), expectedCodes.end(), code) == expectedCodes.end())
    {
        const QString shownCommand = logLine.isEmpty() ? QString::fromLatin1(line) : logLine;
        m_error = PDFTranslationContext::tr("The mail server refused '%1': %2").arg(shownCommand, QString::fromUtf8(m_lastReply).trimmed());
        return false;
    }

    return true;
}

bool SmtpConversation::sayHello()
{
    QByteArray hostName = QHostInfo::localHostName().toLatin1();
    if (hostName.isEmpty() || hostName.contains(' '))
    {
        hostName = "localhost";
    }

    return command("EHLO " + hostName, { 250 });
}

bool SmtpConversation::authenticate()
{
    if (m_settings.userName.isEmpty())
    {
        return true;
    }

    if (!m_socket.isEncrypted() && !m_settings.allowUnencryptedAuthentication)
    {
        m_error = PDFTranslationContext::tr("The password would be sent without encryption. Choose the security STARTTLS or SSL/TLS in the mail server settings.");
        return false;
    }

    const bool supportsPlain = std::any_of(m_capabilities.cbegin(), m_capabilities.cend(), [](const QString& capability) { return capability.startsWith("AUTH") && capability.contains("PLAIN"); });
    const bool supportsLogin = std::any_of(m_capabilities.cbegin(), m_capabilities.cend(), [](const QString& capability) { return capability.startsWith("AUTH") && capability.contains("LOGIN"); });

    if (supportsPlain || !supportsLogin)
    {
        const QByteArray credentials = QByteArray(1, '\0') + m_settings.userName.toUtf8() + QByteArray(1, '\0') + m_settings.password.toUtf8();
        return command("AUTH PLAIN " + credentials.toBase64(), { 235 }, "AUTH PLAIN ***");
    }

    return command("AUTH LOGIN", { 334 }) &&
           command(m_settings.userName.toUtf8().toBase64(), { 334 }, "***") &&
           command(m_settings.password.toUtf8().toBase64(), { 235 }, "***");
}

PDFOperationResult SmtpConversation::run(const QByteArray& sender, const QList<QByteArray>& recipients, const QByteArray& message)
{
    if (m_settings.security == PDFSmtpClient::Security::SslTls)
    {
        m_socket.connectToHostEncrypted(m_settings.host, quint16(m_settings.port));
        if (!m_socket.waitForEncrypted(m_settings.timeout))
        {
            return PDFTranslationContext::tr("Encrypted connection to the mail server '%1' failed: %2").arg(m_settings.host, m_socket.errorString());
        }
    }
    else
    {
        m_socket.connectToHost(m_settings.host, quint16(m_settings.port));
        if (!m_socket.waitForConnected(m_settings.timeout))
        {
            return PDFTranslationContext::tr("Connection to the mail server '%1' failed: %2").arg(m_settings.host, m_socket.errorString());
        }
    }

    // The greeting of the server
    if (readReply() != 220)
    {
        return m_error.isEmpty() ? PDFTranslationContext::tr("The mail server refused the connection: %1").arg(QString::fromUtf8(m_lastReply).trimmed()) : m_error;
    }

    if (!sayHello())
    {
        return m_error;
    }

    if (m_settings.security == PDFSmtpClient::Security::StartTls)
    {
        if (!command("STARTTLS", { 220 }))
        {
            return m_error;
        }

        m_socket.startClientEncryption();
        if (!m_socket.waitForEncrypted(m_settings.timeout))
        {
            return PDFTranslationContext::tr("Encryption of the connection to the mail server failed: %1").arg(m_socket.errorString());
        }

        // The capabilities (the authentication methods) are announced again on the encrypted connection
        if (!sayHello())
        {
            return m_error;
        }
    }

    if (!authenticate())
    {
        return m_error;
    }

    if (!command("MAIL FROM:<" + sender + ">", { 250 }))
    {
        return m_error;
    }

    for (const QByteArray& recipient : recipients)
    {
        if (!command("RCPT TO:<" + recipient + ">", { 250, 251 }))
        {
            return m_error;
        }
    }

    if (!command("DATA", { 354 }))
    {
        return m_error;
    }

    // A line beginning with a dot has the dot doubled, the single dot ends the message
    QByteArray data = message;
    if (data.startsWith('.'))
    {
        data.prepend('.');
    }
    data.replace("\r\n.", "\r\n..");
    if (!data.endsWith("\r\n"))
    {
        data += "\r\n";
    }

    m_socket.write(data);
    if (!command(".", { 250 }, "end of the message"))
    {
        return m_error;
    }

    command("QUIT", { 221 });
    m_socket.disconnectFromHost();
    return true;
}

}   // namespace

bool PDFSmtpClient::isValidAddress(const QString& address)
{
    static const QRegularExpression expression("^[^@\\s<>\"]+@[^@\\s<>\"]+\\.[^@\\s<>\"]+$");
    return expression.match(address).hasMatch();
}

QByteArray PDFSmtpClient::createMimeMessage(const Settings& settings, const Message& message)
{
    const QByteArray boundary = "PDFFire-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    const QString fromAddress = sanitizeHeaderText(settings.fromAddress);
    const QString domain = fromAddress.section(QChar('@'), 1);

    QByteArray from = fromAddress.toLatin1();
    if (!settings.fromName.trimmed().isEmpty())
    {
        from = encodeHeaderText(settings.fromName) + " <" + from + ">";
    }

    QStringList recipients;
    for (const QString& recipient : message.to)
    {
        recipients << sanitizeHeaderText(recipient);
    }

    QByteArray mime;
    mime += "From: " + from + "\r\n";
    mime += "To: " + recipients.join(", ").toLatin1() + "\r\n";
    mime += "Subject: " + encodeHeaderText(message.subject) + "\r\n";
    mime += "Date: " + QLocale::c().toString(QDateTime::currentDateTime(), "ddd, dd MMM yyyy hh:mm:ss ").toLatin1() +
            QDateTime::currentDateTime().toString("t").toLatin1().replace("UTC", "+0000") + "\r\n";
    mime += "Message-ID: <" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces) + "@" + (domain.isEmpty() ? QByteArray("pdf-fire") : domain.toLatin1()) + ">\r\n";
    mime += "MIME-Version: 1.0\r\n";
    mime += "X-Mailer: PDF Fire\r\n";
    mime += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n";
    mime += "\r\n";
    mime += "This is a message in the MIME format.\r\n";

    // The text of the message
    QString text = message.text;
    text.replace("\r\n", "\n");
    text.replace(QChar('\n'), "\r\n");
    mime += "\r\n--" + boundary + "\r\n";
    mime += "Content-Type: text/plain; charset=UTF-8\r\n";
    mime += "Content-Transfer-Encoding: base64\r\n\r\n";
    mime += base64Lines(text.toUtf8());

    // The attachment
    if (!message.attachment.isEmpty())
    {
        const QString fileName = sanitizeHeaderText(message.attachmentFileName).remove(QChar('"'));
        const QByteArray encodedFileName = encodeHeaderText(fileName.isEmpty() ? QString("document.pdf") : fileName);
        mime += "\r\n--" + boundary + "\r\n";
        mime += "Content-Type: " + message.attachmentMimeType + "; name=\"" + encodedFileName + "\"\r\n";
        mime += "Content-Disposition: attachment; filename=\"" + encodedFileName + "\"\r\n";
        mime += "Content-Transfer-Encoding: base64\r\n\r\n";
        mime += base64Lines(message.attachment);
    }

    mime += "\r\n--" + boundary + "--\r\n";
    return mime;
}

PDFOperationResult PDFSmtpClient::send(const Settings& settings, const Message& message)
{
    if (settings.host.trimmed().isEmpty())
    {
        return PDFTranslationContext::tr("The mail server is not set.");
    }

    if (!isValidAddress(settings.fromAddress))
    {
        return PDFTranslationContext::tr("The sender address '%1' is not valid.").arg(settings.fromAddress);
    }

    QList<QByteArray> recipients;
    for (const QString& recipient : message.to)
    {
        const QString address = recipient.trimmed();
        if (!isValidAddress(address))
        {
            return PDFTranslationContext::tr("The recipient address '%1' is not valid.").arg(address);
        }
        recipients << address.toLatin1();
    }

    if (recipients.isEmpty())
    {
        return PDFTranslationContext::tr("There is no recipient.");
    }

    if (settings.security != Security::None && !QSslSocket::supportsSsl())
    {
        return PDFTranslationContext::tr("Encrypted connections are not available (the TLS library is missing).");
    }

    SmtpConversation conversation(settings);
    return conversation.run(settings.fromAddress.trimmed().toLatin1(), recipients, createMimeMessage(settings, message));
}

}   // namespace pdf
