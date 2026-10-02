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

// PDF Fire: tests of "Export to Word Processor (ODT)" - the ZIP package, the XML parts,
// the paragraph building from positioned text, and a whole conversion of a PDF.

#include "odtlayout.h"
#include "odtwriter.h"
#include "odtzipwriter.h"
#include "pdftoodtconverter.h"

#include "pdfdocumentreader.h"

#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <map>
#include <zlib.h>

using namespace pdfplugin;

namespace
{

struct ZipEntry
{
    QString name;
    quint16 method = 0;
    quint16 localExtraLength = 0;
    qsizetype dataOffset = 0;
    QByteArray data;            ///< Uncompressed
    bool crcValid = false;
};

quint16 readUInt16(const QByteArray& data, qsizetype offset)
{
    return quint16(quint8(data[offset])) | quint16(quint8(data[offset + 1])) << 8;
}

quint32 readUInt32(const QByteArray& data, qsizetype offset)
{
    return quint32(readUInt16(data, offset)) | quint32(readUInt16(data, offset + 2)) << 16;
}

QByteArray inflateRaw(const QByteArray& compressed, qsizetype size)
{
    QByteArray result(size, Qt::Uninitialized);
    z_stream stream = { };
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
        return QByteArray();
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    stream.avail_in = uInt(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(result.data());
    stream.avail_out = uInt(result.size());
    const int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return status == Z_STREAM_END ? result : QByteArray();
}

/// A small ZIP reader - reads the central directory and the entries, as any reader does
std::vector<ZipEntry> readZip(const QByteArray& zip)
{
    std::vector<ZipEntry> entries;
    const qsizetype endOffset = zip.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (endOffset < 0)
    {
        return entries;
    }

    const quint16 count = readUInt16(zip, endOffset + 10);
    qsizetype offset = readUInt32(zip, endOffset + 16);
    for (quint16 i = 0; i < count; ++i)
    {
        if (readUInt32(zip, offset) != 0x02014b50)
        {
            return { };
        }

        ZipEntry entry;
        entry.method = readUInt16(zip, offset + 10);
        const quint32 crc = readUInt32(zip, offset + 16);
        const quint32 compressedSize = readUInt32(zip, offset + 20);
        const quint32 size = readUInt32(zip, offset + 24);
        const quint16 nameLength = readUInt16(zip, offset + 28);
        const quint16 extraLength = readUInt16(zip, offset + 30);
        const quint16 commentLength = readUInt16(zip, offset + 32);
        const quint32 localOffset = readUInt32(zip, offset + 42);
        entry.name = QString::fromUtf8(zip.mid(offset + 46, nameLength));

        // Local header must agree
        if (readUInt32(zip, localOffset) != 0x04034b50 || zip.mid(localOffset + 30, readUInt16(zip, localOffset + 26)) != entry.name.toUtf8())
        {
            return { };
        }
        entry.localExtraLength = readUInt16(zip, localOffset + 28);
        entry.dataOffset = localOffset + 30 + readUInt16(zip, localOffset + 26) + entry.localExtraLength;

        const QByteArray raw = zip.mid(entry.dataOffset, compressedSize);
        entry.data = entry.method == 8 ? inflateRaw(raw, size) : raw;
        entry.crcValid = entry.data.size() == qsizetype(size) && OdtZipWriter::crc32(entry.data) == crc;
        entries.push_back(entry);

        offset += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
}

QString xmlError(const QByteArray& xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd())
    {
        reader.readNext();
    }
    return reader.hasError() ? reader.errorString() + QString(" at line %1 column %2").arg(reader.lineNumber()).arg(reader.columnNumber()) : QString();
}

/// A synthetic line: each character is 0.5 em wide, a space is a 0.25 em gap
OdtLayoutLine makeLine(const QString& text, double x, double baseline, double size, int style = 0, int block = 0)
{
    OdtLayoutLine line;
    line.block = block;
    for (QChar character : text)
    {
        if (character == QChar(' '))
        {
            x += 0.25 * size;
            continue;
        }
        OdtLayoutCharacter layoutCharacter;
        layoutCharacter.character = character;
        layoutCharacter.x = x;
        layoutCharacter.right = x + 0.5 * size;
        layoutCharacter.baseline = baseline;
        layoutCharacter.fontSize = size;
        layoutCharacter.style = style;
        line.characters.push_back(layoutCharacter);
        x += 0.5 * size;
    }
    return line;
}

std::vector<OdtCharacterStyle> testStyles()
{
    OdtCharacterStyle regular;
    regular.fontFamily = "Carlito";
    OdtCharacterStyle bold = regular;
    bold.bold = true;
    bold.color = QColor(0x2F, 0x54, 0x96);
    OdtCharacterStyle italic = regular;
    italic.italic = true;
    return { regular, bold, italic };
}

OdtDocument sampleDocument()
{
    OdtDocument document;
    document.title = "Sample & <test>";
    document.bodyStyle.text.fontFamily = "Carlito";
    document.bodyStyle.text.fontSize = 11.0;
    document.headingStyles[0].text = document.bodyStyle.text;
    document.headingStyles[0].text.fontSize = 16.0;
    document.headingStyles[0].text.bold = true;

    OdtImage image;
    image.fileName = "Pictures/image1.png";
    image.mimeType = "image/png";
    QImage picture(8, 8, QImage::Format_RGB32);
    picture.fill(Qt::red);
    QBuffer buffer(&image.data);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    document.images.push_back(image);

    OdtParagraph heading;
    heading.headingLevel = 1;
    heading.spans.push_back(OdtSpan{ "Article 1 & <Members>", document.headingStyles[0].text, OdtSpan::Kind::Text });
    document.body.push_back(heading);

    OdtParagraph paragraph;
    OdtTextStyle italic = document.bodyStyle.text;
    italic.italic = true;
    paragraph.spans.push_back(OdtSpan{ "Plain text,  two spaces\tand a tab, ", document.bodyStyle.text, OdtSpan::Kind::Text });
    paragraph.spans.push_back(OdtSpan{ QString("italic") + QChar(0x0001) + QString(" text"), italic, OdtSpan::Kind::Text });
    document.body.push_back(paragraph);

    OdtParagraph listItem;
    listItem.listBullet = QChar(0x2022);
    listItem.marginLeft = 18.0;
    listItem.textIndent = -18.0;
    listItem.spans.push_back(OdtSpan{ "A list item", document.bodyStyle.text, OdtSpan::Kind::Text });
    document.body.push_back(listItem);

    OdtParagraph imageParagraph;
    imageParagraph.imageIndex = 0;
    imageParagraph.imageWidth = 50;
    imageParagraph.imageHeight = 50;
    imageParagraph.pageBreakBefore = true;
    document.body.push_back(imageParagraph);

    OdtParagraph footer;
    footer.alignment = OdtAlignment::Center;
    footer.spans.push_back(OdtSpan{ "Page ", document.bodyStyle.text, OdtSpan::Kind::Text });
    footer.spans.push_back(OdtSpan{ "1", document.bodyStyle.text, OdtSpan::Kind::PageNumber });
    document.footer.push_back(footer);
    document.page.footerHeight = 20;

    OdtPageImage logo;
    logo.imageIndex = 0;
    logo.x = 40;
    logo.y = 30;
    logo.width = 60;
    logo.height = 70;
    document.headerImages.push_back(logo);
    document.page.headerHeight = 60;
    return document;
}

}   // namespace

class OdtExportTest : public QObject
{
    Q_OBJECT

private slots:
    void testCrc32();
    void testZipStructure();
    void testManifestListsAllFiles();
    void testXmlWellFormed();
    void testLineText();
    void testParagraphBuilding();
    void testHeadersAndFooters();
    void testBulletedList();
    void testFontNames();
    void testDetails();
    void testConvertPdf();
    void testConvertFileFromEnvironment();
};

void OdtExportTest::testCrc32()
{
    // The check value of CRC-32 (ISO-HDLC)
    QCOMPARE(OdtZipWriter::crc32("123456789"), quint32(0xCBF43926));
    QCOMPARE(OdtZipWriter::crc32(QByteArray()), quint32(0));
}

void OdtExportTest::testZipStructure()
{
    const QByteArray package = OdtWriter::createPackage(sampleDocument());

    // "mimetype" is the first file, stored, no extra field - its content is at offset 38
    QCOMPARE(readUInt32(package, 0), quint32(0x04034b50));
    QCOMPARE(readUInt16(package, 8), quint16(0));
    QCOMPARE(readUInt16(package, 28), quint16(0));
    QCOMPARE(package.mid(30, 8), QByteArray("mimetype"));
    QCOMPARE(package.mid(38, 39), QByteArray("application/vnd.oasis.opendocument.text"));

    const std::vector<ZipEntry> entries = readZip(package);
    QVERIFY(!entries.empty());
    QCOMPARE(entries.front().name, QString("mimetype"));
    QCOMPARE(entries.front().method, quint16(0));

    for (const ZipEntry& entry : entries)
    {
        QVERIFY2(entry.crcValid, qPrintable(entry.name));
        QVERIFY2(entry.method == 0 || entry.method == 8, qPrintable(entry.name));
    }

    // The XML parts are compressed
    auto content = std::find_if(entries.begin(), entries.end(), [](const ZipEntry& entry) { return entry.name == "content.xml"; });
    QVERIFY(content != entries.end());
    QCOMPARE(content->method, quint16(8));

    // DEFLATE round trip of a large text
    QByteArray text;
    for (int i = 0; i < 5000; ++i)
    {
        text += "Firefighters shall maintain a professional attitude. ";
    }
    const QByteArray compressed = OdtZipWriter::deflateRaw(text);
    QVERIFY(compressed.size() < text.size() / 10);
    QCOMPARE(inflateRaw(compressed, text.size()), text);
}

void OdtExportTest::testManifestListsAllFiles()
{
    const QByteArray package = OdtWriter::createPackage(sampleDocument());
    const std::vector<ZipEntry> entries = readZip(package);

    QSet<QString> files;
    QByteArray manifest;
    for (const ZipEntry& entry : entries)
    {
        if (entry.name == "META-INF/manifest.xml")
        {
            manifest = entry.data;
        }
        else if (entry.name != "mimetype")
        {
            files.insert(entry.name);
        }
    }
    QVERIFY(!manifest.isEmpty());
    QVERIFY(files.contains("Pictures/image1.png"));

    QSet<QString> listed;
    bool hasRoot = false;
    QXmlStreamReader reader(manifest);
    while (!reader.atEnd())
    {
        if (reader.readNext() == QXmlStreamReader::StartElement && reader.name() == QLatin1String("file-entry"))
        {
            const QString path = reader.attributes().value("urn:oasis:names:tc:opendocument:xmlns:manifest:1.0", "full-path").toString();
            const QString mediaType = reader.attributes().value("urn:oasis:names:tc:opendocument:xmlns:manifest:1.0", "media-type").toString();
            if (path == "/")
            {
                hasRoot = true;
                QCOMPARE(mediaType, QString("application/vnd.oasis.opendocument.text"));
            }
            else
            {
                QVERIFY(!mediaType.isEmpty());
                listed.insert(path);
            }
        }
    }
    QVERIFY(!reader.hasError());
    QVERIFY(hasRoot);
    QCOMPARE(listed, files);
}

void OdtExportTest::testXmlWellFormed()
{
    const OdtDocument document = sampleDocument();
    for (const QByteArray& xml : { OdtWriter::createContentXml(document), OdtWriter::createStylesXml(document), OdtWriter::createMetaXml(document), OdtWriter::createManifestXml(document) })
    {
        const QString error = xmlError(xml);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    const QByteArray content = OdtWriter::createContentXml(document);
    QVERIFY(content.contains("Article 1 &amp; &lt;Members&gt;"));
    QVERIFY(content.contains("<text:h "));
    QVERIFY(content.contains("text:outline-level=\"1\""));
    QVERIFY(content.contains("<text:tab/>"));
    QVERIFY(content.contains("<text:s/>"));
    QVERIFY(content.contains("<text:list "));
    QVERIFY(content.contains("fo:break-before=\"page\""));
    QVERIFY(content.contains("xlink:href=\"Pictures/image1.png\""));
    QVERIFY(!content.contains(char(0x01)));

    const QByteArray styles = OdtWriter::createStylesXml(document);
    QVERIFY(styles.contains("<text:page-number"));
    QVERIFY(styles.contains("style:vertical-rel=\"page\""));
    QVERIFY(styles.contains("style:display-name=\"Heading 1\""));
    QVERIFY(styles.contains("fo:page-width=\"612.00pt\""));
}

void OdtExportTest::testLineText()
{
    OdtLayoutLine line = makeLine("Fire Department", 72, 100, 10);
    QCOMPARE(OdtLayoutBuilder::getLineText(line).text, QString("Fire Department"));

    // A wide gap is a tab
    OdtLayoutLine wide = makeLine("Name", 72, 100, 10);
    OdtLayoutLine value = makeLine("Value", 200, 100, 10);
    wide.characters.insert(wide.characters.end(), value.characters.begin(), value.characters.end());
    QCOMPARE(OdtLayoutBuilder::getLineText(wide).text, QString("Name\tValue"));

    // A glyph printed twice at almost the same place (fake bold) is one character
    OdtLayoutLine doubled = makeLine("AB", 72, 100, 10);
    OdtLayoutCharacter copy = doubled.characters[1];
    copy.x += 0.3;
    copy.right += 0.3;
    doubled.characters.push_back(copy);
    QCOMPARE(OdtLayoutBuilder::getLineText(doubled).text, QString("AB"));
}

void OdtExportTest::testParagraphBuilding()
{
    // Heading, a paragraph of three lines (ragged), a second paragraph after a gap,
    // a hyphenated word at a line end, a short line which ends a paragraph without a gap
    OdtLayoutPage page;
    const double size = 11.0;
    const double pitch = 13.4;
    double y = 100;
    page.lines.push_back(makeLine("Article 1 Members", 72, y, 16.0, 1, 0));
    y += 26;
    page.lines.push_back(makeLine("Upon acceptance by the department membership the", 72, y, size, 0, 1));
    y += pitch;
    page.lines.push_back(makeLine("individual will begin a minimum probationary fire-", 72, y, size, 0, 1));
    y += pitch;
    page.lines.push_back(makeLine("fighters period of three months.", 72, y, size, 0, 1));
    y += pitch * 1.8;
    page.lines.push_back(makeLine("New firefighters must attend at least three meetings", 72, y, size, 0, 1));
    y += pitch;
    page.lines.push_back(makeLine("to vote.", 72, y, size, 0, 1));
    y += pitch;
    page.lines.push_back(makeLine("Visitors are permitted in the station.", 72, y, size, 2, 1));

    OdtLayoutBuilder::Settings settings;
    OdtDocument document = OdtLayoutBuilder::build({ page }, testStyles(), { }, settings);

    QCOMPARE(document.body.size(), size_t(4));
    QCOMPARE(document.body[0].headingLevel, 1);
    QCOMPARE(document.body[0].getPlainText(), QString("Article 1 Members"));
    QCOMPARE(document.headingStyles[0].text.fontSize, 16.0);
    QVERIFY(document.headingStyles[0].text.bold);
    QCOMPARE(document.headingStyles[0].text.color, QColor(0x2F, 0x54, 0x96));

    QCOMPARE(document.body[1].headingLevel, 0);
    QCOMPARE(document.body[1].getPlainText(), QString("Upon acceptance by the department membership the individual will begin a minimum probationary firefighters period of three months."));
    QCOMPARE(document.body[2].getPlainText(), QString("New firefighters must attend at least three meetings to vote."));
    QVERIFY(document.body[2].spaceBefore > 5.0);

    // "to vote." ends short - the next line is a new paragraph although there is no gap
    QCOMPARE(document.body[3].getPlainText(), QString("Visitors are permitted in the station."));
    QCOMPARE(document.body[3].spaceBefore, 0.0);
    QVERIFY(document.body[3].spans.front().style.italic);

    QCOMPARE(document.bodyStyle.text.fontSize, 11.0);
    QCOMPARE(document.bodyStyle.text.fontFamily, QString("Carlito"));
    QVERIFY(qAbs(document.page.marginLeft - 72.0) < 0.01);

    // Indented first line: a new paragraph, its continuation lines are not new ones
    OdtLayoutPage indented;
    y = 100;
    indented.lines.push_back(makeLine("This paragraph has an indented first line and", 90, y, size));
    y += pitch;
    indented.lines.push_back(makeLine("continues on the second line which is longer.", 72, y, size));
    y += pitch;
    indented.lines.push_back(makeLine("And this is another paragraph with a first line", 90, y, size));
    y += pitch;
    indented.lines.push_back(makeLine("indentation, as old typewriters used to do it.", 72, y, size));
    document = OdtLayoutBuilder::build({ indented }, testStyles(), { }, settings);
    QCOMPARE(document.body.size(), size_t(2));
    QVERIFY(qAbs(document.body[0].textIndent - 18.0) < 0.01);
    QCOMPARE(document.body[1].getPlainText(), QString("And this is another paragraph with a first line indentation, as old typewriters used to do it."));

    // Centered title
    OdtLayoutPage centered;
    centered.lines.push_back(makeLine("A long line of the text which sets the margins of the page body", 72, 100, size));
    centered.lines.push_back(makeLine("TITLE", 72 + (makeLine("A long line of the text which sets the margins of the page body", 72, 100, size).right() - 72) / 2.0 - 13.75, 140, size));
    document = OdtLayoutBuilder::build({ centered }, testStyles(), { }, settings);
    QCOMPARE(document.body.size(), size_t(2));
    QCOMPARE(document.body[1].alignment, OdtAlignment::Center);
}

void OdtExportTest::testHeadersAndFooters()
{
    std::vector<OdtLayoutPage> pages;
    for (int i = 0; i < 3; ++i)
    {
        OdtLayoutPage page;
        page.pageNumber = i + 1;
        page.lines.push_back(makeLine("Prospect VFD", 400, 60, 10, 1));
        page.lines.push_back(makeLine(QString("Body text of the page number %1 which is long enough").arg(i + 1), 72, 150, 11));
        page.lines.push_back(makeLine(QString::number(i + 1), 300, 760, 10));
        OdtLayoutImage logo;
        logo.imageIndex = 0;
        logo.rect = QRectF(40, 30, 60, 70);
        page.images.push_back(logo);
        pages.push_back(page);
    }

    std::vector<OdtImage> images(1);
    images[0].fileName = "Pictures/image1.png";
    images[0].mimeType = "image/png";
    images[0].data = "x";

    OdtLayoutBuilder::Settings settings;
    settings.documentPageCount = 3;
    OdtDocument document = OdtLayoutBuilder::build(pages, testStyles(), images, settings);

    QCOMPARE(document.header.size(), size_t(1));
    QCOMPARE(document.header[0].getPlainText(), QString("Prospect VFD"));
    QCOMPARE(document.footer.size(), size_t(1));
    QCOMPARE(document.footer[0].spans.size(), size_t(1));
    QCOMPARE(document.footer[0].spans[0].kind, OdtSpan::Kind::PageNumber);
    QCOMPARE(document.headerImages.size(), size_t(1));
    QCOMPARE(document.headerImages[0].x, 40.0);

    // Only the body text stays in the body, one page break per page
    QCOMPARE(document.body.size(), size_t(3));
    QVERIFY(!document.body[0].pageBreakBefore);
    QVERIFY(document.body[1].pageBreakBefore);
    QVERIFY(document.body[2].pageBreakBefore);
    QVERIFY(document.page.headerHeight > 0.0);
    QVERIFY(document.page.footerHeight > 0.0);

    // Without the option, everything stays in the body
    settings.moveHeadersAndFooters = false;
    document = OdtLayoutBuilder::build(pages, testStyles(), images, settings);
    QVERIFY(document.header.empty());
    QVERIFY(document.footer.empty());
    QCOMPARE(document.body.size(), size_t(12));
}

void OdtExportTest::testBulletedList()
{
    // The bullets are in their own column (the layout analysis made a block of them)
    OdtLayoutPage page;
    const double size = 11.0;
    page.lines.push_back(makeLine("The members shall:", 72, 100, size, 0, 0));
    page.lines.push_back(makeLine(QString(QChar(0x2022)), 90, 114, size, 0, 1));
    page.lines.push_back(makeLine(QString(QChar(0x2022)), 90, 128, size, 0, 1));
    page.lines.push_back(makeLine("Read the guidelines of the department and learn them", 108, 114, size, 0, 2));
    page.lines.push_back(makeLine("Attend the drills", 108, 128, size, 0, 2));

    OdtDocument document = OdtLayoutBuilder::build({ page }, testStyles(), { }, OdtLayoutBuilder::Settings());
    QCOMPARE(document.body.size(), size_t(3));
    QCOMPARE(document.body[1].listBullet, QChar(0x2022));
    QCOMPARE(document.body[1].getPlainText(), QString("Read the guidelines of the department and learn them"));
    QCOMPARE(document.body[2].listBullet, QChar(0x2022));
    QCOMPARE(document.body[2].getPlainText(), QString("Attend the drills"));
    QVERIFY(qAbs(document.body[1].marginLeft - 36.0) < 0.01);
    QVERIFY(qAbs(document.body[1].textIndent + 18.0) < 0.01);

    // A dash glued to the word is not a bullet ("-SCBA: Self Contained Breathing Apparatus")
    QVERIFY(!OdtLayoutBuilder::startsWithBullet("-SCBA: Self Contained", nullptr));
    QVERIFY(OdtLayoutBuilder::startsWithBullet("- SCBA: Self Contained", nullptr));
    QChar bullet;
    QVERIFY(OdtLayoutBuilder::startsWithBullet(QString(QChar(0xF0B7)) + " Symbol bullet", &bullet));
    QCOMPARE(bullet, QChar(0x2022));

    const QString error = xmlError(OdtWriter::createContentXml(document));
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void OdtExportTest::testFontNames()
{
    QString family;
    bool bold = false;
    bool italic = false;

    PdfToOdtConverter::parseFontName("BAAAAA+Carlito-Bold", &family, &bold, &italic);
    QCOMPARE(family, QString("Carlito"));
    QVERIFY(bold && !italic);

    PdfToOdtConverter::parseFontName("TimesNewRomanPS-BoldItalicMT", &family, &bold, &italic);
    QCOMPARE(family, QString("Times New Roman"));
    QVERIFY(bold && italic);

    PdfToOdtConverter::parseFontName("Arial,Italic", &family, &bold, &italic);
    QCOMPARE(family, QString("Arial"));
    QVERIFY(!bold && italic);

    PdfToOdtConverter::parseFontName("ArialMT", &family, &bold, &italic);
    QCOMPARE(family, QString("Arial"));
    QVERIFY(!bold && !italic);

    PdfToOdtConverter::parseFontName("Helvetica-Oblique", &family, &bold, &italic);
    QCOMPARE(family, QString("Helvetica"));
    QVERIFY(italic);

    PdfToOdtConverter::parseFontName("CalibriBold", &family, &bold, &italic);
    QCOMPARE(family, QString("Calibri"));
    QVERIFY(bold);
}

void OdtExportTest::testDetails()
{
    const double size = 11.0;
    OdtLayoutBuilder::Settings settings;

    // Table of contents entries are as large as the headings, but they are not headings
    {
        OdtLayoutPage page;
        page.lines.push_back(makeLine("Article 1 Members", 72, 100, 16.0, 1, 0));
        page.lines.push_back(makeLine(QString("Article 2 Fire Station") + QChar(0x2026) + " 4", 72, 140, 16.0, 1, 1));
        page.lines.push_back(makeLine("Body text which is the most common text of the page by far.", 72, 180, size, 0, 2));
        const OdtDocument document = OdtLayoutBuilder::build({ page }, testStyles(), { }, settings);
        QCOMPARE(document.body.size(), size_t(3));
        QCOMPARE(document.body[0].headingLevel, 1);
        QCOMPARE(document.body[1].headingLevel, 0);
    }

    // Hyphen at a line end: a split word is joined, a compound word keeps its hyphen
    {
        OdtLayoutPage page;
        double y = 100;
        page.lines.push_back(makeLine("The element is a machine and data are generated by the", 72, y, size)); y += 13.4;
        page.lines.push_back(makeLine("parser for each ele-", 72, y, size)); y += 13.4;
        page.lines.push_back(makeLine("ment of the input which is machine-", 72, y, size)); y += 13.4;
        page.lines.push_back(makeLine("generated by a program.", 72, y, size));
        const OdtDocument document = OdtLayoutBuilder::build({ page }, testStyles(), { }, settings);
        QCOMPARE(document.body.size(), size_t(1));
        const QString text = document.body[0].getPlainText();
        QVERIFY2(text.contains("each element of"), qPrintable(text));
        QVERIFY2(text.contains("machine-generated by"), qPrintable(text));
        QVERIFY(qAbs(document.body[0].lineHeight - 13.4) < 0.01);
    }

    // A wide gap is a tab with a tab stop at the place of the text after it
    {
        OdtLayoutPage page;
        OdtLayoutLine line = makeLine("Name", 72, 100, size);
        OdtLayoutLine value = makeLine("Value", 272, 100, size);
        line.characters.insert(line.characters.end(), value.characters.begin(), value.characters.end());
        page.lines.push_back(line);
        page.lines.push_back(makeLine("A long line of the text, which sets the right margin of the page body text area", 72, 130, size));
        const OdtDocument document = OdtLayoutBuilder::build({ page }, testStyles(), { }, settings);
        QCOMPARE(document.body[0].getPlainText(), QString("Name\tValue"));
        QCOMPARE(document.body[0].tabStops.size(), size_t(1));
        QVERIFY(qAbs(document.body[0].tabStops[0] - 200.0) < 0.01);
        QVERIFY(OdtWriter::createContentXml(document).contains("style:tab-stop style:position=\"200.00pt\""));
    }

    // Running headers with a varying text and the page number: out of the body, a page
    // number field in the header instead
    {
        std::vector<OdtLayoutPage> pages;
        for (int i = 0; i < 4; ++i)
        {
            OdtLayoutPage page;
            page.pageNumber = i + 1;
            page.lines.push_back(makeLine(QString("Chapter %1: Topic %2 page %3").arg(i).arg(QChar('A' + i)).arg(i + 1), 72, 50, 10));
            page.lines.push_back(makeLine("Body text of the page", 72, 150, size));
            pages.push_back(page);
        }
        const OdtDocument document = OdtLayoutBuilder::build(pages, testStyles(), { }, settings);
        QCOMPARE(document.body.size(), size_t(4));
        QCOMPARE(document.header.size(), size_t(1));
        QCOMPARE(document.header[0].spans.size(), size_t(1));
        QCOMPARE(document.header[0].spans[0].kind, OdtSpan::Kind::PageNumber);
    }

    // Fonts not installed are replaced by an installed font of the same kind
    const QStringList installed = { "Carlito", "Times New Roman", "Arial", "Liberation Mono" };
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("Carlito", "Carlito-Bold", false, false, installed), QString("Carlito"));
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("CMR10", "CMR10", false, false, installed), QString("Times New Roman"));
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("CMTT12", "CMTT12", false, false, installed), QString("Liberation Mono"));
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("Frutiger", "Frutiger", false, false, installed), QString("Arial"));
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("CMR10", "CMR10", false, false, QStringList()), QString("CMR10"));
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("Calibri", "Calibri", false, false, installed), QString("Calibri"));
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("Cambria", "Cambria", true, false, installed), QString("Cambria"));
    QCOMPARE(PdfToOdtConverter::getSubstituteFamily("Helvetica", "Helvetica", false, false, installed), QString("Helvetica"));
}

void OdtExportTest::testConvertPdf()
{
    // A real PDF written by Qt: a heading, two paragraphs, an image
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdfFileName = directory.filePath("test.pdf");

    {
        QPdfWriter writer(pdfFileName);
        writer.setPageSize(QPageSize(QPageSize::Letter));
        writer.setResolution(72);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter(&writer);

        // A font with a real bold face on this computer (Windows has no DejaVu Sans)
        QFont heading(QFontDatabase::hasFamily("DejaVu Sans") ? QStringLiteral("DejaVu Sans") : QStringLiteral("Arial"));
        heading.setPixelSize(20);
        heading.setBold(true);
        painter.setFont(heading);
        painter.setPen(QColor(0x2F, 0x54, 0x96));
        painter.drawText(QPointF(72, 100), "Department Guidelines");

        QFont body(QFontDatabase::hasFamily("DejaVu Sans") ? QStringLiteral("DejaVu Sans") : QStringLiteral("Arial"));
        body.setPixelSize(10);
        painter.setFont(body);
        painter.setPen(Qt::black);
        painter.drawText(QPointF(72, 140), "The officers shall make every attempt to be present");
        painter.drawText(QPointF(72, 152), "during drills and calls.");
        painter.drawText(QPointF(72, 180), "Turnout gear should remain at the station.");

        QImage image(64, 48, QImage::Format_RGB32);
        image.fill(QColor(200, 30, 30));
        painter.drawImage(QRectF(72, 220, 128, 96), image);

        writer.newPage();
        painter.setFont(body);
        painter.drawText(QPointF(72, 100), "Second page text.");
        painter.end();
    }

    QFile file(pdfFileName);
    QVERIFY(file.open(QIODevice::ReadOnly));
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    pdf::PDFDocument pdfDocument = reader.readFromBuffer(file.readAll());
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    PdfToOdtConverter::Settings settings;
    settings.pages = { 0, 1 };
    settings.title = "Test";
    PdfToOdtConverter converter(pdfDocument, settings);
    converter.run();
    QVERIFY2(converter.getErrorMessage().isEmpty(), qPrintable(converter.getErrorMessage()));
    QVERIFY(!converter.getPackage().isEmpty());
    QCOMPARE(converter.getProgress(), converter.getMaximumProgress());

    const OdtDocument& document = converter.getDocument();
    QStringList texts;
    for (const OdtParagraph& paragraph : document.body)
    {
        texts << paragraph.getPlainText();
    }
    QVERIFY2(texts.contains("Department Guidelines"), qPrintable(texts.join(" | ")));
    QVERIFY2(texts.contains("The officers shall make every attempt to be present during drills and calls."), qPrintable(texts.join(" | ")));
    QVERIFY2(texts.contains("Turnout gear should remain at the station."), qPrintable(texts.join(" | ")));
    QVERIFY2(texts.contains("Second page text."), qPrintable(texts.join(" | ")));

    auto heading = std::find_if(document.body.begin(), document.body.end(), [](const OdtParagraph& paragraph) { return paragraph.getPlainText() == "Department Guidelines"; });
    QCOMPARE(heading->headingLevel, 1);
    QVERIFY(document.headingStyles[0].text.bold);
    QCOMPARE(document.headingStyles[0].text.color, QColor(0x2F, 0x54, 0x96));

    // The image, between the paragraphs, at its size
    QCOMPARE(document.images.size(), size_t(1));
    auto image = std::find_if(document.body.begin(), document.body.end(), [](const OdtParagraph& paragraph) { return paragraph.imageIndex == 0; });
    QVERIFY(image != document.body.end());
    QVERIFY(qAbs(image->imageWidth - 128.0) < 1.0);
    QVERIFY(qAbs(image->imageHeight - 96.0) < 1.0);
    QVERIFY(std::distance(document.body.begin(), image) > 2);

    auto secondPage = std::find_if(document.body.begin(), document.body.end(), [](const OdtParagraph& paragraph) { return paragraph.getPlainText() == "Second page text."; });
    QVERIFY(secondPage->pageBreakBefore);

    // Cancelled before it starts - nothing is produced
    PdfToOdtConverter cancelled(pdfDocument, settings);
    cancelled.cancel();
    cancelled.run();
    QVERIFY(cancelled.getPackage().isEmpty());
}

void OdtExportTest::testConvertFileFromEnvironment()
{
    // Manual check of a real document: PDFFIRE_ODT_INPUT=<copy of a PDF> PDFFIRE_ODT_OUTPUT=<file.odt>
    const QString input = qEnvironmentVariable("PDFFIRE_ODT_INPUT");
    const QString output = qEnvironmentVariable("PDFFIRE_ODT_OUTPUT");
    if (input.isEmpty() || output.isEmpty())
    {
        QSKIP("PDFFIRE_ODT_INPUT and PDFFIRE_ODT_OUTPUT are not set");
    }

    QFile file(input);
    QVERIFY(file.open(QIODevice::ReadOnly));
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    pdf::PDFDocument pdfDocument = reader.readFromBuffer(file.readAll());
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    PdfToOdtConverter::Settings settings;
    for (size_t i = 0; i < pdfDocument.getCatalog()->getPageCount(); ++i)
    {
        settings.pages.push_back(pdf::PDFInteger(i));
    }
    settings.installedFontFamilies = QFontDatabase::families();
    PdfToOdtConverter converter(pdfDocument, settings);
    converter.run();
    QVERIFY2(converter.getErrorMessage().isEmpty(), qPrintable(converter.getErrorMessage()));

    QFile outputFile(output);
    QVERIFY(outputFile.open(QIODevice::WriteOnly));
    outputFile.write(converter.getPackage());
}

QTEST_MAIN(OdtExportTest)
#include "tst_odtexporttest.moc"
