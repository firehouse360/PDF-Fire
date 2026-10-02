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

#include "pdffirepageoperations.h"
#include "pdfdocumentbuilder.h"
#include "pdfoptimizer.h"
#include "pdfexception.h"

#include "pdfdbgheap.h"

namespace pdf
{

bool PDFPageOperations::normalizePages(const PDFDocument* document, std::vector<PDFInteger>& pages)
{
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    return !pages.empty() && pages.front() >= 0 && pages.back() < pageCount;
}

namespace
{

/// Appends a blank page, which is complete: it has an (empty) content stream and
/// resources. A page without them is valid too, but it is reported as an error
/// when it is rendered, and the tools working with the page content expect them.
PDFObjectReference appendBlankPage(PDFDocumentBuilder& builder, QRectF mediaBox)
{
    const PDFObjectReference page = builder.appendPage(mediaBox);
    const PDFObjectReference contents = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(), QByteArray())));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << contents;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory.beginDictionary();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(page, factory.takeObject());

    return page;
}

}   // namespace

PDFDocument PDFPageOperations::createBlankDocument(QSizeF pageSize)
{
    PDFDocumentBuilder builder;
    appendBlankPage(builder, QRectF(QPointF(0, 0), pageSize));
    return builder.build();
}

PDFOperationResult PDFPageOperations::insertBlankPage(const PDFDocument* document, PDFInteger position, QRectF mediaBox, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (position < 0 || position > pageCount || !mediaBox.isValid())
    {
        return PDFTranslationContext::tr("Invalid position of the new page.");
    }

    try
    {
        PDFDocumentBuilder builder(document);
        builder.flattenPageTree();

        // The new page is appended as the last one, then it is moved to its position
        const PDFObjectReference newPage = appendBlankPage(builder, mediaBox);
        std::vector<PDFObjectReference> pages = builder.getPages();
        pages.pop_back();
        pages.insert(std::next(pages.begin(), position), newPage);
        builder.setPages(pages);

        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFOperationResult PDFPageOperations::deletePages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    if (!normalizePages(document, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    if (pages.size() >= document->getCatalog()->getPageCount())
    {
        return PDFTranslationContext::tr("All pages cannot be deleted, a document must have at least one page.");
    }

    try
    {
        PDFDocumentBuilder builder(document);
        builder.flattenPageTree();

        std::vector<PDFObjectReference> pageReferences = builder.getPages();
        std::vector<PDFObjectReference> deletedPageReferences;

        for (auto it = pages.crbegin(); it != pages.crend(); ++it)
        {
            deletedPageReferences.push_back(pageReferences[*it]);
            pageReferences.erase(std::next(pageReferences.begin(), *it));
        }
        builder.setPages(pageReferences);

        // A page taken out of the page tree is not displayed, but it is still in the
        // document - its object is referenced by the outline, by the annotations, by the
        // structure tree..., so it would be written into the saved file with its whole
        // content. The object of the page is therefore replaced by a null object, the
        // references to it then refer to nothing, and its content (which is not shared
        // with another page) becomes unused and is removed.
        for (const PDFObjectReference& deletedPageReference : deletedPageReferences)
        {
            builder.setObject(deletedPageReference, PDFObject());
        }

        const PDFDocument documentWithoutPages = builder.build();

        // The unused objects are replaced by null objects, the numbers of the others stay
        PDFOptimizer optimizer(PDFOptimizer::RemoveUnusedObjects, nullptr);
        optimizer.setDocument(&documentWithoutPages);
        optimizer.optimize();

        PDFDocumentBuilder finalBuilder(optimizer.getStorage(), documentWithoutPages.getInfo()->version);
        *result = finalBuilder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFOperationResult PDFPageOperations::extractPages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    if (!normalizePages(document, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (PDFInteger(pages.size()) == pageCount)
    {
        *result = *document;
        return true;
    }

    std::vector<PDFInteger> deletedPages;
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        if (!std::binary_search(pages.cbegin(), pages.cend(), i))
        {
            deletedPages.push_back(i);
        }
    }

    return deletePages(document, qMove(deletedPages), result);
}

PDFOperationResult PDFPageOperations::rotatePages(const PDFDocument* document, std::vector<PDFInteger> pages, bool right, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    if (!normalizePages(document, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    try
    {
        PDFDocumentBuilder builder(document);

        for (const PDFInteger pageIndex : pages)
        {
            const PDFPage* page = document->getCatalog()->getPage(pageIndex);
            const PageRotation rotation = right ? getPageRotationRotatedRight(page->getPageRotation())
                                                : getPageRotationRotatedLeft(page->getPageRotation());
            builder.setPageRotation(page->getPageReference(), rotation);
        }

        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFOperationResult PDFPageOperations::movePages(const PDFDocument* document, std::vector<PDFInteger> pages, bool towardsEnd, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    if (!normalizePages(document, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    try
    {
        PDFDocumentBuilder builder(document);
        builder.flattenPageTree();

        std::vector<PDFObjectReference> pageReferences = builder.getPages();
        const PDFInteger pageCount = PDFInteger(pageReferences.size());
        std::vector<bool> isMoved(pageReferences.size(), false);
        for (const PDFInteger pageIndex : pages)
        {
            isMoved[pageIndex] = true;
        }

        // Each moved page is swapped with its neighbour, if the neighbour is not moved
        // too - a block of pages at the very start (or end) therefore stays where it is.
        if (towardsEnd)
        {
            for (PDFInteger i = pageCount - 2; i >= 0; --i)
            {
                if (isMoved[i] && !isMoved[i + 1])
                {
                    std::swap(pageReferences[i], pageReferences[i + 1]);
                    isMoved[i] = false;
                    isMoved[i + 1] = true;
                }
            }
        }
        else
        {
            for (PDFInteger i = 1; i < pageCount; ++i)
            {
                if (isMoved[i] && !isMoved[i - 1])
                {
                    std::swap(pageReferences[i], pageReferences[i - 1]);
                    isMoved[i] = false;
                    isMoved[i - 1] = true;
                }
            }
        }

        builder.setPages(pageReferences);
        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFOperationResult PDFPageOperations::insertDocument(const PDFDocument* document, const PDFDocument* insertedDocument, PDFInteger position, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(insertedDocument);
    Q_ASSERT(result);

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (position < 0 || position > pageCount)
    {
        return PDFTranslationContext::tr("Invalid position of the inserted pages.");
    }

    if (insertedDocument->getCatalog()->getPageCount() == 0)
    {
        return PDFTranslationContext::tr("Inserted document has no pages.");
    }

    try
    {
        PDFDocumentBuilder builder(document);
        builder.flattenPageTree();
        std::vector<PDFObjectReference> pageReferences = builder.getPages();

        PDFDocumentBuilder insertedBuilder(insertedDocument);
        insertedBuilder.flattenPageTree();
        std::vector<PDFObjectReference> objectsToCopy = insertedBuilder.getPages();
        const size_t insertedPageCount = objectsToCopy.size();

        // The interactive form of the inserted document is copied together with
        // its pages, otherwise the form fields on them would stop working.
        const PDFObject insertedFormObject = insertedDocument->getCatalog()->getFormObject();
        const bool hasInsertedForm = !insertedBuilder.getObject(insertedFormObject).isNull();
        if (hasInsertedForm)
        {
            objectsToCopy.push_back(insertedFormObject.isReference() ? insertedFormObject.getReference() : insertedBuilder.addObject(insertedFormObject));
        }

        std::vector<PDFObjectReference> copiedReferences = PDFDocumentBuilder::createReferencesFromObjects(
                    builder.copyFrom(PDFDocumentBuilder::createObjectsFromReferences(objectsToCopy), *insertedBuilder.getStorage(), true));

        if (hasInsertedForm)
        {
            const PDFObjectReference copiedFormReference = copiedReferences.back();
            copiedReferences.pop_back();

            // The forms are merged into a new object - the arrays of their fields are concatenated
            const PDFObjectReference mergedFormReference = builder.addObject(PDFObject());
            builder.appendTo(mergedFormReference, builder.getObject(document->getCatalog()->getFormObject()));
            builder.appendTo(mergedFormReference, builder.getObjectByReference(copiedFormReference));
            builder.setCatalogAcroForm(mergedFormReference);
        }

        Q_ASSERT(copiedReferences.size() == insertedPageCount);
        pageReferences.insert(std::next(pageReferences.begin(), position), copiedReferences.cbegin(), copiedReferences.cend());
        builder.setPages(pageReferences);

        // The copied pages still refer to the page tree of their former document
        builder.flattenPageTree();

        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

}   // namespace pdf
