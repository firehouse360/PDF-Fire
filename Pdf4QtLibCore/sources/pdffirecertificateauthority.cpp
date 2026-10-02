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

#include "pdffirecertificateauthority.h"
#include "pdfcertificatestore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

namespace pdf
{

namespace
{

template<typename T>
using ssl_ptr = std::unique_ptr<T, void(*)(T*)>;

QString tr(const char* text)
{
    return QCoreApplication::translate("PDFFireCertificateAuthority", text);
}

/// Key derivation iterations of the files - high, the files may be copied anywhere
constexpr int AUTHORITY_ITERATIONS = 200000;
constexpr int MEMBER_ITERATIONS = 100000;
constexpr int AUTHORITY_KEY_BITS = 3072;
constexpr int MEMBER_KEY_BITS = 2048;
constexpr int CRL_VALIDITY_DAYS = 365;

QByteArray toDer(X509* certificate)
{
    unsigned char* buffer = nullptr;
    const int length = i2d_X509(certificate, &buffer);
    QByteArray result;
    if (length > 0)
    {
        result = QByteArray(reinterpret_cast<const char*>(buffer), length);
    }
    OPENSSL_free(buffer);
    return result;
}

/// Reads a certificate, DER or PEM
X509* readCertificate(const QByteArray& data)
{
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(data.constData());
    if (X509* certificate = d2i_X509(nullptr, &pointer, data.size()))
    {
        return certificate;
    }

    ssl_ptr<BIO> bio(BIO_new_mem_buf(data.constData(), int(data.size())), &BIO_free_all);
    return PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
}

/// Reads a revocation list, DER or PEM
X509_CRL* readRevocationList(const QByteArray& data)
{
    const unsigned char* pointer = reinterpret_cast<const unsigned char*>(data.constData());
    if (X509_CRL* crl = d2i_X509_CRL(nullptr, &pointer, data.size()))
    {
        return crl;
    }

    ssl_ptr<BIO> bio(BIO_new_mem_buf(data.constData(), int(data.size())), &BIO_free_all);
    return PEM_read_bio_X509_CRL(bio.get(), nullptr, nullptr, nullptr);
}

QDateTime fromAsn1Time(const ASN1_TIME* time)
{
    struct tm value = { };
    if (time && ASN1_TIME_to_tm(time, &value) == 1)
    {
        return QDateTime(QDate(value.tm_year + 1900, value.tm_mon + 1, value.tm_mday), QTime(value.tm_hour, value.tm_min, value.tm_sec), QTimeZone::UTC);
    }
    return QDateTime();
}

QString serialToString(const ASN1_INTEGER* serial)
{
    ssl_ptr<BIGNUM> number(ASN1_INTEGER_to_BN(serial, nullptr), &BN_free);
    char* hex = BN_bn2hex(number.get());
    const QString result = QString::fromLatin1(hex).toUpper();
    OPENSSL_free(hex);
    return result;
}

bool writeFile(const QString& fileName, const QByteArray& data, bool isPrivate)
{
    // The file is written completely or not at all (a broken key file would lose the authority)
    QSaveFile file(fileName);
    if (!file.open(QFile::WriteOnly))
    {
        return false;
    }
    if (isPrivate)
    {
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    }
    file.write(data);
    return file.commit();
}

bool addExtension(X509* certificate, X509* issuer, int nid, const char* value)
{
    X509V3_CTX context = { };
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
    X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value);
    if (!extension)
    {
        return false;
    }
    const bool result = X509_add_ext(certificate, extension, -1) == 1;
    X509_EXTENSION_free(extension);
    return result;
}

bool setRandomSerialNumber(X509* certificate)
{
    // A random positive serial number of 159 bits (RFC 5280: at most 20 bytes)
    ssl_ptr<BIGNUM> number(BN_new(), &BN_free);
    if (!number || BN_rand(number.get(), 159, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) != 1)
    {
        return false;
    }
    if (BN_is_zero(number.get()))
    {
        BN_set_word(number.get(), 1);
    }
    return BN_to_ASN1_INTEGER(number.get(), X509_get_serialNumber(certificate)) != nullptr;
}

void addNameEntry(X509_NAME* name, const char* field, const QString& value)
{
    if (!value.trimmed().isEmpty())
    {
        const QByteArray utf8 = value.trimmed().toUtf8();
        X509_NAME_add_entry_by_txt(name, field, MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(utf8.constData()), int(utf8.size()), -1, 0);
    }
}

QString getCommonName(X509_NAME* name)
{
    const int index = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (index < 0)
    {
        return QString();
    }
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, index));
    unsigned char* utf8 = nullptr;
    const int length = ASN1_STRING_to_UTF8(&utf8, data);
    QString result;
    if (length >= 0)
    {
        result = QString::fromUtf8(reinterpret_cast<const char*>(utf8), length);
    }
    OPENSSL_free(utf8);
    return result;
}

bool addPolicyExtension(X509* certificate, const PDFFireSigningPolicy& policy)
{
    ssl_ptr<ASN1_OBJECT> object(OBJ_txt2obj(PDFFireSigningPolicy::EXTENSION_OID, 1), &ASN1_OBJECT_free);
    ssl_ptr<ASN1_UTF8STRING> text(ASN1_UTF8STRING_new(), &ASN1_UTF8STRING_free);
    const QByteArray encoded = policy.encode();
    if (!object || !text || ASN1_STRING_set(text.get(), encoded.constData(), int(encoded.size())) != 1)
    {
        return false;
    }

    // The value of an extension is the DER encoding of its type (here UTF8String)
    unsigned char* der = nullptr;
    const int derLength = i2d_ASN1_UTF8STRING(text.get(), &der);
    ssl_ptr<ASN1_OCTET_STRING> octets(ASN1_OCTET_STRING_new(), &ASN1_OCTET_STRING_free);
    const bool isSet = derLength > 0 && octets && ASN1_OCTET_STRING_set(octets.get(), der, derLength) == 1;
    OPENSSL_free(der);
    if (!isSet)
    {
        return false;
    }

    X509_EXTENSION* extension = X509_EXTENSION_create_by_OBJ(nullptr, object.get(), 0, octets.get());
    if (!extension)
    {
        return false;
    }
    const bool result = X509_add_ext(certificate, extension, -1) == 1;
    X509_EXTENSION_free(extension);
    return result;
}

std::optional<PDFFireSigningPolicy> readPolicyExtension(X509* certificate)
{
    ssl_ptr<ASN1_OBJECT> object(OBJ_txt2obj(PDFFireSigningPolicy::EXTENSION_OID, 1), &ASN1_OBJECT_free);
    const int index = object ? X509_get_ext_by_OBJ(certificate, object.get(), -1) : -1;
    if (index < 0)
    {
        return std::nullopt;
    }

    ASN1_OCTET_STRING* octets = X509_EXTENSION_get_data(X509_get_ext(certificate, index));
    const unsigned char* pointer = ASN1_STRING_get0_data(octets);
    ssl_ptr<ASN1_UTF8STRING> text(d2i_ASN1_UTF8STRING(nullptr, &pointer, ASN1_STRING_length(octets)), &ASN1_UTF8STRING_free);
    if (!text)
    {
        return std::nullopt;
    }

    std::optional<PDFFireSigningPolicy> policy = PDFFireSigningPolicy::decode(QByteArray(reinterpret_cast<const char*>(ASN1_STRING_get0_data(text.get())), ASN1_STRING_length(text.get())));
    if (policy)
    {
        policy->authorityName = getCommonName(X509_get_issuer_name(certificate));
    }
    return policy;
}

/// Returns the certificate of the owner of the PKCS#12 file (not the certificates
/// of the authorities in it). With the password, the file is opened completely;
/// without it, only the certificates stored without encryption are read (PDF Fire
/// stores the certificates of the members so, a certificate is public anyway).
X509* readOwnerCertificate(const QByteArray& pkcs12Data, const QString& password, bool authority = false)
{
    ssl_ptr<BIO> bio(BIO_new_mem_buf(pkcs12Data.constData(), int(pkcs12Data.size())), &BIO_free_all);
    ssl_ptr<PKCS12> pkcs12(d2i_PKCS12_bio(bio.get(), nullptr), &PKCS12_free);
    if (!pkcs12)
    {
        return nullptr;
    }

    X509* result = nullptr;
    STACK_OF(PKCS7)* safes = PKCS12_unpack_authsafes(pkcs12.get());
    for (int i = 0; safes && !result && i < sk_PKCS7_num(safes); ++i)
    {
        PKCS7* safe = sk_PKCS7_value(safes, i);
        if (OBJ_obj2nid(safe->type) != NID_pkcs7_data)
        {
            continue;
        }

        STACK_OF(PKCS12_SAFEBAG)* bags = PKCS12_unpack_p7data(safe);
        for (int j = 0; bags && !result && j < sk_PKCS12_SAFEBAG_num(bags); ++j)
        {
            const PKCS12_SAFEBAG* bag = sk_PKCS12_SAFEBAG_value(bags, j);
            if (PKCS12_SAFEBAG_get_nid(bag) == NID_certBag)
            {
                X509* certificate = PKCS12_SAFEBAG_get1_cert(bag);
                if (certificate && (X509_check_ca(certificate) != 0) == authority)
                {
                    result = certificate;
                }
                else
                {
                    X509_free(certificate);
                }
            }
        }
        sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    }
    sk_PKCS7_pop_free(safes, PKCS7_free);
    if (result || authority)
    {
        return result;
    }

    // An encrypted certificate is read with the password (it is slower - the
    // key of the encryption is derived from the password first)
    if (!password.isEmpty())
    {
        const QByteArray passwordUtf8 = password.toUtf8();
        X509* certificate = nullptr;
        EVP_PKEY* key = nullptr;
        const bool isParsed = PKCS12_parse(pkcs12.get(), passwordUtf8.constData(), &key, &certificate, nullptr) == 1;
        EVP_PKEY_free(key);
        if (isParsed && certificate)
        {
            return certificate;
        }
        X509_free(certificate);
    }

    return nullptr;
}

QStringList s_trustedDirectoriesOverride;

}   // namespace

QByteArray PDFFireSigningPolicy::encode() const
{
    return QString("pdffire-signing-policy;v=1;os=%1;computer=%2;local-ip=%3;public-ip=%4;locked=%5")
            .arg(int(recordOperatingSystem)).arg(int(recordComputer)).arg(int(recordLocalAddresses))
            .arg(int(recordPublicAddress)).arg(int(isLocked)).toLatin1();
}

std::optional<PDFFireSigningPolicy> PDFFireSigningPolicy::decode(const QByteArray& text)
{
    const QList<QByteArray> items = text.split(';');
    if (items.isEmpty() || items.front() != "pdffire-signing-policy")
    {
        return std::nullopt;
    }

    PDFFireSigningPolicy policy;
    for (const QByteArray& item : items)
    {
        const int separator = item.indexOf('=');
        if (separator < 0)
        {
            continue;
        }
        const QByteArray key = item.left(separator);
        const bool value = item.mid(separator + 1) == "1";
        if (key == "os") { policy.recordOperatingSystem = value; }
        else if (key == "computer") { policy.recordComputer = value; }
        else if (key == "local-ip") { policy.recordLocalAddresses = value; }
        else if (key == "public-ip") { policy.recordPublicAddress = value; }
        else if (key == "locked") { policy.isLocked = value; }
    }
    return policy;
}

struct PDFFireCertificateAuthority::KeyData
{
    EVP_PKEY* key = nullptr;
    X509* certificate = nullptr;

    ~KeyData()
    {
        EVP_PKEY_free(key);
        X509_free(certificate);
    }
};

PDFFireCertificateAuthority::PDFFireCertificateAuthority() = default;

PDFFireCertificateAuthority::~PDFFireCertificateAuthority()
{
    delete m_key;
}

bool PDFFireCertificateAuthority::isOpen() const
{
    return m_key && m_key->key && m_key->certificate;
}

bool PDFFireCertificateAuthority::create(const QString& directory, const AuthorityInfo& info, const QString& passphrase, QString* errorMessage)
{
    QDir dir(directory);
    if (info.name.trimmed().isEmpty())
    {
        *errorMessage = tr("Type the name of the authority.");
        return false;
    }
    if (passphrase.size() < MINIMAL_PASSPHRASE_LENGTH)
    {
        *errorMessage = tr("The passphrase must have at least %1 characters.").arg(MINIMAL_PASSPHRASE_LENGTH);
        return false;
    }
    if (!dir.mkpath(QStringLiteral(".")))
    {
        *errorMessage = tr("The folder '%1' can't be created.").arg(directory);
        return false;
    }
    if (dir.exists(KEY_FILE_NAME))
    {
        *errorMessage = tr("The folder '%1' already holds an authority. Choose an empty folder.").arg(directory);
        return false;
    }

    std::unique_ptr<KeyData> keyData(new KeyData());
    keyData->key = EVP_RSA_gen(AUTHORITY_KEY_BITS);
    keyData->certificate = X509_new();
    X509* certificate = keyData->certificate;
    if (!keyData->key || !certificate)
    {
        *errorMessage = tr("The key of the authority can't be created.");
        return false;
    }

    X509_set_version(certificate, X509_VERSION_3);
    setRandomSerialNumber(certificate);
    X509_gmtime_adj(X509_getm_notBefore(certificate), -300);
    X509_time_adj_ex(X509_getm_notAfter(certificate), 365 * qBound(1, info.validityYears, 30), 0, nullptr);

    X509_NAME* name = X509_get_subject_name(certificate);
    addNameEntry(name, "C", info.country);
    addNameEntry(name, "O", info.organization);
    addNameEntry(name, "CN", info.name);
    X509_set_issuer_name(certificate, name);
    X509_set_pubkey(certificate, keyData->key);

    // The authority signs certificates and revocation lists only, and it can't
    // create another authority (path length 0)
    if (!addExtension(certificate, certificate, NID_basic_constraints, "critical,CA:TRUE,pathlen:0") ||
        !addExtension(certificate, certificate, NID_key_usage, "critical,keyCertSign,cRLSign") ||
        !addExtension(certificate, certificate, NID_subject_key_identifier, "hash") ||
        X509_sign(certificate, keyData->key, EVP_sha256()) <= 0)
    {
        *errorMessage = tr("The certificate of the authority can't be created.");
        return false;
    }

    const QByteArray passphraseUtf8 = passphrase.toUtf8();
    const QByteArray friendlyName = info.name.toUtf8();
    ssl_ptr<PKCS12> pkcs12(PKCS12_create(passphraseUtf8.constData(), friendlyName.constData(), keyData->key, certificate, nullptr,
                                         NID_aes_256_cbc, NID_aes_256_cbc, AUTHORITY_ITERATIONS, AUTHORITY_ITERATIONS, 0), &PKCS12_free);
    ssl_ptr<BIO> output(BIO_new(BIO_s_mem()), &BIO_free_all);
    if (!pkcs12 || i2d_PKCS12_bio(output.get(), pkcs12.get()) != 1)
    {
        *errorMessage = tr("The key file of the authority can't be created.");
        return false;
    }
    BUF_MEM* buffer = nullptr;
    BIO_get_mem_ptr(output.get(), &buffer);

    m_directory = dir.absolutePath();
    m_info = info;
    m_certificate = toDer(certificate);
    m_issued.clear();
    m_crlNumber = 0;
    delete m_key;
    m_key = keyData.release();

    if (!writeFile(dir.filePath(KEY_FILE_NAME), QByteArray(buffer->data, int(buffer->length)), true) ||
        !writeFile(dir.filePath(CERTIFICATE_FILE_NAME), m_certificate, false))
    {
        *errorMessage = tr("The files of the authority can't be written into '%1'.").arg(directory);
        return false;
    }

    return writeRevocationList(errorMessage);
}

bool PDFFireCertificateAuthority::open(const QString& directory, const QString& passphrase, QString* errorMessage)
{
    QDir dir(directory);
    QFile keyFile(dir.filePath(KEY_FILE_NAME));
    if (!keyFile.open(QFile::ReadOnly))
    {
        *errorMessage = tr("The folder '%1' doesn't hold an authority (the file %2 is missing).").arg(directory, QString::fromLatin1(KEY_FILE_NAME));
        return false;
    }
    const QByteArray keyFileData = keyFile.readAll();
    keyFile.close();

    ssl_ptr<BIO> bio(BIO_new_mem_buf(keyFileData.constData(), int(keyFileData.size())), &BIO_free_all);
    ssl_ptr<PKCS12> pkcs12(d2i_PKCS12_bio(bio.get(), nullptr), &PKCS12_free);
    std::unique_ptr<KeyData> keyData(new KeyData());
    const QByteArray passphraseUtf8 = passphrase.toUtf8();
    if (!pkcs12 || PKCS12_parse(pkcs12.get(), passphraseUtf8.constData(), &keyData->key, &keyData->certificate, nullptr) != 1 ||
        !keyData->key || !keyData->certificate)
    {
        *errorMessage = tr("The passphrase is not correct, or the key file is damaged.");
        return false;
    }

    m_directory = dir.absolutePath();
    m_certificate = toDer(keyData->certificate);
    delete m_key;
    m_key = keyData.release();
    m_info = AuthorityInfo();
    m_info.name = getCommonName(X509_get_subject_name(m_key->certificate));
    return loadRegister(errorMessage);
}

bool PDFFireCertificateAuthority::issue(const MemberInfo& member, const QString& memberPassword, QByteArray* pkcs12Data, IssuedCertificate* issuedCertificate, QString* errorMessage)
{
    if (!isOpen())
    {
        *errorMessage = tr("No authority is open.");
        return false;
    }
    if (member.name.trimmed().isEmpty())
    {
        *errorMessage = tr("Type the name of the member.");
        return false;
    }
    if (memberPassword.size() < 8)
    {
        *errorMessage = tr("The password of the certificate must have at least 8 characters.");
        return false;
    }

    ssl_ptr<EVP_PKEY> key(EVP_RSA_gen(MEMBER_KEY_BITS), &EVP_PKEY_free);
    ssl_ptr<X509> certificate(X509_new(), &X509_free);
    if (!key || !certificate)
    {
        *errorMessage = tr("The key of the member can't be created.");
        return false;
    }

    X509* authority = m_key->certificate;
    X509_set_version(certificate.get(), X509_VERSION_3);
    setRandomSerialNumber(certificate.get());
    X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -300);
    X509_time_adj_ex(X509_getm_notAfter(certificate.get()), 365 * qBound(1, member.validityYears, 10), 0, nullptr);

    // A certificate is never valid longer than its authority
    if (ASN1_TIME_compare(X509_get0_notAfter(certificate.get()), X509_get0_notAfter(authority)) > 0)
    {
        X509_set1_notAfter(certificate.get(), X509_get0_notAfter(authority));
    }

    X509_NAME* name = X509_get_subject_name(certificate.get());
    addNameEntry(name, "C", m_info.country.isEmpty() ? QStringLiteral("US") : m_info.country);
    addNameEntry(name, "O", m_info.organization);
    addNameEntry(name, "title", member.title);
    addNameEntry(name, "CN", member.name);
    addNameEntry(name, "emailAddress", member.email);
    X509_set_issuer_name(certificate.get(), X509_get_subject_name(authority));
    X509_set_pubkey(certificate.get(), key.get());

    // Signing of documents only: digital signature + non-repudiation; the extended
    // key usage "e-mail protection" is the one Acrobat and OpenSSL accept for
    // document signatures, "document signing" (RFC 9336) is added for the others
    bool isOk = addExtension(certificate.get(), authority, NID_basic_constraints, "critical,CA:FALSE") &&
                addExtension(certificate.get(), authority, NID_key_usage, "critical,digitalSignature,nonRepudiation") &&
                addExtension(certificate.get(), authority, NID_ext_key_usage, "emailProtection,1.3.6.1.5.5.7.3.36") &&
                addExtension(certificate.get(), authority, NID_subject_key_identifier, "hash") &&
                addExtension(certificate.get(), authority, NID_authority_key_identifier, "keyid:always") &&
                addPolicyExtension(certificate.get(), member.policy);
    if (isOk && !member.email.trimmed().isEmpty())
    {
        isOk = addExtension(certificate.get(), authority, NID_subject_alt_name, QString("email:%1").arg(member.email.trimmed()).toUtf8().constData());
    }
    if (isOk && !m_info.crlUrl.trimmed().isEmpty())
    {
        isOk = addExtension(certificate.get(), authority, NID_crl_distribution_points, QString("URI:%1").arg(m_info.crlUrl.trimmed()).toUtf8().constData());
    }
    if (!isOk || X509_sign(certificate.get(), m_key->key, EVP_sha256()) <= 0)
    {
        *errorMessage = tr("The certificate of the member can't be created (check the e-mail address).");
        return false;
    }

    // The file holds the key (encrypted), the certificate and the certificate of
    // the authority - the signatures carry the whole chain. The certificates are
    // not encrypted, so the name of the member is seen without the password.
    STACK_OF(X509)* chain = sk_X509_new_null();
    sk_X509_push(chain, authority);
    const QByteArray passwordUtf8 = memberPassword.toUtf8();
    const QByteArray friendlyName = member.name.trimmed().toUtf8();
    ssl_ptr<PKCS12> pkcs12(PKCS12_create(passwordUtf8.constData(), friendlyName.constData(), key.get(), certificate.get(), chain,
                                         NID_aes_256_cbc, -1, MEMBER_ITERATIONS, MEMBER_ITERATIONS, 0), &PKCS12_free);
    sk_X509_free(chain);

    ssl_ptr<BIO> output(BIO_new(BIO_s_mem()), &BIO_free_all);
    if (!pkcs12 || i2d_PKCS12_bio(output.get(), pkcs12.get()) != 1)
    {
        *errorMessage = tr("The certificate file of the member can't be created.");
        return false;
    }
    BUF_MEM* buffer = nullptr;
    BIO_get_mem_ptr(output.get(), &buffer);
    *pkcs12Data = QByteArray(buffer->data, int(buffer->length));

    IssuedCertificate issued;
    issued.serialNumber = serialToString(X509_get_serialNumber(certificate.get()));
    issued.name = member.name.trimmed();
    issued.title = member.title.trimmed();
    issued.email = member.email.trimmed();
    issued.issued = fromAsn1Time(X509_get0_notBefore(certificate.get()));
    issued.expires = fromAsn1Time(X509_get0_notAfter(certificate.get()));
    issued.policy = QString::fromLatin1(member.policy.encode());
    m_issued.push_back(issued);
    if (issuedCertificate)
    {
        *issuedCertificate = issued;
    }

    return saveRegister(errorMessage);
}

bool PDFFireCertificateAuthority::revoke(const QString& serialNumber, RevocationReason reason, QString* errorMessage)
{
    auto it = std::find_if(m_issued.begin(), m_issued.end(), [&](const IssuedCertificate& item) { return item.serialNumber == serialNumber; });
    if (it == m_issued.end())
    {
        *errorMessage = tr("The certificate is not in the register of the authority.");
        return false;
    }
    if (it->isRevoked())
    {
        *errorMessage = tr("The certificate is already revoked.");
        return false;
    }

    it->revoked = QDateTime::currentDateTimeUtc();
    it->revocationReason = reason;
    return saveRegister(errorMessage) && writeRevocationList(errorMessage);
}

bool PDFFireCertificateAuthority::writeRevocationList(QString* errorMessage)
{
    if (!isOpen())
    {
        *errorMessage = tr("No authority is open.");
        return false;
    }

    ssl_ptr<X509_CRL> crl(X509_CRL_new(), &X509_CRL_free);
    X509_CRL_set_version(crl.get(), X509_CRL_VERSION_2);
    X509_CRL_set_issuer_name(crl.get(), X509_get_subject_name(m_key->certificate));

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime nextUpdate = now.addDays(CRL_VALIDITY_DAYS);
    ssl_ptr<ASN1_TIME> lastUpdateTime(ASN1_TIME_set(nullptr, time_t(now.toSecsSinceEpoch())), &ASN1_TIME_free);
    ssl_ptr<ASN1_TIME> nextUpdateTime(ASN1_TIME_set(nullptr, time_t(nextUpdate.toSecsSinceEpoch())), &ASN1_TIME_free);
    X509_CRL_set1_lastUpdate(crl.get(), lastUpdateTime.get());
    X509_CRL_set1_nextUpdate(crl.get(), nextUpdateTime.get());

    for (const IssuedCertificate& issued : m_issued)
    {
        if (!issued.isRevoked())
        {
            continue;
        }

        X509_REVOKED* revoked = X509_REVOKED_new();
        BIGNUM* number = nullptr;
        BN_hex2bn(&number, issued.serialNumber.toLatin1().constData());
        ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(number, nullptr);
        BN_free(number);
        X509_REVOKED_set_serialNumber(revoked, serial);
        ASN1_INTEGER_free(serial);

        ASN1_TIME* revocationTime = ASN1_TIME_set(nullptr, time_t(issued.revoked.toSecsSinceEpoch()));
        X509_REVOKED_set_revocationDate(revoked, revocationTime);
        ASN1_TIME_free(revocationTime);

        ASN1_ENUMERATED* reasonCode = ASN1_ENUMERATED_new();
        ASN1_ENUMERATED_set(reasonCode, long(issued.revocationReason));
        X509_REVOKED_add1_ext_i2d(revoked, NID_crl_reason, reasonCode, 0, 0);
        ASN1_ENUMERATED_free(reasonCode);

        X509_CRL_add0_revoked(crl.get(), revoked);
    }
    X509_CRL_sort(crl.get());

    ++m_crlNumber;
    ASN1_INTEGER* crlNumber = ASN1_INTEGER_new();
    ASN1_INTEGER_set_int64(crlNumber, m_crlNumber);
    X509_CRL_add1_ext_i2d(crl.get(), NID_crl_number, crlNumber, 0, 0);
    ASN1_INTEGER_free(crlNumber);

    X509V3_CTX context = { };
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, m_key->certificate, nullptr, nullptr, crl.get(), 0);
    if (X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, NID_authority_key_identifier, "keyid:always"))
    {
        X509_CRL_add_ext(crl.get(), extension, -1);
        X509_EXTENSION_free(extension);
    }

    unsigned char* der = nullptr;
    int derLength = 0;
    if (X509_CRL_sign(crl.get(), m_key->key, EVP_sha256()) <= 0 || (derLength = i2d_X509_CRL(crl.get(), &der)) <= 0)
    {
        OPENSSL_free(der);
        *errorMessage = tr("The revocation list can't be created.");
        return false;
    }
    const QByteArray crlData(reinterpret_cast<const char*>(der), derLength);
    OPENSSL_free(der);

    m_crlNextUpdate = nextUpdate;
    if (!writeFile(QDir(m_directory).filePath(CRL_FILE_NAME), crlData, false))
    {
        *errorMessage = tr("The revocation list can't be written into '%1'.").arg(m_directory);
        return false;
    }

    // This computer knows about the revocation at once, when it trusts the authority
    if (isAuthorityTrusted(m_certificate))
    {
        QString trustErrorMessage;
        trustAuthority(m_certificate, crlData, &trustErrorMessage);
    }

    return saveRegister(errorMessage);
}

bool PDFFireCertificateAuthority::saveRegister(QString* errorMessage) const
{
    QJsonObject authority;
    authority["name"] = m_info.name;
    authority["organization"] = m_info.organization;
    authority["country"] = m_info.country;
    authority["crlUrl"] = m_info.crlUrl;

    QJsonArray issuedArray;
    for (const IssuedCertificate& issued : m_issued)
    {
        QJsonObject item;
        item["serialNumber"] = issued.serialNumber;
        item["name"] = issued.name;
        item["title"] = issued.title;
        item["email"] = issued.email;
        item["issued"] = issued.issued.toString(Qt::ISODate);
        item["expires"] = issued.expires.toString(Qt::ISODate);
        item["policy"] = issued.policy;
        if (issued.isRevoked())
        {
            item["revoked"] = issued.revoked.toString(Qt::ISODate);
            item["revocationReason"] = int(issued.revocationReason);
        }
        issuedArray.append(item);
    }

    QJsonObject root;
    root["format"] = QStringLiteral("pdf-fire-certificate-authority");
    root["version"] = 1;
    root["authority"] = authority;
    root["crlNumber"] = double(m_crlNumber);
    root["crlNextUpdate"] = m_crlNextUpdate.toString(Qt::ISODate);
    root["issued"] = issuedArray;

    if (!writeFile(QDir(m_directory).filePath(REGISTER_FILE_NAME), QJsonDocument(root).toJson(QJsonDocument::Indented), false))
    {
        *errorMessage = tr("The register of the authority can't be written into '%1'.").arg(m_directory);
        return false;
    }
    return true;
}

bool PDFFireCertificateAuthority::loadRegister(QString* errorMessage)
{
    QFile file(QDir(m_directory).filePath(REGISTER_FILE_NAME));
    if (!file.open(QFile::ReadOnly))
    {
        *errorMessage = tr("The register of the authority (%1) is missing.").arg(QString::fromLatin1(REGISTER_FILE_NAME));
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root["format"].toString() != QStringLiteral("pdf-fire-certificate-authority"))
    {
        *errorMessage = tr("The register of the authority is damaged.");
        return false;
    }

    const QJsonObject authority = root["authority"].toObject();
    m_info.organization = authority["organization"].toString();
    m_info.country = authority["country"].toString();
    m_info.crlUrl = authority["crlUrl"].toString();
    m_crlNumber = qint64(root["crlNumber"].toDouble());
    m_crlNextUpdate = QDateTime::fromString(root["crlNextUpdate"].toString(), Qt::ISODate);

    m_issued.clear();
    for (const QJsonValue& value : root["issued"].toArray())
    {
        const QJsonObject item = value.toObject();
        IssuedCertificate issued;
        issued.serialNumber = item["serialNumber"].toString();
        issued.name = item["name"].toString();
        issued.title = item["title"].toString();
        issued.email = item["email"].toString();
        issued.issued = QDateTime::fromString(item["issued"].toString(), Qt::ISODate);
        issued.expires = QDateTime::fromString(item["expires"].toString(), Qt::ISODate);
        issued.policy = item["policy"].toString();
        issued.revoked = QDateTime::fromString(item["revoked"].toString(), Qt::ISODate);
        issued.revocationReason = RevocationReason(item["revocationReason"].toInt());
        m_issued.push_back(issued);
    }
    return true;
}

QString PDFFireCertificateAuthority::generatePassword()
{
    // 4 groups of 4 characters, ~82 bits; no 0/O, 1/l/I
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    QString password;
    for (int group = 0; group < 4; ++group)
    {
        if (group > 0)
        {
            password += QChar('-');
        }
        for (int i = 0; i < 4; ++i)
        {
            password += QChar(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
        }
    }
    return password;
}

QString PDFFireCertificateAuthority::getTrustedAuthoritiesDirectory()
{
    if (!s_trustedDirectoriesOverride.isEmpty())
    {
        return s_trustedDirectoriesOverride.front();
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/trusted-authorities");
}

QStringList PDFFireCertificateAuthority::getTrustedAuthoritiesDirectories()
{
    if (!s_trustedDirectoriesOverride.isEmpty())
    {
        return s_trustedDirectoriesOverride;
    }
    return { getTrustedAuthoritiesDirectory(), QStringLiteral("/etc/pdf-fire/trusted-authorities") };
}

void PDFFireCertificateAuthority::setTrustedAuthoritiesDirectoriesOverride(const QStringList& directories)
{
    s_trustedDirectoriesOverride = directories;
}

bool PDFFireCertificateAuthority::trustAuthority(const QByteArray& certificateData, const QByteArray& revocationListData, QString* errorMessage)
{
    ssl_ptr<X509> certificate(readCertificate(certificateData), &X509_free);
    if (!certificate)
    {
        *errorMessage = tr("The file is not a certificate.");
        return false;
    }
    if (X509_check_ca(certificate.get()) == 0)
    {
        *errorMessage = tr("The certificate is not a certificate of an authority - it can't be trusted as one.");
        return false;
    }

    const QByteArray der = toDer(certificate.get());
    const QString baseName = QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex().left(16));
    QDir directory(getTrustedAuthoritiesDirectory());
    directory.mkpath(QStringLiteral("."));
    if (!writeFile(directory.filePath(baseName + ".cer"), der, false))
    {
        *errorMessage = tr("The certificate can't be written into '%1'.").arg(directory.path());
        return false;
    }

    if (!revocationListData.isEmpty())
    {
        // Only a revocation list signed by this authority is kept
        ssl_ptr<X509_CRL> crl(readRevocationList(revocationListData), &X509_CRL_free);
        ssl_ptr<EVP_PKEY> publicKey(X509_get_pubkey(certificate.get()), &EVP_PKEY_free);
        if (!crl || X509_CRL_verify(crl.get(), publicKey.get()) != 1)
        {
            *errorMessage = tr("The revocation list is damaged or doesn't belong to the authority.");
            return false;
        }

        unsigned char* crlDer = nullptr;
        const int crlLength = i2d_X509_CRL(crl.get(), &crlDer);
        const bool isWritten = crlLength > 0 && writeFile(directory.filePath(baseName + ".crl"), QByteArray(reinterpret_cast<const char*>(crlDer), crlLength), false);
        OPENSSL_free(crlDer);
        if (!isWritten)
        {
            *errorMessage = tr("The revocation list can't be written into '%1'.").arg(directory.path());
            return false;
        }
    }
    return true;
}

QString PDFFireCertificateAuthority::getFingerprint(const QByteArray& certificateData)
{
    ssl_ptr<X509> certificate(readCertificate(certificateData), &X509_free);
    if (!certificate)
    {
        return QString();
    }

    const QString hex = QString::fromLatin1(QCryptographicHash::hash(toDer(certificate.get()), QCryptographicHash::Sha256).toHex().toUpper());
    QStringList groups;
    for (int i = 0; i < hex.size(); i += 4)
    {
        groups << hex.mid(i, 4);
    }
    return groups.join(QChar(' '));
}

QString PDFFireCertificateAuthority::getCertificateName(const QByteArray& certificateData)
{
    ssl_ptr<X509> certificate(readCertificate(certificateData), &X509_free);
    return certificate ? getCommonName(X509_get_subject_name(certificate.get())) : QString();
}

bool PDFFireCertificateAuthority::isAuthorityTrusted(const QByteArray& certificate)
{
    for (const QByteArray& trusted : getTrustedAuthorityCertificates())
    {
        if (trusted == certificate)
        {
            return true;
        }
    }
    return false;
}

std::vector<PDFFireCertificateAuthority::TrustedAuthority> PDFFireCertificateAuthority::getTrustedAuthorities()
{
    std::vector<TrustedAuthority> result;
    const QStringList directories = getTrustedAuthoritiesDirectories();
    for (const QString& path : directories)
    {
        QDir directory(path);
        for (const QFileInfo& fileInfo : directory.entryInfoList({ "*.cer", "*.crt", "*.pem", "*.der" }, QDir::Files | QDir::Readable, QDir::Name))
        {
            QFile file(fileInfo.absoluteFilePath());
            if (!file.open(QFile::ReadOnly))
            {
                continue;
            }
            const QByteArray data = file.readAll();
            ssl_ptr<X509> certificate(readCertificate(data), &X509_free);
            if (!certificate || X509_check_ca(certificate.get()) == 0)
            {
                continue;
            }

            TrustedAuthority authority;
            authority.name = getCommonName(X509_get_subject_name(certificate.get()));
            authority.fingerprint = getFingerprint(data);
            authority.fileName = fileInfo.absoluteFilePath();
            authority.isForAllUsers = path != directories.front();
            authority.expires = fromAsn1Time(X509_get0_notAfter(certificate.get()));

            QFile crlFile(directory.filePath(fileInfo.completeBaseName() + ".crl"));
            if (crlFile.open(QFile::ReadOnly))
            {
                ssl_ptr<X509_CRL> crl(readRevocationList(crlFile.readAll()), &X509_CRL_free);
                if (crl)
                {
                    authority.crlNextUpdate = fromAsn1Time(X509_CRL_get0_nextUpdate(crl.get()));
                }
            }
            result.push_back(authority);
        }
    }
    return result;
}

bool PDFFireCertificateAuthority::removeTrustedAuthority(const QString& fileName, QString* errorMessage)
{
    const QFileInfo fileInfo(fileName);
    if (fileInfo.absolutePath() != QDir(getTrustedAuthoritiesDirectory()).absolutePath())
    {
        *errorMessage = tr("This authority is trusted for all users of the computer - only the administrator can remove it.");
        return false;
    }
    if (!QFile::remove(fileInfo.absoluteFilePath()))
    {
        *errorMessage = tr("The file '%1' can't be removed.").arg(fileInfo.absoluteFilePath());
        return false;
    }
    QFile::remove(fileInfo.dir().filePath(fileInfo.completeBaseName() + ".crl"));
    return true;
}

std::vector<QByteArray> PDFFireCertificateAuthority::getTrustedAuthorityCertificates()
{
    std::vector<QByteArray> result;
    for (const QString& path : getTrustedAuthoritiesDirectories())
    {
        QDir directory(path);
        for (const QFileInfo& fileInfo : directory.entryInfoList({ "*.cer", "*.crt", "*.pem", "*.der" }, QDir::Files | QDir::Readable, QDir::Name))
        {
            QFile file(fileInfo.absoluteFilePath());
            if (file.open(QFile::ReadOnly))
            {
                ssl_ptr<X509> certificate(readCertificate(file.readAll()), &X509_free);
                if (certificate && X509_check_ca(certificate.get()) != 0)
                {
                    result.push_back(toDer(certificate.get()));
                }
            }
        }
    }
    return result;
}

std::vector<QByteArray> PDFFireCertificateAuthority::getTrustedAuthorityRevocationLists()
{
    std::vector<QByteArray> result;
    for (const QString& path : getTrustedAuthoritiesDirectories())
    {
        QDir directory(path);
        for (const QFileInfo& fileInfo : directory.entryInfoList({ "*.crl" }, QDir::Files | QDir::Readable, QDir::Name))
        {
            QFile file(fileInfo.absoluteFilePath());
            if (file.open(QFile::ReadOnly))
            {
                ssl_ptr<X509_CRL> crl(readRevocationList(file.readAll()), &X509_CRL_free);
                unsigned char* der = nullptr;
                const int length = crl ? i2d_X509_CRL(crl.get(), &der) : 0;
                if (length > 0)
                {
                    result.emplace_back(reinterpret_cast<const char*>(der), length);
                }
                OPENSSL_free(der);
            }
        }
    }
    return result;
}

std::optional<PDFFireSigningPolicy> PDFFireCertificateAuthority::readSigningPolicy(const PDFCertificateEntry& entry, const QString& password)
{
    if (entry.pkcs12.isEmpty())
    {
        return std::nullopt;
    }
    ssl_ptr<X509> certificate(readOwnerCertificate(entry.pkcs12, password), &X509_free);
    return certificate ? readPolicyExtension(certificate.get()) : std::nullopt;
}

QByteArray PDFFireCertificateAuthority::readPublicCertificate(const QByteArray& pkcs12)
{
    ssl_ptr<X509> certificate(readOwnerCertificate(pkcs12, QString()), &X509_free);
    return certificate ? toDer(certificate.get()) : QByteArray();
}

QByteArray PDFFireCertificateAuthority::readAuthorityCertificate(const QByteArray& pkcs12)
{
    ssl_ptr<X509> certificate(readOwnerCertificate(pkcs12, QString(), true), &X509_free);
    return certificate ? toDer(certificate.get()) : QByteArray();
}

QString PDFFireCertificateAuthority::readIssuerName(const PDFCertificateEntry& entry, const QString& password)
{
    if (entry.pkcs12.isEmpty())
    {
        return QString();
    }
    ssl_ptr<X509> certificate(readOwnerCertificate(entry.pkcs12, password), &X509_free);
    return certificate ? getCommonName(X509_get_issuer_name(certificate.get())) : QString();
}

}   // namespace pdf
