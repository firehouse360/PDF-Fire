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

#include "pdfsignaturehandler.h"
#include "pdfdocument.h"
#include "pdfencoding.h"
#include "pdfform.h"
#include "pdfutils.h"
#include "pdfsignaturehandler_impl.h"
#include "pdffirecertificateauthority.h"

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/rsaerr.h>
#include <openssl/ts.h>
#include <openssl/tserr.h>

#include <QDir>
#include <QMutex>
#include <QFileInfo>
#include <QLockFile>
#include <QDataStream>
#include <QMutexLocker>
#include <QStandardPaths>

#include "pdfparser.h"
#include <cctype>
#include "pdfdbgheap.h"

#include <array>
#ifdef Q_OS_UNIX
#include <time.h>
#endif

namespace pdf
{

namespace signaturehandler
{

/// Fills the verification context of a RFC 3161 timestamp. The functions with
/// the 0 in their name have clear ownership semantics, but they were introduced
/// in OpenSSL 3.4, so the older ones are used with the older versions, which the
/// project still supports. All objects are owned by the context in both cases.
void setTimestampVerifyContextData(TS_VERIFY_CTX* context, X509_STORE* store, STACK_OF(X509)* certificates, BIO* data)
{
#if OPENSSL_VERSION_NUMBER >= 0x30400000L
    TS_VERIFY_CTX_set0_store(context, store);
    TS_VERIFY_CTX_set0_certs(context, certificates);
    TS_VERIFY_CTX_set0_data(context, data);
#else
    TS_VERIFY_CTX_set_store(context, store);
    TS_VERIFY_CTS_set_certs(context, certificates);
    TS_VERIFY_CTX_set_data(context, data);
#endif
}

}   // namespace signaturehandler

using namespace signaturehandler;

template<typename T>
using openssl_ptr = std::unique_ptr<T, void(*)(T*)>;

PDFSignatureReference PDFSignatureReference::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFSignatureReference result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr const std::array<std::pair<const char*, PDFSignatureReference::TransformMethod>, 3> types = {
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "DocMDP", PDFSignatureReference::TransformMethod::DocMDP },
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "UR", PDFSignatureReference::TransformMethod::UR },
            std::pair<const char*, PDFSignatureReference::TransformMethod>{ "FieldMDP", PDFSignatureReference::TransformMethod::FieldMDP }
        };

        // Jakub Melka: parse the signature reference dictionary
        result.m_transformMethod = loader.readEnumByName(dictionary->get("TransformMethod"), types.cbegin(), types.cend(), PDFSignatureReference::TransformMethod::Invalid);
        result.m_transformParams = dictionary->get("TransformParams");
        result.m_data = dictionary->get("Data");
        result.m_digestMethod = loader.readNameFromDictionary(dictionary, "DigestMethod");

        // PDF Fire: the permissions of a certification (P is 2, when it is missing)
        if (result.m_transformMethod == PDFSignatureReference::TransformMethod::DocMDP)
        {
            const PDFDictionary* parameters = storage->getDictionaryFromObject(result.m_transformParams);
            result.m_docMDPPermissions = qBound(PDFInteger(1), parameters ? loader.readIntegerFromDictionary(parameters, "P", 2) : PDFInteger(2), PDFInteger(3));
        }
    }

    return result;
}

PDFSignature PDFSignature::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFSignature result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr const std::array<std::pair<const char*, Type>, 2> types = {
            std::pair<const char*, Type>{ "Sig", Type::Sig },
            std::pair<const char*, Type>{ "DocTimeStamp", Type::DocTimeStamp }
        };

        // Jakub Melka: parse the signature dictionary
        result.m_type = loader.readEnumByName(dictionary->get("Type"), types.cbegin(), types.cend(), Type::Sig);
        result.m_filter = loader.readNameFromDictionary(dictionary, "Filter");
        result.m_subfilter = loader.readNameFromDictionary(dictionary, "SubFilter");
        result.m_contents = loader.readStringFromDictionary(dictionary, "Contents");

        if (dictionary->hasKey("Cert"))
        {
            PDFObject certificates = storage->getObject(dictionary->get("Cert"));
            if (certificates.isString())
            {
                result.m_certificates = { loader.readString(certificates) };
            }
            else if (certificates.isArray())
            {
                result.m_certificates = loader.readStringArray(certificates);
            }
        }

        std::vector<PDFInteger> byteRangesArray = loader.readIntegerArrayFromDictionary(dictionary, "ByteRange");
        const size_t byteRangeCount = byteRangesArray.size() / 2;
        result.m_byteRanges.reserve(byteRangeCount);
        for (size_t i = 0; i < byteRangeCount; ++i)
        {
            ByteRange byteRange = { byteRangesArray[2 * i], byteRangesArray[2 * i + 1] };
            result.m_byteRanges.push_back(byteRange);
        }

        result.m_references = loader.readObjectList<PDFSignatureReference>(dictionary->get("Reference"));
        std::vector<PDFInteger> changes = loader.readIntegerArrayFromDictionary(dictionary, "Changes");

        if (changes.size() == 3)
        {
            result.m_changes = { changes[0], changes[1], changes[2] };
        }

        result.m_name = loader.readTextStringFromDictionary(dictionary, "Name", QString());
        result.m_signingDateTime = PDFEncoding::convertToDateTime(loader.readStringFromDictionary(dictionary, "M"));
        result.m_location = loader.readTextStringFromDictionary(dictionary, "Location", QString());
        result.m_reason = loader.readTextStringFromDictionary(dictionary, "Reason", QString());
        result.m_contactInfo = loader.readTextStringFromDictionary(dictionary, "ContactInfo", QString());
        result.m_R = loader.readIntegerFromDictionary(dictionary, "R", 0);
        result.m_V = loader.readIntegerFromDictionary(dictionary, "V", 0);
        result.m_propBuild = dictionary->get("Prop_Build");
        result.m_propTime = loader.readIntegerFromDictionary(dictionary, "Prop_AuthTime", 0);
        result.m_signingRecord = loader.readTextStringFromDictionary(dictionary, "PDFFire_SigningRecord", QString());

        constexpr const std::array<std::pair<const char*, AuthentificationType>, 3> authentificationTypes = {
            std::pair<const char*, AuthentificationType>{ "PIN", AuthentificationType::PIN },
            std::pair<const char*, AuthentificationType>{ "Password", AuthentificationType::Password },
            std::pair<const char*, AuthentificationType>{ "Fingerprint", AuthentificationType::Fingerprint }
        };
        result.m_propType = loader.readEnumByName(dictionary->get("Prop_AuthType"), authentificationTypes.cbegin(), authentificationTypes.cend(), AuthentificationType::Invalid);
    }

    return result;
}

PDFSignatureHandler* PDFSignatureHandler::createHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters)
{
    Q_ASSERT(signatureField);

    const QByteArray& subfilter = signatureField->getSignature().getSubfilter();
    if (subfilter == "adbe.pkcs7.detached")
    {
        return new PDFSignatureHandler_adbe_pkcs7_detached(signatureField, sourceData, parameters);
    }
    else if (subfilter == "adbe.pkcs7.sha1")
    {
        return new PDFSignatureHandler_adbe_pkcs7_sha1(signatureField, sourceData, parameters);
    }
    else if (subfilter == "adbe.x509.rsa_sha1")
    {
        return new PDFSignatureHandler_adbe_pkcs7_rsa_sha1(signatureField, sourceData, parameters);
    }
    else if (subfilter == "ETSI.CAdES.detached")
    {
        return new PDFSignatureHandler_ETSI_CAdES_detached(signatureField, sourceData, parameters);
    }
    else if (subfilter == "ETSI.RFC3161")
    {
        return new PDFSignatureHandler_ETSI_RFC3161(signatureField, sourceData, parameters);
    }

    return nullptr;
}

std::vector<PDFSignatureVerificationResult> PDFSignatureHandler::verifySignatures(const PDFForm& form, const QByteArray& sourceData, const Parameters& parameters)
{
    std::vector<PDFSignatureVerificationResult> result;

    if (parameters.enableVerification && (form.isAcroForm() || form.isXFAForm()))
    {
        std::vector<const PDFFormFieldSignature*> signatureFields;
        auto getSignatureFields = [&signatureFields](const PDFFormField* field)
        {
            if (field->getFieldType() == PDFFormField::FieldType::Signature)
            {
                const PDFFormFieldSignature* signatureField = dynamic_cast<const PDFFormFieldSignature*>(field);
                Q_ASSERT(signatureField);
                signatureFields.push_back(signatureField);
            }
        };
        form.apply(getSignatureFields);
        result.reserve(signatureFields.size());

        for (const PDFFormFieldSignature* signatureField : signatureFields)
        {
            if (const PDFSignatureHandler* signatureHandler = createHandler(signatureField, sourceData, parameters))
            {
                result.emplace_back(signatureHandler->verify());
                delete signatureHandler;
            }
            else
            {
                PDFObjectReference signatureFieldReference = signatureField->getSelfReference();
                QString qualifiedName = signatureField->getName(PDFFormField::NameType::FullyQualified);
                PDFSignatureVerificationResult verificationResult(signatureField->getSignature().getType(), signatureFieldReference, qMove(qualifiedName));
                verificationResult.addNoHandlerError(signatureField->getSignature().getSubfilter());
                result.emplace_back(qMove(verificationResult));
            }
        }
    }

    return result;
}

void PDFSignatureVerificationResult::addNoHandlerError(const QByteArray& format)
{
    m_flags.setFlag(Error_NoHandler);
    m_errors << PDFTranslationContext::tr("No signature handler for signature format '%1'.").arg(QString::fromLatin1(format));
}

void PDFSignatureVerificationResult::addInvalidCertificateError()
{
    m_flags.setFlag(Error_Certificate_Invalid);
    m_errors << PDFTranslationContext::tr("Certificate format is invalid.");
}

void PDFSignatureVerificationResult::addNoSignaturesError()
{
    m_flags.setFlag(Error_Certificate_NoSignatures);
    m_errors << PDFTranslationContext::tr("No signatures in certificate data.");
}

void PDFSignatureVerificationResult::addCertificateMissingError()
{
    m_flags.setFlag(Error_Certificate_Missing);
    m_errors << PDFTranslationContext::tr("Certificate is missing.");
}

void PDFSignatureVerificationResult::addCertificateGenericError()
{
    m_flags.setFlag(Error_Certificate_Generic);
    m_errors << PDFTranslationContext::tr("Generic error occured during certificate validation.");
}

void PDFSignatureVerificationResult::addCertificateExpiredError()
{
    m_flags.setFlag(Error_Certificate_Expired);
    m_errors << PDFTranslationContext::tr("Certificate has expired.");
}

void PDFSignatureVerificationResult::addCertificateSelfSignedError()
{
    m_flags.setFlag(Error_Certificate_SelfSigned);
    m_errors << PDFTranslationContext::tr("Certificate is self-signed.");
}

void PDFSignatureVerificationResult::addCertificateSelfSignedInChainError()
{
    m_flags.setFlag(Error_Certificate_SelfSignedChain);
    m_errors << PDFTranslationContext::tr("Self-signed certificate in chain.");
}

void PDFSignatureVerificationResult::addCertificateTrustedNotFoundError()
{
    m_flags.setFlag(Error_Certificate_TrustedNotFound);
    m_errors << PDFTranslationContext::tr("Trusted certificate not found.");
}

void PDFSignatureVerificationResult::addCertificateRevokedError()
{
    m_flags.setFlag(Error_Certificate_Revoked);
    m_errors << PDFTranslationContext::tr("Certificate has been revoked.");
}

void PDFSignatureVerificationResult::addCertificateRevokedError(const QDateTime& revocationDate)
{
    m_flags.setFlag(Error_Certificate_Revoked);
    m_errors << PDFTranslationContext::tr("Certificate was revoked on %1.").arg(QLocale::system().toString(revocationDate.toLocalTime(), QLocale::ShortFormat));
}

void PDFSignatureVerificationResult::addCertificateRevokedAfterTimestampWarning(const QDateTime& revocationDate)
{
    m_flags.setFlag(Warning_Certificate_RevokedAfterTimestamp);
    m_warnings << PDFTranslationContext::tr("Certificate was revoked on %1, after this signature was made (the time is attested by a timestamp) - the signature stays valid.")
                  .arg(QLocale::system().toString(revocationDate.toLocalTime(), QLocale::ShortFormat));
}

void PDFSignatureVerificationResult::addCertificateOtherError(int error)
{
    m_flags.setFlag(Error_Certificate_Other);
    m_errors << PDFTranslationContext::tr("Certificate validation failed with code %1.").arg(error);
}

void PDFSignatureVerificationResult::addInvalidSignatureError()
{
    m_flags.setFlag(Error_Signature_Invalid);
    m_errors << PDFTranslationContext::tr("Signature is invalid.");
}

void PDFSignatureVerificationResult::addSignatureNoSignaturesFoundError()
{
    m_flags.setFlag(Error_Signature_NoSignaturesFound);
    m_errors << PDFTranslationContext::tr("No signatures found in certificate.");
}

void PDFSignatureVerificationResult::addSignatureCertificateMissingError()
{
    m_flags.setFlag(Error_Signature_SourceCertificateMissing);
    m_errors << PDFTranslationContext::tr("Signature certificate is missing.");
}

void PDFSignatureVerificationResult::addSignatureDigestFailureError()
{
    m_flags.setFlag(Error_Signature_DigestFailure);
    m_errors << PDFTranslationContext::tr("Signed data has different hash function digest.");
}

void PDFSignatureVerificationResult::addSignatureDataOtherError()
{
    m_flags.setFlag(Error_Signature_DataOther);
    m_errors << PDFTranslationContext::tr("Signed data are invalid.");
}

void PDFSignatureVerificationResult::addSignatureDataCoveredBySignatureMissingError()
{
    m_flags.setFlag(Error_Signature_DataCoveredBySignatureMissing);
    m_errors << PDFTranslationContext::tr("Data covered by signature are not present.");
}

void PDFSignatureVerificationResult::addSignatureByteRangeInvalidError()
{
    if (!m_flags.testFlag(Error_Signature_ByteRangeInvalid))
    {
        m_flags.setFlag(Error_Signature_ByteRangeInvalid);
        m_errors << PDFTranslationContext::tr("Byte range of the signature is not valid - the signature must cover the document from its start, except the signature value itself.");
    }
}

void PDFSignatureVerificationResult::addSignatureNotCoveredBytesWarning(PDFInteger count)
{
    if (!m_flags.testFlag(Warning_Signature_NotCoveredBytes))
    {
        m_flags.setFlag(Warning_Signature_NotCoveredBytes);
        m_warnings << PDFTranslationContext::tr("%1 bytes are not covered by signature.").arg(count);
    }
}

void PDFSignatureVerificationResult::addSignatureTimestampNotVerifiedWarning()
{
    if (!m_flags.testFlag(Warning_Signature_TimestampNotVerified))
    {
        m_flags.setFlag(Warning_Signature_TimestampNotVerified);
        m_warnings << PDFTranslationContext::tr("Timestamp of the signature could not be verified, the time of the signing is not attested by a timestamp authority.");
    }
}

void PDFSignatureVerificationResult::addCertificateCRLValidityTimeExpiredWarning()
{
    if (!m_flags.testFlag(Warning_Certificate_CRLValidityTimeExpired))
    {
        m_flags.setFlag(Warning_Certificate_CRLValidityTimeExpired);
        m_warnings << PDFTranslationContext::tr("Certificate revocation list (CRL) not checked, validity time has expired.");
    }
}

void PDFSignatureVerificationResult::addCertificateQualifiedStatementNotVerifiedWarning()
{
    if (!m_flags.testFlag(Warning_Certificate_QualifiedStatement))
    {
        m_flags.setFlag(Warning_Certificate_QualifiedStatement);
        m_warnings << PDFTranslationContext::tr("Qualified certificate statement not verified.");
    }
}

void PDFSignatureVerificationResult::addCertificateUnableToGetCRLWarning()
{
    if (!m_flags.testFlag(Warning_Certificate_UnableToGetCRL))
    {
        m_flags.setFlag(Warning_Certificate_UnableToGetCRL);
        m_warnings << PDFTranslationContext::tr("Unable to get CRL.");
    }
}

void PDFSignatureVerificationResult::setSignatureFieldQualifiedName(const QString& signatureFieldQualifiedName)
{
    m_signatureFieldQualifiedName = signatureFieldQualifiedName;
}

void PDFSignatureVerificationResult::setSignatureFieldReference(PDFObjectReference signatureFieldReference)
{
    m_signatureFieldReference = signatureFieldReference;
}

void PDFSignatureVerificationResult::addHashAlgorithm(const QString& algorithm)
{
    if (!m_hashAlgorithms.contains(algorithm))
    {
        m_hashAlgorithms << algorithm;
    }
}

void PDFSignatureVerificationResult::validate()
{
    if (isCertificateValid() && isSignatureValid())
    {
        m_flags.setFlag(OK);
    }
}

QDateTime PDFSignatureVerificationResult::getSignatureDate() const
{
    return m_signatureDate;
}

void PDFSignatureVerificationResult::setSignatureDate(const QDateTime& signatureDate)
{
    m_signatureDate = signatureDate;
}

void PDFSignatureVerificationResult::setSignatureDetails(const PDFSignature& signature)
{
    m_reason = signature.getReason();
    m_location = signature.getLocation();
    m_contactInfo = signature.getContactInfo();
    m_signingRecord = signature.getSigningRecord();

    m_certification = 0;
    for (const PDFSignatureReference& reference : signature.getReferences())
    {
        if (reference.getDocMDPPermissions() > 0)
        {
            m_certification = reference.getDocMDPPermissions();
        }
    }
}

void PDFSignatureVerificationResult::verifyCertification()
{
    // PDF Fire: the bytes after the certification are a later revision of the
    // document. A certification, which allows no changes, is broken by it; the
    // other certifications allow some changes, which are not analysed here.
    if (m_certification == 0 || !m_flags.testFlag(Warning_Signature_NotCoveredBytes))
    {
        return;
    }

    if (m_certification == 1)
    {
        m_flags.setFlag(Error_Signature_ChangedAfterCertification);
        m_flags.setFlag(Signature_OK, false);
        m_errors << PDFTranslationContext::tr("The document was changed after it was certified - the certification allows no changes.");
    }
    else
    {
        m_flags.setFlag(Warning_Signature_ChangedAfterCertification);
        m_warnings << (m_certification == 2 ? PDFTranslationContext::tr("The document was changed after it was certified. The certification allows only filling in forms and signing - check, that only these changes were made.")
                                            : PDFTranslationContext::tr("The document was changed after it was certified. The certification allows only filling in forms, signing and comments - check, that only these changes were made."));
    }
}

QDateTime PDFSignatureVerificationResult::getTimestampDate() const
{
    return m_timestampDate;
}

void PDFSignatureVerificationResult::setTimestampDate(const QDateTime& timestampDate)
{
    m_timestampDate = timestampDate;
}

QByteArray PDFSignatureVerificationResult::getSignatureHandler() const
{
    return m_signatureHandler;
}

void PDFSignatureVerificationResult::setSignatureHandler(const QByteArray& signatureFilter)
{
    m_signatureHandler = signatureFilter;
}

PDFSignatureVerificationResult::Status PDFSignatureVerificationResult::getCertificateStatus() const
{
    if (hasCertificateError())
    {
        return Status::Error;
    }

    if (hasCertificateWarning())
    {
        return Status::Warning;
    }

    return Status::OK;
}

PDFSignatureVerificationResult::Status PDFSignatureVerificationResult::getSignatureStatus() const
{
    if (hasSignatureError())
    {
        return Status::Error;
    }

    if (hasSignatureWarning())
    {
        return Status::Warning;
    }

    return Status::OK;
}

QString PDFSignatureVerificationResult::getStatusText(Status status)
{
    switch (status)
    {
        case Status::OK:
            return PDFTranslationContext::tr("OK");

        case Status::Warning:
            return PDFTranslationContext::tr("Warning");

        case Status::Error:
            return PDFTranslationContext::tr("Error");

        default:
            break;
    }

    return QString();
}

const PDFClosedIntervalSet& PDFSignatureVerificationResult::getBytesCoveredBySignature() const
{
    return m_bytesCoveredBySignature;
}

void PDFSignatureVerificationResult::setBytesCoveredBySignature(const PDFClosedIntervalSet& bytesCoveredBySignature)
{
    m_bytesCoveredBySignature = bytesCoveredBySignature;
}

PDFSignature::Type PDFSignatureVerificationResult::getType() const
{
    return m_type;
}

void PDFSignatureVerificationResult::setType(const PDFSignature::Type& type)
{
    m_type = type;
}

void PDFPublicKeySignatureHandler::initializeResult(PDFSignatureVerificationResult& result) const
{
    PDFObjectReference signatureFieldReference = m_signatureField->getSelfReference();
    QString signatureFieldQualifiedName = m_signatureField->getName(PDFFormField::NameType::FullyQualified);
    result.setType(m_signatureField->getSignature().getType());
    result.setSignatureFieldReference(signatureFieldReference);
    result.setSignatureFieldQualifiedName(signatureFieldQualifiedName);
    result.setSignatureHandler(m_signatureField->getSignature().getSubfilter());
    result.setSignatureDetails(m_signatureField->getSignature());
}

STACK_OF(X509)* PDFPublicKeySignatureHandler::getCertificates(PKCS7* pkcs7)
{
    if (!pkcs7)
    {
        return nullptr;
    }

    if (PKCS7_type_is_signed(pkcs7))
    {
        return pkcs7->d.sign->cert;
    }

    if (PKCS7_type_is_signedAndEnveloped(pkcs7))
    {
        return pkcs7->d.signed_and_enveloped->cert;
    }

    return nullptr;
}

void PDFPublicKeySignatureHandler::verifyCertificate(PDFSignatureVerificationResult& result) const
{
    PDFOpenSSLGlobalLock lock;

    OpenSSL_add_all_algorithms();

    const PDFSignature& signature = m_signatureField->getSignature();
    const QByteArray& content = signature.getContents();

    // Jakub Melka: we will try to get pkcs7 from signature, then
    // verify signer certificates.
    const unsigned char* data = convertByteArrayToUcharPtr(content);
    if (PKCS7* pkcs7 = d2i_PKCS7(nullptr, &data, content.size()))
    {
        X509_STORE* store = X509_STORE_new();
        X509_STORE_CTX* context = X509_STORE_CTX_new();

        // Above functions can fail only if not enough memory. But in this
        // case, this library will crash anyway.
        Q_ASSERT(store);
        Q_ASSERT(context);

        addTrustedCertificates(store);

        STACK_OF(PKCS7_SIGNER_INFO)* signerInfo = PKCS7_get_signer_info(pkcs7);
        const int signerInfoCount = sk_PKCS7_SIGNER_INFO_num(signerInfo);
        STACK_OF(X509)* certificates = getCertificates(pkcs7);
        if (signerInfo && signerInfoCount > 0 && certificates)
        {
            for (int i = 0; i < signerInfoCount; ++i)
            {
                PKCS7_SIGNER_INFO* signerInfoValue = sk_PKCS7_SIGNER_INFO_value(signerInfo, i);
                PKCS7_ISSUER_AND_SERIAL* issuerAndSerial = signerInfoValue->issuer_and_serial;
                X509* signer = X509_find_by_issuer_and_serial(certificates, issuerAndSerial->issuer, issuerAndSerial->serial);

                if (!signer)
                {
                    result.addCertificateMissingError();
                    break;
                }

                if (!X509_STORE_CTX_init(context, store, signer, certificates))
                {
                    result.addCertificateGenericError();
                    break;
                }

                if (!X509_STORE_CTX_set_purpose(context, X509_PURPOSE_SMIME_SIGN))
                {
                    result.addCertificateGenericError();
                    break;
                }

                unsigned long flags = X509_V_FLAG_TRUSTED_FIRST;
                if (m_parameters.ignoreExpirationDate)
                {
                    flags |= X509_V_FLAG_NO_CHECK_TIME;
                }
                X509_STORE_CTX_set_flags(context, flags);

                int verificationResult = X509_verify_cert(context);
                if (verificationResult <= 0)
                {
                    int error = X509_STORE_CTX_get_error(context);
                    switch (error)
                    {
                        case X509_V_OK:
                            // Strange, this should not occur... when X509_verify_cert fails
                            break;

                        case X509_V_ERR_CERT_HAS_EXPIRED:
                            result.addCertificateExpiredError();
                            break;

                        case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                            result.addCertificateSelfSignedError();
                            break;

                        case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                            result.addCertificateSelfSignedInChainError();
                            break;

                        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
                        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                            result.addCertificateTrustedNotFoundError();
                            break;

                        case X509_V_ERR_CERT_REVOKED:
                            result.addCertificateRevokedError();
                            break;

                        default:
                            result.addCertificateOtherError(error);
                            break;
                    }

                    // We will add certificate info for all certificates
                    const int count = sk_X509_num(certificates);
                    for (int ii = 0; ii < count; ++ii)
                    {
                        result.addCertificateInfo(getCertificateInfo(sk_X509_value(certificates, ii)));
                    }
                }
                else
                {
                    STACK_OF(X509)* validChain = X509_STORE_CTX_get0_chain(context);
                    const int count = sk_X509_num(validChain);
                    for (int ii = 0; ii < count; ++ii)
                    {
                        result.addCertificateInfo(getCertificateInfo(sk_X509_value(validChain, ii)));
                    }
                }
                X509_STORE_CTX_cleanup(context);
            }
        }
        else
        {
            result.addNoSignaturesError();
        }

        X509_STORE_CTX_free(context);
        X509_STORE_free(store);

        PKCS7_free(pkcs7);
    }
    else
    {
        result.addInvalidCertificateError();
    }

    if (!result.hasCertificateError())
    {
        result.setFlag(PDFSignatureVerificationResult::Certificate_OK, true);
    }
}

BIO* PDFPublicKeySignatureHandler::getSignedDataBuffer(pdf::PDFSignatureVerificationResult& result, QByteArray& outputBuffer) const
{
    const PDFSignature& signature = m_signatureField->getSignature();
    const QByteArray& contents = signature.getContents();
    const QByteArray& sourceData = m_sourceData;

    PDFInteger size = 0;
    const PDFSignature::ByteRanges& byteRanges = signature.getByteRanges();
    for (const PDFSignature::ByteRange& byteRange : byteRanges)
    {
        size += byteRange.size;
    }

    // Sanity checks
    if (size > sourceData.size())
    {
        result.addSignatureDataCoveredBySignatureMissingError();
        return nullptr;
    }

    PDFClosedIntervalSet bytesCoveredBySignature;

    outputBuffer.reserve(size);
    for (const PDFSignature::ByteRange& byteRange : byteRanges)
    {
        PDFInteger startOffset = byteRange.offset; // Offset to the first data byte
        PDFInteger endOffset = byteRange.offset + byteRange.size; // Offset to the byte following last data byte

        if (startOffset == endOffset)
        {
            // This means byte range is zero
            continue;
        }

        if (startOffset > endOffset || startOffset < 0 || endOffset < 0 || startOffset >= m_sourceData.size() || endOffset > m_sourceData.size())
        {
            result.addSignatureDataCoveredBySignatureMissingError();
            return nullptr;
        }

        const int length = endOffset - startOffset;
        outputBuffer.append(sourceData.constData() + startOffset, length);
        bytesCoveredBySignature.addInterval(startOffset, endOffset - 1);
    }

    // PDF Fire: the byte ranges must have the structure required by ISO 32000 (12.8.1):
    // two ranges, the first one starts at the beginning of the file, and the gap between
    // them is exactly the value of this signature - the hexadecimal string of the entry
    // Contents, with its delimiters. Anything else lets somebody hide content, which is
    // not signed, in a document reported as validly signed (signature wrapping), so it is
    // an error. The gap was formerly found by searching the file for the first occurrence
    // of the signature value, wherever it was, and any mismatch was only a warning.
    std::vector<PDFSignature::ByteRange> nonEmptyByteRanges;
    for (const PDFSignature::ByteRange& byteRange : byteRanges)
    {
        if (byteRange.size > 0)
        {
            nonEmptyByteRanges.push_back(byteRange);
        }
    }

    bool isByteRangeValid = nonEmptyByteRanges.size() == 2 &&
                            nonEmptyByteRanges[0].offset == 0 &&
                            nonEmptyByteRanges[1].offset > nonEmptyByteRanges[0].size;

    if (isByteRangeValid)
    {
        const PDFInteger gapStart = nonEmptyByteRanges[0].size;
        const PDFInteger gapEnd = nonEmptyByteRanges[1].offset;  // The byte following the gap
        const QByteArray gap = sourceData.mid(gapStart, gapEnd - gapStart);

        isByteRangeValid = gap.size() >= 2 && gap.front() == '<' && gap.back() == '>';

        if (isByteRangeValid)
        {
            QByteArray hexDigits;
            hexDigits.reserve(gap.size());

            for (qsizetype i = 1; i < gap.size() - 1 && isByteRangeValid; ++i)
            {
                const char character = gap[i];
                if (std::isxdigit(static_cast<unsigned char>(character)))
                {
                    hexDigits.push_back(character);
                }
                else if (!PDFLexicalAnalyzer::isWhitespace(character))
                {
                    isByteRangeValid = false;
                }
            }

            if (hexDigits.size() % 2 == 1)
            {
                // The missing last digit of a hexadecimal string is zero
                hexDigits.push_back('0');
            }

            isByteRangeValid = isByteRangeValid && QByteArray::fromHex(hexDigits) == contents;
        }

        if (isByteRangeValid)
        {
            bytesCoveredBySignature.addInterval(gapStart, gapEnd - 1);
        }
    }

    if (!isByteRangeValid)
    {
        result.addSignatureByteRangeInvalidError();
        return nullptr;
    }

    // We add a warning, that this signature doesn't cover whole source byte range
    if (!bytesCoveredBySignature.isCovered(0, sourceData.size() - 1))
    {
        const PDFInteger notCoveredBytes = sourceData.size() - int(bytesCoveredBySignature.getTotalLength());
        result.addSignatureNotCoveredBytesWarning(notCoveredBytes);
    }

    result.setBytesCoveredBySignature(qMove(bytesCoveredBySignature));

    return BIO_new_mem_buf(outputBuffer.data(), outputBuffer.length());
}

void PDFPublicKeySignatureHandler::verifySignature(PDFSignatureVerificationResult& result) const
{
    PDFOpenSSLGlobalLock lock;

    OpenSSL_add_all_algorithms();

    const PDFSignature& signature = m_signatureField->getSignature();
    const QByteArray& content = signature.getContents();

    // Jakub Melka: we will try to get pkcs7 from signature, then
    // verify signer certificates.
    const unsigned char* data = convertByteArrayToUcharPtr(content);
    if (PKCS7* pkcs7 = d2i_PKCS7(nullptr, &data, content.size()))
    {
        QByteArray buffer;
        if (BIO* inputBuffer = getSignedDataBuffer(result, buffer))
        {
            if (BIO* dataBio = PKCS7_dataInit(pkcs7, inputBuffer))
            {
                // Now, we must read from bio to calculate digests (digest is returned)
                std::array<char, 16384> bioReadBuffer = { };
                int bytesRead = 0;
                do
                {
                    bytesRead = BIO_read(dataBio, bioReadBuffer.data(), int(bioReadBuffer.size()));
                } while (bytesRead > 0);

                STACK_OF(PKCS7_SIGNER_INFO)* signerInfo = PKCS7_get_signer_info(pkcs7);
                addHashAlgorithmFromSignerInfoStack(signerInfo, result);
                addSignatureDateFromSignerInfoStack(signerInfo, result);
                verifySignatureTimestampAttribute(signerInfo, result);
                const int signerInfoCount = sk_PKCS7_SIGNER_INFO_num(signerInfo);
                STACK_OF(X509)* certificates = getCertificates(pkcs7);
                if (signerInfo && signerInfoCount > 0 && certificates)
                {
                    for (int i = 0; i < signerInfoCount; ++i)
                    {
                        PKCS7_SIGNER_INFO* signerInfoValue = sk_PKCS7_SIGNER_INFO_value(signerInfo, i);
                        PKCS7_ISSUER_AND_SERIAL* issuerAndSerial = signerInfoValue->issuer_and_serial;
                        X509* signer = X509_find_by_issuer_and_serial(certificates, issuerAndSerial->issuer, issuerAndSerial->serial);

                        if (!signer)
                        {
                            result.addSignatureCertificateMissingError();
                            break;
                        }

                        const int verification = PKCS7_signatureVerify(dataBio, pkcs7, signerInfoValue, signer);
                        if (verification <= 0)
                        {
                            const int reason = ERR_GET_REASON(ERR_get_error());
                            switch (reason)
                            {
                                case PKCS7_R_DIGEST_FAILURE:
                                    result.addSignatureDigestFailureError();
                                    break;

                                default:
                                    result.addSignatureDataOtherError();
                                    break;
                            }
                        }
                    }
                }
                else
                {
                    result.addSignatureNoSignaturesFoundError();
                }

                // According to the documentation, we should not call PKCS7_dataFinal
                // at the end, when pkcs7 is populated.

                BIO_free(dataBio);
            }
            else
            {
                result.addInvalidSignatureError();
            }

            BIO_free(inputBuffer);
        }
        else
        {
            // There is no need for adding error, error is in this case added by getSignedDataBuffer function
        }

        PKCS7_free(pkcs7);
    }
    else
    {
        result.addInvalidSignatureError();
    }

    if (!result.hasSignatureError())
    {
        result.setFlag(PDFSignatureVerificationResult::Signature_OK, true);
    }
}

PDFSignatureVerificationResult PDFSignatureHandler_adbe_pkcs7_detached::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);
    verifyCertificate(result);
    verifySignature(result);
    verifyTrustedAuthorityRevocation(result);
    result.verifyCertification();
    result.validate();
    return result;
}

PDFSignatureVerificationResult PDFSignatureHandler_ETSI_CAdES_detached::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);
    verifyCertificateCAdES(result, X509_PURPOSE_SMIME_SIGN);
    verifySignature(result);
    verifyTrustedAuthorityRevocation(result);
    result.verifyCertification();
    result.validate();
    return result;
}

PDFSignatureVerificationResult PDFSignatureHandler_ETSI_RFC3161::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);
    verifyCertificateCAdES(result, X509_PURPOSE_TIMESTAMP_SIGN);
    verifySignatureTimestamp(result);
    result.validate();
    return result;
}

void PDFSignatureHandler_ETSI_RFC3161::verifySignatureTimestamp(PDFSignatureVerificationResult& result) const
{
    PDFOpenSSLGlobalLock lock;

    OpenSSL_add_all_algorithms();

    const PDFSignature& signature = m_signatureField->getSignature();
    const QByteArray& content = signature.getContents();

    // Jakub Melka: we will try to get pkcs7 from signature, then
    // verify signer certificates.
    const unsigned char* data = convertByteArrayToUcharPtr(content);
    if (PKCS7* pkcs7 = d2i_PKCS7(nullptr, &data, content.size()))
    {
        QByteArray buffer;
        if (BIO* inputBuffer = getSignedDataBuffer(result, buffer))
        {
            X509_STORE* store = X509_STORE_new();

            // Above function can fail only if not enough memory. But in this
            // case, this library will crash anyway.
            Q_ASSERT(store);

            // Add certificates from DSS store
            STACK_OF(X509)* certificatesFromPkcs7 = getCertificates(pkcs7);
            STACK_OF(X509)* usedCertificates = sk_X509_new_null();

            // First, add all certificates from pkcs7
            for (int i = 0; i < sk_X509_num(certificatesFromPkcs7); ++i)
            {
                X509* certificate = sk_X509_value(certificatesFromPkcs7, i);
                sk_X509_push(usedCertificates, certificate);
                X509_up_ref(certificate);
            }

            if (m_parameters.dss && !m_parameters.dss->getMasterItem()->Cert.empty())
            {
                // Second, add all certificates from document's security store
                for (const QByteArray& certificateData : m_parameters.dss->getMasterItem()->Cert)
                {
                    const unsigned char* certificateDataBuffer = convertByteArrayToUcharPtr(certificateData);
                    if (X509* certificate = d2i_X509(nullptr, &certificateDataBuffer, certificateData.size()))
                    {
                        sk_X509_push(usedCertificates, certificate);
                    }
                }
            }

            // Jakub Melka: the signature of the timestamp token and the trust in
            // the timestamp authority are two different things. The certificate
            // of the authority is verified by the function verifyCertificateCAdES
            // and its result is reported as a certificate error, so here we check
            // only, that the token is correctly signed and that it really covers
            // the data of the document - otherwise an untrusted or an expired
            // certificate of the authority would be reported as damaged signed
            // data. The certificate, which signed the token, therefore ends the
            // chain here and neither its issuers nor its validity are verified.
            X509_STORE_set_flags(store, X509_V_FLAG_PARTIAL_CHAIN | X509_V_FLAG_NO_CHECK_TIME);
            if (STACK_OF(X509)* signers = PKCS7_get0_signers(pkcs7, usedCertificates, 0))
            {
                if (sk_X509_num(signers) > 0)
                {
                    X509_STORE_add_cert(store, sk_X509_value(signers, 0));
                }

                sk_X509_free(signers);
            }

            // Initialization of verification context
            TS_VERIFY_CTX* ts_context = TS_VERIFY_CTX_new();
            TS_VERIFY_CTX_init(ts_context);
            TS_VERIFY_CTX_set_flags(ts_context, TS_VFY_ALL_DATA & ~TS_VFY_POLICY & ~TS_VFY_NONCE & ~TS_VFY_TSA_NAME);
            setTimestampVerifyContextData(ts_context, store, usedCertificates, inputBuffer);

            STACK_OF(PKCS7_SIGNER_INFO)* signerInfos = PKCS7_get_signer_info(pkcs7);
            addHashAlgorithmFromSignerInfoStack(signerInfos, result);
            addSignatureDateFromSignerInfoStack(signerInfos, result);

            const int verifyValue = TS_RESP_verify_token(ts_context, pkcs7);
            if (verifyValue > 0)
            {
                // The time of a token, which does not belong to the document,
                // says nothing about it, so it is read only from a verified one.
                if (TS_TST_INFO* info = PKCS7_to_TS_TST_INFO(pkcs7))
                {
                    result.setTimestampDate(getDateTimeFromASN(TS_TST_INFO_get_time(info)));
                    TS_TST_INFO_free(info);
                }
            }
            else
            {
                const int reason = ERR_GET_REASON(ERR_get_error());
                switch (reason)
                {
                    case TS_R_MESSAGE_IMPRINT_MISMATCH:
                        result.addSignatureDigestFailureError();
                        break;

                    default:
                        result.addSignatureDataOtherError();
                        break;
                }
            }

            // Finalization of verification context. Function TS_VERIFY_CTX_cleanup also
            // frees all data, such as context, store, etc.
            TS_VERIFY_CTX_cleanup(ts_context);
            TS_VERIFY_CTX_free(ts_context);
        }
        else
        {
            // There is no need for adding error, error is in this case added by getSignedDataBuffer function
        }

        PKCS7_free(pkcs7);
    }
    else
    {
        result.addInvalidSignatureError();
    }

    if (!result.hasSignatureError())
    {
        result.setFlag(PDFSignatureVerificationResult::Signature_OK, true);
    }
}

// This is protected by global mutex, but it is ugly
static PDFSignatureVerificationResult* s_ETSI_currentResult = nullptr;

int PDFSignatureHandler_ETSI_base::verifyCallback(int ok, X509_STORE_CTX* context)
{
    const int errorCode = X509_STORE_CTX_get_error(context);

    switch (errorCode)
    {
        case X509_V_ERR_CRL_NOT_YET_VALID:
        case X509_V_ERR_CRL_HAS_EXPIRED:
        {
            // We will treat this as only warning
            s_ETSI_currentResult->addCertificateCRLValidityTimeExpiredWarning();
            X509_STORE_CTX_set_error(context, X509_V_OK);
            return 1;
        }

        case X509_V_ERR_UNABLE_TO_GET_CRL:
        {
            // We will treat this as only warning. It means that
            // CRL cannot be downloaded or other error occured.
            s_ETSI_currentResult->addCertificateUnableToGetCRLWarning();
            X509_STORE_CTX_set_error(context, X509_V_OK);
            return 1;
        }

        case X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION:
        {
            // We must handle all critical extensions manually
            X509* certificate = X509_STORE_CTX_get_current_cert(context);
            const STACK_OF(X509_EXTENSION)* extensions = X509_get0_extensions(certificate);
            for (int i = 0, extensionsCount = sk_X509_EXTENSION_num(extensions); i < extensionsCount; ++i)
            {
                X509_EXTENSION* extension = sk_X509_EXTENSION_value(extensions, i);

                // Skip non-critical extensions
                if (!X509_EXTENSION_get_critical(extension))
                {
                    continue;
                }

                const ASN1_OBJECT* object = X509_EXTENSION_get_object(extension);
                const int nid = OBJ_obj2nid(object);

                switch (nid)
                {
                    case NID_basic_constraints:
                    case NID_key_usage:
                        // These are handled by OpenSSL
                        continue;

                    case NID_qcStatements:
                    {
                        // We will treat this as only warning
                        s_ETSI_currentResult->addCertificateQualifiedStatementNotVerifiedWarning();
                        X509_STORE_CTX_set_error(context, X509_V_OK);
                        continue;
                    }

                    default:
                        return ok;
                }
            }

            X509_STORE_CTX_set_error(context, X509_V_OK);
            return 1;
        }

        default:
            break;
    }

    return ok;
}

void PDFSignatureHandler_ETSI_base::verifyCertificateCAdES(PDFSignatureVerificationResult& result, int purpose) const
{
    PDFOpenSSLGlobalLock lock;

    s_ETSI_currentResult = &result;

    OpenSSL_add_all_algorithms();

    const PDFSignature& signature = m_signatureField->getSignature();
    const QByteArray& content = signature.getContents();

    // Jakub Melka: we will try to get pkcs7 from signature, then
    // verify signer certificates.
    const unsigned char* data = convertByteArrayToUcharPtr(content);
    if (PKCS7* pkcs7 = d2i_PKCS7(nullptr, &data, content.size()))
    {
        X509_STORE* store = X509_STORE_new();
        X509_STORE_CTX* context = X509_STORE_CTX_new();

        // Above functions can fail only if not enough memory. But in this
        // case, this library will crash anyway.
        Q_ASSERT(store);
        Q_ASSERT(context);

        addTrustedCertificates(store);

        STACK_OF(PKCS7_SIGNER_INFO)* signerInfo = PKCS7_get_signer_info(pkcs7);
        const int signerInfoCount = sk_PKCS7_SIGNER_INFO_num(signerInfo);
        STACK_OF(X509)* certificates = getCertificates(pkcs7);
        if (signerInfo && signerInfoCount > 0 && certificates)
        {
            STACK_OF(X509)* allCertificates = nullptr;
            if (m_parameters.dss && !m_parameters.dss->getMasterItem()->Cert.empty())
            {
                allCertificates = sk_X509_new_null();

                // First, add all certificates from pkcs7
                for (int i = 0; i < sk_X509_num(certificates); ++i)
                {
                    sk_X509_push(allCertificates, sk_X509_value(certificates, i));
                }

                // Second, add all certificates from document's security store
                for (const QByteArray& certificateData : m_parameters.dss->getMasterItem()->Cert)
                {
                    const unsigned char* certificateDataBuffer = convertByteArrayToUcharPtr(certificateData);
                    if (X509* certificate = d2i_X509(nullptr, &certificateDataBuffer, certificateData.size()))
                    {
                        sk_X509_push(allCertificates, certificate);
                    }
                }
            }
            STACK_OF(X509)* usedCertificates = allCertificates ? allCertificates : certificates;

            // Jakub Melka: add certificate revocation lists
            if (m_parameters.dss && !m_parameters.dss->getMasterItem()->CRL.empty())
            {
                for (const QByteArray& crlData : m_parameters.dss->getMasterItem()->CRL)
                {
                    const unsigned char* crlDataBuffer = convertByteArrayToUcharPtr(crlData);
                    if (X509_CRL* crl = d2i_X509_CRL(nullptr, &crlDataBuffer, crlData.size()))
                    {
                        X509_STORE_add_crl(store, crl);
                        X509_CRL_free(crl);
                    }
                }
            }

            for (int i = 0; i < signerInfoCount; ++i)
            {
                PKCS7_SIGNER_INFO* signerInfoValue = sk_PKCS7_SIGNER_INFO_value(signerInfo, i);
                PKCS7_ISSUER_AND_SERIAL* issuerAndSerial = signerInfoValue->issuer_and_serial;
                X509* signer = X509_find_by_issuer_and_serial(usedCertificates, issuerAndSerial->issuer, issuerAndSerial->serial);

                if (!signer)
                {
                    result.addCertificateMissingError();
                    break;
                }

                if (!X509_STORE_CTX_init(context, store, signer, usedCertificates))
                {
                    result.addCertificateGenericError();
                    break;
                }

                if (!X509_STORE_CTX_set_purpose(context, purpose))
                {
                    result.addCertificateGenericError();
                    break;
                }

                unsigned long flags = X509_V_FLAG_TRUSTED_FIRST | X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL | X509_V_FLAG_EXTENDED_CRL_SUPPORT;
                if (m_parameters.ignoreExpirationDate)
                {
                    flags |= X509_V_FLAG_NO_CHECK_TIME;
                }
                X509_STORE_CTX_set_flags(context, flags);
                X509_STORE_CTX_set_verify_cb(context, &PDFSignatureHandler_ETSI_CAdES_detached::verifyCallback);

                int verificationResult = X509_verify_cert(context);
                if (verificationResult <= 0)
                {
                    int error = X509_STORE_CTX_get_error(context);
                    switch (error)
                    {
                        case X509_V_OK:
                            // Strange, this should not occur... when X509_verify_cert fails
                            break;

                        case X509_V_ERR_CERT_HAS_EXPIRED:
                            result.addCertificateExpiredError();
                            break;

                        case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                            result.addCertificateSelfSignedError();
                            break;

                        case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                            result.addCertificateSelfSignedInChainError();
                            break;

                        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
                        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                            result.addCertificateTrustedNotFoundError();
                            break;

                        case X509_V_ERR_CERT_REVOKED:
                            result.addCertificateRevokedError();
                            break;

                        default:
                            result.addCertificateOtherError(error);
                            break;
                    }

                    // We will add certificate info for all certificates
                    const int count = sk_X509_num(usedCertificates);
                    for (int ii = 0; ii < count; ++ii)
                    {
                        result.addCertificateInfo(getCertificateInfo(sk_X509_value(usedCertificates, ii)));
                    }
                }
                else
                {
                    STACK_OF(X509)* validChain = X509_STORE_CTX_get0_chain(context);
                    const int count = sk_X509_num(validChain);
                    for (int ii = 0; ii < count; ++ii)
                    {
                        result.addCertificateInfo(getCertificateInfo(sk_X509_value(validChain, ii)));
                    }
                }
                X509_STORE_CTX_cleanup(context);
            }

            if (allCertificates)
            {
                for (int i = sk_X509_num(certificates); i < sk_X509_num(allCertificates); ++i)
                {
                    X509_free(sk_X509_value(allCertificates, i));
                }

                sk_X509_free(allCertificates);
            }
        }
        else
        {
            result.addNoSignaturesError();
        }

        X509_STORE_CTX_free(context);
        X509_STORE_free(store);

        PKCS7_free(pkcs7);
    }
    else
    {
        result.addInvalidCertificateError();
    }

    if (!result.hasCertificateError())
    {
        result.setFlag(PDFSignatureVerificationResult::Certificate_OK, true);
    }
}

PDFSignatureVerificationResult PDFSignatureHandler_adbe_pkcs7_rsa_sha1::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);

    verifyRSACertificate(result);
    verifyRSASignature(result);

    result.validate();
    return result;
}

X509* PDFSignatureHandler_adbe_pkcs7_rsa_sha1::createCertificate(size_t index) const
{
    const PDFSignature& signature = m_signatureField->getSignature();
    const std::vector<QByteArray>* certificates = signature.getCertificates();
    if (certificates && index < certificates->size())
    {
        const QByteArray& certificateSize = (*certificates)[index];
        const unsigned char* data = convertByteArrayToUcharPtr(certificateSize);
        return d2i_X509(nullptr, &data, certificateSize.size());
    }

    return nullptr;
}

bool PDFSignatureHandler_adbe_pkcs7_rsa_sha1::getMessageDigest(const QByteArray& message,
                                                               ASN1_OCTET_STRING* encryptedString,
                                                               RSA* rsa,
                                                               int& algorithmNID,
                                                               QByteArray& digest) const
{
    if (!getMessageDigestAlgorithm(encryptedString, rsa, algorithmNID))
    {
        return false;
    }

    if (const EVP_MD* md = EVP_get_digestbynid(algorithmNID))
    {
        unsigned int messageDigestSize = EVP_MD_size(md);
        digest.resize(messageDigestSize);

        EVP_MD_CTX* context = EVP_MD_CTX_new();
        Q_ASSERT(context);

        EVP_DigestInit(context, md);
        EVP_DigestUpdate(context, message.constData(), message.size());
        EVP_DigestFinal(context, convertByteArrayToUcharPtr(digest), &messageDigestSize);

        EVP_MD_CTX_free(context);
        return true;
    }

    return false;
}

bool PDFSignatureHandler_adbe_pkcs7_rsa_sha1::getMessageDigestAlgorithm(ASN1_OCTET_STRING* encryptedString,
                                                                        RSA* rsa,
                                                                        int& algorithmNID) const
{
    algorithmNID = 0;

    int size = RSA_size(rsa);
    std::vector<unsigned char> decryptedBuffer(size, 0);
    const int signatureSize = RSA_public_decrypt(encryptedString->length, encryptedString->data, decryptedBuffer.data(), rsa, RSA_PKCS1_PADDING);

    if (signatureSize <= 0)
    {
        return false;
    }

    Q_ASSERT(static_cast<std::size_t>(signatureSize) < decryptedBuffer.size());

    const unsigned char* decryptedBufferPtr = decryptedBuffer.data();
    if (X509_SIG* x509_sig = d2i_X509_SIG(nullptr, &decryptedBufferPtr, signatureSize))
    {
        const X509_ALGOR* algorithm = nullptr;
        const ASN1_OBJECT* algorithmDescriptor = nullptr;

        X509_SIG_get0(x509_sig, &algorithm, nullptr);
        X509_ALGOR_get0(&algorithmDescriptor, nullptr, nullptr, algorithm);
        algorithmNID = OBJ_obj2nid(algorithmDescriptor);

        X509_SIG_free(x509_sig);
        return true;
    }

    return false;
}

void PDFSignatureHandler_adbe_pkcs7_rsa_sha1::verifyRSACertificate(PDFSignatureVerificationResult& result) const
{
    if (X509* certificate = createCertificate(0))
    {
        STACK_OF(X509)* certificates = sk_X509_new_null();
        sk_X509_push(certificates, certificate);

        for (size_t i = 1;; ++i)
        {
            if (X509* currentCertificate = createCertificate(i))
            {
                sk_X509_push(certificates, currentCertificate);
            }
            else
            {
                break;
            }
        }

        X509_STORE* store = X509_STORE_new();
        X509_STORE_CTX* context = X509_STORE_CTX_new();

        // Above functions can fail only if not enough memory. But in this
        // case, this library will crash anyway.
        Q_ASSERT(store);
        Q_ASSERT(context);

        addTrustedCertificates(store);

        X509* signer = certificate;
        if (!X509_STORE_CTX_init(context, store, signer, certificates))
        {
            result.addCertificateGenericError();
        }

        if (!X509_STORE_CTX_set_purpose(context, X509_PURPOSE_SMIME_SIGN))
        {
            result.addCertificateGenericError();
        }

        if (!result.hasCertificateError())
        {
            unsigned long flags = X509_V_FLAG_TRUSTED_FIRST;
            if (m_parameters.ignoreExpirationDate)
            {
                flags |= X509_V_FLAG_NO_CHECK_TIME;
            }
            X509_STORE_CTX_set_flags(context, flags);

            int verificationResult = X509_verify_cert(context);
            if (verificationResult <= 0)
            {
                int error = X509_STORE_CTX_get_error(context);
                switch (error)
                {
                    case X509_V_OK:
                        // Strange, this should not occur... when X509_verify_cert fails
                        break;

                    case X509_V_ERR_CERT_HAS_EXPIRED:
                        result.addCertificateExpiredError();
                        break;

                    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                        result.addCertificateSelfSignedError();
                        break;

                    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                        result.addCertificateSelfSignedInChainError();
                        break;

                    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
                    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                        result.addCertificateTrustedNotFoundError();
                        break;

                    case X509_V_ERR_CERT_REVOKED:
                        result.addCertificateRevokedError();
                        break;

                    default:
                        result.addCertificateOtherError(error);
                        break;
                }

                // We will add certificate info for all certificates
                const int count = sk_X509_num(certificates);
                for (int i = 0; i < count; ++i)
                {
                    result.addCertificateInfo(getCertificateInfo(sk_X509_value(certificates, i)));
                }
            }
            else
            {
                STACK_OF(X509)* validChain = X509_STORE_CTX_get0_chain(context);
                const int count = sk_X509_num(validChain);
                for (int i = 0; i < count; ++i)
                {
                    result.addCertificateInfo(getCertificateInfo(sk_X509_value(validChain, i)));
                }
            }

            X509_STORE_CTX_cleanup(context);
        }

        X509_STORE_CTX_free(context);
        X509_STORE_free(store);

        sk_X509_pop_free(certificates, X509_free);
    }
    else
    {
        result.addInvalidCertificateError();
    }

    if (!result.hasCertificateError())
    {
        result.setFlag(PDFSignatureVerificationResult::Certificate_OK, true);
    }
}

void PDFSignatureHandler_adbe_pkcs7_rsa_sha1::verifyRSASignature(PDFSignatureVerificationResult& result) const
{
    // Jakub Melka: we will use first certificate to validate signature
    openssl_ptr<X509> certificate(createCertificate(0), X509_free);
    if (!certificate)
    {
        result.addSignatureCertificateMissingError();
        return;
    }

    EVP_PKEY* evpKey = X509_get0_pubkey(certificate.get());
    if (!evpKey)
    {
        result.addSignatureCertificateMissingError();
        return;
    }

    openssl_ptr<RSA> rsa(EVP_PKEY_get1_RSA(evpKey), RSA_free);
    if (!rsa)
    {
        result.addSignatureCertificateMissingError();
        return;
    }

    QByteArray outputBuffer;
    openssl_ptr<BIO> bio(this->getSignedDataBuffer(result, outputBuffer), BIO_free_all);
    if (bio)
    {
        const PDFSignature& signature = m_signatureField->getSignature();
        const QByteArray& signKey = signature.getContents();

        const unsigned char* encryptedSign = convertByteArrayToUcharPtr(signKey);
        const unsigned int encryptedSignLength = signKey.length();

        openssl_ptr<ASN1_OCTET_STRING> encryptedString(d2i_ASN1_OCTET_STRING(nullptr, &encryptedSign, encryptedSignLength), ASN1_OCTET_STRING_free);
        if (encryptedString)
        {
            int algorithmNID = NID_undef;
            QByteArray digestBuffer;
            if (!getMessageDigest(outputBuffer, encryptedString.get(), rsa.get(), algorithmNID, digestBuffer))
            {
                result.addSignatureDataOtherError();
                return;
            }

            const unsigned char* digest = convertByteArrayToUcharPtr(digestBuffer);
            const unsigned int digestLength = digestBuffer.length();

            std::array<char, 64> buffer = { };
            OBJ_obj2txt(buffer.data(), int(buffer.size() - 1), OBJ_nid2obj(algorithmNID), 0);
            result.addHashAlgorithm(QString::fromLatin1(buffer.data()));

            const int verifyValue = RSA_verify(algorithmNID, digest, digestLength, encryptedString->data, encryptedString->length, rsa.get());

            if (verifyValue == 0)
            {
                // We have failed, probably due to invalid signature
                const unsigned long errorCode = ERR_GET_REASON(ERR_get_error());

                switch (errorCode)
                {
                    case RSA_R_DIGEST_DOES_NOT_MATCH:
                        result.addSignatureDigestFailureError();
                        break;

                    default:
                        result.addSignatureDataOtherError();
                        break;
                }
            }
        }
        else
        {
            result.addSignatureDataOtherError();
        }
    }

    if (!result.hasSignatureError())
    {
        result.setFlag(PDFSignatureVerificationResult::Signature_OK, true);
    }
}

PDFSignatureVerificationResult PDFSignatureHandler_adbe_pkcs7_sha1::verify() const
{
    PDFSignatureVerificationResult result;
    initializeResult(result);
    verifyCertificate(result);
    verifySignature(result);
    result.validate();
    return result;
}

BIO* PDFSignatureHandler_adbe_pkcs7_sha1::getSignedDataBuffer(PDFSignatureVerificationResult& result, QByteArray& outputBuffer) const
{
    QByteArray temporaryBuffer;
    if (BIO* bio = PDFPublicKeySignatureHandler::getSignedDataBuffer(result, temporaryBuffer))
    {
        // Calculate SHA1
        outputBuffer.resize(SHA_DIGEST_LENGTH);
        SHA1(convertByteArrayToUcharPtr(temporaryBuffer), temporaryBuffer.length(), convertByteArrayToUcharPtr(outputBuffer));
        BIO_free(bio);

        return BIO_new_mem_buf(outputBuffer.data(), outputBuffer.length());
    }

    return nullptr;
}

PDFCertificateInfo PDFPublicKeySignatureHandler::getCertificateInfo(X509* certificate)
{
    return PDFCertificateInfo::getCertificateInfo(certificate);
}

QString PDFPublicKeySignatureHandler::getStringFromX509Name(X509_NAME* name, int nid)
{
    QString result;

    const int stringLocation = X509_NAME_get_index_by_NID(name, nid, -1);
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, stringLocation);
    return getStringFromASN1_STRING(X509_NAME_ENTRY_get_data(entry));
}

QString pdf::PDFPublicKeySignatureHandler::getStringFromASN1_STRING(ASN1_STRING* string)
{
    QString result;

    if (string)
    {
        // Jakub Melka: we must convert entry to UTF8 encoding using function ASN1_STRING_to_UTF8
        unsigned char* utf8Buffer = nullptr;
        int errorCodeOrLength = ASN1_STRING_to_UTF8(&utf8Buffer, string);
        if (errorCodeOrLength > 0)
        {
            result = QString::fromUtf8(reinterpret_cast<const char*>(utf8Buffer), errorCodeOrLength);
        }
        OPENSSL_free(utf8Buffer);
    }

    return result;
}

QDateTime PDFPublicKeySignatureHandler::getDateTimeFromASN(const ASN1_TIME* time)
{
    QDateTime result;

    if (time)
    {
        tm internalTime = { };
        if (ASN1_TIME_to_tm(time, &internalTime) > 0)
        {
#if defined(Q_OS_WIN)
            time_t localTime = _mkgmtime(&internalTime);
#elif defined(Q_OS_UNIX)
            time_t localTime = timegm(&internalTime);
#else
            static_assert(false, "Implement this for another OS!");
#endif
            result = QDateTime::fromSecsSinceEpoch(localTime, Qt::UTC);
        }
    }

    return result;
}

void PDFPublicKeySignatureHandler::addHashAlgorithmFromSignerInfoStack(STACK_OF(PKCS7_SIGNER_INFO)* signerInfoStack, PDFSignatureVerificationResult& result)
{
    if (!signerInfoStack)
    {
        // No signature info provided
        return;
    }

    const int count = sk_PKCS7_SIGNER_INFO_num(signerInfoStack);
    for (int i = 0; i < count; ++i)
    {
        PKCS7_SIGNER_INFO* signerInfoValue = sk_PKCS7_SIGNER_INFO_value(signerInfoStack, i);
        if (signerInfoValue && signerInfoValue->digest_alg && signerInfoValue->digest_alg->algorithm)
        {
            std::array<char, 64> buffer = { };
            OBJ_obj2txt(buffer.data(), int(buffer.size() - 1), signerInfoValue->digest_alg->algorithm, 0);
            result.addHashAlgorithm(QString::fromLatin1(buffer.data()));
        }
    }
}

void PDFPublicKeySignatureHandler::addSignatureDateFromSignerInfoStack(STACK_OF(PKCS7_SIGNER_INFO)* signerInfoStack, PDFSignatureVerificationResult& result)
{
    if (!signerInfoStack)
    {
        // No signature info provided
        return;
    }

    if (sk_PKCS7_SIGNER_INFO_num(signerInfoStack) != 1)
    {
        // Multiple signature infos, or no signature info
        return;
    }

    // Jakub Melka: We will get signed attribute from signer info.
    PKCS7_SIGNER_INFO* signerInfo = sk_PKCS7_SIGNER_INFO_value(signerInfoStack, 0);
    ASN1_TYPE* attribute = PKCS7_get_signed_attribute(signerInfo, NID_pkcs9_signingTime);

    if (!attribute)
    {
        return;
    }

    switch (attribute->type)
    {
        case V_ASN1_UTCTIME:
            result.setSignatureDate(getDateTimeFromASN(attribute->value.utctime));
            break;

        case V_ASN1_GENERALIZEDTIME:
            result.setSignatureDate(getDateTimeFromASN(attribute->value.generalizedtime));
            break;

        default:
            break;
    }
}

void PDFPublicKeySignatureHandler::verifySignatureTimestampAttribute(STACK_OF(PKCS7_SIGNER_INFO)* signerInfoStack, PDFSignatureVerificationResult& result) const
{
    if (!signerInfoStack)
    {
        // No signature info provided
        return;
    }

    if (sk_PKCS7_SIGNER_INFO_num(signerInfoStack) != 1)
    {
        // Multiple signature infos, or no signature info
        return;
    }

    PKCS7_SIGNER_INFO* signerInfo = sk_PKCS7_SIGNER_INFO_value(signerInfoStack, 0);
    ASN1_TYPE* attribute = PKCS7_get_attribute(signerInfo, NID_id_smime_aa_timeStampToken);

    if (!attribute)
    {
        // The signature is not timestamped
        return;
    }

    // Jakub Melka: the timestamp of the signature is an unsigned attribute, so it
    // is not covered by the signature of the signer and anyone can replace it. The
    // time it carries can therefore be used only when the token itself is verified -
    // its signature, the data it timestamps, which must be the signature value of
    // the signer, and the certificate of the timestamp authority, which must be
    // trusted (RFC 3161, chapter 2.4.2). An unverified time is never presented as
    // the time of the signing.
    bool isTimestampVerified = false;

    if (attribute->type == V_ASN1_SEQUENCE)
    {
        const unsigned char* tokenData = ASN1_STRING_get0_data(attribute->value.sequence);
        if (PKCS7* token = d2i_PKCS7(nullptr, &tokenData, ASN1_STRING_length(attribute->value.sequence)))
        {
            X509_STORE* store = X509_STORE_new();
            Q_ASSERT(store);
            addTrustedCertificates(store);

            STACK_OF(X509)* usedCertificates = sk_X509_new_null();

            // First, add all certificates from the token
            STACK_OF(X509)* certificatesFromToken = getCertificates(token);
            for (int i = 0; i < sk_X509_num(certificatesFromToken); ++i)
            {
                X509* certificate = sk_X509_value(certificatesFromToken, i);
                sk_X509_push(usedCertificates, certificate);
                X509_up_ref(certificate);
            }

            if (m_parameters.dss && !m_parameters.dss->getMasterItem()->Cert.empty())
            {
                // Second, add all certificates from document's security store
                for (const QByteArray& certificateData : m_parameters.dss->getMasterItem()->Cert)
                {
                    const unsigned char* certificateDataBuffer = convertByteArrayToUcharPtr(certificateData);
                    if (X509* certificate = d2i_X509(nullptr, &certificateDataBuffer, certificateData.size()))
                    {
                        sk_X509_push(usedCertificates, certificate);
                    }
                }
            }

            // The token timestamps the signature value of the signer
            BIO* timestampedData = BIO_new_mem_buf(ASN1_STRING_get0_data(signerInfo->enc_digest),
                                                   ASN1_STRING_length(signerInfo->enc_digest));

            if (m_parameters.ignoreExpirationDate)
            {
                X509_STORE_set_flags(store, X509_V_FLAG_NO_CHECK_TIME);
            }

            TS_VERIFY_CTX* ts_context = TS_VERIFY_CTX_new();
            TS_VERIFY_CTX_init(ts_context);
            TS_VERIFY_CTX_set_flags(ts_context, TS_VFY_ALL_DATA & ~TS_VFY_POLICY & ~TS_VFY_NONCE & ~TS_VFY_TSA_NAME);
            setTimestampVerifyContextData(ts_context, store, usedCertificates, timestampedData);

            isTimestampVerified = TS_RESP_verify_token(ts_context, token) > 0;

            if (isTimestampVerified)
            {
                if (TS_TST_INFO* info = PKCS7_to_TS_TST_INFO(token))
                {
                    result.setTimestampDate(getDateTimeFromASN(TS_TST_INFO_get_time(info)));
                    TS_TST_INFO_free(info);
                }
            }

            // Function TS_VERIFY_CTX_cleanup also frees the store, the certificates
            // and the data buffer given to the context.
            TS_VERIFY_CTX_cleanup(ts_context);
            TS_VERIFY_CTX_free(ts_context);

            PKCS7_free(token);
        }
    }

    if (!isTimestampVerified)
    {
        result.addSignatureTimestampNotVerifiedWarning();
    }
}

}   // namespace pdf

#ifdef Q_OS_WIN
#include <Windows.h>
#include <wincrypt.h>
#if defined(PDF4QT_USE_PRAGMA_LIB)
#pragma comment(lib, "crypt32.lib")
#endif
#endif

void pdf::PDFPublicKeySignatureHandler::verifyTrustedAuthorityRevocation(PDFSignatureVerificationResult& result) const
{
    // PDF Fire: the revocation lists of the trusted department authorities are
    // stored next to their certificates (they are not downloaded). A signature
    // made before the revocation stays valid, when its time is attested by a
    // timestamp authority - the time written by the signer can't be trusted.
    const std::vector<QByteArray> revocationLists = PDFFireCertificateAuthority::getTrustedAuthorityRevocationLists();
    if (revocationLists.empty() || result.hasFlag(PDFSignatureVerificationResult::Error_Certificate_Revoked))
    {
        return;
    }

    PDFOpenSSLGlobalLock lock;

    const QByteArray& content = m_signatureField->getSignature().getContents();
    const unsigned char* data = convertByteArrayToUcharPtr(content);
    PKCS7* pkcs7 = d2i_PKCS7(nullptr, &data, content.size());
    if (!pkcs7)
    {
        return;
    }

    STACK_OF(PKCS7_SIGNER_INFO)* signerInfo = PKCS7_get_signer_info(pkcs7);
    STACK_OF(X509)* certificates = getCertificates(pkcs7);
    if (signerInfo && certificates && sk_PKCS7_SIGNER_INFO_num(signerInfo) > 0)
    {
        PKCS7_ISSUER_AND_SERIAL* issuerAndSerial = sk_PKCS7_SIGNER_INFO_value(signerInfo, 0)->issuer_and_serial;
        X509* signer = X509_find_by_issuer_and_serial(certificates, issuerAndSerial->issuer, issuerAndSerial->serial);

        // The issuer, whose key signs the revocation list, must be a trusted authority
        std::vector<X509*> authorities;
        for (const QByteArray& certificateData : PDFFireCertificateAuthority::getTrustedAuthorityCertificates())
        {
            const unsigned char* pointer = convertByteArrayToUcharPtr(certificateData);
            if (X509* authority = d2i_X509(nullptr, &pointer, certificateData.size()))
            {
                authorities.push_back(authority);
            }
        }

        for (const QByteArray& crlData : revocationLists)
        {
            if (!signer)
            {
                break;
            }

            const unsigned char* pointer = convertByteArrayToUcharPtr(crlData);
            X509_CRL* crl = d2i_X509_CRL(nullptr, &pointer, crlData.size());
            if (!crl)
            {
                continue;
            }

            bool isRevoked = false;
            QDateTime revocationDate;
            if (X509_NAME_cmp(X509_CRL_get_issuer(crl), X509_get_issuer_name(signer)) == 0)
            {
                for (X509* authority : authorities)
                {
                    EVP_PKEY* publicKey = X509_get_pubkey(authority);
                    const bool isAuthorityOfList = X509_NAME_cmp(X509_get_subject_name(authority), X509_CRL_get_issuer(crl)) == 0 &&
                                                   X509_CRL_verify(crl, publicKey) == 1;
                    EVP_PKEY_free(publicKey);
                    if (!isAuthorityOfList)
                    {
                        continue;
                    }

                    X509_REVOKED* revoked = nullptr;
                    if (X509_CRL_get0_by_serial(crl, &revoked, X509_get_serialNumber(signer)) == 1 && revoked)
                    {
                        isRevoked = true;
                        revocationDate = getDateTimeFromASN(X509_REVOKED_get0_revocationDate(revoked));
                    }
                    break;
                }
            }
            X509_CRL_free(crl);

            if (isRevoked)
            {
                const QDateTime timestampDate = result.getTimestampDate();
                if (timestampDate.isValid() && revocationDate.isValid() && timestampDate < revocationDate)
                {
                    result.addCertificateRevokedAfterTimestampWarning(revocationDate);
                }
                else
                {
                    result.addCertificateRevokedError(revocationDate);
                }
                break;
            }
        }

        for (X509* authority : authorities)
        {
            X509_free(authority);
        }
    }

    PKCS7_free(pkcs7);
}

void pdf::PDFPublicKeySignatureHandler::addTrustedCertificates(X509_STORE* store) const
{
    // PDF Fire: the certificates of the trusted department authorities
    for (const QByteArray& certificateData : PDFFireCertificateAuthority::getTrustedAuthorityCertificates())
    {
        const unsigned char* pointer = convertByteArrayToUcharPtr(certificateData);
        if (X509* certificate = d2i_X509(nullptr, &pointer, certificateData.size()))
        {
            X509_STORE_add_cert(store, certificate);
            X509_free(certificate);
        }
    }

    if (m_parameters.store)
    {
        const PDFCertificateEntries& certificates = m_parameters.store->getCertificates();
        for (const auto& entry : certificates)
        {
            QByteArray certificateData = entry.info.getCertificateData();
            const unsigned char* pointer = convertByteArrayToUcharPtr(certificateData);
            X509* certificate = d2i_X509(nullptr, &pointer, certificateData.length());
            if (certificate)
            {
                X509_STORE_add_cert(store, certificate);
                X509_free(certificate);
            }
        }
    }

#ifdef Q_OS_WIN
    if (m_parameters.useSystemCertificateStore)
    {
        HCERTSTORE certStore = CertOpenSystemStore(0, L"ROOT");
        PCCERT_CONTEXT context = nullptr;
        if (certStore)
        {
            while (context = CertEnumCertificatesInStore(certStore, context))
            {
                const unsigned char* pointer = context->pbCertEncoded;
                X509* certificate = d2i_X509(nullptr, &pointer, context->cbCertEncoded);
                if (certificate)
                {
                    X509_STORE_add_cert(store, certificate);
                    X509_free(certificate);
                }
            }

            CertCloseStore(certStore, CERT_CLOSE_STORE_FORCE_FLAG);
        }
    }
#endif

    if (m_parameters.useSystemCertificateStore)
    {
        PDFCertificateEntries aatlCertificates = PDFCertificateStore::getAATLCertificates();
        for (const PDFCertificateEntry& entry : aatlCertificates)
        {
            QByteArray certificateData = entry.info.getCertificateData();
            const unsigned char* pointer = convertByteArrayToUcharPtr(certificateData);
            X509* certificate = d2i_X509(nullptr, &pointer, certificateData.size());
            if (certificate)
            {
                X509_STORE_add_cert(store, certificate);
                X509_free(certificate);
            }
        }
    }
}

#if defined(PDF4QT_COMPILER_MINGW) || defined(PDF4QT_COMPILER_GCC)
#pragma GCC diagnostic pop
#endif

#if defined(PDF4QT_COMPILER_MSVC)
#pragma warning(pop)
#endif
