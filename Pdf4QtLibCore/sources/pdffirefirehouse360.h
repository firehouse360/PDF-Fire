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

#ifndef PDFFIREFIREHOUSE360_H
#define PDFFIREFIREHOUSE360_H

#include "pdfglobal.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace pdf
{

/// PDF Fire: the optional connection to a department's Firehouse 360 account
/// (Secure Share). It is OFF unless the user turns it on - while it is off,
/// nothing here goes online.
///
/// When connected:
///  - every certificate signature gets a verify code (shown on the signature with
///    a QR code), and the signature blob (/Contents - never the document) is sent
///    to Firehouse 360, which proves it and lets anybody check the signed PDF at
///    https://firehouse360.com/verify. Signatures made offline wait in a queue.
///  - the department's authority certificate and revocation list are downloaded,
///    so the department's signatures are trusted on this computer.
///  - a member can keep an encrypted copy of a certificate online (encrypted here,
///    with a passphrase the server never sees).
///
/// The device token is kept in a file only the user can read (Windows: protected
/// with DPAPI). It can only register signatures - which the server proves anyway -
/// and fetch the member's passphrase-encrypted certificate.
class PDF4QTLIBCORESHARED_EXPORT PDFFireFirehouse360
{
public:
    /// Answer of the server
    struct Response
    {
        int httpStatus = 0;     ///< 0 = no answer (no connection)
        QJsonObject json;
        QString error;          ///< Message for the user (empty = OK)

        bool isOk() const { return httpStatus >= 200 && httpStatus < 300 && error.isEmpty(); }
    };

    /// A signature waiting to be registered
    struct QueueItem
    {
        QString verifyCode;
        QByteArray cms;         ///< The signature (/Contents), DER
        QDateTime signedAt;
        QString fileName;       ///< Name of the signed file (without the folder)
    };

    struct FlushResult
    {
        bool reachedServer = false;
        int registered = 0;
        int rejected = 0;
        int pending = 0;
        QStringList messages;   ///< Why signatures were refused
    };

    static constexpr const char* DEFAULT_SERVER = "https://firehouse360.com";
    static constexpr int MINIMAL_PASSPHRASE_LENGTH = 12;
    static constexpr int KDF_ITERATIONS = 600000;

    /// Address of the server (PDFFIRE_FH360_URL overrides it - for testing only)
    static QString getServerUrl();

    /// The switch "Connect to my department" (off by default)
    static bool isEnabled();
    static void setEnabled(bool enabled);

    /// Switched on and paired
    static bool isConnected();

    /// Cached from the last status answer
    static QString getDepartmentName();
    static QString getDepartmentSlug();
    static QString getMemberName();
    static QString getSettingsUrl();

    /// New verify code PF-XXXX-XXXX-XXXX (60 random bits, Crockford base32)
    static QString newVerifyCode();
    static bool isValidVerifyCode(const QString& code);

    /// https://firehouse360.com/verify/<code>
    static QString getVerifyUrl(const QString& code);

    /// The same without "https://" - the short form printed on a signature
    static QString getVerifyDisplayUrl(const QString& code);

    /// Exchanges the one-time code from Firehouse 360 for the device token
    static Response pair(const QString& code, const QString& deviceName);

    /// Department, member, authority... (also refreshes the cached names)
    static Response fetchStatus();

    /// Disconnects this computer (on the server too, when it can be reached)
    static Response disconnect();

    /// Forgets the token here only
    static void forget();

    /// Adds a signature to the queue (also when offline)
    static void enqueue(const QueueItem& item);
    static int getPendingCount();

    /// Sends the queued signatures (does nothing when not connected)
    static FlushResult flushQueue();

    /// Encrypted copy of a certificate (.pfx) online
    static Response saveCertificateOnline(const QByteArray& pkcs12, const QString& label, const QString& fileName, const QString& passphrase);
    static Response fetchCertificateOnline(const QString& passphrase, QByteArray* pkcs12, QString* fileName);
    static Response deleteCertificateOnline();

    /// The encryption itself: PBKDF2-HMAC-SHA256 + AES-256-GCM, as JSON
    static QByteArray encryptCertificate(const QByteArray& pkcs12, const QString& passphrase, const QString& fileName, int iterations = KDF_ITERATIONS);
    static bool decryptCertificate(const QByteArray& blob, const QString& passphrase, QByteArray* pkcs12, QString* fileName, QString* errorMessage);

    /// Downloads a public file (the department's authority.cer / authority.crl)
    static Response download(const QString& url, QByteArray* data);

private:
    static Response request(const QByteArray& method, const QString& path, const QJsonObject* body, int timeoutMilliseconds);
    static QString getToken();
    static bool setToken(const QString& token);
    static QString getTokenFileName();
    static QString getQueueFileName();
    static QList<QueueItem> readQueue();
    static void writeQueue(const QList<QueueItem>& items);
    static void storeStatus(const QJsonObject& status);
};

}   // namespace pdf

#endif // PDFFIREFIREHOUSE360_H
