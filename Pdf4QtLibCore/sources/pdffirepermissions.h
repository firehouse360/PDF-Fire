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

#ifndef PDFFIREPERMISSIONS_H
#define PDFFIREPERMISSIONS_H

#include "pdfglobal.h"
#include "pdfsecurityhandler.h"

#include <QString>
#include <QStringList>

namespace pdf
{
class PDFDocument;
class PDFDocumentBuilder;
class PDFObjectReference;

/// PDF Fire: what the user may do with the document - the permissions of the
/// security handler (a document with restrictions, opened without the owner
/// password) and the certification of the document (DocMDP: a certified
/// document allows no changes, or form filling and signing only, or also comments).
/// Every tool, which changes the document, asks here first.
class PDF4QTLIBCORESHARED_EXPORT PDFFirePermissions
{
public:
    using Permission = PDFSecurityHandler::Permission;

    /// What the certification of the document allows (DocMDP permissions, P)
    enum class Certification
    {
        None = 0,                   ///< The document is not certified
        NoChanges = 1,              ///< No changes at all
        FormFillingAndSigning = 2,  ///< Filling in forms, signing
        FormsSigningAndComments = 3 ///< Also comments (annotations)
    };

    /// Returns true, if the permission is granted
    static bool isAllowed(const PDFDocument* document, Permission permission);

    /// Changes of the page content (text, images, page marks, redaction, OCR)
    static bool canModifyContent(const PDFDocument* document);

    /// Comments and other annotations
    static bool canAnnotate(const PDFDocument* document);

    /// Filling in form fields
    static bool canFillForms(const PDFDocument* document);

    /// Creating and changing form fields (the form designer)
    static bool canDesignForms(const PDFDocument* document);

    /// Signing into a signature field, or a new signature
    static bool canSign(const PDFDocument* document);

    /// Inserting, removing, rotating pages
    static bool canAssemblePages(const PDFDocument* document);

    /// Copying or exporting the text and images
    static bool canCopyContent(const PDFDocument* document);

    /// Changing the security (passwords, permissions) - only the owner may do it
    static bool canChangeSecurity(const PDFDocument* document);

    /// Returns the certification of the document
    static Certification getCertification(const PDFDocument* document);

    /// Makes the signature a certification of the document: the signature reference
    /// (DocMDP, with the allowed changes) is written into the signature dictionary,
    /// and the catalog points to it (/Perms /DocMDP). PDF 2.0, 12.8.2.2.
    static void writeCertification(PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary, Certification certification);

    /// Returns the reason, why the changes are not allowed (for a tooltip or a message)
    static QString getRestrictionReason(const PDFDocument* document);

    /// The protection of the document in plain words (Document Info, the bar above
    /// a protected document)
    struct ProtectionSummary
    {
        bool isEncrypted = false;       ///< The document is encrypted
        bool needsOpenPassword = false; ///< A password is needed to open the document
        bool isOwner = false;           ///< Opened with the permissions (owner) password
        bool isRestricted = false;      ///< Something is not allowed
        bool canUnlock = false;         ///< The permissions password would lift the restrictions
        Certification certification = Certification::None;
        QString headline;               ///< One sentence about the protection
        QString explanation;            ///< Why the tools are greyed out, and what can be done
        QStringList allowed;            ///< What may be done
        QStringList notAllowed;         ///< What may not be done
    };

    /// Returns the protection of the document in plain words
    static ProtectionSummary getProtectionSummary(const PDFDocument* document);
};

}   // namespace pdf

#endif // PDFFIREPERMISSIONS_H
