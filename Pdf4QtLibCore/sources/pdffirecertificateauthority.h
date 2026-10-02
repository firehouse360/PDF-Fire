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

#ifndef PDFFIRECERTIFICATEAUTHORITY_H
#define PDFFIRECERTIFICATEAUTHORITY_H

#include "pdfglobal.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{
struct PDFCertificateEntry;

/// PDF Fire: what is written into the signing record, when a member signs with
/// a certificate of a department authority. The department decides it, when the
/// certificate is issued; it is stored in the certificate itself.
struct PDF4QTLIBCORESHARED_EXPORT PDFFireSigningPolicy
{
    bool recordOperatingSystem = true;  ///< Operating system
    bool recordComputer = true;         ///< Computer name and user account
    bool recordLocalAddresses = true;   ///< IP addresses of the computer
    bool recordPublicAddress = true;    ///< Public IP address (looked up online)
    bool isLocked = false;              ///< The member can't turn the items off

    /// Name of the authority, which issued the certificate (not encoded, it is
    /// taken from the issuer of the certificate)
    QString authorityName;

    QByteArray encode() const;
    static std::optional<PDFFireSigningPolicy> decode(const QByteArray& text);

    /// Object identifier of the certificate extension holding the policy. It is
    /// a UUID based identifier (ITU-T X.667, the arc 2.25), which needs no
    /// registration. The extension is not critical, other programs ignore it.
    static constexpr const char* EXTENSION_OID = "2.25.200250744661301489373361780243810269237";
};

/// PDF Fire: a certificate authority of a department. The authority has its own
/// certificate (the root), and issues the signing certificates of the members.
/// Anybody, who trusts the root certificate, trusts the signatures of the
/// members. The authority can revoke a certificate (a member leaves, a computer
/// is stolen) - the revoked certificates are listed in a certificate revocation
/// list (CRL), which is published on a web address written into the certificates.
///
/// The authority is a folder with these files:
///     authority-private.p12   the key of the authority (encrypted by the passphrase) - SECRET
///     authority.cer           the certificate of the authority - shared with everybody
///     authority.crl           the revocation list - uploaded to the web address
///     register.json           the list of the issued certificates
class PDF4QTLIBCORESHARED_EXPORT PDFFireCertificateAuthority
{
public:
    PDFFireCertificateAuthority();
    ~PDFFireCertificateAuthority();

    PDFFireCertificateAuthority(const PDFFireCertificateAuthority&) = delete;
    PDFFireCertificateAuthority& operator=(const PDFFireCertificateAuthority&) = delete;

    struct AuthorityInfo
    {
        QString name;               ///< Common name, e.g. "Prospect VFD Signing Authority"
        QString organization;       ///< e.g. "Prospect VFD"
        QString country = "US";
        QString crlUrl;             ///< Web address of the revocation list (can be empty)
        int validityYears = 20;
    };

    struct MemberInfo
    {
        QString name;
        QString title;              ///< Rank or position (optional)
        QString email;              ///< (optional)
        int validityYears = 2;
        PDFFireSigningPolicy policy;
    };

    enum class RevocationReason
    {
        Unspecified = 0,
        KeyCompromise = 1,          ///< Lost or stolen computer, password known by others
        Superseded = 4,             ///< Replaced by a new certificate
        CessationOfOperation = 5    ///< The member left the department
    };

    struct IssuedCertificate
    {
        QString serialNumber;       ///< Hexadecimal digits
        QString name;
        QString title;
        QString email;
        QDateTime issued;
        QDateTime expires;
        QDateTime revoked;          ///< Invalid, when not revoked
        RevocationReason revocationReason = RevocationReason::Unspecified;
        QString policy;             ///< Encoded signing policy

        bool isRevoked() const { return revoked.isValid(); }
    };

    static constexpr const char* KEY_FILE_NAME = "authority-private.p12";
    static constexpr const char* CERTIFICATE_FILE_NAME = "authority.cer";
    static constexpr const char* CRL_FILE_NAME = "authority.crl";
    static constexpr const char* REGISTER_FILE_NAME = "register.json";
    static constexpr int MINIMAL_PASSPHRASE_LENGTH = 12;

    /// Creates a new authority in the (empty) folder
    bool create(const QString& directory, const AuthorityInfo& info, const QString& passphrase, QString* errorMessage);

    /// Opens the authority in the folder
    bool open(const QString& directory, const QString& passphrase, QString* errorMessage);

    bool isOpen() const;
    QString getDirectory() const { return m_directory; }
    QString getName() const { return m_info.name; }
    QString getCrlUrl() const { return m_info.crlUrl; }
    QDateTime getCrlNextUpdate() const { return m_crlNextUpdate; }
    const std::vector<IssuedCertificate>& getIssuedCertificates() const { return m_issued; }

    /// Returns the certificate of the authority (DER)
    QByteArray getCertificate() const { return m_certificate; }

    /// Issues a certificate of a member. The certificate, its key and the
    /// certificate of the authority are returned as a PKCS#12 file protected by
    /// the password. The register is saved.
    bool issue(const MemberInfo& member, const QString& memberPassword, QByteArray* pkcs12, IssuedCertificate* issuedCertificate, QString* errorMessage);

    /// Revokes the certificate, the register is saved and the revocation list
    /// is written again
    bool revoke(const QString& serialNumber, RevocationReason reason, QString* errorMessage);

    /// Writes the revocation list (it is valid for a year, then it must be written again)
    bool writeRevocationList(QString* errorMessage);

    /// Creates a password, which is easy to read and type (no confusable characters)
    static QString generatePassword();

    // Trust - the certificates of the trusted department authorities

    /// Folder of the authorities trusted by the user
    static QString getTrustedAuthoritiesDirectory();

    /// Folders of the trusted authorities - the folder of the user and the folder
    /// for the whole computer (/etc/pdf-fire/trusted-authorities, set by the administrator)
    static QStringList getTrustedAuthoritiesDirectories();

    /// Adds the certificate of an authority (DER or PEM) to the trusted authorities
    /// of the user, with its revocation list (DER or PEM, can be empty)
    static bool trustAuthority(const QByteArray& certificate, const QByteArray& revocationList, QString* errorMessage);

    /// Returns the fingerprint (SHA-256) of the certificate (DER or PEM), in groups
    /// of 4 digits - read aloud or compared with a printed copy, before the
    /// certificate is trusted. Empty, when the data is not a certificate.
    static QString getFingerprint(const QByteArray& certificate);

    /// Returns the common name of the certificate (DER or PEM)
    static QString getCertificateName(const QByteArray& certificate);

    /// Is the certificate (DER) among the trusted authorities?
    static bool isAuthorityTrusted(const QByteArray& certificate);

    /// An authority trusted on this computer
    struct TrustedAuthority
    {
        QString name;
        QString fingerprint;
        QString fileName;           ///< File of the certificate
        bool isForAllUsers = false; ///< Set by the administrator (it can't be removed here)
        QDateTime expires;          ///< End of validity of the certificate of the authority
        QDateTime crlNextUpdate;    ///< Validity of the revocation list (invalid = no list)
    };

    /// Returns the authorities trusted on this computer
    static std::vector<TrustedAuthority> getTrustedAuthorities();

    /// Stops trusting the authority (its certificate and revocation list are removed)
    static bool removeTrustedAuthority(const QString& fileName, QString* errorMessage);

    /// Certificates (DER) of the trusted authorities
    static std::vector<QByteArray> getTrustedAuthorityCertificates();

    /// Revocation lists (DER), which are next to the certificates of the trusted authorities
    static std::vector<QByteArray> getTrustedAuthorityRevocationLists();

    /// Overrides the folders of the trusted authorities (the unit tests)
    static void setTrustedAuthoritiesDirectoriesOverride(const QStringList& directories);

    /// Reads the signing policy of the certificate, when the certificate was
    /// issued by a department authority (the certificate is readable without the
    /// password, when it was issued by PDF Fire; otherwise the password is used)
    static std::optional<PDFFireSigningPolicy> readSigningPolicy(const PDFCertificateEntry& entry, const QString& password);

    /// Returns the certificate (DER) of the owner of the PKCS#12 file, when it is
    /// readable without the password (a certificate issued by an authority)
    static QByteArray readPublicCertificate(const QByteArray& pkcs12);

    /// Returns the certificate (DER) of the authority, which is stored in the
    /// PKCS#12 file of a member (empty, when there is none)
    static QByteArray readAuthorityCertificate(const QByteArray& pkcs12);

    /// Returns the common name of the issuer of the certificate
    static QString readIssuerName(const PDFCertificateEntry& entry, const QString& password);

private:
    bool saveRegister(QString* errorMessage) const;
    bool loadRegister(QString* errorMessage);

    struct KeyData;

    QString m_directory;
    AuthorityInfo m_info;
    QByteArray m_certificate;   ///< DER
    KeyData* m_key = nullptr;
    std::vector<IssuedCertificate> m_issued;
    qint64 m_crlNumber = 0;
    QDateTime m_crlNextUpdate;
};

}   // namespace pdf

#endif // PDFFIRECERTIFICATEAUTHORITY_H
