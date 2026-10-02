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

// PDF Fire: tests of the invisible OCR text layer writer (no OCR engine needed -
// known word boxes are written, and the text is read back by the text layout of PDF4QT)

#include "ocrtextlayer.h"

#include "pdffirepageoperations.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumenttextflow.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfrenderer.h"

#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QPainter>

using namespace pdf;
using namespace pdfplugin;

class OcrTextLayerTest : public QObject
{
    Q_OBJECT

private slots:
    void wordsAreExtractedInReadingOrder();
    void layerIsInvisible();
    void rotatedAndOffsetPages_data();
    void rotatedAndOffsetPages();
    void existingContentIsWrapped();
    void unicodeText();
    void emptyTextChangesNothing();
    void fontIsShared();

private:
    static QByteArray fontProgram();
    static OcrPageText sampleText();
    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);
    static PDFDocument roundTrip(const PDFDocument& document) { return read(write(document)); }
    static QStringList extractText(const PDFDocument& document, PDFInteger pageIndex, std::vector<QRectF>* boxes = nullptr);
    static QImage render(const PDFDocument& document, PDFInteger pageIndex, QList<PDFRenderError>* errors);
    static QByteArray pageContent(const PDFDocument& document, PDFInteger pageIndex);
};

QByteArray OcrTextLayerTest::fontProgram()
{
    // Tesseract's glyph-less font, when it is installed (the layer works without it):
    // TESSDATA_PREFIX (the Windows build), or the Ubuntu package
    for (const QString& fileName : { QDir(qEnvironmentVariable("TESSDATA_PREFIX")).filePath("pdf.ttf"),
                                     QStringLiteral("/usr/share/tesseract-ocr/5/tessdata/pdf.ttf") })
    {
        QFile file(fileName);
        if (!qEnvironmentVariableIsEmpty("TESSDATA_PREFIX") || fileName.startsWith('/'))
        {
            if (file.open(QFile::ReadOnly))
            {
                return file.readAll();
            }
        }
    }
    return QByteArray();
}

OcrPageText OcrTextLayerTest::sampleText()
{
    // A 300 dpi image of a letter page: 2550 x 3300 pixels
    OcrPageText text;
    text.imageSize = QSizeF(2550, 3300);

    OcrLine line1;
    line1.box = QRectF(QPointF(250, 300), QPointF(1400, 360));
    line1.baseline = QLineF(250, 348, 1400, 348);
    line1.words.push_back(OcrWord{ "Hello", QRectF(QPointF(250, 300), QPointF(480, 350)), 95.0f });
    line1.words.push_back(OcrWord{ "World!", QRectF(QPointF(510, 300), QPointF(800, 360)), 93.0f });
    line1.words.push_back(OcrWord{ "Searchable", QRectF(QPointF(830, 300), QPointF(1400, 360)), 90.0f });

    OcrLine line2;
    line2.box = QRectF(QPointF(250, 450), QPointF(900, 520));
    line2.baseline = QLineF(250, 505, 900, 505);
    line2.words.push_back(OcrWord{ "Second", QRectF(QPointF(250, 450), QPointF(560, 520)), 91.0f });
    line2.words.push_back(OcrWord{ "line", QRectF(QPointF(600, 450), QPointF(900, 520)), 97.0f });

    text.lines = { line1, line2 };
    return text;
}

QByteArray OcrTextLayerTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument OcrTextLayerTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        qWarning() << reader.getErrorMessage();
    }
    return document;
}

QStringList OcrTextLayerTest::extractText(const PDFDocument& document, PDFInteger pageIndex, std::vector<QRectF>* boxes)
{
    PDFDocumentTextFlowFactory factory;
    const PDFDocumentTextFlow flow = factory.create(&document, { pageIndex }, PDFDocumentTextFlowFactory::Algorithm::Layout);

    QStringList result;
    for (const PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (item.isText())
        {
            for (const QString& line : item.text.split(QChar('\n'), Qt::SkipEmptyParts))
            {
                result << line.trimmed();
            }
            if (boxes)
            {
                boxes->push_back(item.boundingRect);
            }
        }
    }
    return result;
}

QImage OcrTextLayerTest::render(const PDFDocument& document, PDFInteger pageIndex, QList<PDFRenderError>* errors)
{
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::View, nullptr);
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    PDFCMSGeneric cms;
    PDFMeshQualitySettings meshQualitySettings;

    PDFRenderer renderer(&document, &fontCache, &cms, &optionalContentActivity, PDFRenderer::getDefaultFeatures(), meshQualitySettings);
    PDFPrecompiledPage precompiledPage;
    renderer.compile(&precompiledPage, pageIndex);
    *errors = precompiledPage.getErrors();

    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const QSize size = page->getRotatedMediaBox().size().toSize();
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    precompiledPage.draw(&painter, page->getCropBox(), PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), QSizeF(size))), PDFRenderer::getDefaultFeatures(), 1.0);
    painter.end();
    return image;
}

QByteArray OcrTextLayerTest::pageContent(const PDFDocument& document, PDFInteger pageIndex)
{
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    QByteArray result;
    const PDFObject contents = document.getObject(page->getContents());
    std::vector<PDFObject> streams;
    if (contents.isArray())
    {
        for (const PDFObject& item : *contents.getArray())
        {
            streams.push_back(document.getObject(item));
        }
    }
    else
    {
        streams.push_back(contents);
    }

    for (const PDFObject& stream : streams)
    {
        if (stream.isStream())
        {
            result += document.getDecodedStream(stream.getStream());
            result += "\n%%END%%\n";
        }
    }
    return result;
}

void OcrTextLayerTest::wordsAreExtractedInReadingOrder()
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    PDFObjectReference font;
    QVERIFY(OcrTextLayer::addTextLayer(&builder, page, sampleText(), &font, fontProgram()));
    QVERIFY(font.isValid());

    const PDFDocument result = roundTrip(builder.build());
    std::vector<QRectF> boxes;
    const QStringList text = extractText(result, 0, &boxes);
    QCOMPARE(text, QStringList({ "Hello World! Searchable", "Second line" }));

    // The text is where the words are in the image: 250 px = 60 pt from the left,
    // the first line from 300 px = 72 pt from the top (792 - 72 = 720 in the page space)
    QVERIFY(!boxes.empty());
    const QRectF box = boxes.front();
    QVERIFY2(qAbs(box.left() - 60.0) < 2.0, qPrintable(QString::number(box.left())));
    QVERIFY2(qAbs(box.right() - 336.0) < 4.0, qPrintable(QString::number(box.right())));
    QVERIFY2(box.top() < 720.0 && box.top() > 600.0, qPrintable(QString::number(box.top())));

    const QByteArray content = pageContent(result, 0);
    QVERIFY(content.contains("3 Tr"));
    QVERIFY(content.contains("Tz"));
}

void OcrTextLayerTest::layerIsInvisible()
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    PDFObjectReference font;
    QVERIFY(OcrTextLayer::addTextLayer(&builder, page, sampleText(), &font, fontProgram()));
    const PDFDocument result = roundTrip(builder.build());

    QList<PDFRenderError> errors;
    const QImage image = render(result, 0, &errors);
    for (const PDFRenderError& error : errors)
    {
        qWarning() << "render error:" << error.message;
    }
    QVERIFY(errors.isEmpty());

    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixel(x, y) != qRgb(255, 255, 255))
            {
                QFAIL(qPrintable(QString("Visible pixel at %1, %2").arg(x).arg(y)));
            }
        }
    }
}

void OcrTextLayerTest::rotatedAndOffsetPages_data()
{
    QTest::addColumn<int>("rotation");
    QTest::addColumn<QRectF>("mediaBox");

    QTest::newRow("0") << 0 << QRectF(0, 0, 612, 792);
    QTest::newRow("0 offset") << 0 << QRectF(100, 200, 612, 792);
    QTest::newRow("90 offset") << 90 << QRectF(50, 100, 792, 612);
    QTest::newRow("180") << 180 << QRectF(0, 0, 612, 792);
    QTest::newRow("270 offset") << 270 << QRectF(-30, 40, 792, 612);
}

void OcrTextLayerTest::rotatedAndOffsetPages()
{
    QFETCH(int, rotation);
    QFETCH(QRectF, mediaBox);

    const PageRotation pageRotation = rotation == 90 ? PageRotation::Rotate90 : rotation == 180 ? PageRotation::Rotate180 : rotation == 270 ? PageRotation::Rotate270 : PageRotation::None;

    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();
    builder.setPageMediaBox(page, mediaBox);
    builder.setPageRotation(page, pageRotation);

    // The image is the page as it is displayed - always upright, 612 x 792 points
    PDFObjectReference font;
    QVERIFY(OcrTextLayer::addTextLayer(&builder, page, sampleText(), &font, fontProgram()));
    const PDFDocument result = roundTrip(builder.build());

    std::vector<QRectF> boxes;
    const QStringList text = extractText(result, 0, &boxes);
    QCOMPARE(text, QStringList({ "Hello World! Searchable", "Second line" }));

    // The words are where the image shows them: map the first line to the page space
    const QTransform imageToPage = OcrTextLayer::createImageToPageTransform(mediaBox, pageRotation, QSizeF(2550, 3300));
    const QRectF expected = imageToPage.mapRect(QRectF(QPointF(250, 300), QPointF(1400, 520)));
    QVERIFY(!boxes.empty());
    const QRectF box = boxes.front();
    QVERIFY2(expected.adjusted(-6, -6, 6, 6).contains(box.center()), qPrintable(QString("expected %1 %2 %3 %4, got %5 %6 %7 %8")
             .arg(expected.left()).arg(expected.top()).arg(expected.right()).arg(expected.bottom())
             .arg(box.left()).arg(box.top()).arg(box.right()).arg(box.bottom())));
    QVERIFY(mediaBox.contains(box.center()));
}

void OcrTextLayerTest::existingContentIsWrapped()
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    // Content with an unbalanced transformation (no q/Q), and a font named as ours
    const QByteArray oldContent = "0.5 0 0 0.5 300 300 cm 0 0 10 10 re f\n";
    PDFDictionaryBuilder streamDictionary;
    streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(oldContent.size()));
    const PDFObjectReference oldStream = builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), QByteArray(oldContent))));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << oldStream;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory.beginDictionary();
    factory.beginDictionaryItem("Font");
    factory.beginDictionary();
    factory.beginDictionaryItem(OcrTextLayer::FONT_RESOURCE_NAME);
    factory << WrapName("Placeholder");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(page, factory.takeObject());

    PDFObjectReference font;
    QVERIFY(OcrTextLayer::addTextLayer(&builder, page, sampleText(), &font, QByteArray()));
    const PDFDocument result = roundTrip(builder.build());

    const QByteArray content = pageContent(result, 0);
    QVERIFY2(content.startsWith("q\n\n%%END%%\n0.5 0 0 0.5 300 300 cm"), content.left(80).constData());
    QVERIFY(content.contains("Q\n\n%%END%%\nq\nBT\n3 Tr"));
    QVERIFY(content.contains("/PDFFireOCR1 "));

    // The old font entry is kept
    const PDFDictionary* resources = result.getDictionaryFromObject(result.getCatalog()->getPage(0)->getResources());
    QVERIFY(resources);
    const PDFDictionary* fonts = result.getDictionaryFromObject(resources->get("Font"));
    QVERIFY(fonts && fonts->hasKey("PDFFireOCR") && fonts->hasKey("PDFFireOCR1"));

    // The text is not moved by the transformation of the old content
    std::vector<QRectF> boxes;
    QCOMPARE(extractText(result, 0, &boxes), QStringList({ "Hello World! Searchable", "Second line" }));
    QVERIFY2(qAbs(boxes.front().left() - 60.0) < 4.0, qPrintable(QString::number(boxes.front().left())));
}

void OcrTextLayerTest::unicodeText()
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    OcrPageText text;
    text.imageSize = QSizeF(612, 792);
    OcrLine line;
    line.box = QRectF(50, 100, 500, 20);
    line.words.push_back(OcrWord{ QString::fromUtf8("Ünïcödé"), QRectF(50, 100, 100, 20), 90.0f });
    line.words.push_back(OcrWord{ QString::fromUtf8("€100"), QRectF(170, 100, 60, 20), 90.0f });
    line.words.push_back(OcrWord{ QString::fromUtf8("Привет"), QRectF(250, 100, 100, 20), 90.0f });
    line.words.push_back(OcrWord{ QString::fromUtf8("(a)\\b"), QRectF(370, 100, 60, 20), 90.0f });
    text.lines = { line };

    PDFObjectReference font;
    QVERIFY(OcrTextLayer::addTextLayer(&builder, page, text, &font, fontProgram()));
    const PDFDocument result = roundTrip(builder.build());
    QCOMPARE(extractText(result, 0), QStringList({ QString::fromUtf8("Ünïcödé €100 Привет (a)\\b") }));
}

void OcrTextLayerTest::emptyTextChangesNothing()
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    OcrPageText text;
    text.imageSize = QSizeF(612, 792);
    OcrLine line;
    line.words.push_back(OcrWord{ "  ", QRectF(10, 10, 10, 10), 10.0f });
    text.lines = { line };

    PDFObjectReference font;
    QVERIFY(!OcrTextLayer::addTextLayer(&builder, page, text, &font, QByteArray()));
    QVERIFY(!font.isValid());
}

void OcrTextLayerTest::fontIsShared()
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    PDFObjectReference font;
    QVERIFY(OcrTextLayer::addTextLayer(&builder, page, sampleText(), &font, QByteArray()));
    const PDFObjectReference firstFont = font;
    QVERIFY(OcrTextLayer::addTextLayer(&builder, page, sampleText(), &font, QByteArray()));
    QCOMPARE(font, firstFont);
}

QTEST_GUILESS_MAIN(OcrTextLayerTest)

#include "tst_ocrtextlayertest.moc"
