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
#include "pdfstreamfilters.h"

#include <QtEndian>

#include <algorithm>

namespace pdf
{

namespace
{

quint32 makeTag(const char* tag)
{
    return (quint32(quint8(tag[0])) << 24) | (quint32(quint8(tag[1])) << 16) | (quint32(quint8(tag[2])) << 8) | quint32(quint8(tag[3]));
}

quint32 calculateChecksum(const QByteArray& data)
{
    quint32 sum = 0;
    QByteArray padded = data;
    while (padded.size() % 4)
    {
        padded.append('\0');
    }
    for (qsizetype i = 0; i < padded.size(); i += 4)
    {
        sum += qFromBigEndian<quint32>(padded.constData() + i);
    }
    return sum;
}

void appendBigEndian16(QByteArray& data, quint16 value)
{
    char bytes[2];
    qToBigEndian(value, bytes);
    data.append(bytes, 2);
}

void appendBigEndian32(QByteArray& data, quint32 value)
{
    char bytes[4];
    qToBigEndian(value, bytes);
    data.append(bytes, 4);
}

/// The PostScript name of the font (name ID 6 of the table 'name'), or empty
QByteArray readPostScriptName(const QByteArray& nameTable)
{
    if (nameTable.size() < 6)
    {
        return QByteArray();
    }

    const quint16 count = qFromBigEndian<quint16>(nameTable.constData() + 2);
    const quint16 stringOffset = qFromBigEndian<quint16>(nameTable.constData() + 4);
    for (quint16 i = 0; i < count; ++i)
    {
        const qsizetype record = 6 + qsizetype(i) * 12;
        if (record + 12 > nameTable.size())
        {
            break;
        }

        const quint16 platform = qFromBigEndian<quint16>(nameTable.constData() + record);
        const quint16 nameId = qFromBigEndian<quint16>(nameTable.constData() + record + 6);
        const quint16 length = qFromBigEndian<quint16>(nameTable.constData() + record + 8);
        const quint16 offset = qFromBigEndian<quint16>(nameTable.constData() + record + 10);
        const qsizetype start = qsizetype(stringOffset) + offset;
        if (nameId != 6 || start + length > nameTable.size())
        {
            continue;
        }

        QByteArray name;
        if (platform == 3 || platform == 0)
        {
            // UTF-16 big endian
            for (qsizetype k = start; k + 1 < start + length; k += 2)
            {
                name.append(nameTable[k + 1]);
            }
        }
        else
        {
            name = nameTable.mid(start, length);
        }

        // Only the characters allowed in a PDF name without escaping
        QByteArray result;
        for (char character : name)
        {
            if (character > 32 && character < 127 && !QByteArray("()<>[]{}/%#").contains(character))
            {
                result.append(character);
            }
        }
        if (!result.isEmpty())
        {
            return result;
        }
    }

    return QByteArray();
}

}   // namespace

bool PDFFireEmbeddedFont::isEmbeddingAllowed(const QRawFont& rawFont)
{
    const QByteArray os2 = rawFont.fontTable("OS/2");
    if (os2.size() < 10)
    {
        // No OS/2 table - no restriction is given
        return true;
    }

    // fsType: 0x0002 - restricted licence embedding, 0x0200 - bitmap embedding only
    const quint16 fsType = qFromBigEndian<quint16>(os2.constData() + 8);
    return !(fsType & 0x0002) && !(fsType & 0x0200);
}

QByteArray PDFFireEmbeddedFont::createFontFile(const QRawFont& rawFont)
{
    // The tables of a TrueType font, which a PDF reader can use (the tables of the
    // layout, like GSUB/GPOS, are not needed - the text is written as glyphs)
    static const char* const tableTags[] = { "OS/2", "cmap", "cvt ", "fpgm", "gasp", "glyf", "head", "hhea", "hmtx", "loca", "maxp", "name", "post", "prep" };

    std::vector<std::pair<quint32, QByteArray>> tables;
    for (const char* tag : tableTags)
    {
        QByteArray table = rawFont.fontTable(tag);
        if (!table.isEmpty())
        {
            tables.emplace_back(makeTag(tag), std::move(table));
        }
    }

    auto hasTable = [&tables](const char* tag) { return std::any_of(tables.cbegin(), tables.cend(), [tag](const auto& table) { return table.first == makeTag(tag); }); };
    if (!hasTable("glyf") || !hasTable("loca") || !hasTable("head") || !hasTable("hmtx") || !hasTable("maxp"))
    {
        // Not a TrueType font (CFF outlines), or a damaged one
        return QByteArray();
    }

    std::sort(tables.begin(), tables.end(), [](const auto& left, const auto& right) { return left.first < right.first; });

    const quint16 numTables = quint16(tables.size());
    quint16 entrySelector = 0;
    while ((1 << (entrySelector + 1)) <= numTables)
    {
        ++entrySelector;
    }
    const quint16 searchRange = quint16((1 << entrySelector) * 16);
    const quint16 rangeShift = quint16(numTables * 16 - searchRange);

    QByteArray file;
    appendBigEndian32(file, 0x00010000);
    appendBigEndian16(file, numTables);
    appendBigEndian16(file, searchRange);
    appendBigEndian16(file, entrySelector);
    appendBigEndian16(file, rangeShift);

    quint32 offset = 12 + 16 * quint32(numTables);
    QByteArray tableData;
    qsizetype headOffset = -1;
    for (auto& [tag, data] : tables)
    {
        if (tag == makeTag("head") && data.size() >= 12)
        {
            // The checksum adjustment is computed for the whole file below
            qToBigEndian<quint32>(0, data.data() + 8);
            headOffset = qsizetype(offset);
        }

        appendBigEndian32(file, tag);
        appendBigEndian32(file, calculateChecksum(data));
        appendBigEndian32(file, offset);
        appendBigEndian32(file, quint32(data.size()));

        tableData.append(data);
        while (tableData.size() % 4)
        {
            tableData.append('\0');
        }
        offset = 12 + 16 * quint32(numTables) + quint32(tableData.size());
    }
    file.append(tableData);

    if (headOffset >= 0)
    {
        qToBigEndian<quint32>(0xB1B0AFBA - calculateChecksum(file), file.data() + headOffset + 8);
    }

    return file;
}

bool PDFFireEmbeddedFont::initialize(const QRawFont& rawFont)
{
    if (!rawFont.isValid() || !isEmbeddingAllowed(rawFont))
    {
        return false;
    }

    m_fontFile = createFontFile(rawFont);
    if (m_fontFile.isEmpty())
    {
        return false;
    }

    m_rawFont = rawFont;
    m_rawFont.setPixelSize(1000);
    m_postScriptName = readPostScriptName(rawFont.fontTable("name"));
    if (m_postScriptName.isEmpty())
    {
        m_postScriptName = (rawFont.familyName() + rawFont.styleName()).remove(QChar(' ')).toLatin1();
    }
    return true;
}

bool PDFFireEmbeddedFont::addText(const QString& text, QByteArray* glyphs)
{
    glyphs->clear();

    // The glyphs are found character by character (not by the shaping of the text),
    // so every glyph has its characters for the ToUnicode map
    qsizetype i = 0;
    while (i < text.size())
    {
        const qsizetype length = (text[i].isHighSurrogate() && i + 1 < text.size()) ? 2 : 1;
        const QString character = text.mid(i, length);
        i += length;

        const QList<quint32> indices = m_rawFont.glyphIndexesForString(character);
        if (indices.size() != 1 || (indices.front() == 0 && !character.front().isSpace()))
        {
            return false;
        }

        quint32 glyph = indices.front();
        if (glyph == 0)
        {
            // A space missing in the font - the glyph of a normal space is used
            const QList<quint32> spaceIndices = m_rawFont.glyphIndexesForString(QStringLiteral(" "));
            glyph = spaceIndices.isEmpty() ? 0 : spaceIndices.front();
        }

        m_usedGlyphs.emplace(glyph, character);
        glyphs->append(char((glyph >> 8) & 0xFF));
        glyphs->append(char(glyph & 0xFF));
    }

    return true;
}

PDFObject PDFFireEmbeddedFont::createFontObject() const
{
    auto name = [](const QByteArray& value) { return PDFObject::createName(value); };
    auto integer = [](PDFInteger value) { return PDFObject::createInteger(value); };

    // The font file
    QByteArray compressedFile = PDFFlateDecodeFilter::compress(m_fontFile);
    PDFDictionaryBuilder fontFileDictionary;
    fontFileDictionary.setEntry(PDFInplaceOrMemoryString("Length"), integer(compressedFile.size()));
    fontFileDictionary.setEntry(PDFInplaceOrMemoryString("Length1"), integer(m_fontFile.size()));
    fontFileDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), name("FlateDecode"));
    PDFObject fontFile = PDFObject::createStream(PDFStream(std::move(fontFileDictionary), std::move(compressedFile)));

    // The descriptor (the metrics in the units of the glyph space, 1000 per em)
    const QByteArray head = m_rawFont.fontTable("head");
    const qreal unitsPerEm = head.size() >= 54 ? qreal(qFromBigEndian<quint16>(head.constData() + 18)) : 1000.0;
    auto scaled = [unitsPerEm](qint16 value) { return PDFInteger(qRound(value * 1000.0 / unitsPerEm)); };
    PDFArrayBuilder boundingBox;
    if (head.size() >= 54)
    {
        boundingBox.appendItem(integer(scaled(qFromBigEndian<qint16>(head.constData() + 36))));
        boundingBox.appendItem(integer(scaled(qFromBigEndian<qint16>(head.constData() + 38))));
        boundingBox.appendItem(integer(scaled(qFromBigEndian<qint16>(head.constData() + 40))));
        boundingBox.appendItem(integer(scaled(qFromBigEndian<qint16>(head.constData() + 42))));
    }
    else
    {
        for (PDFInteger value : { -200, -300, 1200, 1000 })
        {
            boundingBox.appendItem(integer(value));
        }
    }

    PDFDictionaryBuilder descriptor;
    descriptor.setEntry(PDFInplaceOrMemoryString("Type"), name("FontDescriptor"));
    descriptor.setEntry(PDFInplaceOrMemoryString("FontName"), name(m_postScriptName));
    descriptor.setEntry(PDFInplaceOrMemoryString("Flags"), integer(32)); // Nonsymbolic
    descriptor.setEntry(PDFInplaceOrMemoryString("FontBBox"), PDFObject::createArray(std::move(boundingBox)));
    descriptor.setEntry(PDFInplaceOrMemoryString("ItalicAngle"), integer(0));
    descriptor.setEntry(PDFInplaceOrMemoryString("Ascent"), integer(qRound(m_rawFont.ascent())));
    descriptor.setEntry(PDFInplaceOrMemoryString("Descent"), integer(-qRound(m_rawFont.descent())));
    descriptor.setEntry(PDFInplaceOrMemoryString("CapHeight"), integer(qRound(m_rawFont.capHeight())));
    descriptor.setEntry(PDFInplaceOrMemoryString("StemV"), integer(80));
    descriptor.setEntry(PDFInplaceOrMemoryString("FontFile2"), std::move(fontFile));

    // The widths of the used glyphs: [gid [w] gid [w] ...]
    PDFArrayBuilder widths;
    for (const auto& [glyph, text] : m_usedGlyphs)
    {
        const QList<QPointF> advances = m_rawFont.advancesForGlyphIndexes(QList<quint32>{ glyph });
        PDFArrayBuilder width;
        width.appendItem(integer(advances.isEmpty() ? 1000 : qRound(advances.front().x())));
        widths.appendItem(integer(glyph));
        widths.appendItem(PDFObject::createArray(std::move(width)));
    }

    PDFDictionaryBuilder systemInfo;
    systemInfo.setEntry(PDFInplaceOrMemoryString("Registry"), PDFObject::createString("Adobe"));
    systemInfo.setEntry(PDFInplaceOrMemoryString("Ordering"), PDFObject::createString("Identity"));
    systemInfo.setEntry(PDFInplaceOrMemoryString("Supplement"), integer(0));

    PDFDictionaryBuilder cidFont;
    cidFont.setEntry(PDFInplaceOrMemoryString("Type"), name("Font"));
    cidFont.setEntry(PDFInplaceOrMemoryString("Subtype"), name("CIDFontType2"));
    cidFont.setEntry(PDFInplaceOrMemoryString("BaseFont"), name(m_postScriptName));
    cidFont.setEntry(PDFInplaceOrMemoryString("CIDSystemInfo"), PDFObject::createDictionary(std::move(systemInfo)));
    cidFont.setEntry(PDFInplaceOrMemoryString("FontDescriptor"), PDFObject::createDictionary(std::move(descriptor)));
    cidFont.setEntry(PDFInplaceOrMemoryString("DW"), integer(1000));
    cidFont.setEntry(PDFInplaceOrMemoryString("W"), PDFObject::createArray(std::move(widths)));
    cidFont.setEntry(PDFInplaceOrMemoryString("CIDToGIDMap"), name("Identity"));

    // ToUnicode: every used glyph to its characters
    QByteArray cmap = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                      "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                      "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
                      "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    std::vector<std::pair<quint32, QString>> entries(m_usedGlyphs.cbegin(), m_usedGlyphs.cend());
    for (size_t start = 0; start < entries.size(); start += 100)
    {
        const size_t count = qMin<size_t>(100, entries.size() - start);
        cmap += QByteArray::number(qulonglong(count)) + " beginbfchar\n";
        for (size_t k = start; k < start + count; ++k)
        {
            QByteArray unicode;
            for (QChar character : entries[k].second)
            {
                unicode += QByteArray::number(character.unicode(), 16).rightJustified(4, '0').toUpper();
            }
            cmap += "<" + QByteArray::number(entries[k].first, 16).rightJustified(4, '0').toUpper() + "> <" + unicode + ">\n";
        }
        cmap += "endbfchar\n";
    }
    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    PDFDictionaryBuilder toUnicodeDictionary;
    toUnicodeDictionary.setEntry(PDFInplaceOrMemoryString("Length"), integer(cmap.size()));
    PDFObject toUnicode = PDFObject::createStream(PDFStream(std::move(toUnicodeDictionary), std::move(cmap)));

    PDFArrayBuilder descendants;
    descendants.appendItem(PDFObject::createDictionary(std::move(cidFont)));

    PDFDictionaryBuilder font;
    font.setEntry(PDFInplaceOrMemoryString("Type"), name("Font"));
    font.setEntry(PDFInplaceOrMemoryString("Subtype"), name("Type0"));
    font.setEntry(PDFInplaceOrMemoryString("BaseFont"), name(m_postScriptName));
    font.setEntry(PDFInplaceOrMemoryString("Encoding"), name("Identity-H"));
    font.setEntry(PDFInplaceOrMemoryString("DescendantFonts"), PDFObject::createArray(std::move(descendants)));
    font.setEntry(PDFInplaceOrMemoryString("ToUnicode"), std::move(toUnicode));
    return PDFObject::createDictionary(std::move(font));
}

}   // namespace pdf
