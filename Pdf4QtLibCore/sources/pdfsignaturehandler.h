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

#ifndef PDFSIGNATUREHANDLER_H
#define PDFSIGNATUREHANDLER_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfutils.h"
#include "pdfcertificatestore.h"

#include <QString>
#include <QDateTime>

#include <optional>

class QDataStream;

namespace pdf
{
class PDFForm;
class PDFObjectStorage;
class PDFCertificateStore;
class PDFFormFieldSignature;
class PDFDocumentSecurityStore;

/// Signature reference dictionary.
class PDFSignatureReference
{
public:

    enum class TransformMethod
    {
        Invalid,
        DocMDP,
        UR,
        FieldMDP
    };

    TransformMethod getTransformMethod() const { return m_transformMethod; }
    const PDFObject& getTransformParams() const { return m_transformParams; }
    const PDFObject& getData() const { return m_data; }
    const QByteArray& getDigestMethod() const { return m_digestMethod; }

    /// PDF Fire: the permissions of a certification (DocMDP, P: 1 = no changes,
    /// 2 = form filling and signing, 3 = also comments), 0 for other references
    PDFInteger getDocMDPPermissions() const { return m_docMDPPermissions; }

    /// Tries to parse the signature reference. No exception is thrown, in case of error,
    /// invalid signature reference object is returned.
    /// \param storage Object storage
    /// \param object Object containing the signature
    static PDFSignatureReference parse(const PDFObjectStorage* storage, PDFObject object);

private:
    TransformMethod m_transformMethod = TransformMethod::Invalid;
    PDFObject m_transformParams;
    PDFObject m_data;
    QByteArray m_digestMethod;
    PDFInteger m_docMDPPermissions = 0;
};

/// Signature dictionary. Contains digital signature. This signature can be validated by signature validator.
/// This object contains certificates, digital signatures, and other information about signature.
class PDFSignature
{
public:

    enum class Type
    {
        Invalid,
        Sig,
        DocTimeStamp
    };

    struct ByteRange
    {
        PDFInteger offset = 0;
        PDFInteger size = 0;
    };

    using ByteRanges = std::vector<ByteRange>;

    struct Changes
    {
        PDFInteger pagesAltered = 0;
        PDFInteger fieldsAltered = 0;
        PDFInteger fieldsFilled = 0;
    };

    enum class AuthentificationType
    {
        Invalid,
        PIN,
        Password,
        Fingerprint
    };

    /// Tries to parse the signature. No exception is thrown, in case of error,
    /// invalid signature object is returned.
    /// \param storage Object storage
    /// \param object Object containing the signature
    static PDFSignature parse(const PDFObjectStorage* storage, PDFObject object);

    Type getType() const { return m_type; }
    const QByteArray& getFilter() const { return m_filter; }
    const QByteArray& getSubfilter() const { return m_subfilter; }
    const QByteArray& getContents() const { return m_contents; }
    const std::vector<QByteArray>* getCertificates() const { return m_certificates.has_value() ? &m_certificates.value() : nullptr; }
    const ByteRanges& getByteRanges() const { return m_byteRanges; }
    const std::vector<PDFSignatureReference>& getReferences() const { return m_references; }
    const Changes* getChanges() const { return m_changes.has_value() ? &m_changes.value() : nullptr; }

    const QString& getName() const { return m_name; }
    const QDateTime& getSigningDateTime() const { return m_signingDateTime; }
    const QString& getLocation() const { return m_location; }
    const QString& getReason() const { return m_reason; }
    const QString& getContactInfo() const { return m_contactInfo; }
    PDFInteger getR() const { return m_R; }
    PDFInteger getV() const { return m_V; }
    const PDFObject& getPropBuild() const { return m_propBuild; }
    PDFInteger getPropTime() const { return m_propTime; }
    AuthentificationType getAuthentificationType() const { return m_propType; }

    /// PDF Fire: the signing record (audit trail) written by PDF Fire, empty if none
    const QString& getSigningRecord() const { return m_signingRecord; }

private:
    Type m_type = Type::Invalid;
    QByteArray m_filter;    ///< Preferred signature handler name
    QByteArray m_subfilter; ///< Describes encoding of signature
    QByteArray m_contents;
    std::optional<std::vector<QByteArray>> m_certificates; ///< Certificate chain (only for adbe.x509.rsa_sha1)
    ByteRanges m_byteRanges;
    std::vector<PDFSignatureReference> m_references;
    std::optional<Changes> m_changes;

    QString m_name; ///< Name of signer. Should rather be extracted from signature.
    QDateTime m_signingDateTime; ///< Signing date and time. Should be extracted from signature, if possible.
    QString m_location; ///< CPU hostname or physical location of signing
    QString m_reason; ///< Reason for signing
    QString m_contactInfo; ///< Contact info for verifying the signature
    PDFInteger m_R; ///< Version of signature handler. Obsolete.
    PDFInteger m_V; ///< Version of signature dictionary format. 1 if References should be used.
    PDFObject m_propBuild;
    PDFInteger m_propTime = 0;
    AuthentificationType m_propType = AuthentificationType::Invalid;
    QString m_signingRecord; ///< PDF Fire: signing record (audit trail)
};

class PDF4QTLIBCORESHARED_EXPORT PDFSignatureVerificationResult
{
public:
    explicit PDFSignatureVerificationResult() = default;
    explicit PDFSignatureVerificationResult(PDFSignature::Type type, PDFObjectReference signatureFieldReference, QString qualifiedName) :
        m_type(type),
        m_signatureFieldReference(signatureFieldReference),
        m_signatureFieldQualifiedName(qualifiedName)
    {

    }

    enum class Status
    {
        OK,
        Warning,
        Error
    };

    enum VerificationFlag
    {
        None                                            = 0x00000000,  ///< Used only for initialization
        OK                                              = 0x00000001,  ///< Both certificate and signature is OK
        Certificate_OK                                  = 0x00000002,  ///< Certificate is OK
        Signature_OK                                    = 0x00000004,  ///< Signature is OK
        Error_NoHandler                                 = 0x00000008,  ///< No signature handler for given signature
        Error_Generic                                   = 0x00000010,  ///< Generic error (uknown general error)

        Error_Certificate_Invalid                       = 0x00000020,  ///< Certificate is invalid
        Error_Certificate_NoSignatures                  = 0x00000040,  ///< No signature found in certificate data
        Error_Certificate_Missing                       = 0x00000080,  ///< Certificate is missing
        Error_Certificate_Generic                       = 0x00000100,  ///< Generic error during certificate verification
        Error_Certificate_Expired                       = 0x00000200,  ///< Certificate has expired
        Error_Certificate_SelfSigned                    = 0x00000400,  ///< Self signed certificate
        Error_Certificate_SelfSignedChain               = 0x00000800,  ///< Self signed certificate in chain
        Error_Certificate_TrustedNotFound               = 0x00001000,  ///< No trusted certificate was found
        Error_Certificate_Revoked                       = 0x00002000,  ///< Certificate has been revoked
        Error_Certificate_Other                         = 0x00004000,  ///< Other certificate error. See OpenSSL code for details.

        Error_Signature_Invalid                         = 0x00008000,  ///< Signature is invalid for some reason
        Error_Signature_SourceCertificateMissing        = 0x00010000,  ///< Source certificate of signature is missing
        Error_Signature_NoSignaturesFound               = 0x00020000,  ///< No signatures found
        Error_Signature_DigestFailure                   = 0x00040000,  ///< Digest failure
        Error_Signature_DataOther                       = 0x00080000,  ///< Signed data were not verified
        Error_Signature_DataCoveredBySignatureMissing   = 0x00100000,  ///< Data covered by signature are not present

        Warning_Signature_NotCoveredBytes               = 0x00200000,  ///< Some bytes in source data are not covered by signature
        Warning_Certificate_CRLValidityTimeExpired      = 0x00400000,  ///< Certificate revocation list was not checked, because it's validity expired
        Warning_Certificate_QualifiedStatement          = 0x00800000,  ///< Qualified certificate statement not verified
        Warning_Certificate_UnableToGetCRL              = 0x01000000,  ///< Unable to get CRL
        Warning_Signature_TimestampNotVerified          = 0x02000000,  ///< Timestamp of the signature could not be verified
        Error_Signature_ByteRangeInvalid                = 0x04000000,  ///< PDF Fire: byte ranges of the signature do not have the required structure
        Warning_Certificate_RevokedAfterTimestamp       = 0x08000000,  ///< PDF Fire: certificate was revoked after the timestamped signature was made
        Error_Signature_ChangedAfterCertification       = 0x10000000,  ///< PDF Fire: the document was changed, the certification allows no changes
        Warning_Signature_ChangedAfterCertification     = 0x20000000,  ///< PDF Fire: the document was changed after the certification (allowed changes only?)

        Error_Certificates_Mask = Error_Certificate_Invalid | Error_Certificate_NoSignatures | Error_Certificate_Missing | Error_Certificate_Generic |
                                  Error_Certificate_Expired | Error_Certificate_SelfSigned | Error_Certificate_SelfSignedChain | Error_Certificate_TrustedNotFound |
                                  Error_Certificate_Revoked | Error_Certificate_Other,

        Error_Signatures_Mask = Error_Signature_Invalid | Error_Signature_SourceCertificateMissing | Error_Signature_NoSignaturesFound |
                                Error_Signature_DigestFailure | Error_Signature_DataOther | Error_Signature_DataCoveredBySignatureMissing |
                                Error_Signature_ByteRangeInvalid | Error_Signature_ChangedAfterCertification,

        Warning_Certificates_Mask = Warning_Certificate_CRLValidityTimeExpired | Warning_Certificate_QualifiedStatement | Warning_Certificate_UnableToGetCRL |
                                    Warning_Certificate_RevokedAfterTimestamp,
        Warning_Signatures_Mask = Warning_Signature_NotCoveredBytes | Warning_Signature_TimestampNotVerified | Warning_Signature_ChangedAfterCertification,

        Warnings_Mask = Warning_Certificates_Mask | Warning_Signatures_Mask
    };
    Q_DECLARE_FLAGS(VerificationFlags, VerificationFlag)

    PDFSignature::Type getType() const;
    void setType(const PDFSignature::Type& type);

    /// Adds no handler error for given signature format
    /// \param format Signature format
    void addNoHandlerError(const QByteArray& format);

    void addInvalidCertificateError();
    void addNoSignaturesError();
    void addCertificateMissingError();
    void addCertificateGenericError();
    void addCertificateExpiredError();
    void addCertificateSelfSignedError();
    void addCertificateSelfSignedInChainError();
    void addCertificateTrustedNotFoundError();
    void addCertificateRevokedError();

    /// PDF Fire: the certificate was revoked (on the date), the signature was made
    /// before (its time is attested by a timestamp authority)
    void addCertificateRevokedError(const QDateTime& revocationDate);
    void addCertificateRevokedAfterTimestampWarning(const QDateTime& revocationDate);
    void addCertificateOtherError(int error);
    void addInvalidSignatureError();
    void addSignatureNoSignaturesFoundError();
    void addSignatureCertificateMissingError();
    void addSignatureDigestFailureError();
    void addSignatureDataOtherError();
    void addSignatureDataCoveredBySignatureMissingError();
    void addSignatureByteRangeInvalidError();

    void addSignatureNotCoveredBytesWarning(PDFInteger count);
    void addSignatureTimestampNotVerifiedWarning();
    void addCertificateCRLValidityTimeExpiredWarning();
    void addCertificateQualifiedStatementNotVerifiedWarning();
    void addCertificateUnableToGetCRLWarning();

    bool isValid() const { return hasFlag(OK); }
    bool isCertificateValid() const { return hasFlag(Certificate_OK); }
    bool isSignatureValid() const { return hasFlag(Signature_OK); }
    bool hasError() const { return !isValid(); }
    bool hasWarning() const { return m_flags & Warnings_Mask; }
    bool hasCertificateError() const { return m_flags & Error_Certificates_Mask; }
    bool hasSignatureError() const { return m_flags & Error_Signatures_Mask; }
    bool hasCertificateWarning() const { return m_flags & Warning_Certificates_Mask; }
    bool hasSignatureWarning() const { return m_flags & Warning_Signatures_Mask; }
    bool hasFlag(VerificationFlag flag) const { return m_flags.testFlag(flag); }
    void setFlag(VerificationFlag flag, bool value) { m_flags.setFlag(flag, value); }

    PDFObjectReference getSignatureFieldReference() const { return m_signatureFieldReference; }
    const QString& getSignatureFieldQualifiedName() const { return m_signatureFieldQualifiedName; }
    const QStringList& getErrors() const { return m_errors; }
    const QStringList& getWarnings() const { return m_warnings; }
    const QStringList& getHashAlgorithms() const { return m_hashAlgorithms; }
    const PDFCertificateInfos& getCertificateInfos() const { return m_certificateInfos; }

    void setSignatureFieldQualifiedName(const QString& signatureFieldQualifiedName);
    void setSignatureFieldReference(PDFObjectReference signatureFieldReference);

    void addCertificateInfo(PDFCertificateInfo info) { m_certificateInfos.emplace_back(qMove(info)); }
    void addHashAlgorithm(const QString& algorithm);

    /// Adds OK flag, if both certificate and signature are valid
    void validate();

    QDateTime getSignatureDate() const;
    void setSignatureDate(const QDateTime& signatureDate);

    QDateTime getTimestampDate() const;
    void setTimestampDate(const QDateTime& timestampDate);

    QByteArray getSignatureHandler() const;
    void setSignatureHandler(const QByteArray& signatureFilter);

    Status getCertificateStatus() const;
    Status getSignatureStatus() const;

    QString getCertificateStatusText() const { return getStatusText(getCertificateStatus()); }
    QString getSignatureStatusText() const { return getStatusText(getSignatureStatus()); }

    static QString getStatusText(Status status);

    const PDFClosedIntervalSet& getBytesCoveredBySignature() const;
    void setBytesCoveredBySignature(const PDFClosedIntervalSet& bytesCoveredBySignature);

    /// PDF Fire: the details of the signature dictionary, shown to the user
    const QString& getReason() const { return m_reason; }
    const QString& getLocation() const { return m_location; }
    const QString& getContactInfo() const { return m_contactInfo; }
    const QString& getSigningRecord() const { return m_signingRecord; }
    void setSignatureDetails(const PDFSignature& signature);

    /// PDF Fire: the certification of the document by this signature (DocMDP
    /// permissions 1-3), 0 = an ordinary (approval) signature
    PDFInteger getCertification() const { return m_certification; }

    /// PDF Fire: checks the changes made after the certification
    void verifyCertification();

private:
    PDFSignature::Type m_type = PDFSignature::Type::Invalid;
    VerificationFlags m_flags = None;
    PDFObjectReference m_signatureFieldReference;
    QString m_signatureFieldQualifiedName;
    QDateTime m_signatureDate;
    QDateTime m_timestampDate;
    QStringList m_errors;
    QStringList m_warnings;
    QStringList m_hashAlgorithms;
    QByteArray m_signatureHandler;
    PDFCertificateInfos m_certificateInfos;
    PDFClosedIntervalSet m_bytesCoveredBySignature;
    QString m_reason;
    QString m_location;
    QString m_contactInfo;
    QString m_signingRecord;
    PDFInteger m_certification = 0;
};

/// Signature handler. Can verify both certificate and signature validity.
class PDF4QTLIBCORESHARED_EXPORT PDFSignatureHandler
{
public:
    explicit PDFSignatureHandler() = default;
    virtual ~PDFSignatureHandler() = default;

    virtual PDFSignatureVerificationResult verify() const = 0;

    struct Parameters
    {
        const PDFCertificateStore* store = nullptr;
        const PDFDocumentSecurityStore* dss = nullptr;
        bool enableVerification = true;
        bool ignoreExpirationDate = false;
        bool useSystemCertificateStore = true;
    };

    /// Tries to verify all signatures in the form. If form is invalid, then
    /// empty vector is returned.
    /// \param form Form
    /// \param sourceData Source data
    /// \param parameters Verification settings
    static std::vector<PDFSignatureVerificationResult> verifySignatures(const PDFForm& form, const QByteArray& sourceData, const Parameters& parameters);

private:

    /// Creates signature handler using format specified by signature in signature field.
    /// If signature format is unknown, then nullptr is returned.
    /// \param signatureField Signature field
    /// \param sourceData
    /// \param parameters Verification settings
    static PDFSignatureHandler* createHandler(const PDFFormFieldSignature* signatureField, const QByteArray& sourceData, const Parameters& parameters);
};

} // namespace pdf

#endif // PDFSIGNATUREHANDLER_H
