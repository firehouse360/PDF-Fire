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
#include "pdfoutline.h"

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
    void insertDocumentKeepsBookmarks();
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

namespace
{

QSharedPointer<PDFOutlineItem> createBookmark(const QString& title, const PDFDestination& destination)
{
    QSharedPointer<PDFOutlineItem> item(new PDFOutlineItem());
    item->setTitle(title);
    item->setAction(PDFActionPtr(new PDFActionGoTo(destination, PDFDestination())));
    return item;
}

/// Lists the bookmarks as "title:page" (page from 1, 0 - none), children in brackets
QString describeOutline(const PDFDocument& document, const PDFOutlineItem* item)
{
    QStringList parts;
    for (size_t i = 0; i < item->getChildCount(); ++i)
    {
        const PDFOutlineItem* child = item->getChild(i);
        int page = 0;
        if (const PDFActionGoTo* action = dynamic_cast<const PDFActionGoTo*>(child->getAction()))
        {
            PDFDestination destination = action->getDestination();
            if (destination.isNamedDestination())
            {
                const PDFDestination* named = document.getCatalog()->getNamedDestination(destination.getName());
                destination = named ? *named : PDFDestination();
            }
            page = int(document.getCatalog()->getPageIndexFromPageReference(destination.getPageReference())) + 1;
        }
        QString text = QString("%1:%2").arg(child->getTitle()).arg(page);
        if (child->getChildCount() > 0)
        {
            text += "[" + describeOutline(document, child) + "]";
        }
        parts << text;
    }
    return parts.join(' ');
}

}   // namespace

void PageOperationsTest::insertDocumentKeepsBookmarks()
{
    // The document with bookmarks to its first and last page
    PDFDocument document;
    {
        const PDFDocument plain = createDocument(3);
        PDFDocumentBuilder builder(&plain);
        const std::vector<PDFObjectReference> pages = builder.getPages();
        QSharedPointer<PDFOutlineItem> root(new PDFOutlineItem());
        root->addChild(createBookmark("A1", PDFDestination::createFit(pages[0])));
        root->addChild(createBookmark("A3", PDFDestination::createFit(pages[2])));
        builder.setOutline(root.data());
        document = builder.build();
    }

    // The inserted document has nested bookmarks, and one to a named destination
    PDFDocument other;
    {
        const PDFDocument plain = createDocument(2, "B");
        PDFDocumentBuilder builder(&plain);
        const std::vector<PDFObjectReference> pages = builder.getPages();

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Dests");
        factory.beginDictionary();
        factory.beginDictionaryItem("second");
        factory << PDFDestination::createFit(pages[1]);
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(builder.getCatalogReference(), factory.takeObject());

        QSharedPointer<PDFOutlineItem> root(new PDFOutlineItem());
        QSharedPointer<PDFOutlineItem> chapter = createBookmark("B1", PDFDestination::createFit(pages[0]));
        chapter->addChild(createBookmark("B1.1", PDFDestination::createFit(pages[1])));
        root->addChild(chapter);
        root->addChild(createBookmark("Named", PDFDestination::createNamed("second")));
        builder.setOutline(root.data());
        other = builder.build();
    }
    QCOMPARE(describeOutline(other, other.getCatalog()->getOutlineRootPtr().data()), QString("B1:1[B1.1:2] Named:2"));

    // Inserted after the second page: the bookmark of the file is put between A1 and A3
    PDFDocument result;
    QVERIFY(PDFPageOperations::insertDocument(&document, &other, 2, &result, "Other file"));
    const PDFDocument reread = read(write(result));
    QCOMPARE(describeOutline(reread, reread.getCatalog()->getOutlineRootPtr().data()), QString("A1:1 Other file:3[B1:3[B1.1:4] Named:4] A3:5"));

    // At the end, and with no title given (the inserted document has none)
    QVERIFY(PDFPageOperations::insertDocument(&document, &other, 3, &result));
    QCOMPARE(describeOutline(result, result.getCatalog()->getOutlineRootPtr().data()), QString("A1:1 A3:3 Inserted pages:4[B1:4[B1.1:5] Named:5]"));

    // A document without bookmarks gets the bookmark of the inserted one
    const PDFDocument plain = createDocument(2);
    QVERIFY(PDFPageOperations::insertDocument(&plain, &other, 0, &result, "Front"));
    QCOMPARE(describeOutline(result, result.getCatalog()->getOutlineRootPtr().data()), QString("Front:1[B1:1[B1.1:2] Named:2]"));

    // A document without bookmarks inserted: the bookmarks of the document stay as they were
    QVERIFY(PDFPageOperations::insertDocument(&document, &plain, 0, &result));
    QCOMPARE(describeOutline(result, result.getCatalog()->getOutlineRootPtr().data()), QString("A1:3 A3:5"));
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
