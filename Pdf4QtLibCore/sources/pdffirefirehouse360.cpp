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

#include "pdffirefirehouse360.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <openssl/evp.h>
#include <openssl/rand.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace pdf
{

namespace
{

constexpr const char* SETTINGS_GROUP = "Firehouse360";
constexpr const char* BLOB_FORMAT = "pdffire-encrypted-certificate";
constexpr const char* BLOB_AAD = "pdffire-encrypted-certificate|v1";
constexpr const char* CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr int REQUEST_TIMEOUT = 20000;

QString settingsKey(const char* name)
{
    return QString::fromLatin1(SETTINGS_GROUP) + QLatin1Char('/') + QString::fromLatin1(name);
}

QString dataDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("firehouse360"));
}

/// PBKDF2-HMAC-SHA256 -> 32 byte key
QByteArray deriveKey(const QString& passphrase, const QByteArray& salt, int iterations)
{
    const QByteArray password = passphrase.toUtf8();
    QByteArray key(32, Qt::Uninitialized);
    if (PKCS5_PBKDF2_HMAC(password.constData(), int(password.size()), reinterpret_cast<const unsigned char*>(salt.constData()), int(salt.size()),
                          iterations, EVP_sha256(), int(key.size()), reinterpret_cast<unsigned char*>(key.data())) != 1)
    {
        return QByteArray();
    }
    return key;
}

/// AES-256-GCM, the 16 byte tag appended to the cipher text
bool aesGcm(bool encrypt, const QByteArray& key, const QByteArray& nonce, const QByteArray& input, QByteArray* output)
{
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (!context)
    {
        return false;
    }

    const QByteArray aad(BLOB_AAD);
    QByteArray data = input;
    QByteArray tag;
    if (!encrypt)
    {
        if (data.size() < 16)
        {
            EVP_CIPHER_CTX_free(context);
            return false;
        }
        tag = data.right(16);
        data.chop(16);
    }

    bool isOk = (encrypt ? EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)
                         : EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)) == 1 &&
                EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, int(nonce.size()), nullptr) == 1 &&
                (encrypt ? EVP_EncryptInit_ex(context, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()), reinterpret_cast<const unsigned char*>(nonce.constData()))
                         : EVP_DecryptInit_ex(context, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()), reinterpret_cast<const unsigned char*>(nonce.constData()))) == 1;

    int length = 0;
    if (isOk)
    {
        isOk = (encrypt ? EVP_EncryptUpdate(context, nullptr, &length, reinterpret_cast<const unsigned char*>(aad.constData()), int(aad.size()))
                        : EVP_DecryptUpdate(context, nullptr, &length, reinterpret_cast<const unsigned char*>(aad.constData()), int(aad.size()))) == 1;
    }

    QByteArray result(data.size() + 16, Qt::Uninitialized);
    int total = 0;
    if (isOk)
    {
        isOk = (encrypt ? EVP_EncryptUpdate(context, reinterpret_cast<unsigned char*>(result.data()), &length, reinterpret_cast<const unsigned char*>(data.constData()), int(data.size()))
                        : EVP_DecryptUpdate(context, reinterpret_cast<unsigned char*>(result.data()), &length, reinterpret_cast<const unsigned char*>(data.constData()), int(data.size()))) == 1;
        total = length;
    }

    if (isOk && !encrypt)
    {
        isOk = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) == 1;
    }

    if (isOk)
    {
        isOk = (encrypt ? EVP_EncryptFinal_ex(context, reinterpret_cast<unsigned char*>(result.data()) + total, &length)
                        : EVP_DecryptFinal_ex(context, reinterpret_cast<unsigned char*>(result.data()) + total, &length)) == 1;
        total += length;
    }

    if (isOk && encrypt)
    {
        QByteArray outTag(16, Qt::Uninitialized);
        isOk = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, 16, outTag.data()) == 1;
        result.resize(total);
        result.append(outTag);
    }
    else
    {
        result.resize(total);
    }

    EVP_CIPHER_CTX_free(context);
    if (isOk)
    {
        *output = result;
    }
    return isOk;
}

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), count) != 1)
    {
        return QByteArray();
    }
    return bytes;
}

}   // namespace

QString PDFFireFirehouse360::getServerUrl()
{
    const QString overridden = qEnvironmentVariable("PDFFIRE_FH360_URL").trimmed();
    QString url = overridden.isEmpty() ? QString::fromLatin1(DEFAULT_SERVER) : overridden;
    while (url.endsWith(QLatin1Char('/')))
    {
        url.chop(1);
    }
    return url;
}

bool PDFFireFirehouse360::isEnabled()
{
    return QSettings().value(settingsKey("Enabled"), false).toBool();
}

void PDFFireFirehouse360::setEnabled(bool enabled)
{
    QSettings().setValue(settingsKey("Enabled"), enabled);
}

bool PDFFireFirehouse360::isConnected()
{
    return isEnabled() && !getToken().isEmpty();
}

QString PDFFireFirehouse360::getDepartmentName()
{
    return QSettings().value(settingsKey("Department")).toString();
}

QString PDFFireFirehouse360::getDepartmentSlug()
{
    return QSettings().value(settingsKey("DepartmentSlug")).toString();
}

QString PDFFireFirehouse360::getMemberName()
{
    return QSettings().value(settingsKey("Member")).toString();
}

QString PDFFireFirehouse360::getSettingsUrl()
{
    return getServerUrl() + QStringLiteral("/secure-share/settings#pdffire");
}

QString PDFFireFirehouse360::newVerifyCode()
{
    // 60 random bits = 12 characters of 5 bits
    quint64 bits = QRandomGenerator::system()->generate64();
    QString code = QStringLiteral("PF-");
    for (int i = 0; i < 12; ++i)
    {
        if (i > 0 && i % 4 == 0)
        {
            code += QLatin1Char('-');
        }
        code += QLatin1Char(CROCKFORD[bits & 31]);
        bits >>= 5;
    }
    return code;
}

bool PDFFireFirehouse360::isValidVerifyCode(const QString& code)
{
    static const QRegularExpression expression(QStringLiteral("^PF-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}$"));
    return expression.match(code).hasMatch();
}

QString PDFFireFirehouse360::getVerifyUrl(const QString& code)
{
    return getServerUrl() + QStringLiteral("/verify/") + code;
}

QString PDFFireFirehouse360::getVerifyDisplayUrl(const QString& code)
{
    QString url = getVerifyUrl(code);
    url.remove(QRegularExpression(QStringLiteral("^https?://")));
    return url;
}

PDFFireFirehouse360::Response PDFFireFirehouse360::pair(const QString& code, const QString& deviceName)
{
    QJsonObject body;
    body[QStringLiteral("code")] = code.trimmed();
    body[QStringLiteral("device_name")] = deviceName.left(80);
    Response response = request("POST", QStringLiteral("/secure-share/api/pdffire/pair"), &body, REQUEST_TIMEOUT);
    if (response.isOk())
    {
        const QString token = response.json.value(QStringLiteral("token")).toString();
        if (token.isEmpty() || !setToken(token))
        {
            response.error = QCoreApplication::translate("PDFFireFirehouse360", "The connection could not be saved on this computer.");
            return response;
        }
        setEnabled(true);
        storeStatus(response.json);
    }
    return response;
}

PDFFireFirehouse360::Response PDFFireFirehouse360::fetchStatus()
{
    Response response = request("GET", QStringLiteral("/secure-share/api/pdffire/status"), nullptr, REQUEST_TIMEOUT);
    if (response.isOk())
    {
        storeStatus(response.json);
    }
    return response;
}

PDFFireFirehouse360::Response PDFFireFirehouse360::disconnect()
{
    Response response;
    if (!getToken().isEmpty())
    {
        response = request("POST", QStringLiteral("/secure-share/api/pdffire/disconnect"), nullptr, 8000);
    }
    forget();
    setEnabled(false);
    return response;
}

void PDFFireFirehouse360::forget()
{
    QFile::remove(getTokenFileName());
}

void PDFFireFirehouse360::enqueue(const QueueItem& item)
{
    QList<QueueItem> items = readQueue();
    for (const QueueItem& existing : items)
    {
        if (existing.verifyCode == item.verifyCode)
        {
            return;
        }
    }
    items.append(item);
    writeQueue(items);
}

int PDFFireFirehouse360::getPendingCount()
{
    return int(readQueue().size());
}

PDFFireFirehouse360::FlushResult PDFFireFirehouse360::flushQueue()
{
    FlushResult result;
    QList<QueueItem> items = readQueue();
    result.pending = int(items.size());
    if (items.isEmpty() || !isConnected())
    {
        return result;
    }

    QList<QueueItem> remaining;
    for (qsizetype start = 0; start < items.size(); start += 20)
    {
        const QList<QueueItem> batch = items.mid(start, 20);
        QJsonArray array;
        for (const QueueItem& item : batch)
        {
            QJsonObject object;
            object[QStringLiteral("verify_code")] = item.verifyCode;
            object[QStringLiteral("cms")] = QString::fromLatin1(item.cms.toBase64());
            object[QStringLiteral("signed_at")] = item.signedAt.toUTC().toString(Qt::ISODate);
            object[QStringLiteral("file_name")] = item.fileName;
            array.append(object);
        }
        QJsonObject body;
        body[QStringLiteral("items")] = array;

        const Response response = request("POST", QStringLiteral("/secure-share/api/pdffire/signatures"), &body, REQUEST_TIMEOUT * 2);
        if (!response.isOk())
        {
            // Not reached (offline), or refused as a whole - everything stays queued
            remaining.append(items.mid(start));
            if (response.httpStatus > 0)
            {
                result.reachedServer = true;
                result.messages << response.error;
            }
            break;
        }

        result.reachedServer = true;
        QStringList handled;
        for (const QJsonValue& value : response.json.value(QStringLiteral("results")).toArray())
        {
            const QJsonObject object = value.toObject();
            const QString code = object.value(QStringLiteral("verify_code")).toString();
            const QString status = object.value(QStringLiteral("status")).toString();
            handled << code;
            if (status == QLatin1String("registered") || status == QLatin1String("duplicate"))
            {
                ++result.registered;
            }
            else
            {
                ++result.rejected;
                result.messages << QStringLiteral("%1: %2").arg(code, object.value(QStringLiteral("message")).toString());
            }
        }
        for (const QueueItem& item : batch)
        {
            if (!handled.contains(item.verifyCode))
            {
                remaining.append(item);
            }
        }
    }

    writeQueue(remaining);
    result.pending = int(remaining.size());
    return result;
}

PDFFireFirehouse360::Response PDFFireFirehouse360::saveCertificateOnline(const QByteArray& pkcs12, const QString& label, const QString& fileName, const QString& passphrase)
{
    Response response;
    if (passphrase.size() < MINIMAL_PASSPHRASE_LENGTH)
    {
        response.error = QCoreApplication::translate("PDFFireFirehouse360", "The passphrase must have at least %1 characters.").arg(MINIMAL_PASSPHRASE_LENGTH);
        return response;
    }

    const QByteArray blob = encryptCertificate(pkcs12, passphrase, fileName);
    if (blob.isEmpty())
    {
        response.error = QCoreApplication::translate("PDFFireFirehouse360", "The certificate could not be encrypted.");
        return response;
    }

    QJsonObject body;
    body[QStringLiteral("blob")] = QString::fromUtf8(blob);
    body[QStringLiteral("label")] = label.left(190);
    return request("POST", QStringLiteral("/secure-share/api/pdffire/key"), &body, REQUEST_TIMEOUT);
}

PDFFireFirehouse360::Response PDFFireFirehouse360::fetchCertificateOnline(const QString& passphrase, QByteArray* pkcs12, QString* fileName)
{
    Response response = request("GET", QStringLiteral("/secure-share/api/pdffire/key"), nullptr, REQUEST_TIMEOUT);
    if (response.isOk())
    {
        QString errorMessage;
        if (!decryptCertificate(response.json.value(QStringLiteral("blob")).toString().toUtf8(), passphrase, pkcs12, fileName, &errorMessage))
        {
            response.error = errorMessage;
        }
    }
    return response;
}

PDFFireFirehouse360::Response PDFFireFirehouse360::deleteCertificateOnline()
{
    return request("POST", QStringLiteral("/secure-share/api/pdffire/key/delete"), nullptr, REQUEST_TIMEOUT);
}

QByteArray PDFFireFirehouse360::encryptCertificate(const QByteArray& pkcs12, const QString& passphrase, const QString& fileName, int iterations)
{
    const QByteArray salt = randomBytes(16);
    const QByteArray nonce = randomBytes(12);
    if (salt.isEmpty() || nonce.isEmpty() || pkcs12.isEmpty())
    {
        return QByteArray();
    }

    const QByteArray key = deriveKey(passphrase, salt, iterations);
    QByteArray ciphertext;
    if (key.isEmpty() || !aesGcm(true, key, nonce, pkcs12, &ciphertext))
    {
        return QByteArray();
    }

    QJsonObject object;
    object[QStringLiteral("format")] = QString::fromLatin1(BLOB_FORMAT);
    object[QStringLiteral("version")] = 1;
    object[QStringLiteral("kdf")] = QStringLiteral("pbkdf2-sha256");
    object[QStringLiteral("iterations")] = iterations;
    object[QStringLiteral("cipher")] = QStringLiteral("aes-256-gcm");
    object[QStringLiteral("salt")] = QString::fromLatin1(salt.toBase64());
    object[QStringLiteral("nonce")] = QString::fromLatin1(nonce.toBase64());
    object[QStringLiteral("ciphertext")] = QString::fromLatin1(ciphertext.toBase64());
    object[QStringLiteral("file_name")] = QFileInfo(fileName).fileName();
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool PDFFireFirehouse360::decryptCertificate(const QByteArray& blob, const QString& passphrase, QByteArray* pkcs12, QString* fileName, QString* errorMessage)
{
    const QJsonObject object = QJsonDocument::fromJson(blob).object();
    const int iterations = object.value(QStringLiteral("iterations")).toInt();
    if (object.value(QStringLiteral("format")).toString() != QLatin1String(BLOB_FORMAT) || object.value(QStringLiteral("version")).toInt() != 1 ||
        object.value(QStringLiteral("kdf")).toString() != QLatin1String("pbkdf2-sha256") || iterations < 100000 || iterations > 10000000)
    {
        *errorMessage = QCoreApplication::translate("PDFFireFirehouse360", "The online certificate is in an unknown format.");
        return false;
    }

    const QByteArray salt = QByteArray::fromBase64(object.value(QStringLiteral("salt")).toString().toLatin1());
    const QByteArray nonce = QByteArray::fromBase64(object.value(QStringLiteral("nonce")).toString().toLatin1());
    const QByteArray ciphertext = QByteArray::fromBase64(object.value(QStringLiteral("ciphertext")).toString().toLatin1());
    const QByteArray key = deriveKey(passphrase, salt, iterations);
    QByteArray plain;
    if (key.isEmpty() || nonce.size() != 12 || !aesGcm(false, key, nonce, ciphertext, &plain))
    {
        *errorMessage = QCoreApplication::translate("PDFFireFirehouse360", "The passphrase is wrong.");
        return false;
    }

    *pkcs12 = plain;
    if (fileName)
    {
        *fileName = QFileInfo(object.value(QStringLiteral("file_name")).toString()).fileName();
    }
    return true;
}

PDFFireFirehouse360::Response PDFFireFirehouse360::download(const QString& url, QByteArray* data)
{
    Response response;
    // Only files of the server itself (the addresses come from its status answer)
    if (!url.startsWith(getServerUrl() + QLatin1Char('/')))
    {
        response.error = QCoreApplication::translate("PDFFireFirehouse360", "Unexpected address: %1").arg(url);
        return response;
    }

    QNetworkAccessManager manager;
    QNetworkRequest networkRequest{ QUrl(url) };
    networkRequest.setTransferTimeout(REQUEST_TIMEOUT);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(networkRequest);
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);

    response.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    *data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || response.httpStatus != 200)
    {
        response.error = QCoreApplication::translate("PDFFireFirehouse360", "%1 could not be downloaded (%2).").arg(url, reply->errorString());
        data->clear();
    }
    reply->deleteLater();
    return response;
}

PDFFireFirehouse360::Response PDFFireFirehouse360::request(const QByteArray& method, const QString& path, const QJsonObject* body, int timeoutMilliseconds)
{
    Response response;
    const QString token = getToken();

    QNetworkAccessManager manager;
    QNetworkRequest networkRequest{ QUrl(getServerUrl() + path) };
    networkRequest.setTransferTimeout(timeoutMilliseconds);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Accept", "application/json");
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PDF Fire/%1").arg(QCoreApplication::applicationVersion()));
    if (!token.isEmpty())
    {
        // Not "Authorization": the site's web server does not pass that one through
        networkRequest.setRawHeader("X-PDFFire-Token", token.toLatin1());
    }

    const QByteArray payload = body ? QJsonDocument(*body).toJson(QJsonDocument::Compact) : QByteArray();
    QNetworkReply* reply = (method == "GET") ? manager.get(networkRequest) : manager.post(networkRequest, payload);
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);

    response.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray answer = reply->readAll();
    response.json = QJsonDocument::fromJson(answer).object();

    if (response.httpStatus == 0)
    {
        response.error = QCoreApplication::translate("PDFFireFirehouse360", "Firehouse 360 could not be reached (%1). Signatures wait on this computer and are sent later.").arg(reply->errorString());
    }
    else if (response.httpStatus < 200 || response.httpStatus >= 300)
    {
        response.error = response.json.value(QStringLiteral("error")).toString();
        if (response.error.isEmpty())
        {
            response.error = QCoreApplication::translate("PDFFireFirehouse360", "Firehouse 360 answered with an error (%1).").arg(response.httpStatus);
        }
        // The computer was disconnected on the website - the token is useless now
        if (response.httpStatus == 401 && response.json.value(QStringLiteral("code")).toString() == QLatin1String("not_connected"))
        {
            forget();
        }
    }
    reply->deleteLater();
    return response;
}

QString PDFFireFirehouse360::getTokenFileName()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("token"));
}

QString PDFFireFirehouse360::getQueueFileName()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("signatures-to-register.json"));
}

QString PDFFireFirehouse360::getToken()
{
    QFile file(getTokenFileName());
    if (!file.open(QFile::ReadOnly))
    {
        return QString();
    }
    QByteArray data = file.readAll();
    file.close();

#ifdef Q_OS_WIN
    DATA_BLOB input{ DWORD(data.size()), reinterpret_cast<BYTE*>(data.data()) };
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return QString();
    }
    data = QByteArray(reinterpret_cast<const char*>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
#endif

    return QString::fromLatin1(data).trimmed();
}

bool PDFFireFirehouse360::setToken(const QString& token)
{
    QDir().mkpath(dataDirectory());
    QByteArray data = token.toLatin1();

#ifdef Q_OS_WIN
    DATA_BLOB input{ DWORD(data.size()), reinterpret_cast<BYTE*>(data.data()) };
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"PDF Fire - Firehouse 360", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return false;
    }
    data = QByteArray(reinterpret_cast<const char*>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
#endif

    QSaveFile file(getTokenFileName());
    if (!file.open(QFile::WriteOnly))
    {
        return false;
    }
    // Only the user can read it (set before anything is written)
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(data);
    return file.commit();
}

QList<PDFFireFirehouse360::QueueItem> PDFFireFirehouse360::readQueue()
{
    QList<QueueItem> items;
    QFile file(getQueueFileName());
    if (!file.open(QFile::ReadOnly))
    {
        return items;
    }

    for (const QJsonValue& value : QJsonDocument::fromJson(file.readAll()).array())
    {
        const QJsonObject object = value.toObject();
        QueueItem item;
        item.verifyCode = object.value(QStringLiteral("verify_code")).toString();
        item.cms = QByteArray::fromBase64(object.value(QStringLiteral("cms")).toString().toLatin1());
        item.signedAt = QDateTime::fromString(object.value(QStringLiteral("signed_at")).toString(), Qt::ISODate);
        item.fileName = object.value(QStringLiteral("file_name")).toString();
        if (isValidVerifyCode(item.verifyCode) && !item.cms.isEmpty())
        {
            items.append(item);
        }
    }
    return items;
}

void PDFFireFirehouse360::writeQueue(const QList<QueueItem>& items)
{
    QDir().mkpath(dataDirectory());
    if (items.isEmpty())
    {
        QFile::remove(getQueueFileName());
        return;
    }

    QJsonArray array;
    for (const QueueItem& item : items)
    {
        QJsonObject object;
        object[QStringLiteral("verify_code")] = item.verifyCode;
        object[QStringLiteral("cms")] = QString::fromLatin1(item.cms.toBase64());
        object[QStringLiteral("signed_at")] = item.signedAt.toUTC().toString(Qt::ISODate);
        object[QStringLiteral("file_name")] = item.fileName;
        array.append(object);
    }

    QSaveFile file(getQueueFileName());
    if (file.open(QFile::WriteOnly))
    {
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void PDFFireFirehouse360::storeStatus(const QJsonObject& status)
{
    QSettings settings;
    const QJsonObject department = status.value(QStringLiteral("department")).toObject();
    settings.setValue(settingsKey("Department"), department.value(QStringLiteral("name")).toString());
    settings.setValue(settingsKey("DepartmentSlug"), department.value(QStringLiteral("slug")).toString());
    settings.setValue(settingsKey("Member"), status.value(QStringLiteral("member")).toObject().value(QStringLiteral("name")).toString());
}

}   // namespace pdf
