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
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QtTest>
#include <QBuffer>

using namespace pdf;

class PageOperationsTest : public QObject
{
    Q_OBJECT

private slots:
    void blankDocument();
    void insertBlankPage();
    void deletePages();
    void deletedPageIsNotInTheSavedFile();
    void deleteAllPagesIsRefused();
    void extractPages();
    void rotatePages();
    void movePages();
    void insertDocument();
    void invalidPagesAreRefused();

private:
    /// Creates a document, whose pages can be told apart: the page i (from 1) is
    /// 100 * i points wide, and its content contains the text PAGE-<prefix>i-MARKER.
    static PDFDocument createDocument(int pageCount, const QByteArray& prefix = QByteArray());
    static QByteArray marker(int page, const QByteArray& prefix = QByteArray());
    static std::vector<int> pageWidths(const PDFDocument& document);
    static QByteArray write(const PDFDocument& document);
    static PDFDocument read(const QByteArray& data);
};

QByteArray PageOperationsTest::marker(int page, const QByteArray& prefix)
{
    return "PAGE-" + prefix + QByteArray::number(page) + "-MARKER";
}

PDFDocument PageOperationsTest::createDocument(int pageCount, const QByteArray& prefix)
{
    PDFDocumentBuilder builder;

    for (int i = 1; i <= pageCount; ++i)
    {
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 100 * i, 500));

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

std::vector<int> PageOperationsTest::pageWidths(const PDFDocument& document)
{
    std::vector<int> widths;
    for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
    {
        widths.push_back(qRound(document.getCatalog()->getPage(i)->getMediaBox().width()));
    }
    return widths;
}

QByteArray PageOperationsTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument PageOperationsTest::read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

void PageOperationsTest::blankDocument()
{
    const PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(1));
    QCOMPARE(document.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 612, 792));

    // The document can be written and read back
    QCOMPARE(read(write(document)).getCatalog()->getPageCount(), size_t(1));
}

void PageOperationsTest::insertBlankPage()
{
    const PDFDocument document = createDocument(3);
    QCOMPARE(pageWidths(document), (std::vector<int>{ 100, 200, 300 }));

    PDFDocument result;
    QVERIFY(PDFPageOperations::insertBlankPage(&document, 0, QRectF(0, 0, 50, 500), &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 50, 100, 200, 300 }));

    QVERIFY(PDFPageOperations::insertBlankPage(&document, 2, QRectF(0, 0, 50, 500), &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 50, 300 }));

    QVERIFY(PDFPageOperations::insertBlankPage(&document, 3, QRectF(0, 0, 50, 500), &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 50 }));
    QCOMPARE(pageWidths(read(write(result))), (std::vector<int>{ 100, 200, 300, 50 }));

    // The source document is not changed, and an invalid position is refused
    QCOMPARE(pageWidths(document), (std::vector<int>{ 100, 200, 300 }));
    QVERIFY(!PDFPageOperations::insertBlankPage(&document, 4, QRectF(0, 0, 50, 500), &result));
    QVERIFY(!PDFPageOperations::insertBlankPage(&document, -1, QRectF(0, 0, 50, 500), &result));
}

void PageOperationsTest::deletePages()
{
    const PDFDocument document = createDocument(5);

    PDFDocument result;
    QVERIFY(PDFPageOperations::deletePages(&document, { 3, 1, 1 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 300, 500 }));

    // The objects are not renumbered - the kept pages are the same objects as before
    QCOMPARE(result.getCatalog()->getPage(0)->getPageReference(), document.getCatalog()->getPage(0)->getPageReference());
    QCOMPARE(result.getCatalog()->getPage(1)->getPageReference(), document.getCatalog()->getPage(2)->getPageReference());
    QCOMPARE(result.getCatalog()->getPage(2)->getPageReference(), document.getCatalog()->getPage(4)->getPageReference());

    QCOMPARE(pageWidths(read(write(result))), (std::vector<int>{ 100, 300, 500 }));
}

void PageOperationsTest::deletedPageIsNotInTheSavedFile()
{
    const PDFDocument document = createDocument(3);
    const QByteArray original = write(document);
    QVERIFY(original.contains(marker(1)) && original.contains(marker(2)) && original.contains(marker(3)));

    PDFDocument result;
    QVERIFY(PDFPageOperations::deletePages(&document, { 1 }, &result));

    // The content of the deleted page must not be recoverable from the saved file
    const QByteArray saved = write(result);
    QVERIFY(saved.contains(marker(1)));
    QVERIFY(!saved.contains(marker(2)));
    QVERIFY(saved.contains(marker(3)));
}

void PageOperationsTest::deleteAllPagesIsRefused()
{
    const PDFDocument document = createDocument(2);

    PDFDocument result;
    QVERIFY(!PDFPageOperations::deletePages(&document, { 0, 1 }, &result));
    QVERIFY(PDFPageOperations::deletePages(&document, { 0 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 200 }));
}

void PageOperationsTest::extractPages()
{
    const PDFDocument document = createDocument(4);

    PDFDocument result;
    QVERIFY(PDFPageOperations::extractPages(&document, { 2, 0 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 300 }));

    const QByteArray saved = write(result);
    QVERIFY(saved.contains(marker(1)) && saved.contains(marker(3)));
    QVERIFY(!saved.contains(marker(2)) && !saved.contains(marker(4)));

    QVERIFY(PDFPageOperations::extractPages(&document, { 0, 1, 2, 3 }, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 400 }));
}

void PageOperationsTest::rotatePages()
{
    const PDFDocument document = createDocument(3);

    PDFDocument result;
    QVERIFY(PDFPageOperations::rotatePages(&document, { 1 }, true, &result));
    QCOMPARE(result.getCatalog()->getPage(0)->getPageRotation(), PageRotation::None);
    QCOMPARE(result.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(result.getCatalog()->getPage(2)->getPageRotation(), PageRotation::None);

    // The rotation is a part of the saved document
    const PDFDocument saved = read(write(result));
    QCOMPARE(saved.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate90);

    PDFDocument back;
    QVERIFY(PDFPageOperations::rotatePages(&result, { 1 }, false, &back));
    QCOMPARE(back.getCatalog()->getPage(1)->getPageRotation(), PageRotation::None);

    PDFDocument left;
    QVERIFY(PDFPageOperations::rotatePages(&document, { 0, 2 }, false, &left));
    QCOMPARE(left.getCatalog()->getPage(0)->getPageRotation(), PageRotation::Rotate270);
    QCOMPARE(left.getCatalog()->getPage(1)->getPageRotation(), PageRotation::None);
    QCOMPARE(left.getCatalog()->getPage(2)->getPageRotation(), PageRotation::Rotate270);
}

void PageOperationsTest::movePages()
{
    const PDFDocument document = createDocument(4);

    PDFDocument result;
    QVERIFY(PDFPageOperations::movePages(&document, { 1 }, true, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 300, 200, 400 }));

    QVERIFY(PDFPageOperations::movePages(&document, { 1 }, false, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 200, 100, 300, 400 }));

    // A block of pages moves together
    QVERIFY(PDFPageOperations::movePages(&document, { 1, 2 }, true, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 400, 200, 300 }));

    // Pages at the start cannot move further towards the start
    QVERIFY(PDFPageOperations::movePages(&document, { 0 }, false, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 400 }));
    QVERIFY(PDFPageOperations::movePages(&document, { 3 }, true, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 400 }));

    QVERIFY(PDFPageOperations::movePages(&document, { 0, 1, 3 }, false, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 400, 300 }));
    QCOMPARE(pageWidths(read(write(result))), (std::vector<int>{ 100, 200, 400, 300 }));
}

void PageOperationsTest::insertDocument()
{
    const PDFDocument document = createDocument(3);
    PDFDocumentBuilder otherBuilder;
    otherBuilder.appendPage(QRectF(0, 0, 11, 500));
    otherBuilder.appendPage(QRectF(0, 0, 22, 500));
    const PDFDocument other = otherBuilder.build();

    PDFDocument result;
    QVERIFY(PDFPageOperations::insertDocument(&document, &other, 1, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 11, 22, 200, 300 }));

    QVERIFY(PDFPageOperations::insertDocument(&document, &other, 0, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 11, 22, 100, 200, 300 }));

    QVERIFY(PDFPageOperations::insertDocument(&document, &other, 3, &result));
    QCOMPARE(pageWidths(result), (std::vector<int>{ 100, 200, 300, 11, 22 }));
    QVERIFY(!PDFPageOperations::insertDocument(&document, &other, 4, &result));

    // The content of the inserted pages comes with them, and the result is a valid file
    const PDFDocument withContent = createDocument(2, "X");
    QVERIFY(PDFPageOperations::insertDocument(&document, &withContent, 3, &result));
    const QByteArray saved = write(result);
    QVERIFY(saved.contains(marker(1)) && saved.contains(marker(3)));
    QVERIFY(saved.contains(marker(1, "X")) && saved.contains(marker(2, "X")));
    QCOMPARE(pageWidths(read(saved)), (std::vector<int>{ 100, 200, 300, 100, 200 }));

    // A document inserted into itself
    QVERIFY(PDFPageOperations::insertDocument(&document, &document, 3, &result));
    QCOMPARE(pageWidths(read(write(result))), (std::vector<int>{ 100, 200, 300, 100, 200, 300 }));
}

void PageOperationsTest::invalidPagesAreRefused()
{
    const PDFDocument document = createDocument(2);

    PDFDocument result;
    QVERIFY(!PDFPageOperations::deletePages(&document, { }, &result));
    QVERIFY(!PDFPageOperations::deletePages(&document, { 2 }, &result));
    QVERIFY(!PDFPageOperations::deletePages(&document, { -1 }, &result));
    QVERIFY(!PDFPageOperations::rotatePages(&document, { 5 }, true, &result));
    QVERIFY(!PDFPageOperations::movePages(&document, { }, true, &result));
    QVERIFY(!PDFPageOperations::extractPages(&document, { 2 }, &result));
}

QTEST_APPLESS_MAIN(PageOperationsTest)

#include "tst_pageoperationstest.moc"
