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

#ifndef PDFFIRESMTPCLIENT_H
#define PDFFIRESMTPCLIENT_H

#include "pdfglobal.h"
#include "pdfutils.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace pdf
{

/// PDF Fire: sends an e-mail with an attachment through an SMTP server - so a document
/// can be sent without a mail client installed and configured on the computer.
///
/// The client is blocking (the calls wait for the server), so it should be used from a
/// worker thread. It speaks plain SMTP, SMTP upgraded by STARTTLS, and SMTP over TLS; it
/// authenticates by AUTH PLAIN or AUTH LOGIN. It refuses to send a password over a
/// connection, which is not encrypted.
class PDF4QTLIBCORESHARED_EXPORT PDFSmtpClient
{
public:
    enum class Security
    {
        None,       ///< No encryption (only for servers inside a trusted network, without a password)
        StartTls,   ///< Plain connection upgraded by STARTTLS (usually the port 587)
        SslTls,     ///< Encrypted from the start (usually the port 465)
    };

    struct Settings
    {
        QString host;
        int port = 587;
        Security security = Security::StartTls;
        QString userName;
        QString password;
        QString fromAddress;
        QString fromName;
        int timeout = 30000;    ///< Milliseconds, for every step of the conversation

        /// Allows the password over a connection, which is not encrypted (tests only)
        bool allowUnencryptedAuthentication = false;
    };

    struct Message
    {
        QStringList to;
        QString subject;
        QString text;
        QByteArray attachment;
        QString attachmentFileName;
        QByteArray attachmentMimeType = "application/pdf";
    };

    /// Sends the message. Returns an error message, if it failed.
    static PDFOperationResult send(const Settings& settings, const Message& message);

    /// Creates the whole message (the headers and the body, with the attachment), as it
    /// is sent - but without the dot stuffing, which belongs to the transport.
    static QByteArray createMimeMessage(const Settings& settings, const Message& message);

    /// Returns true, if the text is a plausible e-mail address (without spaces and line breaks)
    static bool isValidAddress(const QString& address);
};

}   // namespace pdf

#endif // PDFFIRESMTPCLIENT_H
