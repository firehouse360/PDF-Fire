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

// PDF Fire: tests of the organizing of pages (PDFFirePageOrganizer) - moving pages to a
// place, copies, reversing, replacing, splitting, and documents made from pictures.

#include "pdffirepageorganizer.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfaction.h"
#include "pdfform.h"

#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

using namespace pdf;

class PageOrganizerTest : public QObject
{
    Q_OBJECT

private slots:
    void movePagesTo();
    void copyPages();
    void duplicatePages();
    void copiedPageHasItsOwnContent();
    void copiedPageAnnotations();
    void copiedFormFields();
    void reversePages();
    void replacePages();
    void invalidInputIsRefused();
    void sourceDocumentIsUnchanged();
    void splitEveryNPages();
    void splitByBookmarks();
    void splitPartFileNames();
    void documentFromImages();
    void documentFromJpegKeepsTheFile();
    void notAPictureIsRefused();
    void tiffKinds_data();
    void tiffKinds();

private:
    /// The page i (from 1) is 100 * i points wide and its content contains PAGE-<prefix>i-MARKER
    static PDFDocument createDocument(int pageCount, const QByteArray& prefix = QByteArray(), std::vector<PDFObjectReference>* pages = nullptr, PDFDocumentBuilder* builder = nullptr);
    static QByteArray marker(int page, const QByteArray& prefix = QByteArray());
    static std::vector<int> pageWidths(const PDFDocument& document);
    static QByteArray pageContent(const PDFDocument& document, size_t pageIndex);
    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);
    static std::vector<QByteArray> fieldNames(const PDFDocument& document);
};

QByteArray PageOrganizerTest::marker(int page, const QByteArray& prefix)
{
    return "PAGE-" + prefix + QByteArray::number(page) + "-MARKER";
}

PDFDocument PageOrganizerTest::createDocument(int pageCount, const QByteArray& prefix, std::vector<PDFObjectReference>* pages, PDFDocumentBuilder* externalBuilder)
{
    PDFDocumentBuilder localBuilder;
    PDFDocumentBuilder& builder = externalBuilder ? *externalBuilder : localBuilder;

    for (int i = 1; i <= pageCount; ++i)
    {
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100 * i, 500));
        if (pages)
        {
            pages->push_back(page);
        }

        QByteArray content = "BT (" + marker(i, prefix) + ") Tj ET";
        const PDFObjectReference contents = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(), qMove(content))));

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Contents");
        factory << contents;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(page, factory.takeObject());
    }

    return builder.build();
}

std::vector<int> PageOrganizerTest::pageWidths(const PDFDocument& document)
{
    std::vector<int> widths;
    for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
    {
        widths.push_back(qRound(document.getCatalog()->getPage(i)->getMediaBox().width()));
    }
    return widths;
}

QByteArray PageOrganizerTest::pageContent(const PDFDocument& document, size_t pageIndex)
{
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const PDFObject& contents = document.getObject(page->getContents());
    if (contents.isStream())
    {
        return document.getDecodedStream(contents.getStream());
    }
    return QByteArray();
}

QByteArray PageOrganizerTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument PageOrganizerTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

std::vector<QByteArray> PageOrganizerTest::fieldNames(const PDFDocument& document)
{
    // A widget of a field is a child field too (with the same name) - the names are unique
    std::vector<QByteArray> names;
    const PDFForm form = PDFForm::parse(&document, document.getCatalog()->getFormObject());
    form.apply([&names](const PDFFormField* field) { names.push_back(field->getName(PDFFormField::NameType::FullyQualified).toLatin1()); });
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

void PageOrganizerTest::movePagesTo()
{
    const PDFDocument document = createDocument(5);
    PDFDocument result;

    // Pages 1 and 2 put in front of page 5
    QVERIFY(PDFFirePageOrganizer::movePagesTo(&document, { 0, 1 }, 4, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 300, 400, 100, 200, 500 }));

    // To the end, to the start; the order of the moved pages is kept
    QVERIFY(PDFFirePageOrganizer::movePagesTo(&document, { 3, 1 }, 5, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 300, 500, 200, 400 }));
    QVERIFY(PDFFirePageOrganizer::movePagesTo(&document, { 4, 2 }, 0, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 300, 500, 100, 200, 400 }));

    // Dropped onto its own place - nothing changes
    QVERIFY(PDFFirePageOrganizer::movePagesTo(&document, { 2 }, 2, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 400, 500 }));
    QVERIFY(PDFFirePageOrganizer::movePagesTo(&document, { 2 }, 3, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 400, 500 }));

    // The content goes with the page, and the result survives saving
    QVERIFY(PDFFirePageOrganizer::movePagesTo(&document, { 0 }, 5, &result));
    const PDFDocument reread = read(write(result));
    QCOMPARE(pageWidths(reread), (std::vector<int>{ 200, 300, 400, 500, 100 }));
    QVERIFY(pageContent(reread, 4).contains(marker(1)));
}

void PageOrganizerTest::copyPages()
{
    const PDFDocument document = createDocument(3);
    PDFDocument result;

    // Ctrl + drag: copies of pages 1 and 3 in front of page 2
    QVERIFY(PDFFirePageOrganizer::copyPages(&document, { 2, 0 }, 1, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 100, 300, 200, 300 }));
    QVERIFY(pageContent(result, 1).contains(marker(1)));
    QVERIFY(pageContent(result, 2).contains(marker(3)));

    // At the end
    QVERIFY(PDFFirePageOrganizer::copyPages(&document, { 1 }, 3, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 200 }));

    const PDFDocument reread = read(write(result));
    QCOMPARE(pageWidths(reread), (std::vector<int>{ 100, 200, 300, 200 }));
    QVERIFY(pageContent(reread, 3).contains(marker(2)));
}

void PageOrganizerTest::duplicatePages()
{
    const PDFDocument document = createDocument(4);
    PDFDocument result;

    QVERIFY(PDFFirePageOrganizer::duplicatePages(&document, { 1 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 200, 300, 400 }));

    // Several pages: the copies follow the last one, in their order
    QVERIFY(PDFFirePageOrganizer::duplicatePages(&document, { 2, 0 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 100, 300, 400 }));
}

void PageOrganizerTest::copiedPageHasItsOwnContent()
{
    const PDFDocument document = createDocument(2);
    PDFDocument result;
    QVERIFY(PDFFirePageOrganizer::duplicatePages(&document, { 0 }, &result));

    const PDFPage* original = result.getCatalog()->getPage(0);
    const PDFPage* copy = result.getCatalog()->getPage(1);
    QVERIFY(original->getPageReference() != copy->getPageReference());

    // An edit of the copy must not change the original page - the content streams differ
    const PDFObject originalContents = result.getObjectByReference(original->getPageReference()).getDictionary()->get("Contents");
    const PDFObject copiedContents = result.getObjectByReference(copy->getPageReference()).getDictionary()->get("Contents");
    QVERIFY(originalContents.isReference());
    QVERIFY(copiedContents.isReference());
    QVERIFY(originalContents.getReference() != copiedContents.getReference());
    QCOMPARE(pageContent(result, 1), pageContent(result, 0));
}

void PageOrganizerTest::copiedPageAnnotations()
{
    PDFDocumentBuilder builder;
    std::vector<PDFObjectReference> pages;
    createDocument(2, QByteArray(), &pages, &builder);
    builder.createAnnotationSquare(pages[0], QRectF(10, 10, 50, 50), 1.0, QColor(), Qt::red, "Title", "Subject", "Square note");
    builder.createAnnotationText(pages[0], QRectF(80, 80, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Sticky note", false);
    const PDFDocument document = builder.build();

    PDFDocument result;
    QVERIFY(PDFFirePageOrganizer::duplicatePages(&document, { 0 }, &result));
    const PDFDocument reread = read(write(result));

    const PDFPage* original = reread.getCatalog()->getPage(0);
    const PDFPage* copy = reread.getCatalog()->getPage(1);
    QCOMPARE(copy->getAnnotations().size(), original->getAnnotations().size());
    QVERIFY(original->getAnnotations().size() >= 3);    // square, text and its pop-up

    for (size_t i = 0; i < copy->getAnnotations().size(); ++i)
    {
        const PDFObjectReference originalAnnotation = original->getAnnotations()[i];
        const PDFObjectReference copiedAnnotation = copy->getAnnotations()[i];
        QVERIFY(originalAnnotation != copiedAnnotation);

        // The copy belongs to the copied page, its pop-up is the copied pop-up
        const PDFDictionary* dictionary = reread.getDictionaryFromObject(reread.getObjectByReference(copiedAnnotation));
        QVERIFY(dictionary);
        QCOMPARE(dictionary->get("P").getReference(), copy->getPageReference());
        if (dictionary->get("Popup").isReference())
        {
            const PDFObjectReference popup = dictionary->get("Popup").getReference();
            QVERIFY(std::find(copy->getAnnotations().cbegin(), copy->getAnnotations().cend(), popup) != copy->getAnnotations().cend());
            const PDFDictionary* popupDictionary = reread.getDictionaryFromObject(reread.getObjectByReference(popup));
            QCOMPARE(popupDictionary->get("Parent").getReference(), copiedAnnotation);
        }
    }
}

void PageOrganizerTest::copiedFormFields()
{
    PDFDocumentBuilder builder;
    std::vector<PDFObjectReference> pages;
    createDocument(2, QByteArray(), &pages, &builder);

    // A field, which is its own widget (merged)
    builder.createSignatureField("Merged", PDFObjectReference(), pages[0], PDFObjectReference(), QRectF(10, 10, 100, 30));

    // A field with a separate widget (Kids)
    const PDFObjectReference field = builder.addObject(PDFObject());
    const PDFObjectReference widget = builder.addObject(PDFObject());
    {
        PDFDictionaryBuilder fieldDictionary;
        fieldDictionary.setEntry(PDFInplaceOrMemoryString("FT"), PDFObject::createName("Tx"));
        fieldDictionary.setEntry(PDFInplaceOrMemoryString("T"), PDFObject::createString("Name"));
        fieldDictionary.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createString("Jane"));
        PDFArrayBuilder kids;
        kids.appendItem(PDFObject::createReference(widget));
        fieldDictionary.setEntry(PDFInplaceOrMemoryString("Kids"), PDFObject::createArray(std::move(kids)));
        builder.setObject(field, PDFObject::createDictionary(std::move(fieldDictionary)));

        PDFDictionaryBuilder widgetDictionary;
        widgetDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Annot"));
        widgetDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Widget"));
        widgetDictionary.setEntry(PDFInplaceOrMemoryString("Parent"), PDFObject::createReference(field));
        widgetDictionary.setEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(pages[0]));
        PDFArrayBuilder rect;
        for (int value : { 10, 100, 200, 120 })
        {
            rect.appendItem(PDFObject::createInteger(value));
        }
        widgetDictionary.setEntry(PDFInplaceOrMemoryString("Rect"), PDFObject::createArray(std::move(rect)));
        builder.setObject(widget, PDFObject::createDictionary(std::move(widgetDictionary)));

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Annots");
        factory << PDFObjectReferenceVector{ widget };
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.appendTo(pages[0], factory.takeObject());

        // The form lists both fields
        const PDFObject acroForm = builder.getObject(builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()))->get("AcroForm"));
        PDFDictionaryBuilder newAcroForm(*acroForm.getDictionary());
        PDFArrayBuilder fields(*builder.getObject(acroForm.getDictionary()->get("Fields")).getArray());
        fields.appendItem(PDFObject::createReference(field));
        newAcroForm.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(std::move(fields)));
        builder.setCatalogAcroForm(builder.addObject(PDFObject::createDictionary(std::move(newAcroForm))));
    }
    const PDFDocument document = builder.build();
    QCOMPARE(fieldNames(document), (std::vector<QByteArray>{ "Merged", "Name" }));

    PDFDocument result;
    QVERIFY(PDFFirePageOrganizer::duplicatePages(&document, { 0 }, &result));
    const PDFDocument reread = read(write(result));

    // The merged field got a copy with its own name, the field with kids got another widget
    QCOMPARE(fieldNames(reread), (std::vector<QByteArray>{ "Merged", "Merged_2", "Name" }));
    const PDFDictionary* fieldDictionary = reread.getDictionaryFromObject(reread.getObjectByReference(field));
    QVERIFY(fieldDictionary);
    const PDFObject kids = reread.getObject(fieldDictionary->get("Kids"));
    QVERIFY(kids.isArray());
    QCOMPARE(kids.getArray()->getCount(), size_t(2));
    const PDFObjectReference copiedWidget = kids.getArray()->getItem(1).getReference();
    QVERIFY(copiedWidget != widget);
    const std::vector<PDFObjectReference>& copiedPageAnnotations = reread.getCatalog()->getPage(1)->getAnnotations();
    QVERIFY(std::find(copiedPageAnnotations.cbegin(), copiedPageAnnotations.cend(), copiedWidget) != copiedPageAnnotations.cend());
    QCOMPARE(reread.getCatalog()->getPage(1)->getAnnotations().size(), size_t(2));
}

void PageOrganizerTest::reversePages()
{
    const PDFDocument document = createDocument(5);
    PDFDocument result;

    // All pages
    QVERIFY(PDFFirePageOrganizer::reversePages(&document, { }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 500, 400, 300, 200, 100 }));

    // Selected pages exchange their places, the others stay
    QVERIFY(PDFFirePageOrganizer::reversePages(&document, { 0, 2, 3 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 400, 200, 300, 100, 500 }));

    // Twice = the original order
    PDFDocument twice;
    QVERIFY(PDFFirePageOrganizer::reversePages(&result, { 0, 2, 3 }, &twice));
    QCOMPARE(pageWidths(twice), (std::vector<int>{ 100, 200, 300, 400, 500 }));
}

void PageOrganizerTest::replacePages()
{
    const PDFDocument document = createDocument(4);
    const PDFDocument source = createDocument(3, "S");
    PDFDocument result;

    // Pages 2 and 4 are replaced by pages 3 and 1 of the other document
    QVERIFY(PDFFirePageOrganizer::replacePages(&document, { 1, 3 }, &source, { 2, 0 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 300, 300, 100 }));
    QVERIFY(pageContent(result, 1).contains(marker(3, "S")));
    QVERIFY(pageContent(result, 3).contains(marker(1, "S")));
    QVERIFY(pageContent(result, 2).contains(marker(3)));

    // The replaced pages and the unused pages of the other document are not in the saved file
    const QByteArray saved = write(result);
    QVERIFY(!saved.contains(marker(2)));
    QVERIFY(!saved.contains(marker(4)));
    QVERIFY(!saved.contains(marker(2, "S")));
    QCOMPARE(pageWidths(read(saved)), (std::vector<int>{ 100, 300, 300, 100 }));

    // The counts must match
    QVERIFY(!PDFFirePageOrganizer::replacePages(&document, { 1, 3 }, &source, { 2 }, &result));
}

void PageOrganizerTest::invalidInputIsRefused()
{
    const PDFDocument document = createDocument(3);
    PDFDocument result;
    QVERIFY(!PDFFirePageOrganizer::movePagesTo(&document, { 3 }, 0, &result));
    QVERIFY(!PDFFirePageOrganizer::movePagesTo(&document, { 0 }, 4, &result));
    QVERIFY(!PDFFirePageOrganizer::movePagesTo(&document, { }, 0, &result));
    QVERIFY(!PDFFirePageOrganizer::copyPages(&document, { -1 }, 0, &result));
    QVERIFY(!PDFFirePageOrganizer::copyPages(&document, { 0 }, -1, &result));
    QVERIFY(!PDFFirePageOrganizer::duplicatePages(&document, { }, &result));
    QVERIFY(!PDFFirePageOrganizer::reversePages(&document, { 5 }, &result));
    QVERIFY(!PDFFirePageOrganizer::replacePages(&document, { 0 }, &document, { 3 }, &result));
    QVERIFY(!PDFFirePageOrganizer::replacePages(&document, { 0, 1 }, &document, { 1, 1 }, &result));
}

void PageOrganizerTest::sourceDocumentIsUnchanged()
{
    // Undo in the editor goes back to the document before the operation, so an
    // operation must never change it
    const PDFDocument document = createDocument(4);
    const QByteArray before = write(document);
    const PDFDocument source = createDocument(2, "S");
    PDFDocument result;

    QVERIFY(PDFFirePageOrganizer::movePagesTo(&document, { 0 }, 3, &result));
    QVERIFY(PDFFirePageOrganizer::copyPages(&document, { 1, 2 }, 0, &result));
    QVERIFY(PDFFirePageOrganizer::duplicatePages(&document, { 3 }, &result));
    QVERIFY(PDFFirePageOrganizer::reversePages(&document, { }, &result));
    QVERIFY(PDFFirePageOrganizer::replacePages(&document, { 2 }, &source, { 1 }, &result));

    QCOMPARE(pageWidths(document), (std::vector<int>{ 100, 200, 300, 400 }));
    QCOMPARE(write(document), before);
}

void PageOrganizerTest::splitEveryNPages()
{
    auto sizes = [](const std::vector<PDFFirePageOrganizer::SplitPart>& parts)
    {
        std::vector<size_t> result;
        for (const auto& part : parts)
        {
            result.push_back(part.pages.size());
        }
        return result;
    };

    QCOMPARE(sizes(PDFFirePageOrganizer::getSplitEveryNPages(10, 3)), (std::vector<size_t>{ 3, 3, 3, 1 }));
    QCOMPARE(sizes(PDFFirePageOrganizer::getSplitEveryNPages(10, 5)), (std::vector<size_t>{ 5, 5 }));
    QCOMPARE(sizes(PDFFirePageOrganizer::getSplitEveryNPages(3, 10)), (std::vector<size_t>{ 3 }));
    QCOMPARE(sizes(PDFFirePageOrganizer::getSplitEveryNPages(4, 1)), (std::vector<size_t>{ 1, 1, 1, 1 }));
    QVERIFY(PDFFirePageOrganizer::getSplitEveryNPages(10, 0).empty());

    const auto parts = PDFFirePageOrganizer::getSplitEveryNPages(7, 3);
    QCOMPARE(parts[2].pages, (std::vector<PDFInteger>{ 6 }));
    QCOMPARE(parts[1].pages, (std::vector<PDFInteger>{ 3, 4, 5 }));
}

void PageOrganizerTest::splitByBookmarks()
{
    PDFDocumentBuilder builder;
    std::vector<PDFObjectReference> pages;
    createDocument(6, QByteArray(), &pages, &builder);

    auto addBookmark = [&pages](PDFOutlineItem* parent, const QString& title, size_t page)
    {
        QSharedPointer<PDFOutlineItem> item(new PDFOutlineItem());
        item->setTitle(title);
        item->setAction(PDFActionPtr(new PDFActionGoTo(PDFDestination::createFit(pages[page]), PDFDestination())));
        parent->addChild(item);
        return item;
    };

    PDFOutlineItem root;
    addBookmark(&root, "Chapter: One", 1);
    QSharedPointer<PDFOutlineItem> two = addBookmark(&root, "Two", 3);
    addBookmark(two.data(), "Nested (not a part)", 4);
    addBookmark(&root, "Three", 5);
    builder.setOutline(&root);
    const PDFDocument document = builder.build();

    const auto parts = PDFFirePageOrganizer::getSplitByBookmarks(&document);
    QCOMPARE(parts.size(), size_t(4));
    QCOMPARE(parts[0].title, QString());                        // the page before the first bookmark
    QCOMPARE(parts[0].pages, (std::vector<PDFInteger>{ 0 }));
    QCOMPARE(parts[1].title, QString("Chapter: One"));
    QCOMPARE(parts[1].pages, (std::vector<PDFInteger>{ 1, 2 }));
    QCOMPARE(parts[2].pages, (std::vector<PDFInteger>{ 3, 4 }));
    QCOMPARE(parts[3].title, QString("Three"));
    QCOMPARE(parts[3].pages, (std::vector<PDFInteger>{ 5 }));

    // No bookmarks - no parts
    const PDFDocument plain = createDocument(3);
    QVERIFY(PDFFirePageOrganizer::getSplitByBookmarks(&plain).empty());
}

void PageOrganizerTest::splitPartFileNames()
{
    QCOMPARE(PDFFirePageOrganizer::getSplitPartFileName("Report", 3, 12, QString()), QString("Report - 03.pdf"));
    QCOMPARE(PDFFirePageOrganizer::getSplitPartFileName("Report", 1, 4, "Chapter: One/Two?"), QString("Report - 1 Chapter One Two.pdf"));
    QCOMPARE(PDFFirePageOrganizer::getSplitPartFileName("Report", 7, 100, "  ...  "), QString("Report - 007.pdf"));
}

void PageOrganizerTest::documentFromImages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // 300 x 150 pixels at 150 dpi = 144 x 72 points; 100 x 200 pixels without a resolution
    QImage wide(300, 150, QImage::Format_RGB32);
    wide.fill(Qt::red);
    wide.setDotsPerMeterX(qRound(150 / 0.0254));
    wide.setDotsPerMeterY(qRound(150 / 0.0254));
    const QString wideFile = directory.filePath("wide.png");
    QVERIFY(wide.save(wideFile));

    QImage tall(100, 200, QImage::Format_ARGB32);
    tall.fill(QColor(0, 0, 255, 128));
    tall.setDotsPerMeterX(0);
    tall.setDotsPerMeterY(0);
    const QString tallFile = directory.filePath("tall.png");
    QVERIFY(tall.save(tallFile));

    PDFDocument result;
    QVERIFY(PDFFirePageOrganizer::createDocumentFromImageFiles({ wideFile, tallFile }, PDFFirePageOrganizer::ImagePageSize::Image, &result));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    const QRectF first = result.getCatalog()->getPage(0)->getMediaBox();
    QVERIFY(qAbs(first.width() - 144.0) < 1.0 && qAbs(first.height() - 72.0) < 1.0);
    // Without a resolution in the file the picture has the default one of Qt (96 dpi)
    const QImage tallRead(tallFile);
    const qreal tallDpi = tallRead.dotsPerMeterX() * 0.0254;
    const qreal expectedDpi = (tallDpi >= 36.0 && tallDpi <= 2400.0) ? tallDpi : 72.0;
    const QRectF second = result.getCatalog()->getPage(1)->getMediaBox();
    QVERIFY(qAbs(second.width() - 100.0 * 72.0 / expectedDpi) < 1.0 && qAbs(second.height() - 200.0 * 72.0 / expectedDpi) < 1.0);

    // Letter: the page turns with the picture
    QVERIFY(PDFFirePageOrganizer::createDocumentFromImageFiles({ wideFile, tallFile }, PDFFirePageOrganizer::ImagePageSize::Letter, &result));
    QCOMPARE(result.getCatalog()->getPage(0)->getMediaBox().size(), QSizeF(792, 612));
    QCOMPARE(result.getCatalog()->getPage(1)->getMediaBox().size(), QSizeF(612, 792));

    // A4, and the document can be saved and read
    QVERIFY(PDFFirePageOrganizer::createDocumentFromImageFiles({ tallFile }, PDFFirePageOrganizer::ImagePageSize::A4, &result));
    const PDFDocument reread = read(write(result));
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(qAbs(reread.getCatalog()->getPage(0)->getMediaBox().height() - 841.89) < 0.1);
    QVERIFY(pageContent(reread, 0).contains("/Im1 Do"));
}

void PageOrganizerTest::documentFromJpegKeepsTheFile()
{
    QTemporaryDir directory;
    QImage photo(64, 48, QImage::Format_RGB32);
    photo.fill(QColor(20, 120, 60));
    const QString photoFile = directory.filePath("photo.jpg");
    QVERIFY(photo.save(photoFile, "JPEG", 90));

    QFile file(photoFile);
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray jpegData = file.readAll();

    std::vector<PDFFirePageOrganizer::ImageSource> images;
    QVERIFY(PDFFirePageOrganizer::readImageFile(photoFile, &images));
    QCOMPARE(images.size(), size_t(1));
    QCOMPARE(images[0].jpegData, jpegData);

    PDFDocument result;
    QVERIFY(PDFFirePageOrganizer::createDocumentFromImages(images, PDFFirePageOrganizer::ImagePageSize::Image, &result));
    const QByteArray saved = write(result);
    QVERIFY(saved.contains(jpegData));
    QVERIFY(saved.contains("/DCTDecode"));
}

void PageOrganizerTest::notAPictureIsRefused()
{
    QTemporaryDir directory;
    const QString fileName = directory.filePath("notes.png");
    QFile file(fileName);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write("this is not a picture");
    file.close();

    PDFDocument result;
    QVERIFY(!PDFFirePageOrganizer::createDocumentFromImageFiles({ fileName }, PDFFirePageOrganizer::ImagePageSize::Image, &result));
    QVERIFY(PDFFirePageOrganizer::isSupportedImageFile("a/b/SCAN.TIFF"));
    QVERIFY(PDFFirePageOrganizer::isSupportedImageFile("photo.jpeg"));
    QVERIFY(!PDFFirePageOrganizer::isSupportedImageFile("document.pdf"));
}

QTEST_MAIN(PageOrganizerTest)

void PageOrganizerTest::tiffKinds_data()
{
    // The samples (UnitTests/tiffsamples, 50 dpi Letter pages) are written as scanners and fax
    // programs write them. The page sizes are in points; a colour sample has a red square
    // in its lower right quarter.
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QList<QSizeF>>("pageSizes");
    QTest::addColumn<bool>("hasRed");

    const QSizeF letter(612, 792);
    const QSizeF landscape(792, 612);
    QTest::newRow("fax G4 (1-bit)") << "bw-fax-g4.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("fax G3 (1-bit)") << "bw-fax-g3.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("fax 204 x 98 dpi") << "bw-fax-204x98.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("PackBits (1-bit)") << "bw-packbits.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("uncompressed (1-bit)") << "bw-uncompressed.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("gray LZW") << "gray-lzw.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("gray Deflate") << "gray-deflate.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("gray 16-bit") << "gray16-deflate.tif" << QList<QSizeF>{ letter } << false;
    QTest::newRow("colour JPEG") << "color-jpeg.tif" << QList<QSizeF>{ letter } << true;
    QTest::newRow("colour LZW") << "color-lzw.tif" << QList<QSizeF>{ letter } << true;
    QTest::newRow("colour + alpha") << "color-alpha.tif" << QList<QSizeF>{ letter } << true;
    QTest::newRow("CMYK") << "color-cmyk.tif" << QList<QSizeF>{ letter } << true;
    QTest::newRow("palette") << "color-palette.tif" << QList<QSizeF>{ letter } << true;
    QTest::newRow("multi-page fax") << "multipage-fax-g4.tif" << QList<QSizeF>{ letter, letter, letter } << false;
    QTest::newRow("multi-page mixed") << "multipage-mixed.tif" << QList<QSizeF>{ letter, landscape, letter } << false;
    QTest::newRow("orientation tag 6") << "orientation-6.tif" << QList<QSizeF>{ letter } << false;
    // No resolution: Qt gives the picture its default (96 dpi), or 72 dpi is used - the size
    // is between them (an empty size marks it), the shape is the shape of the picture
    QTest::newRow("no resolution") << "no-dpi.tif" << QList<QSizeF>{ QSizeF() } << false;
}

void PageOrganizerTest::tiffKinds()
{
    QFETCH(QString, fileName);
    QFETCH(QList<QSizeF>, pageSizes);
    QFETCH(bool, hasRed);

    // Without the TIFF plugin of Qt (package qt6-image-formats-plugins / qt6-qtimageformats,
    // module qtimageformats on Windows) no TIFF can be opened at all
    QVERIFY2(QImageReader::supportedImageFormats().contains("tiff"), "Qt has no TIFF image plugin - install the Qt image formats plugins");

    const QString filePath = QStringLiteral(PDF4QT_TIFF_TEST_DIRECTORY) + QLatin1Char('/') + fileName;
    QVERIFY(PDFFirePageOrganizer::isSupportedImageFile(filePath));

    std::vector<PDFFirePageOrganizer::ImageSource> images;
    const PDFOperationResult readResult = PDFFirePageOrganizer::readImageFile(filePath, &images);
    QVERIFY2(readResult, qPrintable(readResult.getErrorMessage()));
    QCOMPARE(images.size(), size_t(pageSizes.size()));

    for (const PDFFirePageOrganizer::ImageSource& source : images)
    {
        // The page is drawn: the border and the text are black, the paper is white
        const QImage gray = source.image.convertToFormat(QImage::Format_Grayscale8);
        int darkest = 255;
        int lightest = 0;
        for (int y = 0; y < gray.height(); y += 3)
        {
            const uchar* line = gray.constScanLine(y);
            for (int x = 0; x < gray.width(); x += 3)
            {
                darkest = qMin(darkest, int(line[x]));
                lightest = qMax(lightest, int(line[x]));
            }
        }
        QVERIFY2(darkest < 80 && lightest > 200, qPrintable(QString("darkest %1, lightest %2").arg(darkest).arg(lightest)));
    }

    if (hasRed)
    {
        const QImage& image = images.front().image;
        const QColor color = image.pixelColor(image.width() * 3 / 4, image.height() * 3 / 4);
        QVERIFY2(color.red() > 150 && color.green() < 110 && color.blue() < 110, qPrintable(color.name()));
    }

    // The PDF document (as opened from the file), saved and read again
    PDFDocument document;
    const PDFOperationResult result = PDFFirePageOrganizer::createDocumentFromImageFiles({ filePath }, PDFFirePageOrganizer::ImagePageSize::Image, &document);
    QVERIFY2(result, qPrintable(result.getErrorMessage()));

    const PDFDocument savedDocument = read(write(document));
    QCOMPARE(savedDocument.getCatalog()->getPageCount(), size_t(pageSizes.size()));
    for (qsizetype i = 0; i < pageSizes.size(); ++i)
    {
        const QSizeF size = savedDocument.getCatalog()->getPage(size_t(i))->getMediaBox().size();
        if (pageSizes[i].isEmpty())
        {
            // 425 x 550 pixels at 72 ... 96 dpi
            QVERIFY2(size.width() > 318.0 && size.width() < 426.0 && qAbs(size.height() / size.width() - 550.0 / 425.0) < 0.01,
                     qPrintable(QString("page %1: %2 x %3").arg(i + 1).arg(size.width()).arg(size.height())));
            continue;
        }
        QVERIFY2(qAbs(size.width() - pageSizes[i].width()) < 2.0 && qAbs(size.height() - pageSizes[i].height()) < 2.0,
                 qPrintable(QString("page %1: %2 x %3").arg(i + 1).arg(size.width()).arg(size.height())));
    }
}

#include "tst_pageorganizertest.moc"
