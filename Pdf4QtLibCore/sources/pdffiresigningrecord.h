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

#ifndef PDFFIRESIGNINGRECORD_H
#define PDFFIRESIGNINGRECORD_H

#include "pdfglobal.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace pdf
{
class PDFDocumentBuilder;
class PDFObjectReference;

/// PDF Fire: the record of a signing (an audit trail) - who signed, when, on which
/// computer and from which network address, which document, and that the signer
/// agreed to sign electronically. The record is written into the signature
/// dictionary before the document is signed, so it is covered by the signature:
/// it cannot be changed afterwards without breaking the signature. It is also
/// appended to the signing log of the user (a second copy, kept on the computer).
///
/// The record is plain text, one "Caption: value" per line, so it can be read
/// by a person, also in a reader, which knows nothing about PDF Fire.
class PDF4QTLIBCORESHARED_EXPORT PDFFireSigningRecord
{
public:
    /// Text of the agreement, which the signer must accept before signing
    static QString getConsentText();

    /// Fills the facts of the computer, which the signer (or the department) chose
    /// to record, and the version of the application
    /// \param operatingSystem Operating system
    /// \param computer Computer name and user account
    /// \param localAddresses IP addresses of the computer
    void collectComputerInformation(bool operatingSystem, bool computer, bool localAddresses);

    /// Looks up the public (internet) address of the computer. It contacts an
    /// outside service, so it is done only when the user allowed it. On failure,
    /// the reason is recorded instead of the address.
    /// \param timeoutMilliseconds Maximal duration of the lookup
    void lookupPublicAddress(int timeoutMilliseconds = 5000);

    /// Returns the text of the record
    QString toText() const;

    /// Writes the record and the build properties (application, operating system)
    /// into the signature dictionary
    void writeTo(PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary) const;

    /// Appends the record to the signing log of the user, together with the
    /// file the signed document was saved to and its fingerprint (SHA-256)
    /// \returns Path of the log, empty string, if it can't be written
    QString appendToLog(const QString& savedFileName, const QByteArray& savedFileData) const;

    /// Returns the path of the signing log of the user
    static QString getLogFileName();

    /// Returns the SHA-256 fingerprint of the data, as hexadecimal digits
    static QString getFingerprint(const QByteArray& data);

    /// Name of the key in the signature dictionary, which holds the record
    static constexpr const char* DICTIONARY_KEY = "PDFFire_SigningRecord";

    QString signerName;             ///< Name of the owner of the certificate
    QString signerEmail;            ///< E-mail address of the certificate
    QString certificateIssuer;      ///< Authority, which issued the certificate (empty = self-made)
    QString recordingSetBy;         ///< Authority, which chose the recorded details (empty = the signer)
    bool isRecordingLocked = false; ///< The signer could not turn the details off
    QString typedName;              ///< Name typed by the signer (shown on the page)
    QString reason;
    QString contactInfo;
    QString signatureType;          ///< "Signature", "Signature with timestamp"...
    QString timestampAuthority;     ///< Address of the timestamp authority (if used)
    QString certification;          ///< The document is certified (the allowed changes), empty = not
    QDateTime signingTime;
    QString documentName;
    QString documentFingerprint;    ///< SHA-256 of the document before this signature
    int pageNumber = 0;             ///< Page of the visible signature (1-based), 0 = invisible
    bool consentAccepted = false;

    QString operatingSystem;
    QString hostName;
    QString userAccount;
    QString applicationVersion;
    QStringList localAddresses;
    QString publicAddress;          ///< Address, or why it is not known
};

}   // namespace pdf

#endif // PDFFIRESIGNINGRECORD_H
