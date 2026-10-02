// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanager.h"
#include "pdfdocumentsigner.h"
#include "pdfsignaturehandler.h"
#include "pdfform.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdffiresigningrecord.h"
#include "pdffirecertificateauthority.h"
#include "pdffirepermissions.h"
#include "pdfsecurityhandler.h"
#include "pdfencoding.h"

#include <QtTest>
#include <QBuffer>
#include <QTemporaryDir>

using namespace pdf;

namespace signaturebuildertest
{
PDFObject dictionary(std::initializer_list<std::pair<const char*, PDFObject>> entries)
{
    PDFDictionaryBuilder result;
    for (const auto& entry : entries)
    {
        result.setEntry(PDFInplaceOrMemoryString(entry.first), PDFObject(entry.second));
    }
    return PDFObject::createDictionary(std::move(result));
}

PDFObjectReference appearance(PDFDocumentBuilder& builder, QRectF bbox)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("XObject");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Form");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BBox");
    factory << bbox;
    factory.endDictionaryItem();
    factory.endDictionary();
    const PDFObject object = factory.takeObject();
    PDFDictionaryBuilder streamDictionary(*object.getDictionary());
    QByteArray data("1 0 0 rg 20 30 80 40 re f\n");
    streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(data.size()));
    return builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(data))));
}
}   // namespace signaturebuildertest

using namespace signaturebuildertest;

class SignatureBuilderTest : public QObject
{
    Q_OBJECT
private slots:
    void certificateSignatureUsage();
    void signedDocumentRoundTrip();
    void signatureSizeChanges_data();
    void signatureSizeChanges();
    void signingFailureIsReported_data();
    void signingFailureIsReported();
    void multipleSignatures();
    void incrementalSaveKeepsSignature();
    void signingOverInvalidSignature();
    void existingSignaturesNotPreserved();
    void preservesAcroForm_data();
    void preservesAcroForm();
    void widgetStructure_data();
    void widgetStructure();
    void signingRecordIsSigned();
    void dateTimeIsWrittenAsUtc();
    void departmentAuthority();
    void certification_data();
    void certification();
    void restrictedDocumentPermissions();

private:
    static bool createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password);
    static std::vector<PDFSignatureVerificationResult> verifySignedDocument(const QByteArray& signedDocument);
    static PDFDocument readDocument(const QByteArray& data);

    /// Signs the first page of the document by an invisible signature
    static PDFDocumentSigner::Result signDocument(const PDFDocument& document,
                                                  const QByteArray& originalData,
                                                  const PDFCertificateEntry& certificate,
                                                  const QString& password,
                                                  const QString& fieldName,
                                                  QByteArray& signedDocument);
};

PDFDocument SignatureBuilderTest::readDocument(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

PDFDocumentSigner::Result SignatureBuilderTest::signDocument(const PDFDocument& document,
                                                             const QByteArray& originalData,
                                                             const PDFCertificateEntry& certificate,
                                                             const QString& password,
                                                             const QString& fieldName,
                                                             QByteArray& signedDocument)
{
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.originalDocumentData = originalData;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::sign(certificate, password, data, signature);
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary)
    {
        return builder.createSignatureField(fieldName, signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
    };
    return PDFDocumentSigner::sign(parameters, signedDocument);
}

bool SignatureBuilderTest::createTestCertificate(const QTemporaryDir& directory, PDFCertificateEntry& certificate, QString& password)
{
    PDFCertificateManager::NewCertificateInfo info;
    info.fileName = directory.filePath("signature-test.p12");
    info.privateKeyPasword = "test-password";
    info.certCommonName = "PDF4QT signature regression test";
    info.rsaKeyLength = 2048;
    PDFCertificateManager().createCertificate(info);

    QFile file(info.fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    certificate = PDFCertificateEntry();
    certificate.pkcs12 = file.readAll();
    file.close();
    password = info.privateKeyPasword;

    return !certificate.pkcs12.isEmpty();
}

std::vector<PDFSignatureVerificationResult> SignatureBuilderTest::verifySignedDocument(const QByteArray& signedDocument)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(signedDocument);

    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        return { };
    }

    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    PDFSignatureHandler::Parameters parameters;
    parameters.useSystemCertificateStore = false;
    return PDFSignatureHandler::verifySignatures(form, signedDocument, parameters);
}

void SignatureBuilderTest::certificateSignatureUsage()
{
    // Only certificates, which declare the digital signature or the non
    // repudiation key usage, can be offered to the user for signing. The
    // personal certificate storage of the operating system contains also
    // certificates without the key usage extension, which are generated
    // by the system for its internal purposes.
    PDFCertificateInfo info;
    QVERIFY(!info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageKeyEncipherment);
    QVERIFY(!info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageDigitalSignature);
    QVERIFY(info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageNonRepudiation);
    QVERIFY(info.isUsableForDigitalSignature());

    info.setKeyUsage(PDFCertificateInfo::KeyUsageFlags(PDFCertificateInfo::KeyUsageDigitalSignature | PDFCertificateInfo::KeyUsageAgreement));
    QVERIFY(info.isUsableForDigitalSignature());
}

void SignatureBuilderTest::signedDocumentRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    for (bool visible : {false, true})
    {
        PDFDocumentBuilder builder;
        const auto page = builder.appendPage(QRectF(100, 200, 300, 400));
        PDFDocument document = builder.build();

        QByteArray signedDocument;
        PDFDocumentSigner::Parameters parameters;
        parameters.document = &document;
        parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
        {
            return PDFSignatureFactory::sign(certificate, password, data, signature);
        };
        parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
        {
            const auto stream = visible ? appearance(signedBuilder, QRectF(20, 30, 80, 40)) : PDFObjectReference();
            return signedBuilder.createSignatureField("Signature", signatureDictionary, page, stream, QRectF(120, 230, 80, 40));
        };

        QCOMPARE(PDFDocumentSigner::sign(parameters, signedDocument), PDFDocumentSigner::Result::OK);
        QVERIFY(!signedDocument.isEmpty());

        const auto results = verifySignedDocument(signedDocument);
        QCOMPARE(results.size(), size_t(1));
        QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
        QVERIFY(!results.front().hasSignatureWarning());

        // A changed signedData byte must be detected independently of certificate trust.
        QByteArray changedBytes = signedDocument;
        changedBytes[10] = changedBytes[10] == 'X' ? 'Y' : 'X';
        const auto changedResults = verifySignedDocument(changedBytes);
        QCOMPARE(changedResults.size(), size_t(1));
        QVERIFY(!changedResults.front().isSignatureValid());
    }
}

void SignatureBuilderTest::signingRecordIsSigned()
{
    // PDF Fire: the signing record (audit trail) is a part of the signature
    // dictionary - it is read back, and a change of it breaks the signature
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 612, 792));
    PDFDocument document = builder.build();

    PDFFireSigningRecord record;
    record.signerName = "Record Test";
    record.typedName = "Record Test";
    record.signingTime = QDateTime(QDate(2026, 10, 1), QTime(16, 30, 0), QTimeZone(-4 * 3600));
    record.documentName = "test.pdf";
    record.documentFingerprint = PDFFireSigningRecord::getFingerprint("original");
    record.consentAccepted = true;
    record.collectComputerInformation(true, true, true);
    record.publicAddress = "203.0.113.7 (reported by test)";
    record.hostName = "test-host";

    QByteArray signedDocument;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signingTime = record.signingTime;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::sign(certificate, password, data, signature);
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        const PDFObjectReference field = signedBuilder.createSignatureField("Signature", signatureDictionary, page);
        record.writeTo(signedBuilder, signatureDictionary);
        return field;
    };
    QCOMPARE(PDFDocumentSigner::sign(parameters, signedDocument), PDFDocumentSigner::Result::OK);

    const auto results = verifySignedDocument(signedDocument);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
    QCOMPARE(results.front().getSigningRecord(), record.toText());
    QCOMPARE(results.front().getLocation(), QString("test-host"));
    QVERIFY(record.toText().contains("Signed (UTC): 2026-10-01T20:30:00Z"));
    QVERIFY(record.toText().contains("Signed (local time): 2026-10-01T16:30:00-04:00"));
    QVERIFY(record.toText().contains("Public IP address: 203.0.113.7"));
    QVERIFY(record.toText().contains("Consent to sign electronically: accepted"));
    QVERIFY(signedDocument.contains("/Prop_Build"));

    // The record is written as a hexadecimal string - a changed digit of it is
    // a changed record, which must be detected
    const int recordPosition = signedDocument.indexOf(PDFFireSigningRecord::DICTIONARY_KEY);
    QVERIFY(recordPosition > 0);
    const int digitPosition = signedDocument.indexOf('<', recordPosition) + 40;
    QByteArray changedRecord = signedDocument;
    changedRecord[digitPosition] = changedRecord[digitPosition] == '6' ? '7' : '6';
    const auto changedResults = verifySignedDocument(changedRecord);
    QCOMPARE(changedResults.size(), size_t(1));
    QVERIFY(!changedResults.front().isSignatureValid());
}

void SignatureBuilderTest::dateTimeIsWrittenAsUtc()
{
    // PDF Fire: a date is written in UTC and marked so ('Z') - other readers would
    // otherwise show it shifted by the offset of the time zone
    const QDateTime dateTime(QDate(2026, 10, 1), QTime(16, 30, 0), QTimeZone(-4 * 3600));
    const QByteArray text = PDFEncoding::convertDateTimeToString(dateTime);
    QCOMPARE(text, QByteArray("D:20261001203000Z"));
    QCOMPARE(PDFEncoding::convertToDateTime(text).toMSecsSinceEpoch(), dateTime.toMSecsSinceEpoch());
}

void SignatureBuilderTest::departmentAuthority()
{
    // PDF Fire: a department authority issues the certificate of a member; the
    // signature is trusted, when the authority is trusted, and the revocation of
    // the certificate is found in the revocation list of the authority
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFFireCertificateAuthority::setTrustedAuthoritiesDirectoriesOverride({ directory.filePath("trusted") });

    const QString authorityDirectory = directory.filePath("authority");
    QString errorMessage;
    {
        PDFFireCertificateAuthority authority;
        PDFFireCertificateAuthority::AuthorityInfo info;
        info.name = "Test VFD Signing Authority";
        info.organization = "Test VFD";
        info.crlUrl = "https://example.org/pki/test-vfd.crl";
        QVERIFY(!authority.create(authorityDirectory, info, "short", &errorMessage));
        QVERIFY2(authority.create(authorityDirectory, info, "correct horse battery", &errorMessage), qPrintable(errorMessage));
        QVERIFY(!PDFFireCertificateAuthority().create(authorityDirectory, info, "correct horse battery", &errorMessage));
    }

    PDFFireCertificateAuthority authority;
    QVERIFY(!authority.open(authorityDirectory, "wrong passphrase!", &errorMessage));
    QVERIFY2(authority.open(authorityDirectory, "correct horse battery", &errorMessage), qPrintable(errorMessage));
    QCOMPARE(authority.getName(), QString("Test VFD Signing Authority"));
    QCOMPARE(authority.getCrlUrl(), QString("https://example.org/pki/test-vfd.crl"));

    PDFFireCertificateAuthority::MemberInfo member;
    member.name = "Jane Firefighter";
    member.title = "Lieutenant";
    member.email = "jane@example.org";
    member.policy.recordOperatingSystem = true;
    member.policy.recordComputer = false;
    member.policy.recordLocalAddresses = true;
    member.policy.recordPublicAddress = false;
    member.policy.isLocked = true;
    const QString memberPassword = PDFFireCertificateAuthority::generatePassword();
    QCOMPARE(memberPassword.size(), 19);

    PDFCertificateEntry certificate;
    PDFFireCertificateAuthority::IssuedCertificate issued;
    QVERIFY2(authority.issue(member, memberPassword, &certificate.pkcs12, &issued, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(authority.getIssuedCertificates().size(), size_t(1));
    QVERIFY(issued.expires > QDateTime::currentDateTimeUtc().addDays(700));

    // The policy and the issuer are read without the password
    const std::optional<PDFFireSigningPolicy> policy = PDFFireCertificateAuthority::readSigningPolicy(certificate, QString());
    QVERIFY(policy.has_value());
    QVERIFY(policy->recordOperatingSystem && !policy->recordComputer && policy->recordLocalAddresses && !policy->recordPublicAddress && policy->isLocked);
    QCOMPARE(policy->authorityName, QString("Test VFD Signing Authority"));
    QCOMPARE(PDFFireCertificateAuthority::readIssuerName(certificate, QString()), QString("Test VFD Signing Authority"));
    QCOMPARE(PDFCertificateManager::getCertificateOwnerName(certificate, memberPassword), QString("Jane Firefighter"));
    QVERIFY(PDFCertificateManager::isCertificateValid(certificate, memberPassword));
    QCOMPARE(PDFFireCertificateAuthority::readAuthorityCertificate(certificate.pkcs12), authority.getCertificate());
    QVERIFY(!PDFCertificateManager::isCertificateValid(certificate, "not the password"));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    PDFDocument document = builder.build();
    QByteArray signedDocument;
    QCOMPARE(signDocument(document, QByteArray(), certificate, memberPassword, "Signature", signedDocument), PDFDocumentSigner::Result::OK);

    // Not trusted yet - the signature is valid, the authority (in the chain) is not trusted
    auto results = verifySignedDocument(signedDocument);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY(results.front().isSignatureValid());
    QVERIFY(results.front().hasFlag(PDFSignatureVerificationResult::Error_Certificate_SelfSignedChain));
    QVERIFY(!results.front().hasFlag(PDFSignatureVerificationResult::Error_Certificate_SelfSigned));

    // Trusted - the whole chain is valid
    QFile crlFile(QDir(authorityDirectory).filePath(PDFFireCertificateAuthority::CRL_FILE_NAME));
    QVERIFY(crlFile.open(QFile::ReadOnly));
    QVERIFY2(PDFFireCertificateAuthority::trustAuthority(authority.getCertificate(), crlFile.readAll(), &errorMessage), qPrintable(errorMessage));
    QVERIFY(PDFFireCertificateAuthority::isAuthorityTrusted(authority.getCertificate()));
    results = verifySignedDocument(signedDocument);
    QVERIFY2(results.front().isCertificateValid(), qPrintable(results.front().getErrors().join('\n')));
    QVERIFY(results.front().isSignatureValid());
    QVERIFY(!results.front().hasError());
    QCOMPARE(results.front().getCertificateInfos().size(), size_t(2));

    // A certificate of a member can't be trusted as an authority
    QVERIFY(!PDFFireCertificateAuthority::trustAuthority(PDFFireCertificateAuthority::readPublicCertificate(certificate.pkcs12), QByteArray(), &errorMessage));

    // Revoked - the revocation list of the trusted authority is updated at once
    QVERIFY2(authority.revoke(issued.serialNumber, PDFFireCertificateAuthority::RevocationReason::CessationOfOperation, &errorMessage), qPrintable(errorMessage));
    QVERIFY(authority.getIssuedCertificates().front().isRevoked());
    results = verifySignedDocument(signedDocument);
    QVERIFY(results.front().hasFlag(PDFSignatureVerificationResult::Error_Certificate_Revoked));
    QVERIFY(results.front().isSignatureValid());

    // The register survives reopening
    PDFFireCertificateAuthority reopened;
    QVERIFY2(reopened.open(authorityDirectory, "correct horse battery", &errorMessage), qPrintable(errorMessage));
    QCOMPARE(reopened.getIssuedCertificates().size(), size_t(1));
    QVERIFY(reopened.getIssuedCertificates().front().isRevoked());
    QCOMPARE(reopened.getIssuedCertificates().front().name, QString("Jane Firefighter"));

    PDFFireCertificateAuthority::setTrustedAuthoritiesDirectoriesOverride(QStringList());
}

void SignatureBuilderTest::certification_data()
{
    QTest::addColumn<int>("level");
    QTest::newRow("no changes") << 1;
    QTest::newRow("forms and signing") << 2;
    QTest::newRow("forms, signing, comments") << 3;
}

void SignatureBuilderTest::certification()
{
    // PDF Fire: a certification signature (DocMDP) - the document knows, which changes
    // are allowed, and a later change is reported (an error, when no change is allowed)
    QFETCH(int, level);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 612, 792));
    PDFDocument document = builder.build();

    QByteArray certifiedData;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return PDFSignatureFactory::sign(certificate, password, data, signature);
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        const PDFObjectReference field = signedBuilder.createSignatureField("Certification", signatureDictionary, page);
        PDFFirePermissions::writeCertification(signedBuilder, signatureDictionary, PDFFirePermissions::Certification(level));
        return field;
    };
    QCOMPARE(PDFDocumentSigner::sign(parameters, certifiedData), PDFDocumentSigner::Result::OK);

    // The certification is found, the signature is valid and has no warning
    const PDFDocument certifiedDocument = readDocument(certifiedData);
    QCOMPARE(int(PDFFirePermissions::getCertification(&certifiedDocument)), level);
    QVERIFY(!PDFFirePermissions::canModifyContent(&certifiedDocument));
    QVERIFY(!PDFFirePermissions::canAssemblePages(&certifiedDocument));
    QCOMPARE(PDFFirePermissions::canFillForms(&certifiedDocument), level >= 2);
    QCOMPARE(PDFFirePermissions::canSign(&certifiedDocument), level >= 2);
    QCOMPARE(PDFFirePermissions::canAnnotate(&certifiedDocument), level == 3);
    QVERIFY(PDFFirePermissions::canCopyContent(&certifiedDocument));
    QVERIFY(PDFFirePermissions::isAllowed(&certifiedDocument, PDFSecurityHandler::Permission::PrintHighResolution));

    auto results = verifySignedDocument(certifiedData);
    QCOMPARE(results.size(), size_t(1));
    QCOMPARE(int(results.front().getCertification()), level);
    QVERIFY(results.front().isSignatureValid());
    QVERIFY(!results.front().hasSignatureWarning());

    // A change after the certification (a page added, an incremental update)
    PDFDocumentBuilder editBuilder(&certifiedDocument);
    editBuilder.appendPage(QRectF(0, 0, 500, 600));
    const PDFDocument editedDocument = editBuilder.build();
    const QString fileName = directory.filePath("changed.pdf");
    PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.writeIncrementalUpdate(fileName, certifiedData, &editedDocument));
    QFile file(fileName);
    QVERIFY(file.open(QFile::ReadOnly));
    results = verifySignedDocument(file.readAll());
    QCOMPARE(results.size(), size_t(1));
    if (level == 1)
    {
        QVERIFY(results.front().hasFlag(PDFSignatureVerificationResult::Error_Signature_ChangedAfterCertification));
        QVERIFY(!results.front().isSignatureValid());
    }
    else
    {
        QVERIFY(results.front().hasFlag(PDFSignatureVerificationResult::Warning_Signature_ChangedAfterCertification));
        QVERIFY(results.front().isSignatureValid());
    }
}

void SignatureBuilderTest::restrictedDocumentPermissions()
{
    // PDF Fire: a document with restrictions (only printing allowed), opened without
    // the owner password, allows no change - not even of its security; opened with
    // the owner password, it allows everything
    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 612, 792));
    PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = PDFSecurityHandlerFactory::AES_256;
    settings.ownerPassword = "owner-secret";
    settings.permissions = uint32_t(PDFSecurityHandler::Permission::PrintLowResolution) | uint32_t(PDFSecurityHandler::Permission::PrintHighResolution);
    builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const PDFDocument document = builder.build();

    QBuffer buffer;
    QVERIFY(buffer.open(QBuffer::WriteOnly));
    PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(&buffer, &document));
    buffer.close();

    PDFDocumentReader userReader(nullptr, [](bool* ok) { *ok = false; return QString(); }, false, false);
    const PDFDocument userDocument = userReader.readFromBuffer(buffer.data());
    QCOMPARE(userReader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(PDFFirePermissions::isAllowed(&userDocument, PDFSecurityHandler::Permission::PrintHighResolution));
    QVERIFY(!PDFFirePermissions::canModifyContent(&userDocument));
    QVERIFY(!PDFFirePermissions::canFillForms(&userDocument));
    QVERIFY(!PDFFirePermissions::canAnnotate(&userDocument));
    QVERIFY(!PDFFirePermissions::canCopyContent(&userDocument));
    QVERIFY(!PDFFirePermissions::canAssemblePages(&userDocument));
    QVERIFY(!PDFFirePermissions::canChangeSecurity(&userDocument));

    PDFDocumentReader ownerReader(nullptr, [](bool* ok) { *ok = true; return QString("owner-secret"); }, false, true);
    const PDFDocument ownerDocument = ownerReader.readFromBuffer(buffer.data());
    QCOMPARE(ownerReader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(PDFFirePermissions::canModifyContent(&ownerDocument));
    QVERIFY(PDFFirePermissions::canCopyContent(&ownerDocument));
    QVERIFY(PDFFirePermissions::canChangeSecurity(&ownerDocument));

    // No security at all - everything is allowed
    PDFDocumentBuilder plainBuilder;
    plainBuilder.appendPage(QRectF(0, 0, 612, 792));
    const PDFDocument plainDocument = plainBuilder.build();
    QVERIFY(PDFFirePermissions::canModifyContent(&plainDocument));
    QVERIFY(PDFFirePermissions::canChangeSecurity(&plainDocument));
    QCOMPARE(PDFFirePermissions::getCertification(&plainDocument), PDFFirePermissions::Certification::None);
}

void SignatureBuilderTest::signatureSizeChanges_data()
{
    QTest::addColumn<int>("extraBytes");

    // The size of a signature is not stable - the same data signedData twice can
    // produce results differing by a few bytes. The document must be signedData
    // correctly whatever the size of the final signature is, including the case
    // when it does not fit into the initially reserved space.
    QTest::newRow("same-size") << 0;
    QTest::newRow("longer-by-one") << 1;
    QTest::newRow("longer-by-eight") << 8;
    QTest::newRow("longer-than-reserved-space") << 2000;
    QTest::newRow("much-longer-than-reserved-space") << 40000;
}

void SignatureBuilderTest::signatureSizeChanges()
{
    QFETCH(int, extraBytes);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    // The trial signature keeps its size, the signature of the document is
    // enlarged - the padding is ignored by the DER decoder, so the signature
    // stays valid.
    QByteArray signedDocument;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        if (!PDFSignatureFactory::sign(certificate, password, data, signature))
        {
            return false;
        }

        if (data.size() > 1000)
        {
            signature.append(QByteArray(extraBytes, char(0)));
        }

        return true;
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        return signedBuilder.createSignatureField("Signature", signatureDictionary, page);
    };

    QCOMPARE(PDFDocumentSigner::sign(parameters, signedDocument), PDFDocumentSigner::Result::OK);

    const auto results = verifySignedDocument(signedDocument);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // Nothing outside of the signature string may be left out of the signature
    QVERIFY(!results.front().hasSignatureWarning());
}

void SignatureBuilderTest::signingFailureIsReported_data()
{
    QTest::addColumn<int>("failureMode");
    QTest::addColumn<int>("expectedResult");

    QTest::newRow("signing-failed") << 0 << int(PDFDocumentSigner::Result::SigningFailed);
    QTest::newRow("signature-never-fits") << 1 << int(PDFDocumentSigner::Result::SignatureTooLarge);
    QTest::newRow("damaged-signature") << 2 << int(PDFDocumentSigner::Result::VerificationFailed);
}

void SignatureBuilderTest::signingFailureIsReported()
{
    QFETCH(int, failureMode);
    QFETCH(int, expectedResult);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 300, 400));
    PDFDocument document = builder.build();

    QByteArray signedDocument;
    int signatureGrowth = 0;
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        const bool isDocumentSigned = data.size() > 1000;

        if (failureMode == 0 && isDocumentSigned)
        {
            return false;
        }

        if (!PDFSignatureFactory::sign(certificate, password, data, signature))
        {
            return false;
        }

        if (failureMode == 1 && isDocumentSigned)
        {
            // The signature quadruples on each attempt, which is faster than the
            // signer can enlarge the reserved space, so it never fits - the
            // signer must give up instead of writing a damaged document.
            signature.append(QByteArray((1 << 16) << (2 * signatureGrowth++), char(0)));
        }

        if (failureMode == 2 && isDocumentSigned)
        {
            // The signature does not belong to the signedData data
            PDFSignatureFactory::sign(certificate, password, QByteArray("something else"), signature);
        }

        return true;
    };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& signedBuilder, PDFObjectReference signatureDictionary)
    {
        return signedBuilder.createSignatureField("Signature", signatureDictionary, page);
    };

    QCOMPARE(int(PDFDocumentSigner::sign(parameters, signedDocument)), expectedResult);

    // Nothing may be handed over to the caller, when the document was not signedData
    QVERIFY(signedDocument.isEmpty());
}

void SignatureBuilderTest::multipleSignatures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QByteArray firstSigned;
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", firstSigned), PDFDocumentSigner::Result::OK);

    // The second signature is added as an incremental update, so the bytes
    // covered by the first signature are not touched and it stays valid.
    QByteArray secondSigned;
    QCOMPARE(signDocument(readDocument(firstSigned), firstSigned, certificate, password, "Second", secondSigned), PDFDocumentSigner::Result::OK);
    QVERIFY(secondSigned.startsWith(firstSigned));

    QByteArray thirdSigned;
    QCOMPARE(signDocument(readDocument(secondSigned), secondSigned, certificate, password, "Third", thirdSigned), PDFDocumentSigner::Result::OK);
    QVERIFY(thirdSigned.startsWith(secondSigned));

    const auto results = verifySignedDocument(thirdSigned);
    QCOMPARE(results.size(), size_t(3));
    for (const PDFSignatureVerificationResult& result : results)
    {
        QVERIFY2(result.isSignatureValid(), qPrintable(result.getSignatureFieldQualifiedName() + ": " + result.getErrors().join('\n')));
    }

    // The last signature covers the whole document, the earlier ones only their part of it
    QVERIFY(results.front().hasSignatureWarning());
    QVERIFY(!results.back().hasSignatureWarning());
    QCOMPARE(results.back().getSignatureFieldQualifiedName(), QString("Third"));
}

void SignatureBuilderTest::incrementalSaveKeepsSignature()
{
    // PDF Fire: an ordinary save of a signed and then edited document. Written as an
    // incremental update, the signature stays valid - written as a whole, it is lost.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QByteArray signedData;
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);

    // Edit the signed document - a page is added
    const PDFDocument signedDocument = readDocument(signedData);
    PDFDocumentBuilder editBuilder(&signedDocument);
    editBuilder.appendPage(QRectF(0, 0, 500, 600));
    const PDFDocument editedDocument = editBuilder.build();
    QCOMPARE(editedDocument.getCatalog()->getPageCount(), size_t(2));

    const QString fileName = directory.filePath("saved.pdf");
    if (qEnvironmentVariableIsSet("PDFFIRE_KEEP_TEST_FILES"))
    {
        // The file can be then checked by an independent validator (pdfsig of poppler)
        directory.setAutoRemove(false);
        qInfo() << "Saved file:" << fileName;
    }
    PDFDocumentWriter writer(nullptr);
    const PDFOperationResult result = writer.writeIncrementalUpdate(fileName, signedData, &editedDocument);
    QVERIFY2(result, qPrintable(result.getErrorMessage()));

    QFile file(fileName);
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray savedData = file.readAll();
    file.close();

    QVERIFY(savedData.startsWith(signedData));
    QVERIFY(savedData.size() > signedData.size());
    QCOMPARE(readDocument(savedData).getCatalog()->getPageCount(), size_t(2));

    const auto results = verifySignedDocument(savedData);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));

    // The signature does not cover the appended change, and the validation says so
    QVERIFY(results.front().hasSignatureWarning());

    // The same document written as a whole: the signature is not valid anymore
    QBuffer buffer;
    QVERIFY(buffer.open(QBuffer::WriteOnly));
    QVERIFY(writer.write(&buffer, &editedDocument));
    buffer.close();
    const auto rewrittenResults = verifySignedDocument(buffer.data());
    QVERIFY(rewrittenResults.empty() || !rewrittenResults.front().isSignatureValid());
}

void SignatureBuilderTest::signingOverInvalidSignature()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QByteArray signedData;
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);

    // Damage the first signature by changing the first byte of its value - the
    // document stays readable, but the signature cannot be decoded anymore
    const qsizetype contents = signedData.indexOf("/Contents <");
    QVERIFY(contents > 0);
    const qsizetype firstDigit = contents + qsizetype(std::strlen("/Contents <"));
    signedData[firstDigit] = signedData[firstDigit] == '3' ? '4' : '3';
    const auto damagedResults = verifySignedDocument(signedData);
    QCOMPARE(damagedResults.size(), size_t(1));
    QVERIFY(!damagedResults.front().isSignatureValid());

    // A document with an invalid signature can still be signedData, only the new
    // signature is verified
    QByteArray resigned;
    QCOMPARE(signDocument(readDocument(signedData), signedData, certificate, password, "Second", resigned), PDFDocumentSigner::Result::OK);

    const auto results = verifySignedDocument(resigned);
    QCOMPARE(results.size(), size_t(2));
    QCOMPARE(results.front().getSignatureFieldQualifiedName(), QString("First"));
    QVERIFY(!results.front().isSignatureValid());
    QCOMPARE(results.back().getSignatureFieldQualifiedName(), QString("Second"));
    QVERIFY2(results.back().isSignatureValid(), qPrintable(results.back().getErrors().join('\n')));
    QVERIFY(!results.back().hasSignatureWarning());
}

void SignatureBuilderTest::existingSignaturesNotPreserved()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateEntry certificate;
    QString password;
    QVERIFY(createTestCertificate(directory, certificate, password));

    PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const PDFDocument document = builder.build();

    QByteArray signedData;
    QCOMPARE(signDocument(document, QByteArray(), certificate, password, "First", signedData), PDFDocumentSigner::Result::OK);
    const PDFDocument signedDocument = readDocument(signedData);

    // Without the original data, or with data which cannot be updated, the
    // existing signature would be damaged by writing the document, so the
    // signer must refuse instead
    QByteArray resigned;
    QCOMPARE(signDocument(signedDocument, QByteArray(), certificate, password, "Second", resigned), PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    QVERIFY(resigned.isEmpty());
    QCOMPARE(signDocument(signedDocument, QByteArray("not a document"), certificate, password, "Second", resigned), PDFDocumentSigner::Result::ExistingSignaturesNotPreserved);
    QVERIFY(resigned.isEmpty());
}

void SignatureBuilderTest::preservesAcroForm_data()
{
    QTest::addColumn<bool>("indirectForm");
    QTest::addColumn<bool>("indirectFields");
    QTest::newRow("direct-form-direct-fields") << false << false;
    QTest::newRow("direct-form-indirect-fields") << false << true;
    QTest::newRow("indirect-form-direct-fields") << true << false;
    QTest::newRow("indirect-form-indirect-fields") << true << true;
}

void SignatureBuilderTest::preservesAcroForm()
{
    QFETCH(bool, indirectForm);
    QFETCH(bool, indirectFields);
    PDFDocumentBuilder original;
    const auto page = original.appendPage(QRectF(0, 0, 300, 400));
    const auto oldField = original.addObject(dictionary({{"FT", PDFObject::createName("Tx")},
                                                       {"T", PDFObjectFactory::createTextString("Existing")},
                                                       {"V", PDFObjectFactory::createTextString("Keep this value")}}));
    PDFObject fields = PDFObject::createArray(PDFArrayBuilder(PDFDocumentBuilder::createObjectsFromReferences({oldField})));
    if (indirectFields)
    {
        fields = PDFObject::createReference(original.addObject(fields));
    }
    const auto resources = original.addObject(dictionary({}));
    const auto flags = original.addObject(PDFObject::createInteger(8));
    const PDFObject form = dictionary({{"Fields", fields}, {"SigFlags", PDFObject::createReference(flags)},
                                      {"NeedAppearances", PDFObject::createBool(true)},
                                      {"DR", PDFObject::createReference(resources)},
                                      {"DA", PDFObjectFactory::createTextString("/Helv 12 Tf 0 g")},
                                      {"Q", PDFObject::createInteger(2)},
                                      {"XFA", PDFObjectFactory::createTextString("Preserve XFA")}});
    const PDFObjectReference formReference = indirectForm ? original.addObject(form) : PDFObjectReference();
    original.mergeTo(original.getCatalogReference(), dictionary({{"AcroForm", indirectForm ? PDFObject::createReference(formReference) : form}}));
    const PDFDocument source = original.build();
    PDFDocumentBuilder builder(&source);
    const auto signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", "test", QDateTime::currentDateTime(), 0);
    const auto field = builder.createSignatureField("Signature", signature, page);
    const PDFDocument document = builder.build();
    const PDFObject acroForm = document.getObjectByReference(builder.getCatalogReference()).getDictionary()->get("AcroForm");
    if (indirectForm)
    {
        QCOMPARE(acroForm.getReference(), formReference);
    }
    const PDFDictionary* result = document.getDictionaryFromObject(acroForm);
    QVERIFY(result);
    const PDFObject resultFields = document.getObject(result->get("Fields"));
    QVERIFY(resultFields.isArray());
    QCOMPARE(resultFields.getArray()->getCount(), size_t(2));
    QCOMPARE(resultFields.getArray()->getItem(0).getReference(), oldField);
    QCOMPARE(resultFields.getArray()->getItem(1).getReference(), field);
    QCOMPARE(document.getObject(result->get("SigFlags")).getInteger(), PDFInteger(11));
    for (const char* key : {"NeedAppearances", "DR", "DA", "Q", "XFA"})
    {
        QVERIFY(result->get(key) == form.getDictionary()->get(key));
    }
    QVERIFY(document.getObjectByReference(oldField) == source.getObjectByReference(oldField));
    const auto* sourceForm = source.getDictionaryFromObject(source.getObjectByReference(builder.getCatalogReference()).getDictionary()->get("AcroForm"));
    QCOMPARE(source.getObject(sourceForm->get("Fields")).getArray()->getCount(), size_t(1));
    QCOMPARE(source.getObject(sourceForm->get("SigFlags")).getInteger(), PDFInteger(8));
}

void SignatureBuilderTest::widgetStructure_data()
{
    QTest::addColumn<bool>("visible");
    QTest::addColumn<QPointF>("origin");
    QTest::addColumn<int>("rotation");
    for (bool visible : {false, true})
    {
        for (QPointF origin : {QPointF(), QPointF(100, 200), QPointF(-100, -200)})
        {
            for (int rotation : {0, 90, 180, 270})
            {
                const QByteArray name = QByteArray::number(visible) + "-" + QByteArray::number(origin.x()) + "-" + QByteArray::number(rotation);
                QTest::newRow(name.constData()) << visible << origin << rotation;
            }
        }
    }
}

void SignatureBuilderTest::widgetStructure()
{
    QFETCH(bool, visible);
    QFETCH(QPointF, origin);
    QFETCH(int, rotation);
    PDFDocumentBuilder builder;
    const auto firstPage = builder.appendPage(QRectF(0, 0, 100, 100));
    const auto page = builder.appendPage(QRectF(origin, QSizeF(300, 400)));
    builder.setPageRotation(page, static_cast<PageRotation>(rotation / 90));
    const auto annotation = builder.addObject(dictionary({{"Type", PDFObject::createName("Annot")},
                                                        {"Subtype", PDFObject::createName("Text")}}));
    builder.mergeTo(page, dictionary({{"Annots", PDFObject::createArray(PDFArrayBuilder(PDFDocumentBuilder::createObjectsFromReferences({annotation})))}}));
    // Existing /Annots may itself be indirect.
    const auto annots = builder.getStorage()->getObjectByReference(page).getDictionary()->get("Annots");
    const auto annotsReference = builder.addObject(annots);
    builder.mergeTo(page, dictionary({{"Annots", PDFObject::createReference(annotsReference)}}));
    const QRectF bbox(20, 30, 80, 40);
    const QRectF rect = bbox.translated(origin);
    const auto stream = visible ? appearance(builder, bbox) : PDFObjectReference();
    const auto signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", "test", QDateTime::currentDateTime(), 0);
    const auto field = builder.createSignatureField("Signature", signature, page, stream, visible ? rect : QRectF());
    PDFDocument document = builder.build();

    // Verify the serialized PDF too, including stream references and page annotations.
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    PDFDocumentWriter writer(nullptr);
    QVERIFY(writer.write(&buffer, &document));
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    document = reader.readFromBuffer(buffer.data());
    QVERIFY2(reader.getReadingResult() == PDFDocumentReader::Result::OK, qPrintable(reader.getErrorMessage()));
    const PDFDictionary* widget = document.getObjectByReference(field).getDictionary();
    QVERIFY(widget);
    PDFDocumentDataLoaderDecorator loader(&document);
    QCOMPARE(loader.readRectangle(widget->get("Rect"), QRectF()), visible ? rect : QRectF());
    QCOMPARE(widget->get("F").getInteger(), PDFInteger(PDFAnnotation::Print));
    QCOMPARE(widget->get("P").getReference(), page);
    QCOMPARE(widget->get("V").getReference(), signature);
    QCOMPARE(widget->get("Subtype").getString(), QByteArray("Widget"));
    const auto* form = document.getDictionaryFromObject(document.getObjectByReference(builder.getCatalogReference()).getDictionary()->get("AcroForm"));
    QVERIFY(form);
    QCOMPARE(form->get("SigFlags").getInteger(), PDFInteger(3));
    QCOMPARE(document.getObject(form->get("Fields")).getArray()->getItem(0).getReference(), field);
    const PDFObject pageAnnots = document.getObject(document.getObjectByReference(page).getDictionary()->get("Annots"));
    QCOMPARE(pageAnnots.getArray()->getCount(), size_t(2));
    QCOMPARE(pageAnnots.getArray()->getItem(0).getReference(), annotation);
    QCOMPARE(pageAnnots.getArray()->getItem(1).getReference(), field);
    QVERIFY(document.getObjectByReference(firstPage).getDictionary()->get("Annots").isNull());
    if (visible)
    {
        const auto* ap = document.getDictionaryFromObject(widget->get("AP"));
        QVERIFY(ap);
        const PDFObject normal = document.getObject(ap->get("N"));
        QVERIFY(normal.isStream());
        QCOMPARE(loader.readRectangle(normal.getStream()->getDictionary()->get("BBox"), QRectF()), bbox);
    }
    else
    {
        QVERIFY(widget->get("AP").isNull());
    }
}

QTEST_MAIN(SignatureBuilderTest)
#include "tst_signaturebuildertest.moc"
