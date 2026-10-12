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

// PDF Fire: the optional Firehouse 360 connection (PDFFireFirehouse360).
// The end-to-end part runs only against a test server: PDFFIRE_FH360_URL (the
// Secure Share harness, php -S), PDFFIRE_FH360_TEST_CODE (a fresh pairing code) and
// PDFFIRE_FH360_TEST_CMS (a signature by a member of that department).

#include "pdffirefirehouse360.h"
#include "pdffirecertificateauthority.h"

#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

class Firehouse360Test : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void verifyCodes();
    void verifyUrls();
    void offByDefault();
    void certificateEncryption();
    void certificateTampering();
    void queuePersistence();
    void departmentCertificates();
    void serverEndToEnd();
};

void Firehouse360Test::initTestCase()
{
    // Settings and data in a test location - never the user's own
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("PDFFireTest"));
    QCoreApplication::setApplicationName(QStringLiteral("Firehouse360Test"));
    QSettings().clear();
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
}

void Firehouse360Test::verifyCodes()
{
    QSet<QString> codes;
    for (int i = 0; i < 2000; ++i)
    {
        const QString code = pdf::PDFFireFirehouse360::newVerifyCode();
        QVERIFY2(pdf::PDFFireFirehouse360::isValidVerifyCode(code), qPrintable(code));
        QVERIFY(!code.contains(QLatin1Char('I')) && !code.contains(QLatin1Char('L')) && !code.contains(QLatin1Char('O')) && !code.contains(QLatin1Char('U')));
        codes.insert(code);
    }
    QCOMPARE(codes.size(), 2000);

    QVERIFY(!pdf::PDFFireFirehouse360::isValidVerifyCode(QStringLiteral("PF-7KQ2-M9XD-4TLA")));   // L is not Crockford
    QVERIFY(!pdf::PDFFireFirehouse360::isValidVerifyCode(QStringLiteral("pf-7kq2-m9xd-4tja")));
    QVERIFY(!pdf::PDFFireFirehouse360::isValidVerifyCode(QStringLiteral("PF-7KQ2-M9XD")));
    QVERIFY(pdf::PDFFireFirehouse360::isValidVerifyCode(QStringLiteral("PF-7KQ2-M9XD-4TJA")));
}

void Firehouse360Test::verifyUrls()
{
    const QByteArray testServer = qgetenv("PDFFIRE_FH360_URL");
    qunsetenv("PDFFIRE_FH360_URL");
    QCOMPARE(pdf::PDFFireFirehouse360::getVerifyUrl(QStringLiteral("PF-7KQ2-M9XD-4TJA")), QStringLiteral("https://firehouse360.com/verify/PF-7KQ2-M9XD-4TJA"));
    QCOMPARE(pdf::PDFFireFirehouse360::getVerifyDisplayUrl(QStringLiteral("PF-7KQ2-M9XD-4TJA")), QStringLiteral("firehouse360.com/verify/PF-7KQ2-M9XD-4TJA"));
    QCOMPARE(pdf::PDFFireFirehouse360::getSettingsUrl(), QStringLiteral("https://firehouse360.com/secure-share/settings#pdffire"));
    if (!testServer.isEmpty())
    {
        qputenv("PDFFIRE_FH360_URL", testServer);
    }
}

void Firehouse360Test::offByDefault()
{
    QVERIFY(!pdf::PDFFireFirehouse360::isEnabled());
    QVERIFY(!pdf::PDFFireFirehouse360::isConnected());

    // Not connected: nothing is sent (no network at all), the queue stays
    const pdf::PDFFireFirehouse360::FlushResult result = pdf::PDFFireFirehouse360::flushQueue();
    QVERIFY(!result.reachedServer);
    QCOMPARE(result.registered, 0);
}

void Firehouse360Test::certificateEncryption()
{
    const QByteArray pkcs12 = QByteArray("not a real pfx - any bytes are encrypted the same way").repeated(40);
    const QString passphrase = QStringLiteral("correct horse battery staple");
    const QByteArray blob = pdf::PDFFireFirehouse360::encryptCertificate(pkcs12, passphrase, QStringLiteral("/home/x/Jane Doe.pfx"), 100000);
    QVERIFY(!blob.isEmpty());
    QVERIFY(!blob.contains("not a real pfx"));

    const QJsonObject object = QJsonDocument::fromJson(blob).object();
    QCOMPARE(object.value(QStringLiteral("format")).toString(), QStringLiteral("pdffire-encrypted-certificate"));
    QCOMPARE(object.value(QStringLiteral("file_name")).toString(), QStringLiteral("Jane Doe.pfx"));   // no folder sent

    QByteArray decrypted;
    QString fileName;
    QString errorMessage;
    QVERIFY(pdf::PDFFireFirehouse360::decryptCertificate(blob, passphrase, &decrypted, &fileName, &errorMessage));
    QCOMPARE(decrypted, pkcs12);
    QCOMPARE(fileName, QStringLiteral("Jane Doe.pfx"));

    QVERIFY(!pdf::PDFFireFirehouse360::decryptCertificate(blob, QStringLiteral("correct horse battery stapler"), &decrypted, &fileName, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("passphrase")));

    // Each encryption is different (new salt and nonce)
    QVERIFY(pdf::PDFFireFirehouse360::encryptCertificate(pkcs12, passphrase, QString(), 100000) != pdf::PDFFireFirehouse360::encryptCertificate(pkcs12, passphrase, QString(), 100000));
}

void Firehouse360Test::certificateTampering()
{
    const QByteArray pkcs12("certificate bytes");
    const QString passphrase = QStringLiteral("a long enough passphrase");
    QJsonObject object = QJsonDocument::fromJson(pdf::PDFFireFirehouse360::encryptCertificate(pkcs12, passphrase, QString(), 100000)).object();

    QByteArray ciphertext = QByteArray::fromBase64(object.value(QStringLiteral("ciphertext")).toString().toLatin1());
    ciphertext[0] = char(ciphertext[0] ^ 0x01);
    QJsonObject tampered = object;
    tampered[QStringLiteral("ciphertext")] = QString::fromLatin1(ciphertext.toBase64());

    QByteArray decrypted;
    QString errorMessage;
    QVERIFY(!pdf::PDFFireFirehouse360::decryptCertificate(QJsonDocument(tampered).toJson(), passphrase, &decrypted, nullptr, &errorMessage));

    QJsonObject weak = object;
    weak[QStringLiteral("iterations")] = 1000;    // a server can't make the key derivation cheap
    QVERIFY(!pdf::PDFFireFirehouse360::decryptCertificate(QJsonDocument(weak).toJson(), passphrase, &decrypted, nullptr, &errorMessage));
    QVERIFY(!pdf::PDFFireFirehouse360::decryptCertificate("not json", passphrase, &decrypted, nullptr, &errorMessage));
}

void Firehouse360Test::queuePersistence()
{
    pdf::PDFFireFirehouse360::QueueItem item;
    item.verifyCode = QStringLiteral("PF-7KQ2-M9XD-4TJA");
    item.cms = QByteArray("\x30\x82\x01\x00 signature", 14);
    item.signedAt = QDateTime::currentDateTimeUtc();
    item.fileName = QStringLiteral("Letter_SIGNED.pdf");

    const int before = pdf::PDFFireFirehouse360::getPendingCount();
    pdf::PDFFireFirehouse360::enqueue(item);
    pdf::PDFFireFirehouse360::enqueue(item);   // the same code only once
    QCOMPARE(pdf::PDFFireFirehouse360::getPendingCount(), before + 1);

    pdf::PDFFireFirehouse360::QueueItem invalid = item;
    invalid.verifyCode = QStringLiteral("nonsense");
    pdf::PDFFireFirehouse360::enqueue(invalid);   // dropped when read back
    QCOMPARE(pdf::PDFFireFirehouse360::getPendingCount(), before + 1);
}

void Firehouse360Test::departmentCertificates()
{
    // Two real department authorities, made the way PDF Fire makes them
    QTemporaryDir directoryA;
    QTemporaryDir directoryB;
    QVERIFY(directoryA.isValid() && directoryB.isValid());
    pdf::PDFFireCertificateAuthority authorityA;
    pdf::PDFFireCertificateAuthority authorityB;
    QString errorMessage;
    pdf::PDFFireCertificateAuthority::AuthorityInfo infoA;
    infoA.name = QStringLiteral("Name VFD Signing Authority");
    infoA.organization = QStringLiteral("Name VFD");
    pdf::PDFFireCertificateAuthority::AuthorityInfo infoB = infoA;   // the same names on purpose - only the key counts
    QVERIFY2(authorityA.create(directoryA.filePath(QStringLiteral("a")), infoA, QStringLiteral("authority passphrase A"), &errorMessage), qPrintable(errorMessage));
    QVERIFY2(authorityB.create(directoryB.filePath(QStringLiteral("b")), infoB, QStringLiteral("authority passphrase B"), &errorMessage), qPrintable(errorMessage));

    pdf::PDFFireCertificateAuthority::MemberInfo member;
    member.name = QStringLiteral("Jane Doe");
    QByteArray pkcs12;
    pdf::PDFFireCertificateAuthority::IssuedCertificate issued;
    QVERIFY2(authorityA.issue(member, QStringLiteral("member password"), &pkcs12, &issued, &errorMessage), qPrintable(errorMessage));
    const QByteArray memberCertificate = pdf::PDFFireCertificateAuthority::readPublicCertificate(pkcs12);
    QVERIFY(!memberCertificate.isEmpty());

    QVERIFY(pdf::PDFFireCertificateAuthority::isIssuedBy(memberCertificate, authorityA.getCertificate()));
    QVERIFY(!pdf::PDFFireCertificateAuthority::isIssuedBy(memberCertificate, authorityB.getCertificate()));   // same name, other key
    QVERIFY(!pdf::PDFFireCertificateAuthority::isIssuedBy(authorityA.getCertificate(), authorityA.getCertificate()));
    QVERIFY(!pdf::PDFFireCertificateAuthority::isIssuedBy(QByteArray("junk"), authorityA.getCertificate()));

    // The Firehouse 360 rule: only the connected department's certificates get a verify code
    QVERIFY(!pdf::PDFFireFirehouse360::isDepartmentCertificate(memberCertificate));   // no authority known yet
    QSettings().setValue(QStringLiteral("Firehouse360/AuthorityCertificate"), authorityA.getCertificate().toBase64());
    QVERIFY(pdf::PDFFireFirehouse360::isDepartmentCertificate(memberCertificate));
    QSettings().setValue(QStringLiteral("Firehouse360/AuthorityCertificate"), authorityB.getCertificate().toBase64());
    QVERIFY(!pdf::PDFFireFirehouse360::isDepartmentCertificate(memberCertificate));
    QSettings().remove(QStringLiteral("Firehouse360/AuthorityCertificate"));
}

void Firehouse360Test::serverEndToEnd()
{
    const QString code = qEnvironmentVariable("PDFFIRE_FH360_TEST_CODE");
    const QString cmsFile = qEnvironmentVariable("PDFFIRE_FH360_TEST_CMS");
    if (qEnvironmentVariable("PDFFIRE_FH360_URL").isEmpty() || code.isEmpty() || cmsFile.isEmpty())
    {
        QSKIP("No test server (PDFFIRE_FH360_URL, PDFFIRE_FH360_TEST_CODE, PDFFIRE_FH360_TEST_CMS)");
    }

    // Wrong code first - a clear message, nothing stored
    pdf::PDFFireFirehouse360::Response response = pdf::PDFFireFirehouse360::pair(QStringLiteral("ZZZZ-ZZZZ"), QStringLiteral("Test computer"));
    QVERIFY(!response.isOk());
    QVERIFY(!response.error.isEmpty());
    QVERIFY(!pdf::PDFFireFirehouse360::isConnected());

    response = pdf::PDFFireFirehouse360::pair(code, QStringLiteral("Test computer"));
    QVERIFY2(response.isOk(), qPrintable(response.error));
    QVERIFY(pdf::PDFFireFirehouse360::isConnected());
    QVERIFY(!pdf::PDFFireFirehouse360::getDepartmentName().isEmpty());

    // The token file is readable by the user only
    const QString tokenFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("firehouse360/token"));
    QVERIFY(QFile::exists(tokenFile));
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(tokenFile) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther), QFile::Permissions());
#endif

    response = pdf::PDFFireFirehouse360::fetchStatus();
    QVERIFY2(response.isOk(), qPrintable(response.error));
    const QJsonObject authority = response.json.value(QStringLiteral("authority")).toObject();
    QVERIFY(!authority.isEmpty());

    QByteArray certificate;
    response = pdf::PDFFireFirehouse360::download(authority.value(QStringLiteral("certificate_url")).toString(), &certificate);
    QVERIFY2(response.isOk(), qPrintable(response.error));
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(certificate, QCryptographicHash::Sha256).toHex()), authority.value(QStringLiteral("sha256")).toString());
    QByteArray ignored;
    QVERIFY(!pdf::PDFFireFirehouse360::download(QStringLiteral("https://example.com/evil.crl"), &ignored).isOk());   // only the server's own files

    // Registration of the queued signatures (the invalid one from queuePersistence is refused by the server)
    QFile file(cmsFile);
    QVERIFY(file.open(QFile::ReadOnly));
    pdf::PDFFireFirehouse360::QueueItem item;
    item.verifyCode = pdf::PDFFireFirehouse360::newVerifyCode();
    item.cms = file.readAll();
    item.signedAt = QDateTime::currentDateTimeUtc();
    item.fileName = QStringLiteral("End to end.pdf");
    pdf::PDFFireFirehouse360::enqueue(item);
    const pdf::PDFFireFirehouse360::FlushResult result = pdf::PDFFireFirehouse360::flushQueue();
    QVERIFY(result.reachedServer);
    QCOMPARE(result.registered, 1);
    QCOMPARE(result.rejected, 1);
    QCOMPARE(result.pending, 0);
    QFile codeOut(qEnvironmentVariable("PDFFIRE_FH360_TEST_OUT"));
    if (codeOut.open(QFile::WriteOnly))
    {
        codeOut.write(item.verifyCode.toLatin1());
    }

    // Certificate online: save, fetch with the passphrase, wrong passphrase, delete
    const QByteArray pkcs12("pretend pfx bytes for the round trip");
    response = pdf::PDFFireFirehouse360::saveCertificateOnline(pkcs12, QStringLiteral("Test (Prospect)"), QStringLiteral("test.pfx"), QStringLiteral("short"));
    QVERIFY(!response.isOk());
    response = pdf::PDFFireFirehouse360::saveCertificateOnline(pkcs12, QStringLiteral("Test (Prospect)"), QStringLiteral("test.pfx"), QStringLiteral("online passphrase 123"));
    QVERIFY2(response.isOk(), qPrintable(response.error));
    QByteArray fetched;
    QString fileName;
    response = pdf::PDFFireFirehouse360::fetchCertificateOnline(QStringLiteral("online passphrase 123"), &fetched, &fileName);
    QVERIFY2(response.isOk(), qPrintable(response.error));
    QCOMPARE(fetched, pkcs12);
    QCOMPARE(fileName, QStringLiteral("test.pfx"));
    response = pdf::PDFFireFirehouse360::fetchCertificateOnline(QStringLiteral("wrong passphrase 123"), &fetched, &fileName);
    QVERIFY(!response.isOk());
    QVERIFY(pdf::PDFFireFirehouse360::deleteCertificateOnline().isOk());
    QCOMPARE(pdf::PDFFireFirehouse360::fetchCertificateOnline(QStringLiteral("online passphrase 123"), &fetched, &fileName).httpStatus, 404);

    // Disconnect: the token is gone and the server refuses it from now on
    pdf::PDFFireFirehouse360::disconnect();
    QVERIFY(!pdf::PDFFireFirehouse360::isConnected());
    QVERIFY(!QFile::exists(tokenFile));
}

QTEST_GUILESS_MAIN(Firehouse360Test)

#include "tst_firehouse360test.moc"
