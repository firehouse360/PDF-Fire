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

#include "pdffireflatten.h"
#include "pdffireformfields.h"
#include "pdffirepageoperations.h"

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfstreamfilters.h"

#include <QtTest>
#include <QBuffer>

using namespace pdf;

class FlattenTest : public QObject
{
    Q_OBJECT

private slots:
    void flattenFieldsAndComments();
    void flattenFieldsOnly();
    void flattenCommentsOnly();
    void flattenSelectedPages();
    void fieldWithoutAppearanceIsKept();
    void signedSignatureAndRedactionMarkAreKept();
    void countMatchesFlatten();

private:
    struct TestDocument
    {
        PDFDocument document;
        PDFObjectReference textWidget;
        PDFObjectReference square;
        PDFObjectReference note;
    };

    static TestDocument createDocument(int pageCount = 1);
    static void appendAnnotation(PDFDocumentBuilder& builder, PDFObjectReference page, PDFObjectReference annotation);
    static void appendField(PDFDocumentBuilder& builder, PDFObjectReference field);
    static std::vector<PDFObjectReference> annotations(const PDFDocument& document, size_t pageIndex);
    static size_t fieldCount(const PDFDocument& document);
    static QByteArray pageContent(const PDFDocument& document, size_t pageIndex);
    static QString pageText(const PDFDocument& document, size_t pageIndex);
    static PDFDocument roundTrip(const PDFDocument& document);
};

FlattenTest::TestDocument FlattenTest::createDocument(int pageCount)
{
    PDFDocument blank = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    for (int i = 1; i < pageCount; ++i)
    {
        PDFDocument more;
        PDFPageOperations::insertBlankPage(&blank, i, QRectF(0, 0, 612, 792), &more);
        blank = std::move(more);
    }

    PDFDocumentBuilder builder(&blank);
    TestDocument result;

    for (int i = 0; i < pageCount; ++i)
    {
        const PDFObjectReference page = builder.getPages()[i];

        PDFFireFormFields::Settings settings = PDFFireFormFields::getDefaultSettings(PDFFireFormFields::Type::Text);
        settings.name = QString("Name%1").arg(i + 1);
        settings.value = QString("FLATTENVALUE%1").arg(i + 1);
        settings.fontSize = 12.0;
        const PDFObjectReference widget = PDFFireFormFields::createField(&builder, page, QRectF(72, 700, 300, 24), settings);

        const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(72, 500, 100, 50), 2.0, Qt::yellow, Qt::red, "Tester", "Square", "A square comment");
        builder.updateAnnotationAppearanceStreams(square);

        const PDFObjectReference note = builder.createAnnotationText(page, QRectF(400, 500, 24, 24), TextAnnotationIcon::Note, "Tester", "Note", "A sticky note", false);
        builder.updateAnnotationAppearanceStreams(note);

        if (i == 0)
        {
            result.textWidget = widget;
            result.square = square;
            result.note = note;
        }
    }

    result.document = builder.build();
    return result;
}

void FlattenTest::appendAnnotation(PDFDocumentBuilder& builder, PDFObjectReference page, PDFObjectReference annotation)
{
    const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(page));
    PDFArrayBuilder annots;
    const PDFObject& existing = builder.getObject(pageDictionary->get("Annots"));
    if (existing.isArray())
    {
        annots = PDFArrayBuilder(*existing.getArray());
    }
    annots.appendItem(PDFObject::createReference(annotation));

    PDFDictionaryBuilder newPage(*pageDictionary);
    newPage.setEntry(PDFInplaceOrMemoryString("Annots"), PDFObject::createArray(std::move(annots)));
    builder.setObject(page, PDFObject::createDictionary(std::move(newPage)));
}

void FlattenTest::appendField(PDFDocumentBuilder& builder, PDFObjectReference field)
{
    const PDFDictionary* catalog = builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()));
    const PDFObject formObject = catalog->get("AcroForm");
    QVERIFY(formObject.isReference());
    const PDFDictionary* form = builder.getDictionaryFromObject(formObject);

    PDFArrayBuilder fields;
    const PDFObject& existing = builder.getObject(form->get("Fields"));
    if (existing.isArray())
    {
        fields = PDFArrayBuilder(*existing.getArray());
    }
    fields.appendItem(PDFObject::createReference(field));

    PDFDictionaryBuilder newForm(*form);
    newForm.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(std::move(fields)));
    builder.setObject(formObject.getReference(), PDFObject::createDictionary(std::move(newForm)));
}

std::vector<PDFObjectReference> FlattenTest::annotations(const PDFDocument& document, size_t pageIndex)
{
    std::vector<PDFObjectReference> result;
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const PDFDictionary* pageDictionary = document.getDictionaryFromObject(document.getObjectByReference(page->getPageReference()));
    const PDFObject& annots = document.getObject(pageDictionary->get("Annots"));
    if (annots.isArray())
    {
        for (size_t i = 0; i < annots.getArray()->getCount(); ++i)
        {
            const PDFObject& item = annots.getArray()->getItem(i);
            if (item.isReference())
            {
                result.push_back(item.getReference());
            }
        }
    }
    return result;
}

size_t FlattenTest::fieldCount(const PDFDocument& document)
{
    const PDFDictionary* form = document.getDictionaryFromObject(document.getCatalog()->getFormObject());
    if (!form)
    {
        return 0;
    }
    const PDFObject& fields = document.getObject(form->get("Fields"));
    return fields.isArray() ? fields.getArray()->getCount() : 0;
}

QByteArray FlattenTest::pageContent(const PDFDocument& document, size_t pageIndex)
{
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    QByteArray result;
    const PDFObject& contents = document.getObject(page->getContents());
    std::vector<PDFObject> streams;
    if (contents.isArray())
    {
        for (size_t i = 0; i < contents.getArray()->getCount(); ++i)
        {
            streams.push_back(contents.getArray()->getItem(i));
        }
    }
    else
    {
        streams.push_back(page->getContents());
    }
    for (const PDFObject& item : streams)
    {
        const PDFObject& stream = document.getObject(item);
        if (stream.isStream())
        {
            result += document.getDecodedStream(stream.getStream());
            result += '\n';
        }
    }
    return result;
}

QString FlattenTest::pageText(const PDFDocument& document, size_t pageIndex)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    PDFTextLayoutGenerator generator(PDFRenderer::IgnoreOptionalContent, page, &document, &fontCache, &cms, &activity, QTransform(), PDFMeshQualitySettings());
    generator.processContents();
    const PDFTextLayout layout = generator.createTextLayout();

    QStringList texts;
    for (const PDFTextFlow& flow : PDFTextFlow::createTextFlows(layout, PDFTextFlow::SeparateBlocks, PDFInteger(pageIndex)))
    {
        texts << flow.getText().simplified();
    }
    return texts.join(' ');
}

PDFDocument FlattenTest::roundTrip(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(buffer.data());
}

void FlattenTest::flattenFieldsAndComments()
{
    const TestDocument test = createDocument();
    QCOMPARE(annotations(test.document, 0).size(), size_t(4));   // widget, square, note, its pop-up
    QCOMPARE(fieldCount(test.document), size_t(1));
    QVERIFY(!pageText(test.document, 0).contains("FLATTENVALUE1"));

    PDFDocument result;
    PDFFireFlatten::Statistics statistics;
    QVERIFY(PDFFireFlatten::flatten(&test.document, {}, PDFFireFlatten::FormFields | PDFFireFlatten::Comments, nullptr, &result, &statistics));
    QCOMPARE(statistics.flattenedFields, 1);
    QCOMPARE(statistics.flattenedComments, 2);

    // The annotations are gone (the pop-up of the note too), and the field is not in the form
    QVERIFY(annotations(result, 0).empty());
    QCOMPARE(fieldCount(result), size_t(0));

    // The appearances are drawn by the page, and the value of the field is text of the page
    const QByteArray content = pageContent(result, 0);
    QVERIFY(content.contains("/PFFl0 Do"));
    QVERIFY(content.contains("/PFFl1 Do"));
    QVERIFY(content.contains("/PFFl2 Do"));
    QVERIFY(content.startsWith("q\n"));
    QVERIFY(pageText(result, 0).contains("FLATTENVALUE1"));

    // The XObjects are the appearance streams of the annotations
    const PDFDictionary* resources = result.getDictionaryFromObject(result.getCatalog()->getPage(0)->getResources());
    QVERIFY(resources);
    const PDFDictionary* xobjects = result.getDictionaryFromObject(resources->get("XObject"));
    QVERIFY(xobjects);
    QVERIFY(xobjects->hasKey("PFFl0"));
    QVERIFY(result.getObject(xobjects->get("PFFl0")).isStream());

    // The result can be written and read back
    const PDFDocument reread = roundTrip(result);
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(annotations(reread, 0).empty());
    QVERIFY(pageText(reread, 0).contains("FLATTENVALUE1"));

    // The source document is not changed
    QCOMPARE(annotations(test.document, 0).size(), size_t(4));
}

void FlattenTest::flattenFieldsOnly()
{
    const TestDocument test = createDocument();

    PDFDocument result;
    QVERIFY(PDFFireFlatten::flatten(&test.document, {}, PDFFireFlatten::FormFields, nullptr, &result));

    const std::vector<PDFObjectReference> kept = annotations(result, 0);
    QCOMPARE(kept.size(), size_t(3));
    QVERIFY(std::find(kept.cbegin(), kept.cend(), test.square) != kept.cend());
    QVERIFY(std::find(kept.cbegin(), kept.cend(), test.textWidget) == kept.cend());
    QCOMPARE(fieldCount(result), size_t(0));
    QVERIFY(pageText(result, 0).contains("FLATTENVALUE1"));
}

void FlattenTest::flattenCommentsOnly()
{
    const TestDocument test = createDocument();

    PDFDocument result;
    QVERIFY(PDFFireFlatten::flatten(&test.document, {}, PDFFireFlatten::Comments, nullptr, &result));

    const std::vector<PDFObjectReference> kept = annotations(result, 0);
    QCOMPARE(kept.size(), size_t(1));
    QCOMPARE(kept.front(), test.textWidget);
    QCOMPARE(fieldCount(result), size_t(1));
    QVERIFY(!pageText(result, 0).contains("FLATTENVALUE1"));
}

void FlattenTest::flattenSelectedPages()
{
    const TestDocument test = createDocument(2);
    QCOMPARE(fieldCount(test.document), size_t(2));

    PDFDocument result;
    QVERIFY(PDFFireFlatten::flatten(&test.document, { 1 }, PDFFireFlatten::FormFields | PDFFireFlatten::Comments, nullptr, &result));
    QCOMPARE(annotations(result, 0).size(), size_t(4));
    QVERIFY(annotations(result, 1).empty());
    QCOMPARE(fieldCount(result), size_t(1));
    QVERIFY(pageText(result, 1).contains("FLATTENVALUE2"));

    // Invalid pages are refused
    QVERIFY(!PDFFireFlatten::flatten(&test.document, { 2 }, PDFFireFlatten::FormFields, nullptr, &result));
}

void FlattenTest::fieldWithoutAppearanceIsKept()
{
    const TestDocument test = createDocument();
    PDFDocumentBuilder builder(&test.document);
    const PDFObjectReference page = builder.getPages().front();

    // A field written by another program, which leaves the appearance to the viewer
    PDFDictionaryBuilder field;
    field.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Annot"));
    field.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Widget"));
    field.setEntry(PDFInplaceOrMemoryString("FT"), PDFObject::createName("Tx"));
    field.setEntry(PDFInplaceOrMemoryString("T"), PDFObject::createString("NoAppearance"));
    field.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createString("KEPT"));
    PDFArrayBuilder rect;
    for (PDFReal value : { 72.0, 600.0, 300.0, 620.0 })
    {
        rect.appendItem(PDFObject::createReal(value));
    }
    field.setEntry(PDFInplaceOrMemoryString("Rect"), PDFObject::createArray(std::move(rect)));
    field.setEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(page));
    const PDFObjectReference fieldReference = builder.addObject(PDFObject::createDictionary(std::move(field)));
    appendAnnotation(builder, page, fieldReference);
    appendField(builder, fieldReference);
    const PDFDocument document = builder.build();
    QCOMPARE(fieldCount(document), size_t(2));

    PDFDocument result;
    PDFFireFlatten::Statistics statistics;
    QVERIFY(PDFFireFlatten::flatten(&document, {}, PDFFireFlatten::FormFields, nullptr, &result, &statistics));
    QCOMPARE(statistics.flattenedFields, 1);
    QCOMPARE(statistics.keptWithoutAppearance, 1);

    // The field without an appearance stays a field (PdfCraft #1794), the other one is flattened
    const std::vector<PDFObjectReference> kept = annotations(result, 0);
    QVERIFY(std::find(kept.cbegin(), kept.cend(), fieldReference) != kept.cend());
    QVERIFY(std::find(kept.cbegin(), kept.cend(), test.textWidget) == kept.cend());
    QCOMPARE(fieldCount(result), size_t(1));
}

void FlattenTest::signedSignatureAndRedactionMarkAreKept()
{
    const TestDocument test = createDocument();
    PDFDocumentBuilder builder(&test.document);
    const PDFObjectReference page = builder.getPages().front();

    // A signed signature field with an appearance
    PDFDictionaryBuilder signatureValue;
    signatureValue.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Sig"));
    const PDFObjectReference valueReference = builder.addObject(PDFObject::createDictionary(std::move(signatureValue)));

    PDFDictionaryBuilder appearanceDictionary;
    appearanceDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    PDFArrayBuilder bbox;
    for (PDFReal value : { 0.0, 0.0, 100.0, 30.0 })
    {
        bbox.appendItem(PDFObject::createReal(value));
    }
    appearanceDictionary.setEntry(PDFInplaceOrMemoryString("BBox"), PDFObject::createArray(std::move(bbox)));
    const PDFObjectReference appearance = builder.addObject(PDFObject::createStream(PDFStream(std::move(appearanceDictionary), QByteArray("0 0 1 rg 0 0 100 30 re f"))));
    PDFDictionaryBuilder ap;
    ap.setEntry(PDFInplaceOrMemoryString("N"), PDFObject::createReference(appearance));

    PDFDictionaryBuilder signature;
    signature.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Annot"));
    signature.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Widget"));
    signature.setEntry(PDFInplaceOrMemoryString("FT"), PDFObject::createName("Sig"));
    signature.setEntry(PDFInplaceOrMemoryString("T"), PDFObject::createString("Signature1"));
    signature.setEntry(PDFInplaceOrMemoryString("V"), PDFObject::createReference(valueReference));
    signature.setEntry(PDFInplaceOrMemoryString("AP"), PDFObject::createDictionary(std::move(ap)));
    PDFArrayBuilder rect;
    for (PDFReal value : { 72.0, 100.0, 172.0, 130.0 })
    {
        rect.appendItem(PDFObject::createReal(value));
    }
    signature.setEntry(PDFInplaceOrMemoryString("Rect"), PDFObject::createArray(std::move(rect)));
    const PDFObjectReference signatureReference = builder.addObject(PDFObject::createDictionary(std::move(signature)));
    appendAnnotation(builder, page, signatureReference);
    appendField(builder, signatureReference);

    // A redaction mark - flattened, it would look redacted with the text still under it
    const PDFObjectReference redaction = builder.createAnnotationRedact(page, QRectF(72, 300, 100, 20), Qt::black, Qt::black);
    builder.updateAnnotationAppearanceStreams(redaction);

    const PDFDocument document = builder.build();
    QVERIFY(PDFFireFlatten::hasSignatures(&document));
    QVERIFY(!PDFFireFlatten::hasSignatures(&test.document));

    PDFDocument result;
    PDFFireFlatten::Statistics statistics;
    QVERIFY(PDFFireFlatten::flatten(&document, {}, PDFFireFlatten::FormFields | PDFFireFlatten::Comments, nullptr, &result, &statistics));
    QCOMPARE(statistics.keptSignatures, 1);
    QCOMPARE(statistics.keptRedactionMarks, 1);

    const std::vector<PDFObjectReference> kept = annotations(result, 0);
    QCOMPARE(kept.size(), size_t(2));
    QVERIFY(std::find(kept.cbegin(), kept.cend(), signatureReference) != kept.cend());
    QVERIFY(std::find(kept.cbegin(), kept.cend(), redaction) != kept.cend());
    QCOMPARE(fieldCount(result), size_t(1));
    QVERIFY(PDFFireFlatten::hasSignatures(&result));
}

void FlattenTest::countMatchesFlatten()
{
    const TestDocument test = createDocument(2);
    const PDFFireFlatten::Statistics counted = PDFFireFlatten::count(&test.document, {}, PDFFireFlatten::FormFields | PDFFireFlatten::Comments);
    QCOMPARE(counted.flattenedFields, 2);
    QCOMPARE(counted.flattenedComments, 4);

    const PDFFireFlatten::Statistics firstPage = PDFFireFlatten::count(&test.document, { 0 }, PDFFireFlatten::Comments);
    QCOMPARE(firstPage.flattenedFields, 0);
    QCOMPARE(firstPage.flattenedComments, 2);
}

QTEST_MAIN(FlattenTest)

#include "tst_flattentest.moc"
