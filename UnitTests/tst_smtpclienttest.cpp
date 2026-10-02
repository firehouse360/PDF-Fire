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

#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtConcurrent/QtConcurrent>

using namespace pdf;

/// A minimal SMTP server, which records what the client sends
class FakeSmtpServer : public QObject
{
public:
    explicit FakeSmtpServer(bool advertiseLogin = false) : m_advertiseLogin(advertiseLogin)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]()
        {
            m_socket = m_server.nextPendingConnection();
            m_socket->write("220 fake.example ESMTP\r\n");
            connect(m_socket, &QTcpSocket::readyRead, this, [this]() { onReadyRead(); });
        });
        m_server.listen(QHostAddress::LocalHost);
    }

    quint16 port() const { return m_server.serverPort(); }

    QStringList commands;
    QByteArray data;
    QByteArray rejectCommand;   ///< Command (its beginning), which is answered by an error

private:
    void reply(const QByteArray& line) { m_socket->write(line + "\r\n"); }

    void onReadyRead()
    {
        while (m_socket->canReadLine())
        {
            const QByteArray line = m_socket->readLine();
            if (m_isInData)
            {
                if (line == ".\r\n")
                {
                    m_isInData = false;
                    reply("250 OK queued");
                }
                else
                {
                    data += line;
                }
                continue;
            }

            const QByteArray command = line.trimmed();
            commands << QString::fromLatin1(command);

            if (!rejectCommand.isEmpty() && command.startsWith(rejectCommand))
            {
                reply("550 rejected by the test");
            }
            else if (command.startsWith("EHLO"))
            {
                reply("250-fake.example");
                reply(m_advertiseLogin ? "250-AUTH LOGIN" : "250-AUTH PLAIN LOGIN");
                reply("250 SIZE 10000000");
            }
            else if (command.startsWith("AUTH PLAIN"))
            {
                reply("235 OK");
            }
            else if (command == "AUTH LOGIN")
            {
                m_loginStep = 1;
                reply("334 VXNlcm5hbWU6");
            }
            else if (m_loginStep == 1)
            {
                m_loginStep = 2;
                reply("334 UGFzc3dvcmQ6");
            }
            else if (m_loginStep == 2)
            {
                m_loginStep = 0;
                reply("235 OK");
            }
            else if (command == "DATA")
            {
                m_isInData = true;
                reply("354 go ahead");
            }
            else if (command == "QUIT")
            {
                reply("221 bye");
            }
            else
            {
                reply("250 OK");
            }
        }
    }

    QTcpServer m_server;
    QTcpSocket* m_socket = nullptr;
    bool m_isInData = false;
    bool m_advertiseLogin = false;
    int m_loginStep = 0;
};

class SmtpClientTest : public QObject
{
    Q_OBJECT

private slots:
    void sendWithAttachment();
    void authPlain();
    void authLogin();
    void passwordIsNotSentUnencrypted();
    void refusedRecipientIsReported();
    void headerInjectionIsPrevented();
    void invalidAddresses();

private:
    static PDFOperationResult send(const PDFSmtpClient::Settings& settings, const PDFSmtpClient::Message& message)
    {
        // The client blocks, so it runs in a worker thread, while the server runs here
        QFuture<PDFOperationResult> future = QtConcurrent::run([settings, message]() { return PDFSmtpClient::send(settings, message); });
        QElapsedTimer timer;
        timer.start();
        while (!future.isFinished() && timer.elapsed() < 20000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return future.result();
    }

    static PDFSmtpClient::Settings settings(quint16 port)
    {
        PDFSmtpClient::Settings result;
        result.host = "127.0.0.1";
        result.port = port;
        result.security = PDFSmtpClient::Security::None;
        result.fromAddress = "mike@example.com";
        result.fromName = "Mike";
        result.timeout = 5000;
        return result;
    }

    static PDFSmtpClient::Message message()
    {
        PDFSmtpClient::Message result;
        result.to = QStringList{ "chief@example.com", "clerk@example.com" };
        result.subject = "SOG update";
        result.text = "Hello,\n.leading dot line\nattached.";
        result.attachmentFileName = "DEPT SOG.pdf";
        result.attachment = "%PDF-1.7\n" + QByteArray(5000, 'x') + "\n.\n%%EOF";
        return result;
    }

    /// Decodes the base64 part of the MIME message following the given header line
    static QByteArray decodePart(const QByteArray& mime, const QByteArray& marker)
    {
        const qsizetype start = mime.indexOf(marker);
        const qsizetype bodyStart = mime.indexOf("\r\n\r\n", start) + 4;
        const qsizetype bodyEnd = mime.indexOf("\r\n--", bodyStart);
        QByteArray body = mime.mid(bodyStart, bodyEnd - bodyStart);
        body.replace("\r\n", "");
        return QByteArray::fromBase64(body);
    }
};

void SmtpClientTest::sendWithAttachment()
{
    FakeSmtpServer server;
    const PDFOperationResult result = send(settings(server.port()), message());
    QVERIFY2(result, qPrintable(result.getErrorMessage()));

    QVERIFY(server.commands.contains("MAIL FROM:<mike@example.com>"));
    QVERIFY(server.commands.contains("RCPT TO:<chief@example.com>"));
    QVERIFY(server.commands.contains("RCPT TO:<clerk@example.com>"));
    QVERIFY(!server.commands.join(" ").contains("AUTH"));

    // Dot stuffing is undone by the server, so the received data are the MIME message
    QByteArray data = server.data;
    data.replace("\r\n..", "\r\n.");
    QVERIFY(data.contains("Subject: SOG update\r\n"));
    QVERIFY(data.contains("filename=\"DEPT SOG.pdf\""));
    QCOMPARE(decodePart(data, "Content-Type: application/pdf"), message().attachment);
    QCOMPARE(decodePart(data, "Content-Type: text/plain"), QByteArray("Hello,\r\n.leading dot line\r\nattached."));
}

void SmtpClientTest::authPlain()
{
    FakeSmtpServer server;
    PDFSmtpClient::Settings smtpSettings = settings(server.port());
    smtpSettings.userName = "mike";
    smtpSettings.password = "secret";
    smtpSettings.allowUnencryptedAuthentication = true;

    const PDFOperationResult result = send(smtpSettings, message());
    QVERIFY2(result, qPrintable(result.getErrorMessage()));
    QVERIFY(server.commands.contains("AUTH PLAIN " + QString::fromLatin1(QByteArray("\0mike\0secret", 12).toBase64())));
}

void SmtpClientTest::authLogin()
{
    FakeSmtpServer server(true);
    PDFSmtpClient::Settings smtpSettings = settings(server.port());
    smtpSettings.userName = "mike";
    smtpSettings.password = "secret";
    smtpSettings.allowUnencryptedAuthentication = true;

    const PDFOperationResult result = send(smtpSettings, message());
    QVERIFY2(result, qPrintable(result.getErrorMessage()));
    QVERIFY(server.commands.contains("AUTH LOGIN"));
    QVERIFY(server.commands.contains(QString::fromLatin1(QByteArray("mike").toBase64())));
    QVERIFY(server.commands.contains(QString::fromLatin1(QByteArray("secret").toBase64())));
}

void SmtpClientTest::passwordIsNotSentUnencrypted()
{
    FakeSmtpServer server;
    PDFSmtpClient::Settings smtpSettings = settings(server.port());
    smtpSettings.userName = "mike";
    smtpSettings.password = "secret";

    const PDFOperationResult result = send(smtpSettings, message());
    QVERIFY(!result);
    QVERIFY(result.getErrorMessage().contains("without encryption"));
    QVERIFY(!server.commands.join(" ").contains("AUTH"));
    QVERIFY(server.data.isEmpty());
}

void SmtpClientTest::refusedRecipientIsReported()
{
    FakeSmtpServer server;
    server.rejectCommand = "RCPT TO:<clerk@";

    const PDFOperationResult result = send(settings(server.port()), message());
    QVERIFY(!result);
    QVERIFY(result.getErrorMessage().contains("clerk@example.com"));
    QVERIFY(result.getErrorMessage().contains("550"));
    QVERIFY(server.data.isEmpty());
}

void SmtpClientTest::headerInjectionIsPrevented()
{
    PDFSmtpClient::Message evil = message();
    evil.subject = "Hello\r\nBcc: victim@example.com";
    const QByteArray mime = PDFSmtpClient::createMimeMessage(settings(25), evil);
    QVERIFY(!mime.contains("\r\nBcc:"));
    QVERIFY(mime.contains("Subject: Hello  Bcc: victim@example.com\r\n"));

    PDFSmtpClient::Message unicode = message();
    unicode.subject = QString::fromUtf8("Příloha – SOG");
    const QByteArray unicodeMime = PDFSmtpClient::createMimeMessage(settings(25), unicode);
    QVERIFY(unicodeMime.contains("Subject: =?UTF-8?B?" + QString::fromUtf8("Příloha – SOG").toUtf8().toBase64() + "?=\r\n"));
}

void SmtpClientTest::invalidAddresses()
{
    QVERIFY(PDFSmtpClient::isValidAddress("mike@example.com"));
    QVERIFY(!PDFSmtpClient::isValidAddress("mike"));
    QVERIFY(!PDFSmtpClient::isValidAddress("mike@example"));
    QVERIFY(!PDFSmtpClient::isValidAddress("mike smith@example.com"));
    QVERIFY(!PDFSmtpClient::isValidAddress("a@b.com>\r\nRCPT TO:<x@y.com"));

    PDFSmtpClient::Message bad = message();
    bad.to = QStringList{ "not an address" };
    QVERIFY(!PDFSmtpClient::send(settings(25), bad));

    PDFSmtpClient::Settings noServer = settings(25);
    noServer.host.clear();
    QVERIFY(!PDFSmtpClient::send(noServer, message()));
}

QTEST_GUILESS_MAIN(SmtpClientTest)

#include "tst_smtpclienttest.moc"
