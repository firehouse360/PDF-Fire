// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfeditorfallbackfont.h"
#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfexception.h"

#include <QFont>
#include <QFontInfo>
#include <QTextStream>
#include <QtEndian>

#include "pdfdbgheap.h"

namespace pdf
{

std::vector<PDFEditorFallbackFontManager::Run> PDFEditorFallbackFontManager::encode(const std::u32string& codePoints,
                                                                                    const PDFFontPointer& similarToFont,
                                                                                    PDFDictionaryBuilder& fontDictionary,
                                                                                    const std::function<void(const QString&)>& errorCallback)
{
    std::vector<Run> runs;

    if (codePoints.empty())
    {
        return runs;
    }

    SourceFontFallback& fallback = getSourceFontFallback(similarToFont);
    std::set<size_t> updatedFontIndices;

    for (const char32_t codePoint : codePoints)
    {
        std::pair<size_t, int> assignment;

        auto it = fallback.codeMap.find(codePoint);
        if (it != fallback.codeMap.cend())
        {
            assignment = it->second;
        }
        else
        {
            assignment = assignCode(fallback, codePoint, fontDictionary, errorCallback);
            updatedFontIndices.insert(assignment.first);
        }

        const FallbackFont& font = fallback.fonts[assignment.first];
        if (!runs.empty() && runs.back().fontResourceKey == font.resourceKey)
        {
            runs.back().encodedBytes.append(static_cast<char>(assignment.second));
        }
        else
        {
            runs.push_back(Run{ font.resourceKey, QByteArray(1, static_cast<char>(assignment.second)) });
        }
    }

    for (const size_t fontIndex : updatedFontIndices)
    {
        const FallbackFont& font = fallback.fonts[fontIndex];
        fontDictionary.setEntry(PDFInplaceOrMemoryString(font.resourceKey), buildFontDictionaryObject(font));
    }

    return runs;
}

PDFEditorFallbackFontManager::SourceFontFallback& PDFEditorFallbackFontManager::getSourceFontFallback(const PDFFontPointer& similarToFont)
{
    const QByteArray sourceFontId = similarToFont ? similarToFont->getFontId() : QByteArray();

    auto it = m_fallbacks.find(sourceFontId);
    if (it == m_fallbacks.cend())
    {
        SourceFontFallback fallbackData;

        QFont substituteFont;
        if (similarToFont)
        {
            const FontDescriptor* descriptor = similarToFont->getFontDescriptor();

            QByteArray fontFamily = descriptor->fontFamily;
            if (fontFamily.isEmpty())
            {
                fontFamily = descriptor->fontName;

                // Strip the subset font prefix ("ABCDEF+")
                if (fontFamily.size() > 7 && fontFamily[6] == '+')
                {
                    fontFamily = fontFamily.mid(7);
                }
            }

            substituteFont = QFont(QString::fromLatin1(fontFamily));

            // PDF Fire: common fonts of Windows documents have metric-compatible free
            // twins, which are usually installed on Linux (the same table as the
            // in-place text editor uses)
            QString familyName = QString::fromLatin1(fontFamily).section(QChar('-'), 0, 0).section(QChar(','), 0, 0);
            static const std::pair<const char*, const char*> twins[] = {
                { "Calibri", "Carlito" }, { "Cambria", "Caladea" }, { "Arial", "Liberation Sans" },
                { "TimesNewRoman", "Liberation Serif" }, { "Times New Roman", "Liberation Serif" },
                { "CourierNew", "Liberation Mono" }, { "Courier New", "Liberation Mono" }, { "Helvetica", "Liberation Sans" },
                { "Times", "Liberation Serif" }, { "Courier", "Liberation Mono" }
            };
            if (QFontInfo(substituteFont).family().compare(familyName, Qt::CaseInsensitive) != 0)
            {
                for (const auto& [windowsName, twin] : twins)
                {
                    if (familyName.startsWith(QLatin1String(windowsName), Qt::CaseInsensitive) && QFontInfo(QFont(QLatin1String(twin))).family() == QLatin1String(twin))
                    {
                        substituteFont = QFont(QLatin1String(twin));
                        break;
                    }
                }
            }

            substituteFont.setWeight(QFont::Weight(qBound(1, int(descriptor->fontWeight), 1000)));
            substituteFont.setStretch(descriptor->fontStretch);
            substituteFont.setItalic(descriptor->isItalic());
            substituteFont.setStyleHint(descriptor->isFixedPitch() ? QFont::Monospace :
                                        descriptor->isSerif() ? QFont::Serif : QFont::SansSerif);
        }

        substituteFont.setPixelSize(1000);
        fallbackData.substituteFont = QRawFont::fromFont(substituteFont);
        fallbackData.documentFont = getDocumentFontProgram(similarToFont);

        it = m_fallbacks.emplace(sourceFontId, std::move(fallbackData)).first;
    }

    return it->second;
}

std::pair<size_t, int> PDFEditorFallbackFontManager::assignCode(SourceFontFallback& fallback,
                                                                char32_t codePoint,
                                                                PDFDictionaryBuilder& fontDictionary,
                                                                const std::function<void(const QString&)>& errorCallback)
{
    if (fallback.fonts.empty() || fallback.fonts.back().nextCode > LAST_CHARACTER_CODE)
    {
        createFallbackFont(fallback, fontDictionary);
    }

    const size_t fontIndex = fallback.fonts.size() - 1;
    FallbackFont& font = fallback.fonts[fontIndex];

    GlyphRecord glyphRecord = createGlyphRecord(fallback, codePoint);
    if (!glyphRecord.hasGlyph && errorCallback)
    {
        errorCallback(PDFTranslationContext::tr("Character '%1' (U+%2) is not available in any substitute font, an empty glyph is used.")
                          .arg(QString::fromUcs4(&codePoint, 1))
                          .arg(uint(codePoint), 4, 16, QChar('0')));
    }

    const int code = font.nextCode++;
    font.fontBBox = font.fontBBox.united(glyphRecord.path.boundingRect());
    font.glyphs[code] = std::move(glyphRecord);
    fallback.codeMap[codePoint] = std::make_pair(fontIndex, code);

    return std::make_pair(fontIndex, code);
}

PDFEditorFallbackFontManager::FallbackFont& PDFEditorFallbackFontManager::createFallbackFont(SourceFontFallback& fallback, PDFDictionaryBuilder& fontDictionary)
{
    FallbackFont font;

    int i = 0;
    while (true)
    {
        QByteArray currentKey = QString("PDF4QT_Fb%1").arg(++i).toLatin1();
        if (!fontDictionary.hasKey(currentKey))
        {
            font.resourceKey = currentKey;

            // Reserve the key immediately, so a second fallback font created
            // during the same call does not get the same key. The placeholder
            // is replaced by the real font dictionary object in encode().
            fontDictionary.setEntry(PDFInplaceOrMemoryString(currentKey), PDFObject());
            break;
        }
    }

    fallback.fonts.push_back(std::move(font));
    return fallback.fonts.back();
}

PDFEditorFallbackFontManager::GlyphRecord PDFEditorFallbackFontManager::createGlyphRecord(const SourceFontFallback& fallback, char32_t codePoint) const
{
    GlyphRecord record;
    record.codePoint = codePoint;

    if (codePoint <= 0xFFFF)
    {
        record.glyphName = QString("uni%1").arg(uint(codePoint), 4, 16, QChar('0')).toUpper().toLatin1();
    }
    else
    {
        record.glyphName = ("u" + QString("%1").arg(uint(codePoint), 0, 16).toUpper()).toLatin1();
    }

    auto tryExtractGlyph = [&record, codePoint](const QRawFont& rawFont) -> bool
    {
        if (!rawFont.isValid())
        {
            return false;
        }

        const QString text = QString::fromUcs4(&codePoint, 1);
        const QList<quint32> glyphIndices = rawFont.glyphIndexesForString(text);
        if (glyphIndices.size() != 1 || glyphIndices.front() == 0)
        {
            return false;
        }

        const quint32 glyphIndex = glyphIndices.front();
        QPainterPath glyphPath = rawFont.pathForGlyph(glyphIndex);

        // PDF Fire: an empty outline of a visible character (a bitmap font, or a font
        // engine without outlines) would write a blank letter - try the next font
        if (glyphPath.isEmpty() && !QChar::isSpace(codePoint))
        {
            return false;
        }

        // Qt glyph paths have the y-axis pointing down, PDF glyph space
        // has the y-axis pointing up.
        glyphPath = QTransform(1, 0, 0, -1, 0, 0).map(glyphPath);

        const QList<QPointF> advances = rawFont.advancesForGlyphIndexes({ glyphIndex });

        record.path = std::move(glyphPath);
        record.width = advances.size() == 1 ? advances.front().x() : rawFont.averageCharWidth();
        record.hasGlyph = true;
        return true;
    };

    // PDF Fire: the font of the document itself first (its complete program), then
    // the similar font of the system
    if (tryExtractGlyph(fallback.documentFont) || tryExtractGlyph(fallback.substituteFont))
    {
        return record;
    }

    for (const QRawFont& cascadeFont : getCascadeFonts())
    {
        if (tryExtractGlyph(cascadeFont))
        {
            return record;
        }
    }

    // No substitute font contains the glyph - use an empty glyph
    // with a reasonable advance, the character still round-trips
    // through the ToUnicode mapping.
    record.width = 500.0;
    record.hasGlyph = false;
    return record;
}

const std::vector<QRawFont>& PDFEditorFallbackFontManager::getCascadeFonts() const
{
    if (m_cascadeFonts.empty())
    {
        auto addCascadeFont = [this](QFont font)
        {
            font.setPixelSize(1000);
            QRawFont rawFont = QRawFont::fromFont(font);
            if (rawFont.isValid())
            {
                m_cascadeFonts.push_back(std::move(rawFont));
            }
        };

        // PDF Fire: the fonts of Linux distributions first (DejaVu, Liberation and Noto
        // cover most of the symbols), the fonts of Windows after them. A font, which is
        // not installed, is not added (Qt would substitute the default font for it).
        addCascadeFont(QFont());
        for (const char* family : { "DejaVu Sans", "Liberation Sans", "Noto Sans", "Noto Sans Symbols", "Noto Sans Symbols 2",
                                    "Noto Sans Math", "DejaVu Serif", "FreeSerif", "Segoe UI Symbol", "Segoe UI Emoji" })
        {
            QFont font{ QLatin1String(family) };
            if (QFontInfo(font).family().compare(QLatin1String(family), Qt::CaseInsensitive) == 0)
            {
                addCascadeFont(font);
            }
        }
    }

    return m_cascadeFonts;
}

PDFReal PDFEditorFallbackFontManager::getSpaceWidth(const PDFFontPointer& similarToFont)
{
    const SourceFontFallback& fallback = getSourceFontFallback(similarToFont);

    PDFReal width = 0.0;
    for (const QRawFont* rawFont : { &fallback.documentFont, &fallback.substituteFont })
    {
        if (width <= 0.0 && rawFont->isValid())
        {
            const QList<quint32> glyphIndices = rawFont->glyphIndexesForString(QStringLiteral(" "));
            if (glyphIndices.size() == 1 && glyphIndices.front() != 0)
            {
                const QList<QPointF> advances = rawFont->advancesForGlyphIndexes(glyphIndices);
                width = advances.size() == 1 ? advances.front().x() * 1000.0 / rawFont->pixelSize() : 0.0;
            }
        }
    }

    if (width <= 0.0)
    {
        width = 250.0;
    }

    // A narrower gap would not be read back as a space
    return qBound(220.0, width, 600.0);
}

QRawFont PDFEditorFallbackFontManager::getDocumentFontProgram(const PDFFontPointer& font)
{
    if (!font)
    {
        return QRawFont();
    }

    // A subset has only the glyphs, which the document uses - it can't draw new ones
    // (and its character map is private). Only a complete font program is used.
    const FontDescriptor* descriptor = font->getFontDescriptor();
    const QByteArray& fontName = descriptor->fontName;
    if (fontName.size() > 7 && fontName[6] == '+')
    {
        return QRawFont();
    }

    const QByteArray* fontData = descriptor->getEmbeddedFontData();
    if (!fontData || fontData->isEmpty() || fontData->size() > 16 * 1024 * 1024)
    {
        return QRawFont();
    }

    QRawFont rawFont(*fontData, 1000);
    if (!rawFont.isValid())
    {
        return QRawFont();
    }

    // Ported from PdfCraft crates/fonts/src/embedding.rs (validate, the licence of the
    // font) (MIT OR Apache-2.0), Copyright (c) 2026 ArtCraft Team and the PdfCraft
    // contributors: a font with the restricted licence (0x0002), with the preview and
    // print licence (0x0004) or with the bitmap only licence (0x0200) is not used to
    // draw new text.
    const QByteArray os2 = rawFont.fontTable("OS/2");
    if (os2.size() >= 10)
    {
        const quint16 fsType = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(os2.constData()) + 8);
        if (fsType & (0x0002 | 0x0004 | 0x0200))
        {
            return QRawFont();
        }
    }

    return rawFont;
}

PDFObject PDFEditorFallbackFontManager::buildFontDictionaryObject(const FallbackFont& font) const
{
    Q_ASSERT(!font.glyphs.empty());

    const int firstChar = font.glyphs.begin()->first;
    const int lastChar = font.glyphs.rbegin()->first;

    QRectF fontBBox = font.fontBBox;
    if (fontBBox.isNull())
    {
        fontBBox = QRectF(0, 0, 1000, 1000);
    }

    PDFArrayBuilder fontBBoxArray;
    fontBBoxArray.appendItem(PDFObject::createReal(fontBBox.left()));
    fontBBoxArray.appendItem(PDFObject::createReal(fontBBox.top()));
    fontBBoxArray.appendItem(PDFObject::createReal(fontBBox.right()));
    fontBBoxArray.appendItem(PDFObject::createReal(fontBBox.bottom()));

    PDFArrayBuilder fontMatrixArray;
    fontMatrixArray.appendItem(PDFObject::createReal(0.001));
    fontMatrixArray.appendItem(PDFObject::createReal(0.0));
    fontMatrixArray.appendItem(PDFObject::createReal(0.0));
    fontMatrixArray.appendItem(PDFObject::createReal(0.001));
    fontMatrixArray.appendItem(PDFObject::createReal(0.0));
    fontMatrixArray.appendItem(PDFObject::createReal(0.0));

    PDFArrayBuilder widthsArray;
    PDFArrayBuilder differencesArray;
    PDFDictionaryBuilder charProcsDictionary;

    differencesArray.appendItem(PDFObject::createInteger(firstChar));

    for (int code = firstChar; code <= lastChar; ++code)
    {
        auto it = font.glyphs.find(code);
        if (it == font.glyphs.cend())
        {
            // Character codes are assigned contiguously, this is a safety net only
            widthsArray.appendItem(PDFObject::createReal(0.0));
            differencesArray.appendItem(PDFObject::createName(QString(".notdef%1").arg(code).toLatin1()));
            continue;
        }

        const GlyphRecord& glyph = it->second;
        widthsArray.appendItem(PDFObject::createReal(glyph.width));
        differencesArray.appendItem(PDFObject::createName(glyph.glyphName));

        QByteArray charProcContent;
        {
            QTextStream stream(&charProcContent, QDataStream::WriteOnly);
            stream << glyph.width << " 0 d0" << Qt::endl;
            PDFPageContentEditorContentStreamBuilder::writePathGeometry(stream, glyph.path);
            if (!glyph.path.isEmpty())
            {
                stream << "f" << Qt::endl;
            }
        }

        PDFDictionaryBuilder charProcStreamDictionary;
        charProcStreamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(charProcContent.size()));
        charProcsDictionary.setEntry(PDFInplaceOrMemoryString(glyph.glyphName),
                                     PDFObject::createStream(PDFStream(qMove(charProcStreamDictionary), qMove(charProcContent))));
    }

    PDFDictionaryBuilder encodingDictionary;
    encodingDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Encoding"));
    encodingDictionary.setEntry(PDFInplaceOrMemoryString("Differences"), PDFObject::createArray(qMove(differencesArray)));

    QByteArray toUnicodeData = buildToUnicodeStreamData(font);
    PDFDictionaryBuilder toUnicodeStreamDictionary;
    toUnicodeStreamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(toUnicodeData.size()));
    PDFObject toUnicodeObject = PDFObject::createStream(PDFStream(qMove(toUnicodeStreamDictionary), qMove(toUnicodeData)));

    PDFDictionaryBuilder fontDictionaryObject;
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Font"));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Type3"));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("FontBBox"), PDFObject::createArray(qMove(fontBBoxArray)));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("FontMatrix"), PDFObject::createArray(qMove(fontMatrixArray)));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("FirstChar"), PDFObject::createInteger(firstChar));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("LastChar"), PDFObject::createInteger(lastChar));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("Widths"), PDFObject::createArray(qMove(widthsArray)));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("Encoding"), PDFObject::createDictionary(qMove(encodingDictionary)));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("CharProcs"), PDFObject::createDictionary(qMove(charProcsDictionary)));
    fontDictionaryObject.setEntry(PDFInplaceOrMemoryString("ToUnicode"), std::move(toUnicodeObject));

    return PDFObject::createDictionary(qMove(fontDictionaryObject));
}

QByteArray PDFEditorFallbackFontManager::buildToUnicodeStreamData(const FallbackFont& font) const
{
    QByteArray data;
    QTextStream stream(&data, QDataStream::WriteOnly);

    stream << "/CIDInit /ProcSet findresource begin" << Qt::endl;
    stream << "12 dict begin" << Qt::endl;
    stream << "begincmap" << Qt::endl;
    stream << "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def" << Qt::endl;
    stream << "/CMapName /Adobe-Identity-UCS def" << Qt::endl;
    stream << "/CMapType 2 def" << Qt::endl;
    stream << "1 begincodespacerange" << Qt::endl;
    stream << "<00> <FF>" << Qt::endl;
    stream << "endcodespacerange" << Qt::endl;

    // At most 100 mappings per beginbfchar section
    constexpr int maxMappingsPerSection = 100;
    auto it = font.glyphs.cbegin();
    int remaining = int(font.glyphs.size());
    while (remaining > 0)
    {
        const int sectionSize = qMin(remaining, maxMappingsPerSection);
        stream << sectionSize << " beginbfchar" << Qt::endl;

        for (int i = 0; i < sectionSize; ++i, ++it)
        {
            const int code = it->first;
            const char32_t codePoint = it->second.codePoint;

            QByteArray unicodeBytes;
            if (codePoint <= 0xFFFF)
            {
                unicodeBytes.append(static_cast<char>((codePoint >> 8) & 0xFF));
                unicodeBytes.append(static_cast<char>(codePoint & 0xFF));
            }
            else
            {
                // Encode as UTF-16BE surrogate pair
                const char32_t value = codePoint - 0x10000;
                const char16_t highSurrogate = static_cast<char16_t>(0xD800 + (value >> 10));
                const char16_t lowSurrogate = static_cast<char16_t>(0xDC00 + (value & 0x3FF));
                unicodeBytes.append(static_cast<char>((highSurrogate >> 8) & 0xFF));
                unicodeBytes.append(static_cast<char>(highSurrogate & 0xFF));
                unicodeBytes.append(static_cast<char>((lowSurrogate >> 8) & 0xFF));
                unicodeBytes.append(static_cast<char>(lowSurrogate & 0xFF));
            }

            stream << "<" << QString("%1").arg(code, 2, 16, QChar('0')).toUpper() << "> <" << unicodeBytes.toHex().toUpper() << ">" << Qt::endl;
        }

        stream << "endbfchar" << Qt::endl;
        remaining -= sectionSize;
    }

    stream << "endcmap" << Qt::endl;
    stream << "CMapName currentdict /CMap defineresource pop" << Qt::endl;
    stream << "end" << Qt::endl;
    stream << "end" << Qt::endl;

    stream.flush();
    return data;
}

}   // namespace pdf
