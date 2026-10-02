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

#include "pagemarks.h"

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdffirepageoperations.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfstreamfilters.h"
#include "pdftextlayoutgenerator.h"

#include <QtTest>
#include <QBuffer>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace pdf;
using pdfplugin::PageMarks;

class PageMarksTest : public QObject
{
    Q_OBJECT

private slots:
    void pageRanges();
    void winAnsiEncoding();
    void tokens();
    void watermarkTextIsExtracted();
    void watermarkOnChosenPagesOnly();
    void watermarkBehindAndInFront();
    void watermarkOpacityAndFitToPage();
    void watermarkRespectsCropBox();
    void headerFooterNumbers();
    void headerOnRotatedPage();
    void batesNumbers();
    void removeRestoresOriginalContent();
    void removeOnlyChosenKinds();
    void removeKeepsForeignPaginationArtifacts();
    void removeAcrobatMarkInMergedStream();
    void imageWatermark();
    void savedDocumentIsValid();
    void backgroundUnderContentOnAllPages();
    void backgroundOnChosenPagesWithOpacity();
    void backgroundRotatedAndCropped();

private:
    struct Text
    {
        QString text;
        QRectF box;
    };

    static PDFDocument createDocument(int pageCount, int rotate = 0, QRectF cropBox = QRectF());
    static std::vector<Text> extractText(const PDFDocument& document, size_t pageIndex);
    static QString pageText(const PDFDocument& document, size_t pageIndex);
    static QRectF findText(const PDFDocument& document, size_t pageIndex, const QString& text);
    static QByteArray pageContent(const PDFDocument& document, size_t pageIndex);
    static PDFObject pageContents(const PDFDocument& document, size_t pageIndex);
    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);
    static PDFDocument apply(const PDFDocument& document, const std::function<bool(PDFDocumentBuilder*, QString*)>& change);
    static PageMarks::WatermarkSettings watermark(const QString& text);
    static PDFDocument createSourceDocument(int rotate = 0);
};

PDFDocument PageMarksTest::createDocument(int pageCount, int rotate, QRectF cropBox)
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);

    std::vector<PDFObjectReference> pages = builder.getPages();
    const PDFObjectReference firstPage = pages.front();

    PDFDictionaryBuilder fontDictionary;
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("Times-Roman"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Encoding"), PDFObject::createName("WinAnsiEncoding"));
    const PDFObjectReference font = builder.addObject(PDFObject::createDictionary(std::move(fontDictionary)));

    std::vector<PDFObjectReference> newPages;
    for (int i = 0; i < pageCount; ++i)
    {
        // The original content leaves a transformation and a color behind it (no q/Q),
        // so a mark in front of it must be protected
        QByteArray content = "0.5 0 0 0.5 0 0 cm 1 0 0 rg BT /F1 24 Tf 144 1200 Td (Original page " + QByteArray::number(i + 1) + ") Tj ET";
        PDFDictionaryBuilder streamDictionary;
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        const PDFObjectReference stream = builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(content))));

        PDFDictionaryBuilder fonts;
        fonts.setEntry(PDFInplaceOrMemoryString("F1"), PDFObject::createReference(font));
        PDFDictionaryBuilder resources;
        resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));

        PDFDictionaryBuilder page(*builder.getStorage()->getDictionaryFromObject(builder.getObjectByReference(firstPage)));
        page.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(stream));
        page.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
        if (rotate)
        {
            page.setEntry(PDFInplaceOrMemoryString("Rotate"), PDFObject::createInteger(rotate));
        }
        if (cropBox.isValid())
        {
            PDFArrayBuilder box;
            for (double value : { cropBox.left(), cropBox.top(), cropBox.right(), cropBox.bottom() })
            {
                box.appendItem(PDFObject::createReal(value));
            }
            page.setEntry(PDFInplaceOrMemoryString("CropBox"), PDFObject::createArray(std::move(box)));
        }

        if (i == 0)
        {
            builder.setObject(firstPage, PDFObject::createDictionary(std::move(page)));
            newPages.push_back(firstPage);
        }
        else
        {
            newPages.push_back(builder.addObject(PDFObject::createDictionary(std::move(page))));
        }
    }
    builder.setPages(newPages);
    return builder.build();
}

std::vector<PageMarksTest::Text> PageMarksTest::extractText(const PDFDocument& document, size_t pageIndex)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    PDFTextLayoutGenerator generator(PDFRenderer::IgnoreOptionalContent, page, &document, &fontCache, &cms, &activity, QTransform(), PDFMeshQualitySettings());
    generator.processContents();
    const PDFTextLayout layout = generator.createTextLayout();

    std::vector<Text> texts;
    for (const PDFTextFlow& flow : PDFTextFlow::createTextFlows(layout, PDFTextFlow::SeparateBlocks, PDFInteger(pageIndex)))
    {
        texts.push_back({ flow.getText().simplified(), flow.getBoundingBox() });
    }
    return texts;
}

QString PageMarksTest::pageText(const PDFDocument& document, size_t pageIndex)
{
    QStringList lines;
    for (const Text& text : extractText(document, pageIndex))
    {
        lines << text.text;
    }
    return lines.join(QChar('\n'));
}

QRectF PageMarksTest::findText(const PDFDocument& document, size_t pageIndex, const QString& searched)
{
    for (const Text& text : extractText(document, pageIndex))
    {
        if (text.text.contains(searched))
        {
            return text.box;
        }
    }
    return QRectF();
}

QByteArray PageMarksTest::pageContent(const PDFDocument& document, size_t pageIndex)
{
    // The decoded content streams of the page, concatenated
    const PDFObjectStorage& storage = document.getStorage();
    const PDFObject& contents = storage.getObject(document.getCatalog()->getPage(pageIndex)->getContents());
    QByteArray result;
    auto append = [&](const PDFObject& item)
    {
        const PDFObject& stream = storage.getObject(item);
        if (stream.isStream())
        {
            result += storage.getDecodedStream(stream.getStream());
        }
    };
    if (contents.isArray())
    {
        for (size_t i = 0; i < contents.getArray()->getCount(); ++i)
        {
            append(contents.getArray()->getItem(i));
        }
    }
    else
    {
        append(contents);
    }
    return result;
}

PDFObject PageMarksTest::pageContents(const PDFDocument& document, size_t pageIndex)
{
    const PDFObjectStorage& storage = document.getStorage();
    const PDFDictionary* page = storage.getDictionaryFromObject(storage.getObject(document.getCatalog()->getPage(pageIndex)->getPageReference()));
    return page->get("Contents");
}

QByteArray PageMarksTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument PageMarksTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
    {
        qWarning() << reader.getErrorMessage();
    }
    return document;
}

PDFDocument PageMarksTest::apply(const PDFDocument& document, const std::function<bool(PDFDocumentBuilder*, QString*)>& change)
{
    PDFDocumentBuilder builder(&document);
    QString errorMessage;
    const bool result = change(&builder, &errorMessage);
    if (!result)
    {
        qWarning() << "change failed:" << errorMessage;
    }
    // Written and read again: what a reader of the saved file gets
    return read(write(builder.build()));
}

PageMarks::WatermarkSettings PageMarksTest::watermark(const QString& text)
{
    PageMarks::WatermarkSettings settings;
    settings.text = text;
    settings.style.font = PageMarks::Font::HelveticaBold;
    settings.style.fontSize = 48;
    settings.style.color = Qt::gray;
    settings.opacity = 0.5;
    settings.diagonal = true;
    return settings;
}

void PageMarksTest::pageRanges()
{
    QString error;
    QCOMPARE(PageMarks::parsePageRange(QString(), 4, &error), (std::vector<PDFInteger>{ 0, 1, 2, 3 }));
    QCOMPARE(PageMarks::parsePageRange(QStringLiteral(" All "), 3, &error), (std::vector<PDFInteger>{ 0, 1, 2 }));
    QCOMPARE(PageMarks::parsePageRange(QStringLiteral("1-3, 5"), 6, &error), (std::vector<PDFInteger>{ 0, 1, 2, 4 }));
    QCOMPARE(PageMarks::parsePageRange(QStringLiteral("odd"), 5, &error), (std::vector<PDFInteger>{ 0, 2, 4 }));
    QCOMPARE(PageMarks::parsePageRange(QStringLiteral("even"), 5, &error), (std::vector<PDFInteger>{ 1, 3 }));
    QCOMPARE(PageMarks::parsePageRange(QStringLiteral("4-"), 6, &error), (std::vector<PDFInteger>{ 3, 4, 5 }));
    QVERIFY(error.isEmpty());

    QVERIFY(PageMarks::parsePageRange(QStringLiteral("7"), 6, &error).empty());
    QVERIFY(!error.isEmpty());
    QVERIFY(PageMarks::parsePageRange(QStringLiteral("abc"), 6, &error).empty());
    QVERIFY(!error.isEmpty());
}

void PageMarksTest::winAnsiEncoding()
{
    QCOMPARE(PageMarks::encodeWinAnsi(QStringLiteral("Abc (1)")), QByteArray("Abc (1)"));
    QCOMPARE(PageMarks::encodeWinAnsi(QString::fromUtf8("é€—")), QByteArray("\xE9\x80\x97"));
    // A character outside WinAnsi is one '?', also outside the basic plane
    QCOMPARE(PageMarks::encodeWinAnsi(QString::fromUtf8("a中b😀c")), QByteArray("a?b?c"));
    QCOMPARE(PageMarks::getTextWidth(PageMarks::Font::Courier, "abc", 10), 18.0);
    QCOMPARE(PageMarks::getTextWidth(PageMarks::Font::Helvetica, "AV", 1000), 1334.0);
    QCOMPARE(PageMarks::getTextWidth(PageMarks::Font::TimesRoman, "A", 1000), 722.0);
}

void PageMarksTest::tokens()
{
    const QDate date(2026, 10, 1);
    QCOMPARE(PageMarks::expandTokens(QStringLiteral("Page <<page>> of <<PAGES>>"), 3, 9, date, QStringLiteral("M/d/yyyy"), QString(), QString()), QStringLiteral("Page 3 of 9"));
    QCOMPARE(PageMarks::expandTokens(QStringLiteral("<<date>> | <<date:yyyy-MM-dd>> | <<filename>>"), 1, 1, date, QStringLiteral("M/d/yyyy"), QStringLiteral("a.pdf"), QString()),
             QStringLiteral("10/1/2026 | 2026-10-01 | a.pdf"));
    QCOMPARE(PageMarks::formatBates(QStringLiteral("ABC"), 42, 6, QStringLiteral("-X")), QStringLiteral("ABC000042-X"));
    QCOMPARE(PageMarks::expandTokens(QStringLiteral("<<bates>> <<unknown>>"), 1, 1, date, QString(), QString(), QStringLiteral("B1")), QStringLiteral("B1 <<unknown>>"));
}

void PageMarksTest::watermarkTextIsExtracted()
{
    const PDFDocument original = createDocument(2);
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, watermark(QStringLiteral("CONFIDENTIAL (draft)")), error); });

    for (size_t i = 0; i < 2; ++i)
    {
        const QString text = pageText(marked, i);
        QVERIFY2(text.contains(QStringLiteral("CONFIDENTIAL (draft)")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Original page %1").arg(i + 1)), qPrintable(text));
    }
    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::Watermark), 2);
    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::HeaderFooter), 0);

    // The watermark is centered on the page (diagonal, around the middle)
    const QRectF box = findText(marked, 0, QStringLiteral("CONFIDENTIAL"));
    QVERIFY(box.isValid());
    QVERIFY2(std::abs(box.center().x() - 306) < 20 && std::abs(box.center().y() - 396) < 20, qPrintable(QStringLiteral("%1 %2").arg(box.center().x()).arg(box.center().y())));

    // The same form is used on both pages (the pages have the same size)
    const QByteArray content0 = pageContent(marked, 0);
    QVERIFY(content0.contains("/Artifact <</Type /Pagination /Subtype /Watermark>> BDC"));
}

void PageMarksTest::watermarkOnChosenPagesOnly()
{
    const PDFDocument original = createDocument(4);
    PageMarks::WatermarkSettings settings = watermark(QStringLiteral("DRAFT"));
    settings.pageRange = QStringLiteral("2, 4");
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, settings, error); });

    QVERIFY(!pageText(marked, 0).contains(QStringLiteral("DRAFT")));
    QVERIFY(pageText(marked, 1).contains(QStringLiteral("DRAFT")));
    QVERIFY(!pageText(marked, 2).contains(QStringLiteral("DRAFT")));
    QVERIFY(pageText(marked, 3).contains(QStringLiteral("DRAFT")));
    QCOMPARE(pageContent(marked, 0), pageContent(original, 0));

    // An invalid range writes nothing
    PDFDocumentBuilder builder(&original);
    QString error;
    settings.pageRange = QStringLiteral("9");
    QVERIFY(!PageMarks::addWatermark(&builder, &original, settings, &error));
    QVERIFY(!error.isEmpty());
}

void PageMarksTest::watermarkBehindAndInFront()
{
    const PDFDocument original = createDocument(1);
    const QByteArray originalContent = pageContent(original, 0);

    PageMarks::WatermarkSettings settings = watermark(QStringLiteral("BEHIND"));
    settings.behind = true;
    const PDFDocument behind = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, settings, error); });
    const QByteArray behindContent = pageContent(behind, 0);
    QVERIFY(behindContent.startsWith("\n/Artifact"));
    QVERIFY(behindContent.endsWith(originalContent));

    settings.behind = false;
    settings.text = QStringLiteral("FRONT");
    const PDFDocument front = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, settings, error); });
    const QByteArray frontContent = pageContent(front, 0);

    // The original content is enclosed in q/Q - its scaling and its red color do not
    // reach the watermark
    QVERIFY(frontContent.startsWith("q\n" + originalContent + "\nQ\n"));
    QVERIFY(frontContent.indexOf("/Artifact") > frontContent.indexOf(originalContent));
    const QRectF box = findText(front, 0, QStringLiteral("FRONT"));
    QVERIFY2(std::abs(box.center().x() - 306) < 20 && std::abs(box.center().y() - 396) < 20, qPrintable(QStringLiteral("%1 %2").arg(box.center().x()).arg(box.center().y())));
}

void PageMarksTest::watermarkOpacityAndFitToPage()
{
    const PDFDocument original = createDocument(1);
    PageMarks::WatermarkSettings settings = watermark(QStringLiteral("WIDE"));
    settings.diagonal = false;
    settings.rotation = 0;
    settings.fitToPage = true;
    settings.opacity = 0.25;
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, settings, error); });

    // The text fills 90 % of the width of the page
    const QRectF box = findText(marked, 0, QStringLiteral("WIDE"));
    QVERIFY2(box.width() > 500 && box.width() < 612, qPrintable(QString::number(box.width())));

    // The opacity is in the graphic state of the form
    const PDFObjectStorage& storage = marked.getStorage();
    bool found = false;
    for (const PDFObjectStorage::Entry& entry : storage.getObjects())
    {
        if (entry.object.isStream())
        {
            const PDFDictionary* dictionary = entry.object.getStream()->getDictionary();
            const PDFDictionary* resources = storage.getDictionaryFromObject(dictionary->get("Resources"));
            const PDFDictionary* states = resources ? storage.getDictionaryFromObject(resources->get("ExtGState")) : nullptr;
            const PDFDictionary* state = states ? storage.getDictionaryFromObject(states->get("GS0")) : nullptr;
            if (state)
            {
                QCOMPARE(state->get("ca").getReal(), 0.25);
                QCOMPARE(state->get("CA").getReal(), 0.25);
                QVERIFY(dictionary->get("PieceInfo").isDictionary());
                found = true;
            }
        }
    }
    QVERIFY(found);
}

void PageMarksTest::watermarkRespectsCropBox()
{
    const PDFDocument original = createDocument(1, 0, QRectF(100, 100, 300, 400));
    PageMarks::WatermarkSettings settings = watermark(QStringLiteral("CROP"));
    settings.diagonal = false;
    settings.style.fontSize = 20;
    settings.alignment = Qt::AlignLeft | Qt::AlignBottom;
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, settings, error); });

    // At the lower left corner of the crop box, not of the media box
    const QRectF box = findText(marked, 0, QStringLiteral("CROP"));
    QVERIFY2(box.left() >= 99 && box.left() < 110 && box.top() >= 99 && box.top() < 115, qPrintable(QStringLiteral("%1 %2").arg(box.left()).arg(box.top())));
}

void PageMarksTest::headerFooterNumbers()
{
    const PDFDocument original = createDocument(3);
    PageMarks::HeaderFooterSettings settings;
    settings.texts[PageMarks::HeaderRight] = QStringLiteral("Page <<page>> of <<pages>>");
    settings.texts[PageMarks::FooterLeft] = QStringLiteral("<<filename>>");
    settings.texts[PageMarks::FooterCenter] = QStringLiteral("<<date:yyyy-MM-dd>>");
    settings.fileName = QStringLiteral("report.pdf");
    settings.date = QDate(2026, 10, 1);
    settings.skipFirstPage = true;
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addHeaderFooter(builder, &original, settings, error); });

    QVERIFY(!pageText(marked, 0).contains(QStringLiteral("Page")));
    QCOMPARE(pageContent(marked, 0), pageContent(original, 0));
    for (size_t i = 1; i < 3; ++i)
    {
        const QString text = pageText(marked, i);
        QVERIFY2(text.contains(QStringLiteral("Page %1 of 3").arg(i + 1)), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("report.pdf")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("2026-10-01")), qPrintable(text));
    }

    // The header is at the top right (right margin 72 pt, top 36 pt), the footer at the bottom
    const QRectF header = findText(marked, 1, QStringLiteral("Page 2"));
    QVERIFY2(header.top() > 792 - 36 - 15 && header.bottom() <= 792 - 36 + 1 && std::abs(header.right() - (612 - 72)) < 2,
             qPrintable(QStringLiteral("%1 %2 %3").arg(header.top()).arg(header.bottom()).arg(header.right())));
    const QRectF footer = findText(marked, 1, QStringLiteral("report.pdf"));
    QVERIFY2(footer.top() >= 36 - 1 && footer.bottom() < 60 && std::abs(footer.left() - 72) < 2,
             qPrintable(QStringLiteral("%1 %2 %3").arg(footer.top()).arg(footer.bottom()).arg(footer.left())));

    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::HeaderFooter), 4);
    QCOMPARE(PageMarks::countMarkedPages(&marked, PageMarks::HeaderFooter), 2);

    // The start number shifts the numbers
    settings.skipFirstPage = false;
    settings.startPageNumber = 0;
    const PDFDocument shifted = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addHeaderFooter(builder, &original, settings, error); });
    QVERIFY(pageText(shifted, 1).contains(QStringLiteral("Page 1 of 2")));
}

void PageMarksTest::headerOnRotatedPage()
{
    for (int rotate : { 90, 180, 270 })
    {
        const PDFDocument original = createDocument(1, rotate);
        PageMarks::HeaderFooterSettings settings;
        settings.texts[PageMarks::HeaderCenter] = QStringLiteral("TOPMARK");
        const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addHeaderFooter(builder, &original, settings, error); });

        // The page as it is shown: its top edge is the left edge of the page (90), the
        // bottom edge (180), the right edge (270)
        const QRectF box = findText(marked, 0, QStringLiteral("TOPMARK"));
        QVERIFY2(box.isValid(), qPrintable(pageText(marked, 0)));
        const QPointF center = box.center();
        switch (rotate)
        {
            case 90:
                QVERIFY2(center.x() < 60 && std::abs(center.y() - 396) < 5, qPrintable(QStringLiteral("%1 %2").arg(center.x()).arg(center.y())));
                break;
            case 180:
                QVERIFY2(center.y() < 60 && std::abs(center.x() - 306) < 5, qPrintable(QStringLiteral("%1 %2").arg(center.x()).arg(center.y())));
                break;
            default:
                QVERIFY2(center.x() > 552 && std::abs(center.y() - 396) < 5, qPrintable(QStringLiteral("%1 %2").arg(center.x()).arg(center.y())));
                break;
        }
    }
}

void PageMarksTest::batesNumbers()
{
    const PDFDocument original = createDocument(3);
    PageMarks::BatesSettings settings;
    settings.prefix = QStringLiteral("SOG-");
    settings.start = 99;
    settings.digits = 5;
    settings.pageRange = QStringLiteral("2-3");
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBatesNumbers(builder, &original, settings, error); });

    QVERIFY(!pageText(marked, 0).contains(QStringLiteral("SOG-")));
    QVERIFY(pageText(marked, 1).contains(QStringLiteral("SOG-00099")));
    QVERIFY(pageText(marked, 2).contains(QStringLiteral("SOG-00100")));
    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::Bates), 2);
    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::HeaderFooter), 0);

    const QRectF box = findText(marked, 2, QStringLiteral("SOG-00100"));
    QVERIFY(box.right() <= 612 - 36 + 1 && box.right() > 612 - 40 && box.top() >= 36 - 1);
}

void PageMarksTest::removeRestoresOriginalContent()
{
    const PDFDocument original = createDocument(3);

    PageMarks::HeaderFooterSettings header;
    header.texts[PageMarks::FooterCenter] = QStringLiteral("<<page>>");
    PageMarks::BatesSettings bates;
    PageMarks::WatermarkSettings behind = watermark(QStringLiteral("UNDER"));
    behind.behind = true;

    PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error)
    {
        return PageMarks::addWatermark(builder, &original, watermark(QStringLiteral("OVER")), error) &&
               PageMarks::addHeaderFooter(builder, &original, header, error);
    });
    marked = apply(marked, [&](PDFDocumentBuilder* builder, QString* error)
    {
        return PageMarks::addBatesNumbers(builder, &marked, bates, error) && PageMarks::addWatermark(builder, &marked, behind, error);
    });
    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::AllKinds), 3 * 4);

    int removed = 0;
    const PDFDocument cleaned = apply(marked, [&](PDFDocumentBuilder* builder, QString*)
    {
        removed = PageMarks::removeMarks(builder, &marked, PageMarks::AllKinds);
        return true;
    });
    QCOMPARE(removed, 12);
    QCOMPARE(PageMarks::countMarks(&cleaned, PageMarks::AllKinds), 0);

    for (size_t i = 0; i < 3; ++i)
    {
        // The page draws the very same original stream, and nothing else
        QCOMPARE(pageContent(cleaned, i), pageContent(original, i));
        QVERIFY(pageContents(cleaned, i).isReference());
        QCOMPARE(pageContents(cleaned, i).getReference(), pageContents(original, i).getReference());
        QCOMPARE(pageText(cleaned, i), pageText(original, i));

        const PDFObjectStorage& storage = cleaned.getStorage();
        const PDFDictionary* resources = storage.getDictionaryFromObject(cleaned.getCatalog()->getPage(i)->getResources());
        const PDFDictionary* xobjects = storage.getDictionaryFromObject(resources->get("XObject"));
        QVERIFY(!xobjects || xobjects->getCount() == 0);
    }
}

void PageMarksTest::removeOnlyChosenKinds()
{
    const PDFDocument original = createDocument(2);
    PageMarks::HeaderFooterSettings header;
    header.texts[PageMarks::HeaderLeft] = QStringLiteral("HEADER");
    PageMarks::BatesSettings bates;
    bates.prefix = QStringLiteral("BATES");

    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error)
    {
        return PageMarks::addWatermark(builder, &original, watermark(QStringLiteral("WATERMARK")), error) &&
               PageMarks::addHeaderFooter(builder, &original, header, error) &&
               PageMarks::addBatesNumbers(builder, &original, bates, error);
    });

    const PDFDocument noWatermark = apply(marked, [&](PDFDocumentBuilder* builder, QString*) { return PageMarks::removeMarks(builder, &marked, PageMarks::Watermark) == 2; });
    QVERIFY(!pageText(noWatermark, 0).contains(QStringLiteral("WATERMARK")));
    QVERIFY(pageText(noWatermark, 0).contains(QStringLiteral("HEADER")));
    QVERIFY(pageText(noWatermark, 0).contains(QStringLiteral("BATES000001")));
    QVERIFY(pageText(noWatermark, 1).contains(QStringLiteral("BATES000002")));

    const PDFDocument noBates = apply(noWatermark, [&](PDFDocumentBuilder* builder, QString*) { return PageMarks::removeMarks(builder, &noWatermark, PageMarks::Bates) == 2; });
    QVERIFY(pageText(noBates, 0).contains(QStringLiteral("HEADER")));
    QVERIFY(!pageText(noBates, 0).contains(QStringLiteral("BATES")));
    QVERIFY(pageText(noBates, 0).contains(QStringLiteral("Original page 1")));
}

void PageMarksTest::removeKeepsForeignPaginationArtifacts()
{
    // A header, which a word processor wrote as a pagination artifact of the page
    // text, is not a mark - it is a part of the document
    PDFDocument original = createDocument(1);
    {
        PDFDocumentBuilder builder(&original);
        const PDFObjectReference page = original.getCatalog()->getPage(0)->getPageReference();
        QByteArray content = "/Artifact <</Type /Pagination /Subtype /Header /Attached [/Top]>> BDC BT /F1 12 Tf 72 750 Td (Word header) Tj ET EMC\n"
                             "BT /F1 12 Tf 72 700 Td (Body) Tj ET";
        PDFDictionaryBuilder streamDictionary;
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        const PDFObjectReference stream = builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(content))));
        PDFDictionaryBuilder pageDictionary(*builder.getStorage()->getDictionaryFromObject(builder.getObjectByReference(page)));
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(stream));
        builder.setObject(page, PDFObject::createDictionary(std::move(pageDictionary)));
        original = builder.build();
    }

    QCOMPARE(PageMarks::countMarks(&original, PageMarks::AllKinds), 0);
    PDFDocumentBuilder builder(&original);
    QCOMPARE(PageMarks::removeMarks(&builder, &original, PageMarks::AllKinds), 0);
    const PDFDocument result = builder.build();
    QCOMPARE(pageContent(result, 0), pageContent(original, 0));
}

void PageMarksTest::removeAcrobatMarkInMergedStream()
{
    // Acrobat's watermark (a form with /PieceInfo /ADBE_CompoundType) inside one
    // stream with the page content, as another program merges the streams
    PDFDocument original = createDocument(1);
    const QByteArray before = "BT /F1 12 Tf 72 700 Td (Before) Tj ET\n";
    const QByteArray after = "\nBT /F1 12 Tf 72 600 Td (After \\(x\\)) Tj ET";
    {
        PDFDocumentBuilder builder(&original);
        const PDFObjectReference pageReference = original.getCatalog()->getPage(0)->getPageReference();

        QByteArray formContent = "BT /F1 40 Tf 100 400 Td (ACROBAT) Tj ET";
        PDFDictionaryBuilder fonts;
        const PDFObjectStorage* storage = builder.getStorage();
        const PDFDictionary* pageResources = storage->getDictionaryFromObject(storage->getDictionaryFromObject(builder.getObjectByReference(pageReference))->get("Resources"));
        fonts.setEntry(PDFInplaceOrMemoryString("F1"), storage->getDictionaryFromObject(pageResources->get("Font"))->get("F1"));
        PDFDictionaryBuilder formResources;
        formResources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
        PDFDictionaryBuilder compound;
        compound.setEntry(PDFInplaceOrMemoryString("Private"), PDFObject::createName("Watermark"));
        PDFDictionaryBuilder pieceInfo;
        pieceInfo.setEntry(PDFInplaceOrMemoryString("ADBE_CompoundType"), PDFObject::createDictionary(std::move(compound)));
        PDFDictionaryBuilder formDictionary;
        formDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        formDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
        PDFArrayBuilder bbox;
        for (double value : { 0.0, 0.0, 612.0, 792.0 })
        {
            bbox.appendItem(PDFObject::createReal(value));
        }
        formDictionary.setEntry(PDFInplaceOrMemoryString("BBox"), PDFObject::createArray(std::move(bbox)));
        formDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(formResources)));
        formDictionary.setEntry(PDFInplaceOrMemoryString("PieceInfo"), PDFObject::createDictionary(std::move(pieceInfo)));
        formDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(formContent.size()));
        const PDFObjectReference form = builder.addObject(PDFObject::createStream(PDFStream(std::move(formDictionary), std::move(formContent))));

        QByteArray content = before + "/Artifact <</Subtype /Watermark /Type /Pagination >>BDC q 0 g 1 0 0 1 0 0 cm /Fm0 Do Q EMC" + after;
        content = PDFFlateDecodeFilter::compress(content);
        PDFDictionaryBuilder streamDictionary;
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        const PDFObjectReference stream = builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(content))));

        PDFDictionaryBuilder pageDictionary(*builder.getStorage()->getDictionaryFromObject(builder.getObjectByReference(pageReference)));
        PDFDictionaryBuilder resources(*builder.getStorage()->getDictionaryFromObject(pageDictionary.get("Resources")));
        PDFDictionaryBuilder xobjects;
        xobjects.setEntry(PDFInplaceOrMemoryString("Fm0"), PDFObject::createReference(form));
        resources.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjects)));
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(stream));
        builder.setObject(pageReference, PDFObject::createDictionary(std::move(pageDictionary)));
        original = builder.build();
    }

    QVERIFY(pageText(original, 0).contains(QStringLiteral("ACROBAT")));
    QCOMPARE(PageMarks::countMarks(&original, PageMarks::Watermark), 1);

    const PDFDocument cleaned = apply(original, [&](PDFDocumentBuilder* builder, QString*) { return PageMarks::removeMarks(builder, &original, PageMarks::Watermark) == 1; });
    QCOMPARE(pageContent(cleaned, 0), before + after);
    QVERIFY(!pageText(cleaned, 0).contains(QStringLiteral("ACROBAT")));
    QVERIFY(pageText(cleaned, 0).contains(QStringLiteral("After (x)")));
}

void PageMarksTest::imageWatermark()
{
    const PDFDocument original = createDocument(1);

    QImage image(40, 20, QImage::Format_ARGB32);
    image.fill(QColor(255, 0, 0, 128));
    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }

    PageMarks::WatermarkSettings settings;
    settings.useImage = true;
    settings.imageData = png;
    settings.imageScale = 0.5;
    settings.imageScaleRelativeToPage = true;
    settings.opacity = 1.0;
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, settings, error); });
    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::Watermark), 1);

    int images = 0;
    int masks = 0;
    for (const PDFObjectStorage::Entry& entry : marked.getStorage().getObjects())
    {
        if (entry.object.isStream() && entry.object.getStream()->getDictionary()->get("Subtype").isName() &&
            entry.object.getStream()->getDictionary()->get("Subtype").getString() == "Image")
        {
            ++images;
            masks += entry.object.getStream()->getDictionary()->hasKey("SMask") ? 1 : 0;
        }
    }
    QCOMPARE(images, 2);
    QCOMPARE(masks, 1);

    // Half of the page width (the image is wider than tall, and not rotated)
    QVERIFY(pageContent(marked, 0).contains("/Artifact"));

    // A JPEG is embedded as it is
    QImage photo(32, 32, QImage::Format_RGB32);
    photo.fill(Qt::blue);
    QByteArray jpeg;
    {
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        photo.save(&buffer, "JPG");
    }
    settings.imageData = jpeg;
    const PDFDocument jpegMarked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &original, settings, error); });
    bool dct = false;
    for (const PDFObjectStorage::Entry& entry : jpegMarked.getStorage().getObjects())
    {
        if (entry.object.isStream() && entry.object.getStream()->getDictionary()->get("Filter").isName() &&
            entry.object.getStream()->getDictionary()->get("Filter").getString() == "DCTDecode")
        {
            dct = *entry.object.getStream()->getContent() == jpeg;
        }
    }
    QVERIFY(dct);

    // Not an image
    settings.imageData = "garbage";
    PDFDocumentBuilder builder(&original);
    QString error;
    QVERIFY(!PageMarks::addWatermark(&builder, &original, settings, &error));
    QVERIFY(!error.isEmpty());
}

void PageMarksTest::savedDocumentIsValid()
{
    const QString qpdf = QStandardPaths::findExecutable(QStringLiteral("qpdf"));
    if (qpdf.isEmpty())
    {
        QSKIP("qpdf is not installed");
    }

    const PDFDocument original = createDocument(2, 90);
    PageMarks::HeaderFooterSettings header;
    header.texts[PageMarks::HeaderCenter] = QStringLiteral("Header (with parentheses) \\ and é");
    header.texts[PageMarks::FooterRight] = QStringLiteral("Page <<page>> of <<pages>>");
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error)
    {
        return PageMarks::addWatermark(builder, &original, watermark(QStringLiteral("SAMPLE")), error) &&
               PageMarks::addHeaderFooter(builder, &original, header, error);
    });

    QTemporaryDir directory;
    const QString fileName = directory.filePath(QStringLiteral("marked.pdf"));
    QFile file(fileName);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(write(marked));
    file.close();

    QProcess process;
    process.start(qpdf, { QStringLiteral("--check"), fileName });
    QVERIFY(process.waitForFinished(30000));
    const QString output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
    QVERIFY2(process.exitCode() == 0 && output.contains(QStringLiteral("No syntax or stream encoding errors")), qPrintable(output));

    // PDFFIRE_MARKS_SAMPLE=<file>: the sample is kept to be looked at
    if (qEnvironmentVariableIsSet("PDFFIRE_MARKS_SAMPLE"))
    {
        QFile::remove(qEnvironmentVariable("PDFFIRE_MARKS_SAMPLE"));
        QFile::copy(fileName, qEnvironmentVariable("PDFFIRE_MARKS_SAMPLE"));
    }
}

// PDF Fire: the source of a background - a letter page with LETTERHEAD at 72, 720 (30 pt)
// and a blue bar at its bottom
PDFDocument PageMarksTest::createSourceDocument(int rotate)
{
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference pageReference = builder.getPages().front();

    PDFDictionaryBuilder fontDictionary;
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("Helvetica"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Encoding"), PDFObject::createName("WinAnsiEncoding"));
    const PDFObjectReference font = builder.addObject(PDFObject::createDictionary(std::move(fontDictionary)));
    PDFDictionaryBuilder fonts;
    fonts.setEntry(PDFInplaceOrMemoryString("LH"), PDFObject::createReference(font));
    PDFDictionaryBuilder resources;
    resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
    const PDFObjectReference resourcesReference = builder.addObject(PDFObject::createDictionary(std::move(resources)));

    // Two content streams (the page content is an array) - they are joined in the form
    QByteArray content1 = "0 0 1 rg 0 0 612 20 re f\nBT /LH 30 Tf 72 720 Td";
    QByteArray content2 = "(LETTERHEAD) Tj ET";
    PDFArrayBuilder contents;
    for (QByteArray* content : { &content1, &content2 })
    {
        PDFDictionaryBuilder streamDictionary;
        streamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content->size()));
        contents.appendItem(PDFObject::createReference(builder.addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), QByteArray(*content))))));
    }

    PDFDictionaryBuilder page(*builder.getStorage()->getDictionaryFromObject(builder.getObjectByReference(pageReference)));
    page.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createArray(std::move(contents)));
    page.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createReference(resourcesReference));
    if (rotate)
    {
        page.setEntry(PDFInplaceOrMemoryString("Rotate"), PDFObject::createInteger(rotate));
    }
    builder.setObject(pageReference, PDFObject::createDictionary(std::move(page)));
    return builder.build();
}

void PageMarksTest::backgroundUnderContentOnAllPages()
{
    const PDFDocument original = createDocument(3);
    const PDFDocument source = createSourceDocument();
    PageMarks::BackgroundSettings settings;
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBackground(builder, &original, &source, settings, error); });

    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::Background), 3);
    QCOMPARE(PageMarks::countMarks(&marked, PageMarks::Watermark | PageMarks::HeaderFooter | PageMarks::Bates), 0);

    for (size_t i = 0; i < 3; ++i)
    {
        // Both texts are there (the page text stays searchable)
        const QString text = pageText(marked, i);
        QVERIFY2(text.contains(QStringLiteral("LETTERHEAD")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Original page %1").arg(i + 1)), qPrintable(text));

        // The background is the first content stream - underneath the page content,
        // which is not changed and not enclosed in q/Q (nothing is drawn after it)
        const QByteArray content = pageContent(marked, i);
        QVERIFY2(content.startsWith("\n/Artifact <</Type /Background /BBox [0 0 612 792]>> BDC\nq\n/PFMark0 Do\nQ\nEMC\n"), content.constData());
        QVERIFY(content.endsWith(pageContent(original, i)));
        QVERIFY(content.indexOf("/Artifact") < content.indexOf("Original page"));
    }

    // The same size: the letterhead is where it is on its own page
    const QRectF box = findText(marked, 1, QStringLiteral("LETTERHEAD"));
    QVERIFY2(std::abs(box.left() - 72) < 3 && std::abs(box.top() - 720) < 12, qPrintable(QStringLiteral("%1 %2").arg(box.left()).arg(box.top())));

    // The source page is imported once (one form without our private data, the form
    // of the mark is shared too), with its font, and its two streams joined
    int importedForms = 0;
    const PDFObjectStorage& storage = marked.getStorage();
    for (const PDFObjectStorage::Entry& entry : storage.getObjects())
    {
        if (entry.object.isStream())
        {
            const PDFDictionary* dictionary = entry.object.getStream()->getDictionary();
            if (dictionary->get("Subtype").isName() && dictionary->get("Subtype").getString() == "Form" && !dictionary->hasKey("PieceInfo"))
            {
                ++importedForms;
                QVERIFY(storage.getDecodedStream(entry.object.getStream()).contains("72 720 Td\n(LETTERHEAD) Tj"));
                QVERIFY(!dictionary->hasKey("Group"));  // opaque - no transparency group
            }
        }
    }
    QCOMPARE(importedForms, 1);

    // A watermark behind the content stays above the background; removing the
    // background restores the pages exactly
    PageMarks::WatermarkSettings behind = watermark(QStringLiteral("BEHIND"));
    behind.behind = true;
    const PDFDocument both = apply(marked, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addWatermark(builder, &marked, behind, error); });
    const PDFDocument background = apply(both, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBackground(builder, &both, &source, settings, error); });
    const QByteArray content = pageContent(background, 0);
    QVERIFY(content.startsWith("\n/Artifact <</Type /Background"));
    QVERIFY2(content.indexOf("/Subtype /Watermark") > content.lastIndexOf("/Type /Background"), content.constData());
    QCOMPARE(PageMarks::countMarks(&background, PageMarks::Background), 6);

    int removed = 0;
    const PDFDocument cleaned = apply(marked, [&](PDFDocumentBuilder* builder, QString*)
    {
        removed = PageMarks::removeMarks(builder, &marked, PageMarks::Background);
        return true;
    });
    QCOMPARE(removed, 3);
    for (size_t i = 0; i < 3; ++i)
    {
        QCOMPARE(pageContent(cleaned, i), pageContent(original, i));
        QVERIFY(pageContents(cleaned, i).isReference());
        QCOMPARE(pageContents(cleaned, i).getReference(), pageContents(original, i).getReference());
        QCOMPARE(pageText(cleaned, i), pageText(original, i));
    }

    // Removing the backgrounds keeps the watermark (and AllKinds includes backgrounds)
    const PDFDocument noBackground = apply(background, [&](PDFDocumentBuilder* builder, QString*) { return PageMarks::removeMarks(builder, &background, PageMarks::Background) == 6; });
    QCOMPARE(PageMarks::countMarks(&noBackground, PageMarks::Watermark), 3);
    QVERIFY(!pageText(noBackground, 0).contains(QStringLiteral("LETTERHEAD")));
    QVERIFY(pageText(noBackground, 0).contains(QStringLiteral("BEHIND")));
    QCOMPARE(PageMarks::countMarks(&background, PageMarks::AllKinds), 9);

    // A wrong source page writes nothing
    PDFDocumentBuilder builder(&original);
    QString error;
    settings.sourcePageIndex = 1;
    QVERIFY(!PageMarks::addBackground(&builder, &original, &source, settings, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!PageMarks::addBackground(&builder, &original, nullptr, PageMarks::BackgroundSettings(), &error));
    QVERIFY(!error.isEmpty());
}

void PageMarksTest::backgroundOnChosenPagesWithOpacity()
{
    const PDFDocument original = createDocument(3);
    const PDFDocument source = createSourceDocument();
    PageMarks::BackgroundSettings settings;
    settings.pageRange = QStringLiteral("2");
    settings.opacity = 0.3;
    const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBackground(builder, &original, &source, settings, error); });

    QCOMPARE(pageContent(marked, 0), pageContent(original, 0));
    QCOMPARE(pageContent(marked, 2), pageContent(original, 2));
    QVERIFY(pageText(marked, 1).contains(QStringLiteral("LETTERHEAD")));
    QCOMPARE(PageMarks::countMarkedPages(&marked, PageMarks::Background), 1);

    // The opacity is in the form of the mark, the imported page is a transparency
    // group (it fades as a whole)
    const PDFObjectStorage& storage = marked.getStorage();
    bool opacityFound = false;
    bool groupFound = false;
    for (const PDFObjectStorage::Entry& entry : storage.getObjects())
    {
        if (!entry.object.isStream())
        {
            continue;
        }
        const PDFDictionary* dictionary = entry.object.getStream()->getDictionary();
        const PDFDictionary* resources = storage.getDictionaryFromObject(dictionary->get("Resources"));
        const PDFDictionary* states = resources ? storage.getDictionaryFromObject(resources->get("ExtGState")) : nullptr;
        if (const PDFDictionary* state = states ? storage.getDictionaryFromObject(states->get("GS0")) : nullptr)
        {
            QCOMPARE(state->get("ca").getReal(), 0.3);
            opacityFound = true;
        }
        if (const PDFDictionary* group = storage.getDictionaryFromObject(dictionary->get("Group")))
        {
            QCOMPARE(group->get("S").getString(), QByteArray("Transparency"));
            groupFound = true;
        }
    }
    QVERIFY(opacityFound);
    QVERIFY(groupFound);

    // Actual size, half of it, in the lower left corner
    settings.pageRange = QString();
    settings.opacity = 1.0;
    settings.fitToPage = false;
    settings.scale = 0.5;
    settings.alignment = Qt::AlignLeft | Qt::AlignBottom;
    const PDFDocument small = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBackground(builder, &original, &source, settings, error); });
    const QRectF box = findText(small, 0, QStringLiteral("LETTERHEAD"));
    QVERIFY2(std::abs(box.left() - 36) < 2 && std::abs(box.top() - 360) < 8, qPrintable(QStringLiteral("%1 %2").arg(box.left()).arg(box.top())));
    QVERIFY(pageContent(small, 0).contains("/BBox [0 0 306 396]"));

    // The saved document is valid
    const QString qpdf = QStandardPaths::findExecutable(QStringLiteral("qpdf"));
    if (!qpdf.isEmpty())
    {
        QTemporaryDir directory;
        const QString fileName = directory.filePath(QStringLiteral("background.pdf"));
        QFile file(fileName);
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(write(marked));
        file.close();

        QProcess process;
        process.start(qpdf, { QStringLiteral("--check"), fileName });
        QVERIFY(process.waitForFinished(30000));
        const QString output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
        QVERIFY2(process.exitCode() == 0 && output.contains(QStringLiteral("No syntax or stream encoding errors")), qPrintable(output));
    }
}

void PageMarksTest::backgroundRotatedAndCropped()
{
    const PDFDocument source = createSourceDocument();

    // A crop box not starting at 0, 0 (300 x 400 at 100, 100): the letter page is
    // scaled to the width (0.49), centered vertically
    {
        const PDFDocument original = createDocument(1, 0, QRectF(100, 100, 300, 400));
        const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBackground(builder, &original, &source, PageMarks::BackgroundSettings(), error); });
        const double scale = 300.0 / 612.0;
        const double expectedX = 100 + 72 * scale;
        const double expectedY = 100 + (400 - 792 * scale) / 2 + 720 * scale;
        const QRectF box = findText(marked, 0, QStringLiteral("LETTERHEAD"));
        QVERIFY2(std::abs(box.left() - expectedX) < 2 && std::abs(box.top() - expectedY) < 6,
                 qPrintable(QStringLiteral("%1 %2 / %3 %4").arg(box.left()).arg(box.top()).arg(expectedX).arg(expectedY)));
    }

    // A page rotated by 90: the background is upright as the page is shown (the
    // text runs up the page, from the shown top-left area)
    {
        const PDFDocument original = createDocument(1, 90);
        const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBackground(builder, &original, &source, PageMarks::BackgroundSettings(), error); });
        const double scale = 612.0 / 792.0;
        const double shownX = (792 - 612 * scale) / 2 + 72 * scale;
        const double shownY = 720 * scale;
        // Shown (u, v) -> page (612 - v, u)
        const QRectF box = findText(marked, 0, QStringLiteral("LETTERHEAD"));
        QVERIFY2(box.height() > box.width() && std::abs(box.right() - (612 - shownY)) < 6 && std::abs(box.top() - shownX) < 2,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.right()).arg(box.bottom())));
        QVERIFY(pageContent(marked, 0).startsWith("\n/Artifact <</Type /Background /BBox [0 "));
    }

    // A source page rotated by 90: it is used as it is shown (landscape), fitted into
    // the portrait page - the text runs down the shown page
    {
        const PDFDocument rotatedSource = createSourceDocument(90);
        const PDFDocument original = createDocument(1);
        const PDFDocument marked = apply(original, [&](PDFDocumentBuilder* builder, QString* error) { return PageMarks::addBackground(builder, &original, &rotatedSource, PageMarks::BackgroundSettings(), error); });
        const double scale = 612.0 / 792.0;
        // Source page (x, y) -> shown source (y, 612 - x), scaled, centered vertically
        const double expectedX = 720 * scale;
        const double expectedY = (792 - 612 * scale) / 2 + (612 - 72) * scale;
        const QRectF box = findText(marked, 0, QStringLiteral("LETTERHEAD"));
        QVERIFY2(box.height() > box.width() && std::abs(box.left() - expectedX) < 6 && std::abs(box.bottom() - expectedY) < 2,
                 qPrintable(QStringLiteral("%1 %2 %3 %4 / %5 %6").arg(box.left()).arg(box.top()).arg(box.right()).arg(box.bottom()).arg(expectedX).arg(expectedY)));
    }
}

QTEST_GUILESS_MAIN(PageMarksTest)

#include "tst_pagemarkstest.moc"
