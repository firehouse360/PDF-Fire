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

#include "pdffirepageorganizer.h"
#include "pdffirepageoperations.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfimage.h"
#include "pdfaction.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include <map>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

/// Makes copies of pages inside one document. The copy of a page gets its own copy of
/// the content streams (an edit of one page must not change the other), it shares the
/// resources (fonts, pictures) with its page. The annotations are copied; a form field
/// widget is copied as another widget of the same field (Kids), or - when the widget is
/// the field itself - as a new field with a changed name.
class PageCopier
{
public:
    explicit PageCopier(PDFDocumentBuilder& builder) : m_builder(builder) { }

    PDFObjectReference copy(PDFObjectReference pageReference);

    /// Adds the new widgets to their fields, and the new fields to the form
    void finish();

private:
    PDFObject copyStreamReference(const PDFObject& object);
    PDFObject copyContents(const PDFObject& contents);
    QByteArray getUniqueFieldName(const QByteArray& name);
    void loadFieldNames();

    PDFDocumentBuilder& m_builder;
    bool m_fieldNamesLoaded = false;
    std::set<QByteArray> m_fieldNames;
    std::vector<PDFObjectReference> m_newFields;
    std::map<PDFObjectReference, std::vector<PDFObjectReference>> m_newKids;
};

PDFObject PageCopier::copyStreamReference(const PDFObject& object)
{
    if (object.isReference())
    {
        const PDFObject target = m_builder.getObjectByReference(object.getReference());
        if (target.isStream())
        {
            return PDFObject::createReference(m_builder.addObject(target));
        }
    }

    return object;
}

PDFObject PageCopier::copyContents(const PDFObject& contents)
{
    const PDFObject dereferenced = m_builder.getObject(contents);
    if (dereferenced.isArray())
    {
        PDFArrayBuilder array;
        for (const PDFObject& item : *dereferenced.getArray())
        {
            array.appendItem(copyStreamReference(item));
        }
        return PDFObject::createArray(std::move(array));
    }

    return copyStreamReference(contents);
}

void PageCopier::loadFieldNames()
{
    if (m_fieldNamesLoaded)
    {
        return;
    }
    m_fieldNamesLoaded = true;

    const PDFObjectStorage* storage = m_builder.getStorage();
    const PDFObject catalog = m_builder.getObjectByReference(m_builder.getCatalogReference());
    const PDFDictionary* catalogDictionary = storage->getDictionaryFromObject(catalog);
    const PDFDictionary* acroForm = catalogDictionary ? storage->getDictionaryFromObject(catalogDictionary->get("AcroForm")) : nullptr;
    const PDFObject fields = acroForm ? storage->getObject(acroForm->get("Fields")) : PDFObject();
    if (fields.isArray())
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        for (const PDFObject& field : *fields.getArray())
        {
            if (const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(field))
            {
                m_fieldNames.insert(loader.readStringFromDictionary(fieldDictionary, "T"));
            }
        }
    }
}

QByteArray PageCopier::getUniqueFieldName(const QByteArray& name)
{
    loadFieldNames();

    // A text string can be in UTF-16BE (with its byte order mark), the suffix must be too
    const bool isUnicode = name.startsWith("\xFE\xFF");
    for (int number = 2; ; ++number)
    {
        const QString suffix = QString("_%1").arg(number);
        QByteArray candidate = name;
        if (isUnicode)
        {
            for (const QChar character : suffix)
            {
                candidate.append(char(0));
                candidate.append(char(character.unicode()));
            }
        }
        else
        {
            candidate.append(suffix.toLatin1());
        }

        if (m_fieldNames.insert(candidate).second)
        {
            return candidate;
        }
    }
}

PDFObjectReference PageCopier::copy(PDFObjectReference pageReference)
{
    const PDFObjectStorage* storage = m_builder.getStorage();
    PDFDocumentDataLoaderDecorator loader(storage);

    // Copies of the objects are kept, the storage grows while the copy is made
    const PDFObject pageObject = m_builder.getObjectByReference(pageReference);
    const PDFDictionary* pageDictionary = pageObject.getDictionary();
    if (!pageDictionary)
    {
        throw PDFException(PDFTranslationContext::tr("Invalid page."));
    }

    const PDFObjectReference newPageReference = m_builder.addObject(PDFObject());
    PDFDictionaryBuilder newPage(*pageDictionary);

    // The copy is not a part of the logical structure (tags) and of the article threads
    newPage.removeEntry("StructParents");
    newPage.removeEntry("B");

    if (pageDictionary->hasKey("Contents"))
    {
        newPage.setEntry(PDFInplaceOrMemoryString("Contents"), copyContents(pageDictionary->get("Contents")));
    }

    const PDFObject annotations = storage->getObject(pageDictionary->get("Annots"));
    if (annotations.isArray())
    {
        // The new numbers first - the annotations refer to each other (pop-up windows, replies)
        std::map<PDFObjectReference, PDFObjectReference> mapping;
        std::vector<std::pair<PDFObjectReference, PDFObject>> copied;
        for (const PDFObject& item : *annotations.getArray())
        {
            if (!item.isReference())
            {
                continue;
            }

            const PDFObject annotation = m_builder.getObjectByReference(item.getReference());
            if (!annotation.isDictionary() || mapping.count(item.getReference()))
            {
                continue;
            }

            const PDFObjectReference newReference = m_builder.addObject(PDFObject());
            mapping[item.getReference()] = newReference;
            copied.emplace_back(newReference, annotation);
        }

        auto remap = [&mapping](PDFDictionaryBuilder& dictionary, const char* key)
        {
            const PDFObject& value = dictionary.get(key);
            if (value.isReference())
            {
                auto it = mapping.find(value.getReference());
                if (it != mapping.cend())
                {
                    dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject::createReference(it->second));
                }
            }
        };

        PDFArrayBuilder newAnnotations;
        for (const auto& [newReference, annotation] : copied)
        {
            const PDFDictionary* annotationDictionary = annotation.getDictionary();
            PDFDictionaryBuilder newAnnotation(*annotationDictionary);
            newAnnotation.setEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(newPageReference));
            newAnnotation.removeEntry("StructParent");
            newAnnotation.removeEntry("NM");     // the unique name of the annotation
            remap(newAnnotation, "Popup");
            remap(newAnnotation, "IRT");

            if (loader.readNameFromDictionary(annotationDictionary, "Subtype") == "Widget")
            {
                const PDFObject parent = annotationDictionary->get("Parent");
                if (annotationDictionary->hasKey("T"))
                {
                    // The widget is the field itself - the copy is a new field
                    newAnnotation.setEntry(PDFInplaceOrMemoryString("T"), PDFObject::createString(getUniqueFieldName(loader.readStringFromDictionary(annotationDictionary, "T"))));
                    if (parent.isReference())
                    {
                        m_newKids[parent.getReference()].push_back(newReference);
                    }
                    else
                    {
                        m_newFields.push_back(newReference);
                    }
                }
                else if (parent.isReference())
                {
                    // Another widget of the same field
                    m_newKids[parent.getReference()].push_back(newReference);
                }
            }
            else
            {
                remap(newAnnotation, "Parent");     // a pop-up window of a copied annotation
            }

            m_builder.setObject(newReference, PDFObject::createDictionary(std::move(newAnnotation)));
            newAnnotations.appendItem(PDFObject::createReference(newReference));
        }

        newPage.setEntry(PDFInplaceOrMemoryString("Annots"), PDFObject::createArray(std::move(newAnnotations)));
    }

    m_builder.setObject(newPageReference, PDFObject::createDictionary(std::move(newPage)));
    return newPageReference;
}

void PageCopier::finish()
{
    const PDFObjectStorage* storage = m_builder.getStorage();

    for (const auto& [parentReference, kids] : m_newKids)
    {
        const PDFObject parent = m_builder.getObjectByReference(parentReference);
        if (!parent.isDictionary())
        {
            continue;
        }

        PDFDictionaryBuilder newParent(*parent.getDictionary());
        const PDFObject oldKids = storage->getObject(parent.getDictionary()->get("Kids"));
        PDFArrayBuilder newKids = oldKids.isArray() ? PDFArrayBuilder(*oldKids.getArray()) : PDFArrayBuilder();
        for (const PDFObjectReference& kid : kids)
        {
            newKids.appendItem(PDFObject::createReference(kid));
        }
        newParent.setEntry(PDFInplaceOrMemoryString("Kids"), PDFObject::createArray(std::move(newKids)));
        m_builder.setObject(parentReference, PDFObject::createDictionary(std::move(newParent)));
    }

    if (m_newFields.empty())
    {
        return;
    }

    const PDFObject catalog = m_builder.getObjectByReference(m_builder.getCatalogReference());
    const PDFDictionary* catalogDictionary = storage->getDictionaryFromObject(catalog);
    const PDFObject acroFormObject = catalogDictionary ? catalogDictionary->get("AcroForm") : PDFObject();
    const PDFObject acroForm = storage->getObject(acroFormObject);

    PDFDictionaryBuilder newAcroForm = acroForm.isDictionary() ? PDFDictionaryBuilder(*acroForm.getDictionary()) : PDFDictionaryBuilder();
    const PDFObject oldFields = acroForm.isDictionary() ? storage->getObject(acroForm.getDictionary()->get("Fields")) : PDFObject();
    PDFArrayBuilder newFields = oldFields.isArray() ? PDFArrayBuilder(*oldFields.getArray()) : PDFArrayBuilder();
    for (const PDFObjectReference& field : m_newFields)
    {
        newFields.appendItem(PDFObject::createReference(field));
    }
    newAcroForm.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(std::move(newFields)));

    if (acroFormObject.isReference())
    {
        m_builder.setObject(acroFormObject.getReference(), PDFObject::createDictionary(std::move(newAcroForm)));
    }
    else
    {
        m_builder.setCatalogAcroForm(m_builder.addObject(PDFObject::createDictionary(std::move(newAcroForm))));
    }
}

/// Removes characters, which can't be in a file name (on Linux and Windows)
QString sanitizeFileNamePart(QString text)
{
    QString result;
    for (const QChar character : text)
    {
        if (character.isPrint() && !QStringLiteral("\\/:*?\"<>|").contains(character))
        {
            result.append(character);
        }
        else
        {
            result.append(QChar(' '));
        }
    }

    result = result.simplified();
    while (result.endsWith(QChar('.')))
    {
        result.chop(1);
    }
    if (result.size() > 60)
    {
        result = result.left(60).trimmed();
    }
    return result;
}

}   // namespace

bool PDFFirePageOrganizer::normalizePages(PDFInteger pageCount, std::vector<PDFInteger>& pages)
{
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return !pages.empty() && pages.front() >= 0 && pages.back() < pageCount;
}

PDFOperationResult PDFFirePageOrganizer::arrangePages(const PDFDocument* document, const std::vector<PDFInteger>& newOrder, PDFDocument* result)
{
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    std::vector<bool> isUsed(pageCount, false);
    if (PDFInteger(newOrder.size()) != pageCount)
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }
    for (const PDFInteger page : newOrder)
    {
        if (page < 0 || page >= pageCount || isUsed[page])
        {
            return PDFTranslationContext::tr("Invalid pages.");
        }
        isUsed[page] = true;
    }

    try
    {
        PDFDocumentBuilder builder(document);
        builder.flattenPageTree();

        const std::vector<PDFObjectReference> pageReferences = builder.getPages();
        std::vector<PDFObjectReference> newPageReferences;
        newPageReferences.reserve(pageReferences.size());
        for (const PDFInteger page : newOrder)
        {
            newPageReferences.push_back(pageReferences[page]);
        }

        builder.setPages(newPageReferences);
        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFOperationResult PDFFirePageOrganizer::movePagesTo(const PDFDocument* document, std::vector<PDFInteger> pages, PDFInteger position, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (!normalizePages(pageCount, pages) || position < 0 || position > pageCount)
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    std::vector<PDFInteger> newOrder;
    newOrder.reserve(pageCount);
    for (PDFInteger i = 0; i < position; ++i)
    {
        if (!std::binary_search(pages.cbegin(), pages.cend(), i))
        {
            newOrder.push_back(i);
        }
    }
    newOrder.insert(newOrder.end(), pages.cbegin(), pages.cend());
    for (PDFInteger i = position; i < pageCount; ++i)
    {
        if (!std::binary_search(pages.cbegin(), pages.cend(), i))
        {
            newOrder.push_back(i);
        }
    }

    return arrangePages(document, newOrder, result);
}

PDFOperationResult PDFFirePageOrganizer::copyPages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFInteger position, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (!normalizePages(pageCount, pages) || position < 0 || position > pageCount)
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    try
    {
        PDFDocumentBuilder builder(document);
        builder.flattenPageTree();

        std::vector<PDFObjectReference> pageReferences = builder.getPages();
        std::vector<PDFObjectReference> copies;

        PageCopier copier(builder);
        for (const PDFInteger page : pages)
        {
            copies.push_back(copier.copy(pageReferences[page]));
        }
        copier.finish();

        pageReferences.insert(std::next(pageReferences.begin(), position), copies.cbegin(), copies.cend());
        builder.setPages(pageReferences);
        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFOperationResult PDFFirePageOrganizer::duplicatePages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result)
{
    Q_ASSERT(document);

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (!normalizePages(pageCount, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    const PDFInteger position = pages.back() + 1;
    return copyPages(document, std::move(pages), position, result);
}

PDFOperationResult PDFFirePageOrganizer::reversePages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (pages.empty())
    {
        for (PDFInteger i = 0; i < pageCount; ++i)
        {
            pages.push_back(i);
        }
    }

    if (!normalizePages(pageCount, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    std::vector<PDFInteger> newOrder;
    newOrder.reserve(pageCount);
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        newOrder.push_back(i);
    }

    // The selected places get the selected pages in the opposite order
    for (size_t i = 0; i < pages.size(); ++i)
    {
        newOrder[pages[i]] = pages[pages.size() - 1 - i];
    }

    return arrangePages(document, newOrder, result);
}

PDFOperationResult PDFFirePageOrganizer::replacePages(const PDFDocument* document, std::vector<PDFInteger> pages,
                                                      const PDFDocument* sourceDocument, std::vector<PDFInteger> sourcePages, PDFDocument* result)
{
    Q_ASSERT(document);
    Q_ASSERT(sourceDocument);
    Q_ASSERT(result);

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    const PDFInteger sourcePageCount = PDFInteger(sourceDocument->getCatalog()->getPageCount());
    if (!normalizePages(pageCount, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    std::set<PDFInteger> usedSourcePages;
    for (const PDFInteger sourcePage : sourcePages)
    {
        if (sourcePage < 0 || sourcePage >= sourcePageCount || !usedSourcePages.insert(sourcePage).second)
        {
            return PDFTranslationContext::tr("Invalid pages of the other document.");
        }
    }

    if (sourcePages.size() != pages.size())
    {
        return PDFTranslationContext::tr("The number of the new pages (%1) is not the number of the replaced pages (%2).").arg(sourcePages.size()).arg(pages.size());
    }

    // The whole other document is put at the end, its pages are moved to the places of
    // the replaced pages, and what is left (the replaced pages and the pages of the other
    // document, which are not used) is deleted - with its content
    PDFDocument mergedDocument;
    PDFOperationResult operationResult = PDFPageOperations::insertDocument(document, sourceDocument, pageCount, &mergedDocument);
    if (!operationResult)
    {
        return operationResult;
    }

    std::vector<PDFInteger> newOrder;
    std::vector<bool> isUsed(pageCount + sourcePageCount, false);
    size_t replacedIndex = 0;
    for (PDFInteger i = 0; i < pageCount; ++i)
    {
        PDFInteger page = i;
        if (replacedIndex < pages.size() && pages[replacedIndex] == i)
        {
            page = pageCount + sourcePages[replacedIndex++];
        }
        newOrder.push_back(page);
        isUsed[page] = true;
    }

    std::vector<PDFInteger> deletedPages;
    for (PDFInteger i = 0; i < pageCount + sourcePageCount; ++i)
    {
        if (!isUsed[i])
        {
            deletedPages.push_back(PDFInteger(newOrder.size()));
            newOrder.push_back(i);
        }
    }

    PDFDocument arrangedDocument;
    operationResult = arrangePages(&mergedDocument, newOrder, &arrangedDocument);
    if (!operationResult)
    {
        return operationResult;
    }

    if (deletedPages.empty())
    {
        *result = std::move(arrangedDocument);
        return true;
    }

    return PDFPageOperations::deletePages(&arrangedDocument, std::move(deletedPages), result);
}

std::vector<PDFFirePageOrganizer::SplitPart> PDFFirePageOrganizer::getSplitEveryNPages(PDFInteger pageCount, PDFInteger pagesPerPart)
{
    std::vector<SplitPart> parts;
    if (pagesPerPart <= 0)
    {
        return parts;
    }

    for (PDFInteger first = 0; first < pageCount; first += pagesPerPart)
    {
        SplitPart part;
        for (PDFInteger page = first; page < qMin(first + pagesPerPart, pageCount); ++page)
        {
            part.pages.push_back(page);
        }
        parts.push_back(std::move(part));
    }

    return parts;
}

std::vector<PDFFirePageOrganizer::SplitPart> PDFFirePageOrganizer::getSplitByBookmarks(const PDFDocument* document)
{
    std::vector<SplitPart> parts;

    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    QSharedPointer<PDFOutlineItem> root = catalog->getOutlineRootPtr();
    if (!root || pageCount == 0)
    {
        return parts;
    }

    // The first page of each top-level bookmark (the first bookmark of a page wins)
    std::map<PDFInteger, QString> starts;
    for (size_t i = 0; i < root->getChildCount(); ++i)
    {
        const PDFOutlineItem* item = root->getChild(i);
        const PDFActionGoTo* goToAction = dynamic_cast<const PDFActionGoTo*>(item->getAction());
        if (!goToAction)
        {
            continue;
        }

        const PDFDestination* destination = &goToAction->getDestination();
        if (destination->getDestinationType() == DestinationType::Named)
        {
            destination = catalog->getNamedDestination(destination->getName());
        }
        if (!destination)
        {
            continue;
        }

        PDFInteger pageIndex = -1;
        if (destination->getPageReference().isValid())
        {
            const size_t index = catalog->getPageIndexFromPageReference(destination->getPageReference());
            pageIndex = index < size_t(pageCount) ? PDFInteger(index) : -1;
        }
        else
        {
            pageIndex = destination->getPageIndex();
        }

        if (pageIndex >= 0 && pageIndex < pageCount && !starts.count(pageIndex))
        {
            starts[pageIndex] = item->getTitle();
        }
    }

    if (starts.empty())
    {
        return parts;
    }

    if (starts.cbegin()->first > 0)
    {
        starts[0] = QString();
    }

    for (auto it = starts.cbegin(); it != starts.cend(); ++it)
    {
        auto next = std::next(it);
        const PDFInteger end = (next != starts.cend()) ? next->first : pageCount;

        SplitPart part;
        part.title = it->second;
        for (PDFInteger page = it->first; page < end; ++page)
        {
            part.pages.push_back(page);
        }
        parts.push_back(std::move(part));
    }

    return parts;
}

QString PDFFirePageOrganizer::getSplitPartFileName(const QString& baseName, int partNumber, int partCount, const QString& title)
{
    const int digits = QString::number(qMax(partCount, 1)).size();
    QString fileName = QString("%1 - %2").arg(baseName).arg(partNumber, digits, 10, QChar('0'));

    const QString cleanTitle = sanitizeFileNamePart(title);
    if (!cleanTitle.isEmpty())
    {
        fileName += QChar(' ') + cleanTitle;
    }

    return fileName + QStringLiteral(".pdf");
}

bool PDFFirePageOrganizer::isSupportedImageFile(const QString& fileName)
{
    static const QStringList suffixes = { "png", "jpg", "jpeg", "jpe", "tif", "tiff", "bmp", "gif", "webp" };
    return suffixes.contains(QFileInfo(fileName).suffix().toLower());
}

PDFOperationResult PDFFirePageOrganizer::readImageFile(const QString& fileName, std::vector<ImageSource>* images)
{
    Q_ASSERT(images);

    QImageReader reader(fileName);
    reader.setAutoTransform(true);
    if (!reader.canRead())
    {
        return PDFTranslationContext::tr("File '%1' is not a picture, which can be read.").arg(QFileInfo(fileName).fileName());
    }

    const QByteArray format = reader.format().toLower();
    const int count = qMax(reader.imageCount(), 1);
    const size_t firstImage = images->size();

    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && !reader.jumpToImage(i))
        {
            break;
        }

        QImage image = reader.read();
        if (image.isNull())
        {
            if (i == 0)
            {
                return PDFTranslationContext::tr("File '%1' cannot be read. %2").arg(QFileInfo(fileName).fileName(), reader.errorString());
            }
            break;
        }

        // The resolution of the picture gives its size; 72 dpi, if it has none (or a silly one)
        auto getDpi = [](int dotsPerMeter)
        {
            const PDFReal dpi = dotsPerMeter * 0.0254;
            return (dpi >= 36.0 && dpi <= 2400.0) ? dpi : 72.0;
        };

        ImageSource source;
        source.sizeInPoints = QSizeF(image.width() * 72.0 / getDpi(image.dotsPerMeterX()), image.height() * 72.0 / getDpi(image.dotsPerMeterY()));

        // A JPEG photo is stored as it is - decoding and encoding it again loses quality.
        // Not when it must be turned (its EXIF orientation), and not in CMYK.
        if (count == 1 && (format == "jpeg" || format == "jpg") &&
            reader.transformation() == QImageIOHandler::TransformationNone
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            && image.format() != QImage::Format_CMYK8888
#endif
            )
        {
            QFile file(fileName);
            if (file.open(QFile::ReadOnly))
            {
                source.jpegData = file.readAll();
            }
        }

        source.image = std::move(image);
        images->push_back(std::move(source));
    }

    if (images->size() == firstImage)
    {
        return PDFTranslationContext::tr("File '%1' contains no picture.").arg(QFileInfo(fileName).fileName());
    }

    return true;
}

PDFOperationResult PDFFirePageOrganizer::createDocumentFromImages(const std::vector<ImageSource>& images, ImagePageSize pageSize, PDFDocument* result, QSizeF customPageSize)
{
    Q_ASSERT(result);

    if (images.empty())
    {
        return PDFTranslationContext::tr("There are no pictures.");
    }

    // The largest page allowed by the PDF specification (200 inches)
    constexpr PDFReal MAXIMAL_PAGE_SIZE = 14400.0;
    constexpr PDFReal MARGIN = 18.0;

    try
    {
        PDFDocumentBuilder builder;

        for (const ImageSource& source : images)
        {
            if (source.image.isNull())
            {
                continue;
            }

            QSizeF imageSize = source.sizeInPoints.isValid() && !source.sizeInPoints.isEmpty() ? source.sizeInPoints : QSizeF(source.image.size());
            QRectF mediaBox;
            QRectF imageRect;

            if (pageSize == ImagePageSize::Image)
            {
                const PDFReal scale = qMin(1.0, MAXIMAL_PAGE_SIZE / qMax(imageSize.width(), imageSize.height()));
                imageSize *= scale;
                imageSize = imageSize.expandedTo(QSizeF(1.0, 1.0));
                mediaBox = QRectF(QPointF(0, 0), imageSize);
                imageRect = mediaBox;
            }
            else
            {
                QSizeF paperSize = (pageSize == ImagePageSize::Letter) ? QSizeF(612.0, 792.0) : QSizeF(595.276, 841.89);
                if (pageSize == ImagePageSize::Custom && customPageSize.width() >= 72.0 && customPageSize.height() >= 72.0)
                {
                    paperSize = customPageSize.width() <= customPageSize.height() ? customPageSize : customPageSize.transposed();
                }
                if (imageSize.width() > imageSize.height())
                {
                    paperSize.transpose();
                }

                mediaBox = QRectF(QPointF(0, 0), paperSize);
                const QRectF area = mediaBox.adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
                const QSizeF fittedSize = imageSize.scaled(area.size(), Qt::KeepAspectRatio);
                imageRect = QRectF(QPointF(area.center().x() - fittedSize.width() * 0.5, area.center().y() - fittedSize.height() * 0.5), fittedSize);
            }

            // The picture
            PDFObject imageObject;
            if (!source.jpegData.isEmpty())
            {
                PDFDictionaryBuilder dictionary;
                dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
                dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
                dictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(source.image.width()));
                dictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(source.image.height()));
                dictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName(source.image.isGrayscale() ? "DeviceGray" : "DeviceRGB"));
                dictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
                dictionary.addEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("DCTDecode"));
                dictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(source.jpegData.size()));
                imageObject = PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(source.jpegData)));
            }
            else
            {
                PDFImage::ImageEncodeOptions options;
                options.compression = PDFImage::ImageCompression::Flate;
                PDFStream stream = PDFImage::createStreamFromImage(source.image, options, nullptr);
                PDFDictionaryBuilder dictionary(*stream.getDictionary());
                imageObject = PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(*stream.getContent())));
            }
            const PDFObjectReference imageReference = builder.addObject(std::move(imageObject));

            // The content draws the picture into its rectangle (PDF: y goes up)
            const PDFReal x = imageRect.left();
            const PDFReal y = mediaBox.height() - imageRect.bottom();
            const QByteArray content = QString("q %1 0 0 %2 %3 %4 cm /Im1 Do Q").arg(imageRect.width(), 0, 'f', 4).arg(imageRect.height(), 0, 'f', 4).arg(x, 0, 'f', 4).arg(y, 0, 'f', 4).toLatin1();
            const PDFObjectReference contents = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(), QByteArray(content))));

            const PDFObjectReference page = builder.appendPage(mediaBox);

            PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("Contents");
            factory << contents;
            factory.endDictionaryItem();
            factory.beginDictionaryItem("Resources");
            factory.beginDictionary();
            factory.beginDictionaryItem("XObject");
            factory.beginDictionary();
            factory.beginDictionaryItem("Im1");
            factory << imageReference;
            factory.endDictionaryItem();
            factory.endDictionary();
            factory.endDictionaryItem();
            factory.endDictionary();
            factory.endDictionaryItem();
            factory.endDictionary();
            builder.mergeTo(page, factory.takeObject());
        }

        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    if (result->getCatalog()->getPageCount() == 0)
    {
        return PDFTranslationContext::tr("There are no pictures.");
    }

    return true;
}

PDFOperationResult PDFFirePageOrganizer::createDocumentFromImageFiles(const QStringList& fileNames, ImagePageSize pageSize, PDFDocument* result)
{
    std::vector<ImageSource> images;
    for (const QString& fileName : fileNames)
    {
        const PDFOperationResult readResult = readImageFile(fileName, &images);
        if (!readResult)
        {
            return readResult;
        }
    }

    return createDocumentFromImages(images, pageSize, result);
}

}   // namespace pdf
