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

#include "pdffireembeddedfont.h"
#include "pdffirepageoperations.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdftextlayoutgenerator.h"

#include <QtTest>
#include <QBuffer>
#include <QFontDatabase>
#include <QRawFont>

using namespace pdf;

class EmbeddedFontTest : public QObject
{
    Q_OBJECT

private slots:
    void fontFileIsValid();
    void textIsExtractable();
    void missingCharacterIsRefused();
};

namespace
{

QRawFont systemFont()
{
    // A TrueType font, which is always present on the build machine
    QFont font(QStringLiteral("DejaVu Sans"));
    return QRawFont::fromFont(font);
}

}   // namespace

void EmbeddedFontTest::fontFileIsValid()
{
    const QRawFont rawFont = systemFont();
    QVERIFY(rawFont.isValid());
    const QByteArray file = PDFFireEmbeddedFont::createFontFile(rawFont);
    QVERIFY(file.size() > 1000);

    // The reassembled file is a font Qt can load again, with the same glyphs
    const QRawFont reloaded(file, 12);
    QVERIFY(reloaded.isValid());
    QCOMPARE(reloaded.glyphIndexesForString("Fire"), rawFont.glyphIndexesForString("Fire"));
}

void EmbeddedFontTest::textIsExtractable()
{
    PDFFireEmbeddedFont font;
    QVERIFY(font.initialize(systemFont()));
    QByteArray glyphs;
    const QString text = QString::fromUtf8("Fire Drill été 2026");
    QVERIFY(font.addText(text, &glyphs));
    QCOMPARE(glyphs.size(), text.size() * 2);

    // A page with the text in the embedded font
    PDFDocument document = PDFPageOperations::createBlankDocument(QSizeF(612, 792));
    PDFDocumentBuilder builder(&document);
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();
    QByteArray content = "BT /PFE1 14 Tf 72 700 Td <" + glyphs.toHex() + "> Tj ET";
    const PDFObjectReference contents = builder.addObject(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(), std::move(content))));
    PDFDictionaryBuilder fonts;
    fonts.setEntry(PDFInplaceOrMemoryString("PFE1"), PDFObject::createReference(builder.addObject(builder.replaceNestedStreamsByReferences(font.createFontObject()))));
    PDFDictionaryBuilder resources;
    resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::move(fonts)));
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << contents;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory << PDFObject::createDictionary(std::move(resources));
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(page, factory.takeObject());

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    const PDFDocument built = builder.build();
    PDFDocumentWriter(nullptr).write(&buffer, &built);
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    const PDFDocument result = reader.readFromBuffer(buffer.data());
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&result, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&result), &activity));
    PDFTextLayoutGenerator generator(PDFRenderer::IgnoreOptionalContent, result.getCatalog()->getPage(0), &result, &fontCache, &cms, &activity, QTransform(), PDFMeshQualitySettings());
    const QList<PDFRenderError> errors = generator.processContents();
    for (const PDFRenderError& error : errors)
    {
        QVERIFY2(error.type != RenderErrorType::Error, qPrintable(error.message));
    }

    QString extracted;
    const PDFTextLayout layout = generator.createTextLayout();
    for (const PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            for (const TextCharacter& character : line.getCharacters())
            {
                extracted += character.character;
            }
        }
    }

    // The layout leaves the spaces out
    QString expected = text;
    expected.remove(QChar(' '));
    QCOMPARE(extracted, expected);
}

void EmbeddedFontTest::missingCharacterIsRefused()
{
    PDFFireEmbeddedFont font;
    QVERIFY(font.initialize(systemFont()));
    QByteArray glyphs;
    // A character of a private use area is not in the font
    QVERIFY(!font.addText(QString(QChar(0xF8FF)) + QStringLiteral("x"), &glyphs));
}

QTEST_MAIN(EmbeddedFontTest)

#include "tst_embeddedfonttest.moc"
