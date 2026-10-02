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

#include "ocrtextlayer.h"

#include "pdfrenderer.h"
#include "pdfstreamfilters.h"

#include <QtMath>

#include <cmath>

namespace pdfplugin
{

namespace
{

/// Formats a number for the content stream: fixed point, without trailing zeros
QByteArray formatNumber(double value)
{
    if (!std::isfinite(value))
    {
        return QByteArray("0");
    }

    QByteArray result = QByteArray::number(value, 'f', 4);
    if (result.contains('.'))
    {
        while (result.endsWith('0'))
        {
            result.chop(1);
        }
        if (result.endsWith('.'))
        {
            result.chop(1);
        }
    }

    if (result == "-0")
    {
        result = "0";
    }

    return result;
}

double length(const QPointF& vector)
{
    return std::hypot(vector.x(), vector.y());
}

double dot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

/// Text of the word, which can be written: control characters are dropped
QString cleanWordText(const QString& text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar character : text)
    {
        if (!character.isNull() && character.category() != QChar::Other_Control && !character.isSpace())
        {
            result.append(character);
        }
    }
    return result;
}

pdf::PDFObjectReference addStream(pdf::PDFDocumentBuilder* builder, QByteArray data, bool compress, std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries = {})
{
    pdf::PDFDictionaryBuilder dictionary;
    for (const auto& entry : entries)
    {
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
    }

    if (compress)
    {
        data = pdf::PDFFlateDecodeFilter::compress(data);
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createName("FlateDecode"));
    }

    dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
    return builder->addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(data))));
}

/// Adds the references of the content streams of the page to the list
void collectContentReferences(pdf::PDFDocumentBuilder* builder, std::vector<pdf::PDFObjectReference>& references, const pdf::PDFObject& contents)
{
    if (contents.isReference())
    {
        const pdf::PDFObject dereferenced = builder->getObject(contents);
        if (dereferenced.isStream())
        {
            references.push_back(contents.getReference());
            return;
        }

        if (dereferenced.isArray())
        {
            collectContentReferences(builder, references, dereferenced);
        }
        return;
    }

    if (contents.isArray())
    {
        // Copy the items first, the storage can change when a stream is added below
        std::vector<pdf::PDFObject> items;
        for (const pdf::PDFObject& item : *contents.getArray())
        {
            items.push_back(item);
        }

        for (const pdf::PDFObject& item : items)
        {
            collectContentReferences(builder, references, item);
        }
        return;
    }

    if (contents.isStream())
    {
        // A direct stream is not allowed by the specification, but it can be read - make it indirect
        references.push_back(builder->addObject(contents));
    }
}

/// Finds an inheritable attribute of the page (it can be in the page tree above the page)
pdf::PDFObject findInheritableAttribute(const pdf::PDFObjectStorage* storage, pdf::PDFObjectReference page, const char* key)
{
    pdf::PDFObject object = storage->getObjectByReference(page);
    for (int depth = 0; depth < 64; ++depth)
    {
        const pdf::PDFDictionary* dictionary = storage->getDictionaryFromObject(object);
        if (!dictionary)
        {
            break;
        }

        if (dictionary->hasKey(key))
        {
            return storage->getObject(dictionary->get(key));
        }

        object = storage->getObject(dictionary->get("Parent"));
    }

    return pdf::PDFObject();
}

}   // namespace

bool OcrPageText::isEmpty() const
{
    for (const OcrLine& line : lines)
    {
        for (const OcrWord& word : line.words)
        {
            if (!cleanWordText(word.text).isEmpty())
            {
                return false;
            }
        }
    }

    return true;
}

QString OcrPageText::toPlainText() const
{
    QStringList result;
    for (const OcrLine& line : lines)
    {
        QStringList words;
        for (const OcrWord& word : line.words)
        {
            words << word.text;
        }
        result << words.join(QChar(' '));
    }
    return result.join(QChar('\n'));
}

QByteArray OcrTextLayer::createToUnicodeCMap()
{
    // Every code (a UTF-16 code unit) maps to itself. The ranges must not cross
    // the boundary of the last byte, so there is one range for every first byte.
    QByteArray data;
    data.append("/CIDInit /ProcSet findresource begin\n"
                "12 dict begin\n"
                "begincmap\n"
                "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                "/CMapName /Adobe-Identity-UCS def\n"
                "/CMapType 2 def\n"
                "1 begincodespacerange\n"
                "<0000> <FFFF>\n"
                "endcodespacerange\n");

    constexpr int rangesPerSection = 100;   // The limit of the CMap syntax
    for (int first = 0; first < 256; first += rangesPerSection)
    {
        const int count = qMin(rangesPerSection, 256 - first);
        data.append(QByteArray::number(count) + " beginbfrange\n");
        for (int highByte = first; highByte < first + count; ++highByte)
        {
            const QByteArray prefix = QByteArray::number(highByte, 16).rightJustified(2, '0').toUpper();
            data.append("<" + prefix + "00> <" + prefix + "FF> <" + prefix + "00>\n");
        }
        data.append("endbfrange\n");
    }

    data.append("endcmap\n"
                "CMapName currentdict /CMap defineresource pop\n"
                "end\n"
                "end\n");
    return data;
}

pdf::PDFObjectReference OcrTextLayer::createFont(pdf::PDFDocumentBuilder* builder, const QByteArray& fontProgram)
{
    // PDF Fire: the idea of the glyph-less font comes from the PDF renderer of Tesseract
    // (an invisible layer of a font, whose codes are the Unicode code units); this is
    // an independent implementation using the object model of PDF4QT.
    pdf::PDFObjectReference fontFile;
    if (!fontProgram.isEmpty())
    {
        fontFile = addStream(builder, fontProgram, true, { { "Length1", pdf::PDFObject::createInteger(fontProgram.size()) } });
    }

    pdf::PDFObjectReference cidToGidMap;
    if (fontFile.isValid())
    {
        // Two bytes (big endian glyph index) for each of the 65536 codes, all glyph 1
        QByteArray map(65536 * 2, '\0');
        for (int code = 0; code < 65536; ++code)
        {
            map[code * 2 + 1] = 1;
        }
        cidToGidMap = addStream(builder, map, true);
    }

    pdf::PDFObjectFactory descriptor;
    descriptor.beginDictionary();
    descriptor.beginDictionaryItem("Type");
    descriptor << pdf::WrapName("FontDescriptor");
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("FontName");
    descriptor << pdf::WrapName("GlyphLessFont");
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("Flags");
    descriptor << pdf::PDFInteger(5);    // Fixed pitch, symbolic
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("FontBBox");
    descriptor << std::vector<pdf::PDFInteger>{ 0, -200, 500, 800 };
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("ItalicAngle");
    descriptor << pdf::PDFInteger(0);
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("Ascent");
    descriptor << pdf::PDFInteger(800);
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("Descent");
    descriptor << pdf::PDFInteger(-200);
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("CapHeight");
    descriptor << pdf::PDFInteger(800);
    descriptor.endDictionaryItem();
    descriptor.beginDictionaryItem("StemV");
    descriptor << pdf::PDFInteger(80);
    descriptor.endDictionaryItem();
    if (fontFile.isValid())
    {
        descriptor.beginDictionaryItem("FontFile2");
        descriptor << fontFile;
        descriptor.endDictionaryItem();
    }
    descriptor.endDictionary();
    const pdf::PDFObjectReference descriptorReference = builder->addObject(descriptor.takeObject());

    pdf::PDFObjectFactory cidFont;
    cidFont.beginDictionary();
    cidFont.beginDictionaryItem("Type");
    cidFont << pdf::WrapName("Font");
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("Subtype");
    cidFont << pdf::WrapName("CIDFontType2");
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("BaseFont");
    cidFont << pdf::WrapName("GlyphLessFont");
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("CIDSystemInfo");
    cidFont.beginDictionary();
    cidFont.beginDictionaryItem("Registry");
    cidFont << pdf::WrapString("Adobe");
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("Ordering");
    cidFont << pdf::WrapString("Identity");
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("Supplement");
    cidFont << pdf::PDFInteger(0);
    cidFont.endDictionaryItem();
    cidFont.endDictionary();
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("FontDescriptor");
    cidFont << descriptorReference;
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("DW");
    cidFont << pdf::PDFInteger(500);
    cidFont.endDictionaryItem();
    cidFont.beginDictionaryItem("CIDToGIDMap");
    if (fontFile.isValid())
    {
        // PDF Fire: the glyph-less font has two glyphs only - 0 (.notdef) and 1 (the empty
        // glyph of width 500). An /Identity map would select glyphs, which do not exist, for
        // nearly all codes; FreeType refuses to load them, and PDF4QT then drops the
        // characters (search and copy in PDF Fire would not see the text, although Poppler
        // does). So every code is mapped to glyph 1; the codes stay the Unicode code units.
        cidFont << cidToGidMap;
    }
    else
    {
        cidFont << pdf::WrapName("Identity");
    }
    cidFont.endDictionaryItem();
    cidFont.endDictionary();
    const pdf::PDFObjectReference cidFontReference = builder->addObject(cidFont.takeObject());

    const pdf::PDFObjectReference toUnicodeReference = addStream(builder, createToUnicodeCMap(), true);

    pdf::PDFObjectFactory font;
    font.beginDictionary();
    font.beginDictionaryItem("Type");
    font << pdf::WrapName("Font");
    font.endDictionaryItem();
    font.beginDictionaryItem("Subtype");
    font << pdf::WrapName("Type0");
    font.endDictionaryItem();
    font.beginDictionaryItem("BaseFont");
    font << pdf::WrapName("GlyphLessFont");
    font.endDictionaryItem();
    font.beginDictionaryItem("Encoding");
    font << pdf::WrapName("Identity-H");
    font.endDictionaryItem();
    font.beginDictionaryItem("DescendantFonts");
    font << std::vector<pdf::PDFObjectReference>{ cidFontReference };
    font.endDictionaryItem();
    font.beginDictionaryItem("ToUnicode");
    font << toUnicodeReference;
    font.endDictionaryItem();
    font.endDictionary();
    return builder->addObject(font.takeObject());
}

QByteArray OcrTextLayer::createContentStream(const OcrPageText& text, const QTransform& imageToPage, const QByteArray& fontResourceName)
{
    // The width of every glyph is 500/1000 of the font size (/DW 500 of the font)
    constexpr double glyphWidth = 0.5;

    QByteArray content;
    content.append("q\nBT\n3 Tr\n");

    QByteArray currentFontSize;
    for (const OcrLine& line : text.lines)
    {
        // Words with any text only, the rest is skipped
        std::vector<std::pair<QString, QRectF>> words;
        for (const OcrWord& word : line.words)
        {
            QString wordText = cleanWordText(word.text);
            if (!wordText.isEmpty() && word.box.isValid())
            {
                words.emplace_back(std::move(wordText), word.box);
            }
        }

        if (words.empty())
        {
            continue;
        }

        QRectF lineBox = line.box;
        if (!lineBox.isValid())
        {
            for (const auto& word : words)
            {
                lineBox = lineBox.united(word.second);
            }
        }

        // The baseline gives the direction of the line (skewed scans) and its position
        QLineF baseline = line.baseline;
        if (baseline.isNull() || baseline.length() < 1.0)
        {
            baseline = QLineF(lineBox.bottomLeft(), lineBox.bottomRight());
        }

        const QPointF direction = (baseline.p2() - baseline.p1()) / baseline.length();
        const QPointF up(direction.y(), -direction.x()); // Image y goes down, so "up" is -y for a horizontal line
        const double lineHeight = lineBox.height();
        if (!(lineHeight > 0.0))
        {
            continue;
        }

        auto positionOnBaseline = [&](const QRectF& box) -> double
        {
            // The left edge of the word, projected onto the baseline
            return dot(QPointF(box.left(), box.bottom()) - baseline.p1(), direction);
        };

        for (size_t i = 0; i < words.size(); ++i)
        {
            const QString& wordText = words[i].first;
            const QRectF& box = words[i].second;
            const bool hasNextWord = i + 1 < words.size();

            // The word with the space after it spans to the next word, so the text
            // extraction sees one continuous line with real spaces between the words
            const double start = positionOnBaseline(box);
            double span = box.width();
            if (hasNextWord)
            {
                const double nextStart = positionOnBaseline(words[i + 1].second);
                if (nextStart - start > box.width())
                {
                    span = nextStart - start;
                }
            }

            const QPointF origin = baseline.p1() + direction * start;
            const QPointF pageOrigin = imageToPage.map(origin);
            const QPointF pageAlong = imageToPage.map(origin + direction * span) - pageOrigin;
            const QPointF pageUp = imageToPage.map(origin + up * lineHeight) - pageOrigin;
            const double pageWidth = length(pageAlong);
            const double fontSize = length(pageUp);

            if (pageWidth < 0.01 || fontSize < 0.01)
            {
                continue;
            }

            QString codes = wordText;
            if (hasNextWord)
            {
                codes.append(QChar(' '));
            }

            // Horizontal scaling stretches the word over its box
            const double naturalWidth = codes.size() * glyphWidth * fontSize;
            const double horizontalScaling = 100.0 * pageWidth / naturalWidth;

            const QByteArray fontSizeText = formatNumber(fontSize);
            if (fontSizeText != currentFontSize)
            {
                content.append("/" + fontResourceName + " " + fontSizeText + " Tf\n");
                currentFontSize = fontSizeText;
            }

            const QPointF a = pageAlong / pageWidth;
            const QPointF b = pageUp / fontSize;
            content.append(formatNumber(horizontalScaling) + " Tz\n");
            content.append(formatNumber(a.x()) + " " + formatNumber(a.y()) + " " +
                           formatNumber(b.x()) + " " + formatNumber(b.y()) + " " +
                           formatNumber(pageOrigin.x()) + " " + formatNumber(pageOrigin.y()) + " Tm\n");

            QByteArray hex;
            hex.reserve(codes.size() * 4 + 2);
            hex.append('<');
            for (const QChar character : codes)
            {
                hex.append(QByteArray::number(character.unicode(), 16).rightJustified(4, '0').toUpper());
            }
            hex.append('>');
            content.append(hex + " Tj\n");
        }
    }

    content.append("ET\nQ\n");
    return content;
}

bool OcrTextLayer::readPageGeometry(const pdf::PDFObjectStorage* storage,
                                    pdf::PDFObjectReference page,
                                    QRectF* mediaBox,
                                    pdf::PageRotation* rotation)
{
    const pdf::PDFObject mediaBoxObject = findInheritableAttribute(storage, page, "MediaBox");
    if (!mediaBoxObject.isArray() || mediaBoxObject.getArray()->getCount() != 4)
    {
        return false;
    }

    std::array<double, 4> numbers = { };
    for (size_t i = 0; i < 4; ++i)
    {
        const pdf::PDFObject item = storage->getObject(mediaBoxObject.getArray()->getItem(i));
        if (item.isInt())
        {
            numbers[i] = item.getInteger();
        }
        else if (item.isReal())
        {
            numbers[i] = item.getReal();
        }
        else
        {
            return false;
        }
    }

    const QRectF box = QRectF(QPointF(numbers[0], numbers[1]), QPointF(numbers[2], numbers[3])).normalized();
    if (!box.isValid())
    {
        return false;
    }
    *mediaBox = box;

    const pdf::PDFObject rotateObject = findInheritableAttribute(storage, page, "Rotate");
    int degrees = rotateObject.isInt() ? int(rotateObject.getInteger() % 360) : 0;
    if (degrees < 0)
    {
        degrees += 360;
    }

    switch (degrees)
    {
        case 90:
            *rotation = pdf::PageRotation::Rotate90;
            break;
        case 180:
            *rotation = pdf::PageRotation::Rotate180;
            break;
        case 270:
            *rotation = pdf::PageRotation::Rotate270;
            break;
        default:
            *rotation = pdf::PageRotation::None;
            break;
    }

    return true;
}

QTransform OcrTextLayer::createImageToPageTransform(const QRectF& mediaBox, pdf::PageRotation rotation, const QSizeF& imageSize)
{
    // The same transformation as the renderer uses to draw the page into the image, inverted
    const QRectF rotatedMediaBox = pdf::PDFPage::getRotatedBox(mediaBox, rotation);
    const QTransform pageToImage = pdf::PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedMediaBox, QRectF(QPointF(0, 0), imageSize), rotation);
    return pageToImage.inverted();
}

bool OcrTextLayer::addTextLayer(pdf::PDFDocumentBuilder* builder,
                                pdf::PDFObjectReference page,
                                const OcrPageText& text,
                                pdf::PDFObjectReference* font,
                                const QByteArray& fontProgram)
{
    QRectF mediaBox;
    pdf::PageRotation rotation = pdf::PageRotation::None;
    if (!builder || !text.imageSize.isValid() || !readPageGeometry(builder->getStorage(), page, &mediaBox, &rotation))
    {
        return false;
    }

    return addTextLayer(builder, page, text, createImageToPageTransform(mediaBox, rotation, text.imageSize), font, fontProgram);
}

bool OcrTextLayer::addTextLayer(pdf::PDFDocumentBuilder* builder,
                                pdf::PDFObjectReference page,
                                const OcrPageText& text,
                                const QTransform& imageToPage,
                                pdf::PDFObjectReference* font,
                                const QByteArray& fontProgram)
{
    if (!builder || !font || text.isEmpty() || !imageToPage.isInvertible())
    {
        return false;
    }

    // Copies - the storage changes when objects are added
    const pdf::PDFObject pageObject = builder->getObjectByReference(page);
    const pdf::PDFDictionary* pageDictionary = builder->getStorage()->getDictionaryFromObject(pageObject);
    if (!pageDictionary)
    {
        return false;
    }
    const pdf::PDFObject oldContents = pageDictionary->get("Contents");
    const pdf::PDFObject oldResources = findInheritableAttribute(builder->getStorage(), page, "Resources");

    if (!font->isValid())
    {
        *font = createFont(builder, fontProgram);
    }

    // Resources: the effective (possibly inherited) resources of the page, with our font
    // added under a name, which is not taken yet. They are written into the page itself.
    pdf::PDFDictionaryBuilder resources;
    if (const pdf::PDFDictionary* oldResourcesDictionary = builder->getDictionaryFromObject(oldResources))
    {
        resources = pdf::PDFDictionaryBuilder(*oldResourcesDictionary);
    }

    pdf::PDFDictionaryBuilder fonts;
    if (const pdf::PDFDictionary* oldFonts = builder->getDictionaryFromObject(resources.getDictionary()->get("Font")))
    {
        fonts = pdf::PDFDictionaryBuilder(*oldFonts);
    }

    QByteArray fontResourceName = FONT_RESOURCE_NAME;
    for (int index = 1; fonts.hasKey(fontResourceName); ++index)
    {
        fontResourceName = QByteArray(FONT_RESOURCE_NAME) + QByteArray::number(index);
    }
    fonts.setEntry(pdf::PDFInplaceOrMemoryString(fontResourceName), pdf::PDFObject::createReference(*font));
    resources.setEntry(pdf::PDFInplaceOrMemoryString("Font"), pdf::PDFObject::createDictionary(std::move(fonts)));

    // Contents: q <old content> Q <text layer>
    std::vector<pdf::PDFObjectReference> contents;
    collectContentReferences(builder, contents, oldContents);
    if (!contents.empty())
    {
        contents.insert(contents.begin(), addStream(builder, QByteArray("q\n"), false));
        contents.push_back(addStream(builder, QByteArray("Q\n"), false));
    }
    contents.push_back(addStream(builder, createContentStream(text, imageToPage, fontResourceName), true));

    // The page dictionary is set as a whole (merging would merge the old and the new resources)
    const pdf::PDFDictionary* currentPageDictionary = builder->getStorage()->getDictionaryFromObject(builder->getObjectByReference(page));
    if (!currentPageDictionary)
    {
        return false;
    }

    pdf::PDFArrayBuilder contentsArray;
    for (const pdf::PDFObjectReference& reference : contents)
    {
        contentsArray.appendItem(pdf::PDFObject::createReference(reference));
    }

    pdf::PDFDictionaryBuilder newPageDictionary(*currentPageDictionary);
    newPageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createArray(std::move(contentsArray)));
    newPageDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
    builder->setObject(page, pdf::PDFObject::createDictionary(std::move(newPageDictionary)));
    return true;
}

}   // namespace pdfplugin
