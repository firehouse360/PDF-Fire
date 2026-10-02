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

#include "pdffirepermissions.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"

#include <QCoreApplication>

namespace pdf
{

bool PDFFirePermissions::isAllowed(const PDFDocument* document, Permission permission)
{
    if (!document)
    {
        return false;
    }

    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    if (securityHandler && !securityHandler->isAllowed(permission))
    {
        return false;
    }

    // A certified document - every change, which the certification doesn't
    // allow, breaks the certification (also for the owner of the document)
    switch (getCertification(document))
    {
        case Certification::None:
            return true;

        case Certification::NoChanges:
            return permission == Permission::PrintLowResolution || permission == Permission::PrintHighResolution ||
                   permission == Permission::CopyContent || permission == Permission::Accessibility;

        case Certification::FormFillingAndSigning:
            return permission != Permission::Modify && permission != Permission::Assemble && permission != Permission::ModifyInteractiveItems;

        case Certification::FormsSigningAndComments:
            return permission != Permission::Modify && permission != Permission::Assemble;
    }

    return true;
}

bool PDFFirePermissions::canModifyContent(const PDFDocument* document)
{
    return isAllowed(document, Permission::Modify);
}

bool PDFFirePermissions::canAnnotate(const PDFDocument* document)
{
    return isAllowed(document, Permission::ModifyInteractiveItems);
}

bool PDFFirePermissions::canFillForms(const PDFDocument* document)
{
    // Filling in the forms is allowed by the "fill in forms" bit, or by the "comments
    // and forms" bit (PDF 2.0, Table 22, bits 6 and 9)
    return isAllowed(document, Permission::ModifyFormFields) || isAllowed(document, Permission::ModifyInteractiveItems);
}

bool PDFFirePermissions::canDesignForms(const PDFDocument* document)
{
    // Creating form fields needs both bit 4 (modify) and bit 6 (interactive items)
    return isAllowed(document, Permission::Modify) && isAllowed(document, Permission::ModifyInteractiveItems);
}

bool PDFFirePermissions::canSign(const PDFDocument* document)
{
    // A signature fills in a signature field (bit 9); a certified document, which
    // allows no changes, can't be signed again
    return getCertification(document) != Certification::NoChanges && canFillForms(document);
}

bool PDFFirePermissions::canAssemblePages(const PDFDocument* document)
{
    return isAllowed(document, Permission::Assemble) || isAllowed(document, Permission::Modify);
}

bool PDFFirePermissions::canCopyContent(const PDFDocument* document)
{
    return isAllowed(document, Permission::CopyContent);
}

bool PDFFirePermissions::canChangeSecurity(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    if (!securityHandler)
    {
        return true;
    }

    const PDFSecurityHandler::AuthorizationResult result = securityHandler->getAuthorizationResult();
    return result == PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired ||
           result == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
}

PDFFirePermissions::Certification PDFFirePermissions::getCertification(const PDFDocument* document)
{
    if (!document || !document->getCatalog())
    {
        return Certification::None;
    }

    // Catalog /Perms /DocMDP -> signature dictionary -> /Reference [ << /TransformMethod /DocMDP
    // /TransformParams << /P n >> >> ] (PDF 2.0, 12.8.2.2); P is 2, when it is missing
    const PDFDocumentDataLoaderDecorator loader(document);
    const PDFDictionary* perms = document->getDictionaryFromObject(document->getCatalog()->getPerms());
    const PDFDictionary* signature = perms ? document->getDictionaryFromObject(perms->get("DocMDP")) : nullptr;
    if (!signature)
    {
        return Certification::None;
    }

    const PDFObject& referencesObject = document->getObject(signature->get("Reference"));
    if (referencesObject.isArray())
    {
        const PDFArray* references = referencesObject.getArray();
        for (size_t i = 0; i < references->getCount(); ++i)
        {
            const PDFDictionary* reference = document->getDictionaryFromObject(references->getItem(i));
            if (reference && loader.readNameFromDictionary(reference, "TransformMethod") == "DocMDP")
            {
                const PDFDictionary* parameters = document->getDictionaryFromObject(reference->get("TransformParams"));
                const PDFInteger p = parameters ? loader.readIntegerFromDictionary(parameters, "P", 2) : 2;
                return Certification(int(qBound(PDFInteger(1), p, PDFInteger(3))));
            }
        }
    }

    // A /DocMDP entry without the transform parameters - the default of the specification
    return Certification::FormFillingAndSigning;
}

void PDFFirePermissions::writeCertification(PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary, Certification certification)
{
    PDFObjectFactory referenceFactory;
    referenceFactory.beginDictionary();
    referenceFactory.beginDictionaryItem("Reference");
    referenceFactory.beginArray();
    referenceFactory.beginDictionary();
    referenceFactory.beginDictionaryItem("Type");
    referenceFactory << WrapName("SigRef");
    referenceFactory.endDictionaryItem();
    referenceFactory.beginDictionaryItem("TransformMethod");
    referenceFactory << WrapName("DocMDP");
    referenceFactory.endDictionaryItem();
    referenceFactory.beginDictionaryItem("TransformParams");
    referenceFactory.beginDictionary();
    referenceFactory.beginDictionaryItem("Type");
    referenceFactory << WrapName("TransformParams");
    referenceFactory.endDictionaryItem();
    referenceFactory.beginDictionaryItem("P");
    referenceFactory << PDFInteger(certification);
    referenceFactory.endDictionaryItem();
    referenceFactory.beginDictionaryItem("V");
    referenceFactory << WrapName("1.2");
    referenceFactory.endDictionaryItem();
    referenceFactory.endDictionary();
    referenceFactory.endDictionaryItem();
    referenceFactory.endDictionary();
    referenceFactory.endArray();
    referenceFactory.endDictionaryItem();
    referenceFactory.endDictionary();
    builder.mergeTo(signatureDictionary, referenceFactory.takeObject());

    PDFObjectFactory catalogFactory;
    catalogFactory.beginDictionary();
    catalogFactory.beginDictionaryItem("Perms");
    catalogFactory.beginDictionary();
    catalogFactory.beginDictionaryItem("DocMDP");
    catalogFactory << signatureDictionary;
    catalogFactory.endDictionaryItem();
    catalogFactory.endDictionary();
    catalogFactory.endDictionaryItem();
    catalogFactory.endDictionary();
    builder.mergeTo(builder.getCatalogReference(), catalogFactory.takeObject());
}

QString PDFFirePermissions::getRestrictionReason(const PDFDocument* document)
{
    switch (getCertification(document))
    {
        case Certification::NoChanges:
            return QCoreApplication::translate("PDFFirePermissions", "The document is certified - no changes are allowed.");

        case Certification::FormFillingAndSigning:
            return QCoreApplication::translate("PDFFirePermissions", "The document is certified - only filling in forms and signing are allowed.");

        case Certification::FormsSigningAndComments:
            return QCoreApplication::translate("PDFFirePermissions", "The document is certified - only filling in forms, signing and comments are allowed.");

        case Certification::None:
            break;
    }

    return QCoreApplication::translate("PDFFirePermissions", "The document is protected by its author - its security settings don't allow this. "
                                                             "Enter the permissions password (Protect > Enter Password) to change it.");
}

PDFFirePermissions::ProtectionSummary PDFFirePermissions::getProtectionSummary(const PDFDocument* document)
{
    ProtectionSummary summary;
    if (!document)
    {
        return summary;
    }

    auto tr = [](const char* text) { return QCoreApplication::translate("PDFFirePermissions", text); };

    const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    const PDFSecurityHandler::AuthorizationResult authorization = securityHandler ? securityHandler->getAuthorizationResult()
                                                                                    : PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired;
    summary.isEncrypted = securityHandler && securityHandler->getMode() != EncryptionMode::None;
    summary.isOwner = authorization == PDFSecurityHandler::AuthorizationResult::OwnerAuthorized;
    summary.certification = getCertification(document);

    if (summary.isEncrypted && securityHandler->getMode() == EncryptionMode::Standard)
    {
        // The document needs a password to open, if the empty password doesn't open it
        PDFSecurityHandlerPointer clonedHandler(securityHandler->clone());
        const PDFSecurityHandler::AuthorizationResult emptyPasswordResult = clonedHandler->authenticate([](bool* ok) { *ok = false; return QString(); }, false);
        summary.needsOpenPassword = emptyPasswordResult == PDFSecurityHandler::AuthorizationResult::Failed ||
                                    emptyPasswordResult == PDFSecurityHandler::AuthorizationResult::Cancelled;
    }
    else if (summary.isEncrypted)
    {
        // A certificate (public key) opens the document
        summary.needsOpenPassword = true;
    }

    struct Item
    {
        bool isAllowed;
        QString text;
    };

    const bool canPrintHigh = isAllowed(document, Permission::PrintHighResolution);
    const bool canPrintLow = isAllowed(document, Permission::PrintLowResolution);
    QString printText = tr("Printing");
    if (canPrintLow && !canPrintHigh)
    {
        printText = tr("Printing (low quality only)");
    }

    const Item items[] = {
        { canPrintLow || canPrintHigh, printText },
        { canModifyContent(document), tr("Changing the text and images of the pages") },
        { canAnnotate(document), tr("Adding text, comments, highlights and drawings") },
        { canFillForms(document), tr("Filling in form fields") },
        { canSign(document), tr("Signing") },
        { canAssemblePages(document), tr("Inserting, deleting and rotating pages") },
        { canCopyContent(document), tr("Copying text and images") },
        { canChangeSecurity(document), tr("Changing the passwords and security") }
    };

    for (const Item& item : items)
    {
        (item.isAllowed ? summary.allowed : summary.notAllowed) << item.text;
    }

    summary.isRestricted = !summary.notAllowed.isEmpty();

    // Only the restrictions of the security handler can be lifted by the permissions
    // password - a certification stays (a change would break the signature)
    const bool isRestrictedBySecurity = summary.isEncrypted && !summary.isOwner &&
                                        authorization != PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired;
    summary.canUnlock = isRestrictedBySecurity && securityHandler->getMode() == EncryptionMode::Standard;

    if (summary.certification != Certification::None)
    {
        summary.headline = getRestrictionReason(document);
        summary.explanation = tr("The author certified the document with a digital signature. Changes, which the certification "
                                 "doesn't allow, would make the signature invalid, so the tools for them are turned off.");
    }
    else if (isRestrictedBySecurity && summary.isRestricted)
    {
        summary.headline = summary.needsOpenPassword ? tr("Protected: a password opens it, and its author restricted what may be done with it.")
                                                     : tr("Protected by its author: it opens without a password, but changes are restricted.");
        summary.explanation = tr("The author of the document set a permissions password and turned off the things below, so the "
                                 "tools for them are greyed out. Acrobat and other careful PDF programs honour the same settings. "
                                 "If you know the permissions password, enter it to unlock the document; otherwise ask the sender "
                                 "for a copy without the restrictions.");
    }
    else if (summary.isEncrypted)
    {
        summary.headline = summary.isOwner ? tr("Encrypted - opened with the permissions password, everything is allowed.")
                                           : tr("Encrypted - a password opens it; nothing else is restricted.");
    }
    else
    {
        summary.headline = tr("Not protected - no password, nothing is restricted.");
    }

    return summary;
}

}   // namespace pdf
