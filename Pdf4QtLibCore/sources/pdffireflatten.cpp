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

// Ported from PdfCraft crates/edit/src/flatten.rs (MIT OR Apache-2.0):
// the selection of the annotations, the placement of the appearance (ISO 32000-2,
// 12.5.5, Algorithm 8.1), and the rule, that an annotation without an appearance
// is kept (PdfCraft #1794), the pruning of the field tree.

#include "pdffireflatten.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfstreamfilters.h"

#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

namespace
{

constexpr PDFInteger HiddenFlag = 2;
constexpr PDFInteger NoViewFlag = 32;
constexpr int MaximalDepth = 64;

enum class Kind
{
    Other,      ///< Not flattened at all (links, pop-ups, multimedia...)
    Field,      ///< Widget of a form field
    Comment,    ///< Comment (markup annotation)
    Redaction   ///< Redaction mark - never flattened
};

Kind classify(const QByteArray& subtype)
{
    if (subtype == "Widget")
    {
        return Kind::Field;
    }
    if (subtype == "Redact")
    {
        return Kind::Redaction;
    }

    // Comments are what the comment list calls comments: links, pop-ups and the
    // multimedia and print production annotations are neither comments nor fields
    static const std::set<QByteArray> others = { "Link", "Popup", "Screen", "Movie", "RichMedia", "3D", "PrinterMark", "TrapNet", "Watermark" };
    if (subtype.isEmpty() || others.count(subtype))
    {
        return Kind::Other;
    }
    return Kind::Comment;
}

bool isSelected(Kind kind, PDFFireFlatten::Options options)
{
    return (kind == Kind::Field && options.testFlag(PDFFireFlatten::FormFields)) ||
           (kind == Kind::Comment && options.testFlag(PDFFireFlatten::Comments));
}

/// The widget belongs to a signature field, which is signed (FT and V are inheritable)
bool isSignedSignature(const PDFObjectStorage* storage, const PDFDictionary* dictionary)
{
    QByteArray fieldType;
    bool hasValue = false;
    for (int depth = 0; dictionary && depth < MaximalDepth; ++depth)
    {
        if (fieldType.isEmpty())
        {
            const PDFObject& type = storage->getObject(dictionary->get("FT"));
            if (type.isName())
            {
                fieldType = type.getString();
            }
        }
        if (!hasValue)
        {
            hasValue = !storage->getObject(dictionary->get("V")).isNull();
        }
        dictionary = storage->getDictionaryFromObject(dictionary->get("Parent"));
    }
    return fieldType == "Sig" && hasValue;
}

/// The normal appearance, which is shown (for check boxes and radio buttons the one
/// of the state /AS), or a null object
PDFObject getNormalAppearance(const PDFObjectStorage* storage, const PDFDictionary* dictionary)
{
    const PDFDictionary* appearance = storage->getDictionaryFromObject(dictionary->get("AP"));
    if (!appearance)
    {
        return PDFObject();
    }

    PDFObject normal = appearance->get("N");
    const PDFObject& dereferenced = storage->getObject(normal);
    if (dereferenced.isDictionary())
    {
        const PDFObject& state = storage->getObject(dictionary->get("AS"));
        if (!state.isName())
        {
            return PDFObject();
        }
        normal = dereferenced.getDictionary()->get(state.getString());
    }

    return storage->getObject(normal).isStream() ? normal : PDFObject();
}

/// Algorithm 8.1: the matrix, which maps the bounding box of the appearance
/// (transformed by its matrix) onto the rectangle of the annotation
std::optional<QTransform> getPlacement(const PDFObjectStorage* storage, const PDFObject& appearance, QRectF rectangle)
{
    const PDFObject& formObject = storage->getObject(appearance);
    if (!formObject.isStream() || !rectangle.isValid())
    {
        return std::nullopt;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFDictionary* formDictionary = formObject.getStream()->getDictionary();
    const QRectF boundingBox = loader.readRectangle(formDictionary->get("BBox"), QRectF()).normalized();
    const QTransform formMatrix = loader.readMatrixFromDictionary(formDictionary, "Matrix", QTransform());
    const QRectF transformedBox = formMatrix.mapRect(boundingBox);
    if (transformedBox.width() < 1e-9 || transformedBox.height() < 1e-9)
    {
        // An empty appearance (a reply, for example) draws nothing
        return std::nullopt;
    }

    const PDFReal scaleX = rectangle.width() / transformedBox.width();
    const PDFReal scaleY = rectangle.height() / transformedBox.height();
    return QTransform(scaleX, 0.0, 0.0, scaleY, rectangle.left() - transformedBox.left() * scaleX, rectangle.top() - transformedBox.top() * scaleY);
}

QByteArray formatNumber(PDFReal value)
{
    if (qAbs(value) < 1e-9)
    {
        return "0";
    }
    QByteArray text = QByteArray::number(value, 'f', 6);
    while (text.endsWith('0'))
    {
        text.chop(1);
    }
    if (text.endsWith('.'))
    {
        text.chop(1);
    }
    return text;
}

PDFObject createContentStream(const QByteArray& content, bool compress)
{
    PDFDictionaryBuilder dictionary;
    if (compress)
    {
        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        return PDFObject::createStream(PDFStream(std::move(dictionary), PDFFlateDecodeFilter::compress(content)));
    }
    return PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(content)));
}

/// The entries of an array (direct or referenced), or nothing
std::vector<PDFObject> getArrayItems(const PDFObjectStorage* storage, const PDFObject& object)
{
    std::vector<PDFObject> items;
    const PDFObject& dereferenced = storage->getObject(object);
    if (dereferenced.isArray())
    {
        const PDFArray* array = dereferenced.getArray();
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            items.push_back(array->getItem(i));
        }
    }
    return items;
}

PDFObject createArray(const std::vector<PDFObject>& items)
{
    PDFArrayBuilder array;
    for (const PDFObject& item : items)
    {
        array.appendItem(item);
    }
    return PDFObject::createArray(std::move(array));
}

struct AnnotationInfo
{
    Kind kind = Kind::Other;
    bool visible = true;
    bool signedSignature = false;
};

AnnotationInfo getAnnotationInfo(const PDFObjectStorage* storage, const PDFDictionary* dictionary)
{
    AnnotationInfo info;
    PDFDocumentDataLoaderDecorator loader(storage);
    info.kind = classify(loader.readNameFromDictionary(dictionary, "Subtype"));
    const PDFInteger flags = loader.readIntegerFromDictionary(dictionary, "F", 0);
    info.visible = (flags & (HiddenFlag | NoViewFlag)) == 0;
    info.signedSignature = info.kind == Kind::Field && isSignedSignature(storage, dictionary);
    return info;
}

/// Removes the flattened widgets from the field tree, and the fields left
/// without widgets. Returns true, if the node stays.
bool pruneField(PDFDocumentBuilder& builder, const PDFObject& node, const std::set<PDFObjectReference>& removedWidgets, int depth)
{
    if (depth > MaximalDepth)
    {
        return true;
    }
    if (node.isReference() && removedWidgets.count(node.getReference()))
    {
        return false;
    }

    const PDFDictionary* dictionary = builder.getDictionaryFromObject(node);
    if (!dictionary || !dictionary->hasKey("Kids"))
    {
        return true;
    }

    const std::vector<PDFObject> kids = getArrayItems(builder.getStorage(), dictionary->get("Kids"));
    std::vector<PDFObject> survivors;
    for (const PDFObject& kid : kids)
    {
        if (pruneField(builder, kid, removedWidgets, depth + 1))
        {
            survivors.push_back(kid);
        }
    }

    if (survivors.size() != kids.size() && node.isReference())
    {
        // The dictionary is read again - the recursion may have changed the storage
        PDFDictionaryBuilder updated(*builder.getDictionaryFromObject(node));
        updated.setEntry(PDFInplaceOrMemoryString("Kids"), createArray(survivors));
        builder.setObject(node.getReference(), PDFObject::createDictionary(std::move(updated)));
    }

    return !survivors.empty();
}

void pruneFields(PDFDocumentBuilder& builder, const std::set<PDFObjectReference>& removedWidgets)
{
    const PDFDictionary* catalog = builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()));
    if (!catalog)
    {
        return;
    }

    const PDFObject formObject = catalog->get("AcroForm");
    const PDFDictionary* form = builder.getDictionaryFromObject(formObject);
    if (!form)
    {
        return;
    }

    const std::vector<PDFObject> fields = getArrayItems(builder.getStorage(), form->get("Fields"));
    std::vector<PDFObject> survivors;
    for (const PDFObject& field : fields)
    {
        if (pruneField(builder, field, removedWidgets, 0))
        {
            survivors.push_back(field);
        }
    }

    PDFDictionaryBuilder updatedForm(*builder.getDictionaryFromObject(formObject));
    updatedForm.setEntry(PDFInplaceOrMemoryString("Fields"), createArray(survivors));
    if (survivors.empty())
    {
        // Nothing is left to be filled in
        updatedForm.removeEntry("NeedAppearances");
    }

    if (formObject.isReference())
    {
        builder.setObject(formObject.getReference(), PDFObject::createDictionary(std::move(updatedForm)));
    }
    else
    {
        builder.setCatalogAcroForm(builder.addObject(PDFObject::createDictionary(std::move(updatedForm))));
    }
}

bool normalizePages(const PDFDocument* document, std::vector<PDFInteger>& pages)
{
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    if (pages.empty())
    {
        for (PDFInteger i = 0; i < pageCount; ++i)
        {
            pages.push_back(i);
        }
    }

    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return !pages.empty() && pages.front() >= 0 && pages.back() < pageCount;
}

}   // namespace

PDFOperationResult PDFFireFlatten::flatten(const PDFDocument* document,
                                           std::vector<PDFInteger> pages,
                                           Options options,
                                           const PDFFormManager* formManager,
                                           PDFDocument* result,
                                           Statistics* statistics)
{
    Q_ASSERT(document);
    Q_ASSERT(result);

    if (!normalizePages(document, pages))
    {
        return PDFTranslationContext::tr("Invalid pages.");
    }

    Statistics localStatistics;
    Statistics& stats = statistics ? *statistics : localStatistics;
    stats = Statistics();

    try
    {
        PDFDocumentBuilder builder(document);
        builder.flattenPageTree();
        if (formManager && options.testFlag(FormFields))
        {
            builder.setFormManager(formManager);
        }

        const bool regenerateFields = formManager && formManager->getForm()->isAppearanceUpdateNeeded();
        const std::vector<PDFObjectReference> pageReferences = builder.getPages();
        std::set<PDFObjectReference> removedWidgets;

        for (const PDFInteger pageIndex : pages)
        {
            const PDFObjectReference pageReference = pageReferences[pageIndex];
            const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
            if (!pageDictionary)
            {
                continue;
            }

            const std::vector<PDFObject> annotations = getArrayItems(builder.getStorage(), pageDictionary->get("Annots"));
            if (annotations.empty())
            {
                continue;
            }

            // A comment without an appearance is shown by the viewers drawn from its
            // dictionary (notes, for example), so the appearance is made first - the
            // comment is not lost. The same for the fields, if the form asks the viewer
            // to make the appearances (or a field has none), and the values are known.
            for (const PDFObject& annotation : annotations)
            {
                const PDFDictionary* dictionary = builder.getDictionaryFromObject(annotation);
                if (!dictionary || !annotation.isReference())
                {
                    continue;
                }

                const AnnotationInfo info = getAnnotationInfo(builder.getStorage(), dictionary);
                if (!info.visible || !isSelected(info.kind, options) || info.signedSignature)
                {
                    continue;
                }

                const bool hasAppearance = !getNormalAppearance(builder.getStorage(), dictionary).isNull();
                if (info.kind == Kind::Comment && !hasAppearance)
                {
                    builder.updateAnnotationAppearanceStreams(annotation.getReference(), true);
                }
                else if (info.kind == Kind::Field && formManager && (regenerateFields || !hasAppearance) &&
                         formManager->getFormFieldForWidget(annotation.getReference()))
                {
                    builder.updateAnnotationAppearanceStreams(annotation.getReference());
                }
            }

            // The page is read again, the appearances may have changed the storage
            pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
            const PDFDictionary* resources = builder.getDictionaryFromObject(pageDictionary->get("Resources"));
            const PDFDictionary* existingXObjects = resources ? builder.getDictionaryFromObject(resources->get("XObject")) : nullptr;

            QByteArray content;
            std::map<QByteArray, PDFObject> xobjects;
            std::set<PDFObjectReference> removed;
            std::set<size_t> removedDirect;
            std::set<PDFObjectReference> undrawn;
            int nameCounter = 0;

            PDFDocumentDataLoaderDecorator loader(builder.getStorage());
            for (size_t i = 0; i < annotations.size(); ++i)
            {
                const PDFObject& annotation = annotations[i];
                const PDFDictionary* dictionary = builder.getDictionaryFromObject(annotation);
                if (!dictionary)
                {
                    continue;
                }

                const AnnotationInfo info = getAnnotationInfo(builder.getStorage(), dictionary);
                if (!info.visible)
                {
                    continue;
                }
                if (info.kind == Kind::Redaction && options.testFlag(Comments))
                {
                    ++stats.keptRedactionMarks;
                    continue;
                }
                if (!isSelected(info.kind, options))
                {
                    continue;
                }
                if (info.signedSignature)
                {
                    ++stats.keptSignatures;
                    continue;
                }

                const PDFObject appearance = getNormalAppearance(builder.getStorage(), dictionary);
                const QRectF rectangle = loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized();

                bool drawn = false;
                if (!appearance.isNull())
                {
                    if (std::optional<QTransform> placement = getPlacement(builder.getStorage(), appearance, rectangle))
                    {
                        QByteArray name;
                        do
                        {
                            name = "PFFl" + QByteArray::number(nameCounter++);
                        }
                        while (existingXObjects && existingXObjects->hasKey(name));

                        // The appearance stream is shared - it can be referenced directly,
                        // a direct stream object is added as a new object
                        xobjects[name] = appearance.isReference() ? appearance : PDFObject::createReference(builder.addObject(appearance));
                        content += "q " + formatNumber(placement->m11()) + " " + formatNumber(placement->m12()) + " " +
                                   formatNumber(placement->m21()) + " " + formatNumber(placement->m22()) + " " +
                                   formatNumber(placement->dx()) + " " + formatNumber(placement->dy()) + " cm /" + name + " Do Q\n";
                        drawn = true;
                    }
                }

                // An annotation, which has no appearance at all, stays: removing it would
                // lose its value (or its text) with nothing in its place (PdfCraft #1794).
                // One whose appearance draws nothing shows nothing anywhere, so it goes.
                if (appearance.isNull())
                {
                    ++stats.keptWithoutAppearance;
                    if (annotation.isReference())
                    {
                        undrawn.insert(annotation.getReference());
                    }
                    continue;
                }

                if (info.kind == Kind::Field)
                {
                    ++stats.flattenedFields;
                }
                else
                {
                    ++stats.flattenedComments;
                }
                Q_UNUSED(drawn);

                if (annotation.isReference())
                {
                    removed.insert(annotation.getReference());
                    if (info.kind == Kind::Field)
                    {
                        removedWidgets.insert(annotation.getReference());
                    }

                    // The pop-up of a flattened comment goes with it
                    const PDFObject& popup = dictionary->get("Popup");
                    if (popup.isReference())
                    {
                        removed.insert(popup.getReference());
                    }
                }
                else
                {
                    removedDirect.insert(i);
                }
            }

            if (removed.empty() && removedDirect.empty())
            {
                continue;
            }

            // Pop-ups and replies of the flattened comments go too, but not a reply,
            // which stays, because it has nothing to draw
            for (const PDFObject& annotation : annotations)
            {
                const PDFDictionary* dictionary = builder.getDictionaryFromObject(annotation);
                if (!dictionary || !annotation.isReference() || undrawn.count(annotation.getReference()))
                {
                    continue;
                }

                auto pointsToRemoved = [&](const char* key)
                {
                    const PDFObject& target = dictionary->get(key);
                    return target.isReference() && removed.count(target.getReference());
                };

                const bool isPopup = loader.readNameFromDictionary(dictionary, "Subtype") == "Popup";
                if ((isPopup && pointsToRemoved("Parent")) || pointsToRemoved("IRT"))
                {
                    removed.insert(annotation.getReference());
                }
            }

            std::vector<PDFObject> keptAnnotations;
            for (size_t i = 0; i < annotations.size(); ++i)
            {
                const PDFObject& annotation = annotations[i];
                const bool isRemoved = annotation.isReference() ? removed.count(annotation.getReference()) > 0 : removedDirect.count(i) > 0;
                if (!isRemoved)
                {
                    keptAnnotations.push_back(annotation);
                }
            }

            PDFDictionaryBuilder newPage(*pageDictionary);
            if (keptAnnotations.empty())
            {
                newPage.removeEntry("Annots");
            }
            else
            {
                newPage.setEntry(PDFInplaceOrMemoryString("Annots"), createArray(keptAnnotations));
            }

            if (!content.isEmpty())
            {
                // The original content is wrapped in q/Q, so the graphics state it leaves
                // does not move the appearances
                std::vector<PDFObject> contents = getArrayItems(builder.getStorage(), pageDictionary->get("Contents"));
                if (contents.empty() && !pageDictionary->get("Contents").isNull())
                {
                    contents.push_back(pageDictionary->get("Contents"));
                }

                std::vector<PDFObject> newContents;
                newContents.push_back(PDFObject::createReference(builder.addObject(createContentStream("q\n", false))));
                newContents.insert(newContents.end(), contents.cbegin(), contents.cend());
                newContents.push_back(PDFObject::createReference(builder.addObject(createContentStream("\nQ\n", false))));
                newContents.push_back(PDFObject::createReference(builder.addObject(createContentStream(content, true))));
                newPage.setEntry(PDFInplaceOrMemoryString("Contents"), createArray(newContents));

                PDFDictionaryBuilder newResources;
                if (resources)
                {
                    newResources = PDFDictionaryBuilder(*resources);
                }
                PDFDictionaryBuilder newXObjects;
                if (existingXObjects)
                {
                    newXObjects = PDFDictionaryBuilder(*existingXObjects);
                }
                for (const auto& [name, object] : xobjects)
                {
                    newXObjects.setEntry(PDFInplaceOrMemoryString(name), object);
                }
                newResources.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(newXObjects)));
                newPage.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(newResources)));
            }

            builder.setObject(pageReference, PDFObject::createDictionary(std::move(newPage)));
        }

        if (!removedWidgets.empty())
        {
            pruneFields(builder, removedWidgets);
        }

        *result = builder.build();
    }
    catch (const PDFException& exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFFireFlatten::Statistics PDFFireFlatten::count(const PDFDocument* document, std::vector<PDFInteger> pages, Options options)
{
    Statistics stats;
    if (!document || !normalizePages(document, pages))
    {
        return stats;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    for (const PDFInteger pageIndex : pages)
    {
        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        const PDFDictionary* pageDictionary = page ? storage->getDictionaryFromObject(storage->getObjectByReference(page->getPageReference())) : nullptr;
        if (!pageDictionary)
        {
            continue;
        }

        for (const PDFObject& annotation : getArrayItems(storage, pageDictionary->get("Annots")))
        {
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(annotation);
            if (!dictionary)
            {
                continue;
            }

            const AnnotationInfo info = getAnnotationInfo(storage, dictionary);
            if (!info.visible)
            {
                continue;
            }
            if (info.kind == Kind::Redaction && options.testFlag(Comments))
            {
                ++stats.keptRedactionMarks;
            }
            else if (isSelected(info.kind, options))
            {
                if (info.signedSignature)
                {
                    ++stats.keptSignatures;
                }
                else if (info.kind == Kind::Field)
                {
                    ++stats.flattenedFields;
                }
                else
                {
                    ++stats.flattenedComments;
                }
            }
        }
    }

    return stats;
}

bool PDFFireFlatten::hasSignatures(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    bool result = false;
    const PDFForm form = PDFForm::parse(document, document->getCatalog()->getFormObject());
    form.apply([&result](const PDFFormField* field)
    {
        if (field->getFieldType() == PDFFormField::FieldType::Signature && !field->getValue().isNull())
        {
            result = true;
        }
    });
    return result;
}

}   // namespace pdf
