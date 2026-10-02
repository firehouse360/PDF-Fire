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

#include "pdftoodtconverter.h"
#include "odtlayout.h"
#include "odtwriter.h"

#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfimage.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecontentprocessor.h"
#include "pdfrenderer.h"
#include "pdftextlayout.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>

#include <map>
#include <unordered_map>

namespace pdfplugin
{

namespace
{

/// Images smaller than this (in points, on the page) are decorations - not exported
constexpr double MINIMAL_IMAGE_SIZE = 12.0;

/// The largest exported image side in pixels - a 600 DPI full page scan is plenty
constexpr int MAXIMAL_IMAGE_PIXELS = 5000;

/// Shared tables of the whole conversion: character styles and images
struct ConversionTables
{
    std::vector<OdtCharacterStyle> characterStyles;
    std::vector<OdtImage> images;
    std::map<pdf::PDFObjectReference, int> imageByReference;
    QStringList installedFontFamilies;

    int getCharacterStyle(const OdtCharacterStyle& style)
    {
        auto it = std::find(characterStyles.begin(), characterStyles.end(), style);
        if (it != characterStyles.end())
        {
            return int(std::distance(characterStyles.begin(), it));
        }
        characterStyles.push_back(style);
        return int(characterStyles.size()) - 1;
    }

    int addImage(QByteArray data, bool isJpeg)
    {
        OdtImage image;
        image.fileName = QString("Pictures/image%1.%2").arg(images.size() + 1).arg(isJpeg ? "jpg" : "png");
        image.mimeType = isJpeg ? "image/jpeg" : "image/png";
        image.data = std::move(data);
        images.push_back(std::move(image));
        return int(images.size()) - 1;
    }
};

/// Key of a character position - the characters of the layout are found by it, to learn
/// their font and colour (the text layout keeps positions, but not fonts)
quint64 getPositionKey(QPointF point)
{
    const qint32 x = qint32(qRound(point.x() * 8.0));
    const qint32 y = qint32(qRound(point.y() * 8.0));
    return (quint64(quint32(x)) << 32) | quint64(quint32(y));
}

/// The content processor of one page: gives the characters to PDF4QT's text layout (the
/// same way as the text selection of the viewer does), remembers the style of each
/// character and collects the raster images with their placement
class OdtPageExtractor : public pdf::PDFPageContentProcessor
{
    using BaseClass = pdf::PDFPageContentProcessor;

public:
    explicit OdtPageExtractor(const pdf::PDFPage* page,
                              const pdf::PDFDocument* document,
                              const pdf::PDFFontCache* fontCache,
                              const pdf::PDFCMS* cms,
                              const pdf::PDFOptionalContentActivity* optionalContentActivity,
                              QTransform pagePointToDevicePointMatrix,
                              const pdf::PDFMeshQualitySettings& meshQualitySettings,
                              double pageHeight,
                              ConversionTables* tables) :
        BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_pageHeight(pageHeight),
        m_tables(tables)
    {

    }

    OdtLayoutPage createPage();

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performOutputCharacter(const pdf::PDFTextCharacterInfo& info) override;
    virtual bool performOriginalImagePainting(const pdf::PDFImage& image, const pdf::PDFStream* stream, pdf::PDFObjectReference reference) override;
    virtual void performImagePainting(const QImage& image) override;

private:
    struct FontInfo
    {
        QString family;
        bool bold = false;
        bool italic = false;
    };

    const FontInfo& getFontInfo(const pdf::PDFFont* font);
    QRectF getImageRect() const;
    void addImagePlacement(int imageIndex, const QRectF& rect);

    double m_pageHeight;
    ConversionTables* m_tables;
    pdf::PDFTextLayout m_layout;
    std::unordered_map<quint64, std::vector<std::pair<QChar, int>>> m_characterStyles;
    std::map<const pdf::PDFFont*, FontInfo> m_fonts;
    std::vector<OdtLayoutImage> m_images;
    pdf::PDFObjectReference m_pendingImageReference;
    bool m_hasInvisibleText = false;
};

bool OdtPageExtractor::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Shapes:
        case ContentKind::Text:     // the glyph outlines are not painted, characters are still reported
        case ContentKind::Shading:
            return true;

        case ContentKind::Images:
        case ContentKind::Tiling:
        case ContentKind::Forms:
            return false;

        default:
            break;
    }

    return false;
}

const OdtPageExtractor::FontInfo& OdtPageExtractor::getFontInfo(const pdf::PDFFont* font)
{
    auto it = m_fonts.find(font);
    if (it != m_fonts.end())
    {
        return it->second;
    }

    FontInfo info;
    if (font)
    {
        const pdf::FontDescriptor* descriptor = font->getFontDescriptor();
        QByteArray name = descriptor->fontName;

        // The standard 14 fonts often have no font descriptor - the name is given by the type
        if (name.isEmpty())
        {
            if (const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(font))
            {
                switch (simpleFont->getStandardFontType())
                {
                    case pdf::StandardFontType::TimesRoman: name = "Times-Roman"; break;
                    case pdf::StandardFontType::TimesRomanBold: name = "Times-Bold"; break;
                    case pdf::StandardFontType::TimesRomanItalics: name = "Times-Italic"; break;
                    case pdf::StandardFontType::TimesRomanBoldItalics: name = "Times-BoldItalic"; break;
                    case pdf::StandardFontType::Helvetica: name = "Helvetica"; break;
                    case pdf::StandardFontType::HelveticaBold: name = "Helvetica-Bold"; break;
                    case pdf::StandardFontType::HelveticaOblique: name = "Helvetica-Oblique"; break;
                    case pdf::StandardFontType::HelveticaBoldOblique: name = "Helvetica-BoldOblique"; break;
                    case pdf::StandardFontType::Courier: name = "Courier"; break;
                    case pdf::StandardFontType::CourierBold: name = "Courier-Bold"; break;
                    case pdf::StandardFontType::CourierOblique: name = "Courier-Oblique"; break;
                    case pdf::StandardFontType::CourierBoldOblique: name = "Courier-BoldOblique"; break;
                    case pdf::StandardFontType::Symbol: name = "Symbol"; break;
                    case pdf::StandardFontType::ZapfDingbats: name = "ZapfDingbats"; break;
                    default: break;
                }
            }
        }

        PdfToOdtConverter::parseFontName(name, &info.family, &info.bold, &info.italic);

        // The font descriptor may know more than the name ("Arial,Bold" is clear, "F1" is not)
        if (descriptor->fontWeight >= 600.0 || descriptor->isForceBold())
        {
            info.bold = true;
        }
        if (descriptor->isItalic() || qAbs(descriptor->italicAngle) > 1.0)
        {
            info.italic = true;
        }
        if (!descriptor->fontFamily.isEmpty())
        {
            info.family = QString::fromLatin1(descriptor->fontFamily);
        }

        info.family = PdfToOdtConverter::getSubstituteFamily(info.family, name, descriptor->isSerif(), descriptor->isFixedPitch(), m_tables->installedFontFamilies);
    }

    return m_fonts.emplace(font, std::move(info)).first->second;
}

void OdtPageExtractor::performOutputCharacter(const pdf::PDFTextCharacterInfo& originalInfo)
{
    pdf::PDFTextCharacterInfo info = originalInfo;

    // A ligature character (U+FB00..U+FB06, "ﬃ") is written as its letters - a word
    // processor shapes the ligature itself, and "oﬃcers" would not be found or spell-checked
    if (info.text.isEmpty() && info.character.unicode() >= 0xFB00 && info.character.unicode() <= 0xFB06)
    {
        info.text = info.character.decomposition();
    }

    if (isContentSuppressed() || (info.character.isSpace() && info.text.isEmpty()) || (info.character.isNull() && info.text.isEmpty()))
    {
        return;
    }

    const pdf::PDFPageContentProcessorState* state = getGraphicState();
    const pdf::TextRenderingMode mode = state->getTextRenderingMode();
    const bool invisible = mode == pdf::TextRenderingMode::Invisible || mode == pdf::TextRenderingMode::Clip;
    if (invisible)
    {
        m_hasInvisibleText = true;
    }

    const FontInfo& fontInfo = getFontInfo(state->getTextFont().get());

    OdtCharacterStyle style;
    style.fontFamily = fontInfo.family;
    style.italic = fontInfo.italic;
    // Text both filled and stroked is the way some producers make "bold" of a regular font
    style.bold = fontInfo.bold || mode == pdf::TextRenderingMode::FillStroke || mode == pdf::TextRenderingMode::FillStrokeClip;
    if (!invisible)
    {
        const bool stroked = mode == pdf::TextRenderingMode::Stroke || mode == pdf::TextRenderingMode::StrokeClip;
        style.color = stroked ? state->getStrokeColor() : state->getFillColor();
        if (style.color.isValid() && style.color.red() < 40 && style.color.green() < 40 && style.color.blue() < 40)
        {
            style.color = QColor();
        }
    }
    const int styleIndex = m_tables->getCharacterStyle(style);

    // The same split of a ligature as the text layout does - so the parts are found by
    // their positions
    if (info.text.size() > 1)
    {
        const qsizetype count = info.text.size();
        const pdf::PDFReal share = info.advance / count;
        for (qsizetype i = 0; i < count; ++i)
        {
            QTransform shift;
            shift.translate(info.isVerticalWritingSystem ? 0.0 : share * i, info.isVerticalWritingSystem ? share * i : 0.0);
            const QPointF position = (shift * info.matrix).map(QPointF(0.0, 0.0));
            m_characterStyles[getPositionKey(position)].emplace_back(info.text[i], styleIndex);
        }
    }
    else
    {
        const QPointF position = info.matrix.map(QPointF(0.0, 0.0));
        m_characterStyles[getPositionKey(position)].emplace_back(info.character, styleIndex);
    }

    m_layout.addCharacter(info);
}

QRectF OdtPageExtractor::getImageRect() const
{
    // The image is the unit square in its space, the world matrix maps it onto the page
    // (y up) - the bounding box in y down coordinates
    const QRectF rect = getCurrentWorldMatrix().mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
    return QRectF(rect.left(), m_pageHeight - rect.bottom(), rect.width(), rect.height());
}

void OdtPageExtractor::addImagePlacement(int imageIndex, const QRectF& rect)
{
    OdtLayoutImage image;
    image.imageIndex = imageIndex;
    image.rect = rect;
    m_images.push_back(image);
}

bool OdtPageExtractor::performOriginalImagePainting(const pdf::PDFImage& image, const pdf::PDFStream* stream, pdf::PDFObjectReference reference)
{
    Q_UNUSED(image);

    const QRectF rect = getImageRect();
    if (isContentSuppressed() || rect.width() < MINIMAL_IMAGE_SIZE || rect.height() < MINIMAL_IMAGE_SIZE)
    {
        // Not decoded at all - small images are decorations (rules, bullets, dots)
        return true;
    }

    // The same image object on many pages (a logo) is stored only once
    if (reference.isValid())
    {
        auto it = m_tables->imageByReference.find(reference);
        if (it != m_tables->imageByReference.end())
        {
            addImagePlacement(it->second, rect);
            return true;
        }
    }

    // A plain JPEG is written as it is - no quality loss, small file
    const pdf::PDFDictionary* dictionary = stream ? stream->getDictionary() : nullptr;
    if (dictionary && !dictionary->hasKey("SMask") && !dictionary->hasKey("Mask") && !dictionary->hasKey("Decode"))
    {
        const pdf::PDFDocument* document = getDocument();
        const pdf::PDFObject& filter = document->getObject(dictionary->get("Filter"));
        bool isDct = filter.isName() && filter.getString() == "DCTDecode";
        if (filter.isArray() && filter.getArray()->getCount() == 1)
        {
            const pdf::PDFObject& item = document->getObject(filter.getArray()->getItem(0));
            isDct = item.isName() && item.getString() == "DCTDecode";
        }

        const pdf::PDFObject& colorSpace = document->getObject(dictionary->get("ColorSpace"));
        const bool isSimpleColorSpace = colorSpace.isName() && (colorSpace.getString() == "DeviceRGB" || colorSpace.getString() == "DeviceGray");
        const QByteArray* content = stream->getContent();
        if (isDct && isSimpleColorSpace && content && content->startsWith("\xFF\xD8"))
        {
            const int imageIndex = m_tables->addImage(*content, true);
            if (reference.isValid())
            {
                m_tables->imageByReference[reference] = imageIndex;
            }
            addImagePlacement(imageIndex, rect);
            return true;
        }
    }

    // Otherwise the image is decoded (colour spaces, masks...) and written as PNG
    m_pendingImageReference = reference;
    return false;
}

void OdtPageExtractor::performImagePainting(const QImage& image)
{
    const pdf::PDFObjectReference reference = m_pendingImageReference;
    m_pendingImageReference = pdf::PDFObjectReference();

    const QRectF rect = getImageRect();
    if (image.isNull() || isContentSuppressed() || rect.width() < MINIMAL_IMAGE_SIZE || rect.height() < MINIMAL_IMAGE_SIZE)
    {
        return;
    }

    QImage exportedImage = image;
    if (exportedImage.width() > MAXIMAL_IMAGE_PIXELS || exportedImage.height() > MAXIMAL_IMAGE_PIXELS)
    {
        exportedImage = exportedImage.scaled(MAXIMAL_IMAGE_PIXELS, MAXIMAL_IMAGE_PIXELS, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    if (!exportedImage.save(&buffer, "PNG"))
    {
        return;
    }
    buffer.close();

    const int imageIndex = m_tables->addImage(std::move(data), false);
    if (reference.isValid())
    {
        m_tables->imageByReference[reference] = imageIndex;
    }
    addImagePlacement(imageIndex, rect);
}

OdtLayoutPage OdtPageExtractor::createPage()
{
    OdtLayoutPage page;
    page.height = m_pageHeight;
    page.images = std::move(m_images);
    page.hasInvisibleText = m_hasInvisibleText;

    m_layout.perform();

    auto findStyle = [this](const pdf::TextCharacter& character, int fallback)
    {
        const qint64 x = qRound(character.position.x() * 8.0);
        const qint64 y = qRound(character.position.y() * 8.0);

        // The position may differ in the last bit after the rotations of the layout -
        // the neighbouring keys are tried too
        for (int dx : { 0, -1, 1 })
        {
            for (int dy : { 0, -1, 1 })
            {
                const quint64 key = (quint64(quint32(qint32(x + dx))) << 32) | quint64(quint32(qint32(y + dy)));
                auto it = m_characterStyles.find(key);
                if (it != m_characterStyles.end())
                {
                    for (const auto& [characterValue, style] : it->second)
                    {
                        if (characterValue == character.character)
                        {
                            return style;
                        }
                    }
                }
            }
        }
        return fallback;
    };

    int blockIndex = 0;
    for (const pdf::PDFTextBlock& block : m_layout.getTextBlocks())
    {
        // Text, which is not horizontal, is turned to horizontal (for the order of its
        // characters and the gaps between words); it is marked as rotated
        pdf::PDFReal angle = block.getAngle();
        const bool rotated = angle > 2.0 && angle < 358.0;

        pdf::PDFTextBlock horizontalBlock = block;
        if (rotated)
        {
            QTransform angleMatrix;
            angleMatrix.rotate(angle);
            horizontalBlock.applyTransform(angleMatrix);
        }

        const pdf::PDFTextLines& lines = block.getLines();
        const pdf::PDFTextLines& horizontalLines = horizontalBlock.getLines();
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const pdf::TextCharacters& characters = lines[lineIndex].getCharacters();
            const pdf::TextCharacters& horizontalCharacters = horizontalLines[lineIndex].getCharacters();

            OdtLayoutLine line;
            line.block = blockIndex;
            line.rotated = rotated;

            int previousStyle = 0;
            for (size_t i = 0; i < characters.size() && i < horizontalCharacters.size(); ++i)
            {
                const pdf::TextCharacter& character = horizontalCharacters[i];

                OdtLayoutCharacter layoutCharacter;
                layoutCharacter.character = character.character;
                layoutCharacter.x = character.position.x();
                layoutCharacter.right = character.position.x() + character.advance;
                layoutCharacter.baseline = m_pageHeight - character.position.y();
                layoutCharacter.fontSize = character.fontSize;
                layoutCharacter.style = findStyle(characters[i], previousStyle);
                previousStyle = layoutCharacter.style;
                line.characters.push_back(layoutCharacter);
            }

            // The layout sorts by x already - after the rotation, sort again
            std::sort(line.characters.begin(), line.characters.end(), [](const OdtLayoutCharacter& l, const OdtLayoutCharacter& r) { return l.x < r.x; });
            if (!line.characters.empty())
            {
                page.lines.push_back(std::move(line));
            }
        }

        ++blockIndex;
    }

    return page;
}

}   // namespace

PdfToOdtConverter::PdfToOdtConverter(pdf::PDFDocument document, Settings settings) :
    m_pdfDocument(std::move(document)),
    m_settings(std::move(settings))
{

}

void PdfToOdtConverter::parseFontName(QByteArray fontName, QString* family, bool* bold, bool* italic)
{
    // Subset prefix "ABCDEF+"
    if (fontName.size() > 7 && fontName[6] == '+')
    {
        bool isPrefix = true;
        for (int i = 0; i < 6; ++i)
        {
            isPrefix = isPrefix && fontName[i] >= 'A' && fontName[i] <= 'Z';
        }
        if (isPrefix)
        {
            fontName = fontName.mid(7);
        }
    }

    const QString name = QString::fromLatin1(fontName);
    const QString lowerName = name.toLower();

    *bold = lowerName.contains("bold") || lowerName.contains("black") || lowerName.contains("heavy") ||
            lowerName.contains("semibold") || lowerName.contains("demi") || lowerName.endsWith(",bd") || lowerName.endsWith("-bd");
    *italic = lowerName.contains("italic") || lowerName.contains("oblique") || lowerName.endsWith("-it") ||
              lowerName.endsWith(",it") || lowerName.endsWith("-bi") || lowerName.endsWith("-boldit");

    // Family = the name up to the style part ("Arial-BoldMT", "Arial,Bold", "TimesNewRomanPS-ItalicMT")
    QString base = name.section(QChar('-'), 0, 0).section(QChar(','), 0, 0);
    if (base.isEmpty())
    {
        base = name;
    }

    // Well known PostScript names - the family name, which a word processor knows
    static const std::pair<const char*, const char*> knownFamilies[] = {
        { "TimesNewRomanPSMT", "Times New Roman" },
        { "TimesNewRomanPS", "Times New Roman" },
        { "TimesNewRoman", "Times New Roman" },
        { "Times", "Times New Roman" },
        { "ArialMT", "Arial" },
        { "Arial", "Arial" },
        { "Helvetica", "Helvetica" },
        { "CourierNewPSMT", "Courier New" },
        { "CourierNewPS", "Courier New" },
        { "CourierNew", "Courier New" },
        { "Courier", "Courier New" },
        { "ArialNarrow", "Arial Narrow" },
        { "BookAntiqua", "Book Antiqua" },
        { "CenturyGothic", "Century Gothic" },
        { "ComicSansMS", "Comic Sans MS" },
        { "SegoeUI", "Segoe UI" },
        { "TrebuchetMS", "Trebuchet MS" },
        { "PalatinoLinotype", "Palatino Linotype" },
        { "LiberationSans", "Liberation Sans" },
        { "LiberationSerif", "Liberation Serif" },
        { "LiberationMono", "Liberation Mono" },
        { "DejaVuSans", "DejaVu Sans" },
        { "DejaVuSerif", "DejaVu Serif" },
        { "DejaVuSansMono", "DejaVu Sans Mono" },
        { "NotoSans", "Noto Sans" },
        { "NotoSerif", "Noto Serif" },
        { "OpenSans", "Open Sans" },
    };

    // Style words glued to the family ("CalibriBold", "Calibri-Bold" is handled above)
    static const char* styleSuffixes[] = { "BoldItalic", "BoldOblique", "Bold", "Italic", "Oblique", "Regular", "Roman", "MT", "PS" };
    bool chopped = true;
    while (chopped)
    {
        chopped = false;
        for (const char* suffix : styleSuffixes)
        {
            if (base.size() > qsizetype(strlen(suffix)) + 2 && base.endsWith(QLatin1String(suffix)) && base != "TimesNewRomanPS")
            {
                base.chop(qsizetype(strlen(suffix)));
                chopped = true;
            }
        }
    }

    *family = base;
    for (const auto& [postScriptName, familyName] : knownFamilies)
    {
        if (base == QLatin1String(postScriptName))
        {
            *family = QString::fromLatin1(familyName);
            break;
        }
    }
}

QString PdfToOdtConverter::getSubstituteFamily(const QString& family, const QByteArray& fontName, bool serif, bool monospace, const QStringList& installedFamilies)
{
    if (installedFamilies.isEmpty() || family.isEmpty() || installedFamilies.contains(family, Qt::CaseInsensitive))
    {
        return family;
    }

    // Well known families with metric compatible twins are kept as they are, even when
    // they are not installed here: Word has Calibri and Arial, Writer replaces them by
    // Carlito and Liberation Sans itself - the same text takes the same space everywhere
    static const QStringList knownFamilies = { "Carlito", "Calibri", "Caladea", "Cambria", "Liberation Sans", "Arial", "Helvetica", "Arimo",
                                               "Liberation Serif", "Times New Roman", "Times", "Tinos", "Liberation Mono", "Courier New",
                                               "Courier", "Cousine", "Liberation Sans Narrow", "Arial Narrow", "Symbol" };
    if (knownFamilies.contains(family, Qt::CaseInsensitive))
    {
        return family;
    }

    // The kind of the font from its name, when the font descriptor does not say it
    // (TeX: CMR = Computer Modern Roman, CMTT = typewriter, CMSS = sans serif)
    const QString name = QString::fromLatin1(fontName).toLower();
    if (name.contains("mono") || name.contains("nimbusmon") || name.contains("courier") || name.contains("consol") || name.contains("cmtt") || name.contains("typewriter") || name.contains("lmtt"))
    {
        monospace = true;
    }
    else if (name.contains("cmss") || name.contains("lmsans") || name.contains("sans") || name.contains("helvet") || name.contains("nimbussan") || name.contains("arial") || name.contains("gothic"))
    {
        serif = false;
    }
    else if (name.contains("cmr") || name.contains("nimbusrom") || name.contains("century") || name.contains("schoolbook") || name.contains("bookman") || name.contains("palladio") || name.contains("cmbx") || name.contains("cmti") || name.contains("cmsl") || name.contains("lmroman") || name.contains("roman") ||
             name.contains("times") || name.contains("serif") || name.contains("garamond") || name.contains("georgia") || name.contains("book") ||
             name.contains("minion") || name.contains("palatino") || name.contains("cambria") || name.contains("caladea"))
    {
        serif = true;
    }

    // Metric compatible families first (they take the same space as the common fonts)
    QStringList candidates;
    if (monospace)
    {
        candidates = { "Courier New", "Liberation Mono", "DejaVu Sans Mono" };
    }
    else if (serif)
    {
        candidates = { "Times New Roman", "Liberation Serif", "DejaVu Serif" };
    }
    else
    {
        candidates = { "Arial", "Liberation Sans", "DejaVu Sans" };
    }

    for (const QString& candidate : candidates)
    {
        if (installedFamilies.contains(candidate, Qt::CaseInsensitive))
        {
            return candidate;
        }
    }
    return family;
}

void PdfToOdtConverter::run()
{
    try
    {
        const pdf::PDFDocument* document = &m_pdfDocument;
        const pdf::PDFCatalog* catalog = document->getCatalog();

        pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        pdf::PDFCMSGeneric cms;
        pdf::PDFMeshQualitySettings meshQualitySettings;
        pdf::PDFOptionalContentActivity optionalContentActivity(document, pdf::OCUsage::Export, nullptr);
        pdf::PDFModifiedDocument modifiedDocument(const_cast<pdf::PDFDocument*>(document), &optionalContentActivity);
        fontCache.setDocument(modifiedDocument);
        fontCache.setCacheShrinkEnabled(nullptr, false);

        ConversionTables tables;
        tables.installedFontFamilies = m_settings.installedFontFamilies;
        tables.characterStyles.push_back(OdtCharacterStyle());

        std::vector<OdtLayoutPage> pages;
        for (pdf::PDFInteger pageIndex : m_settings.pages)
        {
            if (m_cancelled)
            {
                return;
            }

            m_currentPageNumber = int(pageIndex + 1);
            const pdf::PDFPage* page = (pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount()) ? catalog->getPage(pageIndex) : nullptr;
            if (!page)
            {
                ++m_progress;
                continue;
            }

            // The page as it is shown (crop box, rotation), in points, y up - the text layout
            // expects y up; OdtPageExtractor turns it to y down
            const QRectF cropBox = page->getRotatedCropBox();
            const QSizeF pageSize = cropBox.size();
            const pdf::PageRotation rotation = page->getPageRotation();
            const QRectF rotatedCropBox = pdf::PDFPage::getRotatedBox(page->getCropBox(), rotation);
            QTransform toDevice = pdf::PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedCropBox, QRectF(QPointF(0, 0), pageSize), rotation);
            QTransform flip(1.0, 0.0, 0.0, -1.0, 0.0, pageSize.height());
            const QTransform matrix = toDevice * flip;

            OdtPageExtractor extractor(page, document, &fontCache, &cms, &optionalContentActivity, matrix, meshQualitySettings, pageSize.height(), &tables);
            extractor.processContents();

            OdtLayoutPage layoutPage = extractor.createPage();
            layoutPage.width = pageSize.width();
            layoutPage.pageNumber = int(pageIndex + 1);
            pages.push_back(std::move(layoutPage));

            ++m_progress;
        }

        if (m_cancelled)
        {
            return;
        }

        OdtLayoutBuilder::Settings layoutSettings;
        layoutSettings.moveHeadersAndFooters = m_settings.moveHeadersAndFooters;
        layoutSettings.documentPageCount = int(catalog->getPageCount());
        layoutSettings.title = m_settings.title;

        m_document = OdtLayoutBuilder::build(std::move(pages), tables.characterStyles, std::move(tables.images), layoutSettings);
        m_package = OdtWriter::createPackage(m_document);
        ++m_progress;
    }
    catch (const pdf::PDFException& exception)
    {
        m_errorMessage = exception.getMessage();
        m_package.clear();
    }
    catch (const std::exception& exception)
    {
        m_errorMessage = QString::fromLocal8Bit(exception.what());
        m_package.clear();
    }
}

}   // namespace pdfplugin
