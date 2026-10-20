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

#include "pdffirecovercheck.h"
#include "pdffirepageoperations.h"

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"

#include <QtTest>

using namespace pdf;

class CoverCheckTest : public QObject
{
    Q_OBJECT

private slots:
    void blackBoxOverText();
    void boxBeforeTextIsNotCover();
    void lightBoxIsNotCover();
    void translucentBoxIsNotCover();
    void boxOverOneWordOfLongLine();
    void pathRectangleIsCover();
    void invisibleTextUnderBox();
    void filledSquareComment();
    void contrast();

private:
    /// A page with Helvetica as /F1, whose content is the given one
    static PDFDocument createDocument(const QByteArray& content, bool addCommentBox = false);
    static std::vector<PDFFireCoverCheck::Finding> check(const PDFDocument& document);
};

PDFDocument CoverCheckTest::createDocument(const QByteArray& content, bool addCommentBox)
{
    const PDFDocument blank = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&blank);
    const PDFObjectReference page = builder.getPages().front();

    PDFDictionaryBuilder fontDictionary;
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type1"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("BaseFont"), PDFObject::createName("Helvetica"));
    fontDictionary.setEntry(PDFInplaceOrMemoryString("Encoding"), PDFObject::createName("WinAnsiEncoding"));
    const PDFObjectReference font = builder.addObject(PDFObject::createDictionary(std::move(fontDictionary)));

    PDFDictionaryBuilder transparent;
    transparent.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("ExtGState"));
    transparent.setEntry(PDFInplaceOrMemoryString("ca"), PDFObject::createReal(0.5));

    PDFDictionaryBuilder fonts;
    fonts.setEntry(PDFInplaceOrMemoryString("F1"), PDFObject::createReference(font));
    PDFDictionaryBuilder states;
    states.setEntry(PDFInplaceOrMemoryString("GS1"), PDFObject::createDictionary(std::move(transparent)));
    PDFDictionaryBuilder resources;
    resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
    resources.setEntry(PDFInplaceOrMemoryString("ExtGState"), PDFObject::createDictionary(std::move(states)));

    const PDFObjectReference stream = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(), QByteArray(content))));
    PDFDictionaryBuilder pageDictionary(*builder.getDictionaryFromObject(builder.getObjectByReference(page)));
    pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(stream));
    pageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
    builder.setObject(page, PDFObject::createDictionary(std::move(pageDictionary)));

    if (addCommentBox)
    {
        const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(70, 695, 120, 20), 1.0, Qt::black, Qt::black, "Tester", "Box", QString());
        builder.updateAnnotationAppearanceStreams(square);
    }

    return builder.build();
}

std::vector<PDFFireCoverCheck::Finding> CoverCheckTest::check(const PDFDocument& document)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    return PDFFireCoverCheck::check(&document, &fontCache, &cms, &activity, PDFMeshQualitySettings());
}

void CoverCheckTest::blackBoxOverText()
{
    const std::vector<PDFFireCoverCheck::Finding> findings = check(createDocument("BT /F1 12 Tf 72 700 Td (SECRETNAME) Tj ET 0 g 70 695 120 20 re f"));
    QCOMPARE(findings.size(), size_t(1));
    QCOMPARE(findings.front().pageIndex, PDFInteger(0));
    QCOMPARE(findings.front().kind, PDFFireCoverCheck::CoverKind::PageContent);
    QCOMPARE(findings.front().coveredText, QString("SECRETNAME"));
    QCOMPARE(findings.front().rect, QRectF(70, 695, 120, 20));
}

void CoverCheckTest::boxBeforeTextIsNotCover()
{
    // A dark banner with text on it (the text is painted after the box)
    QVERIFY(check(createDocument("0 g 70 695 120 20 re f BT /F1 12 Tf 1 g 72 700 Td (BANNER) Tj ET")).empty());
    QVERIFY(check(createDocument("0 g 70 695 120 20 re f BT /F1 12 Tf 0 g 72 700 Td (BANNER) Tj ET")).empty());
}

void CoverCheckTest::lightBoxIsNotCover()
{
    // Table shading over black text, the text stays readable
    QVERIFY(check(createDocument("BT /F1 12 Tf 72 700 Td (VISIBLE) Tj ET 0.95 g 70 695 120 20 re f")).empty());
}

void CoverCheckTest::translucentBoxIsNotCover()
{
    QVERIFY(check(createDocument("BT /F1 12 Tf 72 700 Td (VISIBLE) Tj ET /GS1 gs 0 g 70 695 120 20 re f")).empty());
}

void CoverCheckTest::boxOverOneWordOfLongLine()
{
    // Only the number is under the box - the rest of the line is visible
    const std::vector<PDFFireCoverCheck::Finding> findings =
        check(createDocument("BT /F1 12 Tf 72 700 Td (Name: John Smith, account) Tj ET BT /F1 12 Tf 300 700 Td (5550199) Tj ( phone and address on file) Tj ET 0 g 298 696 50 14 re f"));
    QCOMPARE(findings.size(), size_t(1));
    QCOMPARE(findings.front().coveredText, QString("5550199"));
}

void CoverCheckTest::pathRectangleIsCover()
{
    // A rectangle drawn as a closed path of lines, in a transformed coordinate system
    const std::vector<PDFFireCoverCheck::Finding> findings =
        check(createDocument("BT /F1 12 Tf 72 700 Td (HIDDEN) Tj ET q 2 0 0 2 0 0 cm 0 0 0 rg 35 347.5 m 95 347.5 l 95 357.5 l 35 357.5 l h f Q"));
    QCOMPARE(findings.size(), size_t(1));
    QCOMPARE(findings.front().coveredText, QString("HIDDEN"));
}

void CoverCheckTest::invisibleTextUnderBox()
{
    // Recognized (OCR) text is invisible; under a box drawn later it is still there
    const std::vector<PDFFireCoverCheck::Finding> findings = check(createDocument("BT 3 Tr /F1 12 Tf 72 700 Td (OCRTEXT) Tj ET 0.2 0.2 0.2 rg 70 695 120 20 re f"));
    QCOMPARE(findings.size(), size_t(1));
    QCOMPARE(findings.front().coveredText, QString("OCRTEXT"));
}

void CoverCheckTest::filledSquareComment()
{
    const std::vector<PDFFireCoverCheck::Finding> findings = check(createDocument("BT /F1 12 Tf 72 700 Td (UNDERCOMMENT) Tj ET", true));
    QCOMPARE(findings.size(), size_t(1));
    QCOMPARE(findings.front().kind, PDFFireCoverCheck::CoverKind::Comment);
    QCOMPARE(findings.front().coveredText, QString("UNDERCOMMENT"));
}

void CoverCheckTest::contrast()
{
    QVERIFY(qAbs(PDFFireCoverCheck::getContrast(Qt::black, Qt::white) - 21.0) < 0.01);
    QVERIFY(qAbs(PDFFireCoverCheck::getContrast(Qt::red, Qt::red) - 1.0) < 0.01);
    QVERIFY(PDFFireCoverCheck::getContrast(Qt::black, QColor(50, 50, 50)) < 3.0);
}

QTEST_GUILESS_MAIN(CoverCheckTest)

#include "tst_coverchecktest.moc"
