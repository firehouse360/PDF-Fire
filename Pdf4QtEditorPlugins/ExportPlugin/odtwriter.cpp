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

#include "odtwriter.h"
#include "odtzipwriter.h"

#include <QBuffer>
#include <QDateTime>
#include <QXmlStreamWriter>

#include <map>
#include <set>

namespace pdfplugin
{

namespace
{

constexpr const char* NS_DECLARATIONS =
        " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
        " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
        " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
        " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\""
        " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
        " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
        " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
        " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"";

QString pt(double value)
{
    // Lengths in points, rounded to 0.01 pt - enough for any layout, short in the XML
    return QString::number(qRound(value * 100.0) / 100.0, 'f', 2) + QLatin1String("pt");
}

QString fontFaceName(const QString& family)
{
    return family;
}

/// svg:font-family needs quotes around a family name with spaces
QString svgFontFamily(const QString& family)
{
    return family.contains(QChar(' ')) ? QChar('\'') + family + QChar('\'') : family;
}

/// Character formatting of a span, reduced to what differs from its paragraph style
OdtTextStyle getStyleDifference(const OdtTextStyle& style, const OdtTextStyle& base)
{
    OdtTextStyle difference;
    if (!style.fontFamily.isEmpty() && style.fontFamily != base.fontFamily)
    {
        difference.fontFamily = style.fontFamily;
    }
    if (style.fontSize > 0.0 && qAbs(style.fontSize - base.fontSize) >= 0.25)
    {
        difference.fontSize = style.fontSize;
    }
    difference.bold = style.bold;
    difference.italic = style.italic;
    difference.color = style.color;
    return difference;
}

bool isEmptyDifference(const OdtTextStyle& difference, const OdtTextStyle& base)
{
    const QRgb baseColor = base.color.isValid() ? base.color.rgb() : qRgb(0, 0, 0);
    const QRgb color = difference.color.isValid() ? difference.color.rgb() : qRgb(0, 0, 0);
    return difference.fontFamily.isEmpty() && difference.fontSize == 0.0 &&
           difference.bold == base.bold && difference.italic == base.italic && baseColor == color;
}

void writeTextProperties(QXmlStreamWriter& writer, const OdtTextStyle& style, bool writeAll)
{
    writer.writeStartElement("style:text-properties");
    if (!style.fontFamily.isEmpty())
    {
        writer.writeAttribute("style:font-name", fontFaceName(style.fontFamily));
    }
    if (style.fontSize > 0.0)
    {
        // Word processors use half points - keep the size exact anyway
        writer.writeAttribute("fo:font-size", pt(style.fontSize));
    }
    if (style.bold || writeAll)
    {
        writer.writeAttribute("fo:font-weight", style.bold ? "bold" : "normal");
    }
    if (style.italic || writeAll)
    {
        writer.writeAttribute("fo:font-style", style.italic ? "italic" : "normal");
    }
    if (style.color.isValid() || writeAll)
    {
        writer.writeAttribute("fo:color", style.color.isValid() ? style.color.name(QColor::HexRgb) : QString("#000000"));
    }
    writer.writeEndElement();
}

/// Automatic styles of one XML part (content.xml or styles.xml - the styles of the page
/// header and footer must be in styles.xml, the body ones in content.xml)
class AutomaticStyles
{
public:
    explicit AutomaticStyles(const OdtDocument& document, QString prefix) :
        m_document(document),
        m_prefix(std::move(prefix))
    {

    }

    struct ParagraphKey
    {
        QString parent;
        int alignment = 0;
        int marginLeft = 0;
        int textIndent = 0;
        int spaceBefore = 0;
        bool pageBreak = false;
        bool listItem = false;
        int lineHeight = 0;
        std::vector<int> tabStops;

        auto tie() const { return std::tie(parent, alignment, marginLeft, textIndent, spaceBefore, pageBreak, listItem, lineHeight, tabStops); }
        bool operator<(const ParagraphKey& other) const { return tie() < other.tie(); }
    };

    struct ListKey
    {
        QChar bullet;
        int marginLeft = 0;
        int textIndent = 0;

        auto tie() const { return std::tie(bullet, marginLeft, textIndent); }
        bool operator<(const ListKey& other) const { return tie() < other.tie(); }
    };

    static QString getParentStyleName(const OdtParagraph& paragraph, const QString& defaultStyle)
    {
        if (paragraph.headingLevel >= 1 && paragraph.headingLevel <= 3)
        {
            return QString("Heading_20_%1").arg(paragraph.headingLevel);
        }
        return defaultStyle;
    }

    const OdtTextStyle& getParentTextStyle(const OdtParagraph& paragraph) const
    {
        if (paragraph.headingLevel >= 1 && paragraph.headingLevel <= 3)
        {
            return m_document.headingStyles[paragraph.headingLevel - 1].text;
        }
        return m_document.bodyStyle.text;
    }

    QString getParagraphStyle(const OdtParagraph& paragraph, const QString& defaultStyle)
    {
        ParagraphKey key;
        key.parent = getParentStyleName(paragraph, defaultStyle);
        key.alignment = int(paragraph.alignment);
        key.listItem = !paragraph.listBullet.isNull();
        // The indentation of a list item is given by its list style (an indentation of the
        // paragraph would override it and the bullet would be misplaced)
        key.marginLeft = key.listItem ? 0 : qRound(paragraph.marginLeft * 10.0);
        key.textIndent = key.listItem ? 0 : qRound(paragraph.textIndent * 10.0);
        key.spaceBefore = qRound(paragraph.spaceBefore * 10.0);
        key.pageBreak = paragraph.pageBreakBefore;
        key.lineHeight = qRound(paragraph.lineHeight * 10.0);
        for (double stop : paragraph.tabStops)
        {
            key.tabStops.push_back(qRound(stop * 10.0));
        }

        auto it = m_paragraphStyles.find(key);
        if (it == m_paragraphStyles.end())
        {
            it = m_paragraphStyles.emplace(key, QString("%1P%2").arg(m_prefix).arg(m_paragraphStyles.size() + 1)).first;
        }
        return it->second;
    }

    /// Returns the name of the text style, or an empty string, when the span has the
    /// formatting of its paragraph
    QString getTextStyle(const OdtTextStyle& style, const OdtTextStyle& base)
    {
        const OdtTextStyle difference = getStyleDifference(style, base);
        if (isEmptyDifference(difference, base))
        {
            return QString();
        }

        // A span with explicit "not bold" or "black" in a bold/coloured heading must say so -
        // write all three switchable properties then
        auto it = m_textStyles.find(difference);
        if (it == m_textStyles.end())
        {
            it = m_textStyles.emplace(difference, QString("%1T%2").arg(m_prefix).arg(m_textStyles.size() + 1)).first;
        }
        return it->second;
    }

    QString getListStyle(const OdtParagraph& paragraph)
    {
        ListKey key;
        key.bullet = paragraph.listBullet;
        key.marginLeft = qRound(paragraph.marginLeft * 10.0);
        key.textIndent = qRound(paragraph.textIndent * 10.0);

        auto it = m_listStyles.find(key);
        if (it == m_listStyles.end())
        {
            it = m_listStyles.emplace(key, QString("%1L%2").arg(m_prefix).arg(m_listStyles.size() + 1)).first;
        }
        return it->second;
    }

    void write(QXmlStreamWriter& writer) const
    {
        for (const auto& [key, name] : m_paragraphStyles)
        {
            writer.writeStartElement("style:style");
            writer.writeAttribute("style:name", name);
            writer.writeAttribute("style:family", "paragraph");
            writer.writeAttribute("style:parent-style-name", key.parent);
            writer.writeStartElement("style:paragraph-properties");
            static const char* alignments[] = { "start", "center", "end", "justify" };
            writer.writeAttribute("fo:text-align", alignments[qBound(0, key.alignment, 3)]);
            if (!key.listItem)
            {
                writer.writeAttribute("fo:margin-left", pt(key.marginLeft / 10.0));
                writer.writeAttribute("fo:text-indent", pt(key.textIndent / 10.0));
            }
            writer.writeAttribute("fo:margin-top", pt(key.spaceBefore / 10.0));
            if (key.pageBreak)
            {
                writer.writeAttribute("fo:break-before", "page");
            }
            if (key.lineHeight > 0)
            {
                writer.writeAttribute("fo:line-height", pt(key.lineHeight / 10.0));
            }
            if (!key.tabStops.empty())
            {
                writer.writeStartElement("style:tab-stops");
                for (int stop : key.tabStops)
                {
                    writer.writeStartElement("style:tab-stop");
                    writer.writeAttribute("style:position", pt(stop / 10.0));
                    writer.writeEndElement();
                }
                writer.writeEndElement();
            }
            writer.writeEndElement();
            writer.writeEndElement();
        }

        for (const auto& [style, name] : m_textStyles)
        {
            writer.writeStartElement("style:style");
            writer.writeAttribute("style:name", name);
            writer.writeAttribute("style:family", "text");
            writeTextProperties(writer, style, true);
            writer.writeEndElement();
        }

        for (const auto& [key, name] : m_listStyles)
        {
            const double marginLeft = key.marginLeft / 10.0;
            const double textIndent = key.textIndent / 10.0;

            writer.writeStartElement("text:list-style");
            writer.writeAttribute("style:name", name);
            writer.writeStartElement("text:list-level-style-bullet");
            writer.writeAttribute("text:level", "1");
            writer.writeAttribute("text:bullet-char", QString(key.bullet));
            writer.writeStartElement("style:list-level-properties");
            writer.writeAttribute("text:list-level-position-and-space-mode", "label-alignment");
            writer.writeStartElement("style:list-level-label-alignment");
            writer.writeAttribute("text:label-followed-by", "listtab");
            writer.writeAttribute("text:list-tab-stop-position", pt(marginLeft));
            writer.writeAttribute("fo:text-indent", pt(textIndent));
            writer.writeAttribute("fo:margin-left", pt(marginLeft));
            writer.writeEndElement();
            writer.writeEndElement();
            writer.writeEndElement();
            writer.writeEndElement();
        }

        for (const QString& name : m_graphicStyles)
        {
            const bool pageAnchored = name.endsWith("page");
            writer.writeStartElement("style:style");
            writer.writeAttribute("style:name", name);
            writer.writeAttribute("style:family", "graphic");
            writer.writeAttribute("style:parent-style-name", "Graphics");
            writer.writeStartElement("style:graphic-properties");
            if (pageAnchored)
            {
                // A letterhead logo: exactly where it was on the PDF page, the text flows
                // around nothing (it is in the header area anyway)
                writer.writeAttribute("style:wrap", "run-through");
                writer.writeAttribute("style:run-through", "foreground");
                writer.writeAttribute("style:vertical-pos", "from-top");
                writer.writeAttribute("style:vertical-rel", "page");
                writer.writeAttribute("style:horizontal-pos", "from-left");
                writer.writeAttribute("style:horizontal-rel", "page");
                writer.writeAttribute("style:flow-with-text", "false");
            }
            else
            {
                writer.writeAttribute("style:vertical-pos", "top");
                writer.writeAttribute("style:vertical-rel", "baseline");
            }
            writer.writeEndElement();
            writer.writeEndElement();
        }
    }

    QString getGraphicStyle(bool pageAnchored)
    {
        const QString name = m_prefix + (pageAnchored ? QString("fr_page") : QString("fr_char"));
        m_graphicStyles.insert(name);
        return name;
    }

private:
    const OdtDocument& m_document;
    QString m_prefix;
    std::map<ParagraphKey, QString> m_paragraphStyles;
    std::map<OdtTextStyle, QString> m_textStyles;
    std::map<ListKey, QString> m_listStyles;
    std::set<QString> m_graphicStyles;
};

/// Writes text with the white space rules of ODF: a run of spaces is collapsed to one,
/// unless written as <text:s text:c="n"/>, a tab is <text:tab/>, a line break is
/// <text:line-break/>
void writeOdfText(QXmlStreamWriter& writer, const QString& text)
{
    QString pending;
    auto flush = [&]()
    {
        if (!pending.isEmpty())
        {
            writer.writeCharacters(pending);
            pending.clear();
        }
    };

    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar character = text[i];
        if (character == QChar('\t'))
        {
            flush();
            writer.writeEmptyElement("text:tab");
        }
        else if (character == QChar('\n'))
        {
            flush();
            writer.writeEmptyElement("text:line-break");
        }
        else if (character == QChar(' '))
        {
            // The first space is kept as a character (unless it starts the paragraph), the
            // following ones of the run become <text:s/>
            qsizetype count = 1;
            while (i + count < text.size() && text[i + count] == QChar(' '))
            {
                ++count;
            }

            qsizetype extra = count;
            if (i > 0)
            {
                pending += QChar(' ');
                extra = count - 1;
            }

            if (extra > 0)
            {
                flush();
                writer.writeEmptyElement("text:s");
                if (extra > 1)
                {
                    writer.writeAttribute("text:c", QString::number(extra));
                }
            }
            i += count - 1;
        }
        else if (character.unicode() < 0x20 || character == QChar(0xFFFE) || character == QChar(0xFFFF))
        {
            // Characters not allowed in XML 1.0 are dropped
        }
        else if (character.isHighSurrogate())
        {
            if (i + 1 < text.size() && text[i + 1].isLowSurrogate())
            {
                pending += character;
                pending += text[i + 1];
                ++i;
            }
        }
        else if (character.isLowSurrogate())
        {
            // A lone low surrogate is not valid XML
        }
        else
        {
            pending += character;
        }
    }

    flush();
}

void writeImageFrame(QXmlStreamWriter& writer, const OdtDocument& document, AutomaticStyles& styles, int imageIndex,
                     double width, double height, int frameNumber, const OdtPageImage* pageImage)
{
    if (imageIndex < 0 || imageIndex >= int(document.images.size()))
    {
        return;
    }

    writer.writeStartElement("draw:frame");
    writer.writeAttribute("draw:style-name", styles.getGraphicStyle(pageImage != nullptr));
    writer.writeAttribute("draw:name", QString("Image%1").arg(frameNumber));
    writer.writeAttribute("text:anchor-type", pageImage ? "paragraph" : "as-char");
    if (pageImage)
    {
        writer.writeAttribute("svg:x", pt(pageImage->x));
        writer.writeAttribute("svg:y", pt(pageImage->y));
    }
    writer.writeAttribute("svg:width", pt(width));
    writer.writeAttribute("svg:height", pt(height));
    writer.writeAttribute("draw:z-index", QString::number(frameNumber));
    writer.writeStartElement("draw:image");
    writer.writeAttribute("xlink:href", document.images[imageIndex].fileName);
    writer.writeAttribute("xlink:type", "simple");
    writer.writeAttribute("xlink:show", "embed");
    writer.writeAttribute("xlink:actuate", "onLoad");
    writer.writeEndElement();
    writer.writeEndElement();
}

/// Writes a sequence of paragraphs (body, header or footer)
void writeParagraphs(QXmlStreamWriter& writer,
                     const OdtDocument& document,
                     const std::vector<OdtParagraph>& paragraphs,
                     AutomaticStyles& styles,
                     const QString& defaultStyle,
                     int& frameNumber,
                     const std::vector<OdtPageImage>* pageImages)
{
    bool listOpen = false;
    QString openListStyle;

    for (size_t index = 0; index < paragraphs.size(); ++index)
    {
        const OdtParagraph& paragraph = paragraphs[index];
        const bool isListItem = !paragraph.listBullet.isNull() && paragraph.headingLevel == 0;

        // Consecutive list items with the same list style form one list
        const QString listStyle = isListItem ? styles.getListStyle(paragraph) : QString();
        if (listOpen && (!isListItem || listStyle != openListStyle || paragraph.pageBreakBefore))
        {
            writer.writeEndElement(); // text:list
            listOpen = false;
        }
        if (isListItem && !listOpen)
        {
            writer.writeStartElement("text:list");
            writer.writeAttribute("text:style-name", listStyle);
            listOpen = true;
            openListStyle = listStyle;
        }
        if (isListItem)
        {
            writer.writeStartElement("text:list-item");
        }

        const bool isHeading = paragraph.headingLevel >= 1 && paragraph.headingLevel <= 3;
        writer.writeStartElement(isHeading ? "text:h" : "text:p");
        writer.writeAttribute("text:style-name", styles.getParagraphStyle(paragraph, defaultStyle));
        if (isHeading)
        {
            writer.writeAttribute("text:outline-level", QString::number(paragraph.headingLevel));
        }

        // The images of the page header are anchored at its first paragraph
        if (index == 0 && pageImages)
        {
            for (const OdtPageImage& pageImage : *pageImages)
            {
                writeImageFrame(writer, document, styles, pageImage.imageIndex, pageImage.width, pageImage.height, ++frameNumber, &pageImage);
            }
        }

        if (paragraph.imageIndex >= 0)
        {
            writeImageFrame(writer, document, styles, paragraph.imageIndex, paragraph.imageWidth, paragraph.imageHeight, ++frameNumber, nullptr);
        }

        const OdtTextStyle& baseStyle = styles.getParentTextStyle(paragraph);
        for (const OdtSpan& span : paragraph.spans)
        {
            const QString textStyle = styles.getTextStyle(span.style, baseStyle);
            if (!textStyle.isEmpty())
            {
                writer.writeStartElement("text:span");
                writer.writeAttribute("text:style-name", textStyle);
            }

            switch (span.kind)
            {
                case OdtSpan::Kind::Text:
                    writeOdfText(writer, span.text);
                    break;

                case OdtSpan::Kind::PageNumber:
                    writer.writeStartElement("text:page-number");
                    writer.writeAttribute("text:select-page", "current");
                    writer.writeCharacters(span.text);
                    writer.writeEndElement();
                    break;

                case OdtSpan::Kind::PageCount:
                    writer.writeStartElement("text:page-count");
                    writer.writeCharacters(span.text);
                    writer.writeEndElement();
                    break;
            }

            if (!textStyle.isEmpty())
            {
                writer.writeEndElement();
            }
        }

        writer.writeEndElement(); // text:p / text:h
        if (isListItem)
        {
            writer.writeEndElement(); // text:list-item
        }
    }

    if (listOpen)
    {
        writer.writeEndElement();
    }
}

/// Collects the font families of the document, for office:font-face-decls
std::set<QString> getFontFamilies(const OdtDocument& document)
{
    std::set<QString> families;
    auto addStyle = [&families](const OdtTextStyle& style)
    {
        if (!style.fontFamily.isEmpty())
        {
            families.insert(style.fontFamily);
        }
    };

    addStyle(document.bodyStyle.text);
    for (const OdtNamedStyle& style : document.headingStyles)
    {
        addStyle(style.text);
    }

    for (const std::vector<OdtParagraph>* paragraphs : { &document.body, &document.header, &document.footer })
    {
        for (const OdtParagraph& paragraph : *paragraphs)
        {
            for (const OdtSpan& span : paragraph.spans)
            {
                addStyle(span.style);
            }
        }
    }

    return families;
}

void writeFontFaceDecls(QXmlStreamWriter& writer, const OdtDocument& document)
{
    writer.writeStartElement("office:font-face-decls");
    for (const QString& family : getFontFamilies(document))
    {
        writer.writeStartElement("style:font-face");
        writer.writeAttribute("style:name", fontFaceName(family));
        writer.writeAttribute("svg:font-family", svgFontFamily(family));
        writer.writeEndElement();
    }
    writer.writeEndElement();
}

/// Returns XML of a fragment written by \p function, without the XML declaration
template<typename Function>
QByteArray writeFragment(Function function)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buffer);
    writer.setAutoFormatting(false);
    function(writer);
    buffer.close();
    return data;
}

}   // namespace

QByteArray OdtWriter::createContentXml(const OdtDocument& document)
{
    AutomaticStyles styles(document, QString());
    int frameNumber = 100;

    // The body is written first - it collects the automatic styles, which must be written
    // before it in the file
    QByteArray body = writeFragment([&](QXmlStreamWriter& writer)
    {
        writer.writeStartElement("office:body");
        writer.writeStartElement("office:text");
        writeParagraphs(writer, document, document.body, styles, "Standard", frameNumber, nullptr);
        if (document.body.empty())
        {
            writer.writeEmptyElement("text:p");
        }
        writer.writeEndElement();
        writer.writeEndElement();
    });

    QByteArray fonts = writeFragment([&](QXmlStreamWriter& writer) { writeFontFaceDecls(writer, document); });
    QByteArray automaticStyles = writeFragment([&](QXmlStreamWriter& writer)
    {
        writer.writeStartElement("office:automatic-styles");
        styles.write(writer);
        writer.writeEndElement();
    });

    QByteArray result = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    result += QByteArray("<office:document-content") + NS_DECLARATIONS + " office:version=\"1.3\">";
    result += fonts;
    result += automaticStyles;
    result += body;
    result += "</office:document-content>";
    return result;
}

QByteArray OdtWriter::createStylesXml(const OdtDocument& document)
{
    AutomaticStyles styles(document, QString("M"));
    int frameNumber = 0;

    const bool hasHeader = !document.header.empty() || !document.headerImages.empty();
    const bool hasFooter = !document.footer.empty();

    QByteArray masterStyles = writeFragment([&](QXmlStreamWriter& writer)
    {
        writer.writeStartElement("office:master-styles");
        writer.writeStartElement("style:master-page");
        writer.writeAttribute("style:name", "Standard");
        writer.writeAttribute("style:page-layout-name", "pm1");

        if (hasHeader)
        {
            writer.writeStartElement("style:header");
            std::vector<OdtParagraph> header = document.header;
            if (header.empty())
            {
                // An anchor for the images
                header.emplace_back();
            }
            writeParagraphs(writer, document, header, styles, "Header", frameNumber, &document.headerImages);
            writer.writeEndElement();
        }

        if (hasFooter)
        {
            writer.writeStartElement("style:footer");
            writeParagraphs(writer, document, document.footer, styles, "Footer", frameNumber, nullptr);
            writer.writeEndElement();
        }

        writer.writeEndElement();
        writer.writeEndElement();
    });

    QByteArray namedStyles = writeFragment([&](QXmlStreamWriter& writer)
    {
        writer.writeStartElement("office:styles");

        writer.writeStartElement("style:default-style");
        writer.writeAttribute("style:family", "paragraph");
        writer.writeStartElement("style:paragraph-properties");
        writer.writeAttribute("style:writing-mode", "page");
        writer.writeEndElement();
        OdtTextStyle defaultText = document.bodyStyle.text;
        writeTextProperties(writer, defaultText, false);
        writer.writeEndElement();

        // Standard = the body text. Word shows it as "Normal".
        writer.writeStartElement("style:style");
        writer.writeAttribute("style:name", "Standard");
        writer.writeAttribute("style:family", "paragraph");
        writer.writeAttribute("style:class", "text");
        writer.writeStartElement("style:paragraph-properties");
        writer.writeAttribute("fo:margin-top", "0pt");
        writer.writeAttribute("fo:margin-bottom", "0pt");
        writer.writeEndElement();
        writeTextProperties(writer, document.bodyStyle.text, true);
        writer.writeEndElement();

        writer.writeStartElement("style:style");
        writer.writeAttribute("style:name", "Heading");
        writer.writeAttribute("style:family", "paragraph");
        writer.writeAttribute("style:parent-style-name", "Standard");
        writer.writeAttribute("style:next-style-name", "Standard");
        writer.writeAttribute("style:class", "text");
        writer.writeStartElement("style:paragraph-properties");
        writer.writeAttribute("fo:keep-with-next", "always");
        writer.writeEndElement();
        writer.writeEndElement();

        for (int level = 1; level <= 3; ++level)
        {
            writer.writeStartElement("style:style");
            writer.writeAttribute("style:name", QString("Heading_20_%1").arg(level));
            writer.writeAttribute("style:display-name", QString("Heading %1").arg(level));
            writer.writeAttribute("style:family", "paragraph");
            writer.writeAttribute("style:parent-style-name", "Heading");
            writer.writeAttribute("style:next-style-name", "Standard");
            writer.writeAttribute("style:default-outline-level", QString::number(level));
            writer.writeAttribute("style:class", "text");
            writeTextProperties(writer, document.headingStyles[level - 1].text, true);
            writer.writeEndElement();
        }

        for (const char* name : { "Header", "Footer" })
        {
            writer.writeStartElement("style:style");
            writer.writeAttribute("style:name", name);
            writer.writeAttribute("style:family", "paragraph");
            writer.writeAttribute("style:parent-style-name", "Standard");
            writer.writeAttribute("style:class", "extra");
            writer.writeEndElement();
        }

        writer.writeStartElement("style:style");
        writer.writeAttribute("style:name", "Graphics");
        writer.writeAttribute("style:family", "graphic");
        writer.writeStartElement("style:graphic-properties");
        writer.writeAttribute("text:anchor-type", "as-char");
        writer.writeAttribute("style:wrap", "none");
        writer.writeEndElement();
        writer.writeEndElement();

        // Outline numbering without numbers - the headings have an outline level (navigator,
        // table of contents), but no "1.2.3" is added to their text
        writer.writeStartElement("text:outline-style");
        writer.writeAttribute("style:name", "Outline");
        for (int level = 1; level <= 10; ++level)
        {
            writer.writeStartElement("text:outline-level-style");
            writer.writeAttribute("text:level", QString::number(level));
            writer.writeAttribute("style:num-format", "");
            writer.writeEndElement();
        }
        writer.writeEndElement();

        writer.writeEndElement();
    });

    QByteArray automaticStyles = writeFragment([&](QXmlStreamWriter& writer)
    {
        const OdtPageLayout& page = document.page;

        writer.writeStartElement("office:automatic-styles");
        writer.writeStartElement("style:page-layout");
        writer.writeAttribute("style:name", "pm1");
        writer.writeStartElement("style:page-layout-properties");
        writer.writeAttribute("fo:page-width", pt(page.width));
        writer.writeAttribute("fo:page-height", pt(page.height));
        writer.writeAttribute("style:print-orientation", page.width > page.height ? "landscape" : "portrait");
        writer.writeAttribute("fo:margin-top", pt(page.marginTop));
        writer.writeAttribute("fo:margin-bottom", pt(page.marginBottom));
        writer.writeAttribute("fo:margin-left", pt(page.marginLeft));
        writer.writeAttribute("fo:margin-right", pt(page.marginRight));
        writer.writeEndElement();

        // The header takes the space from the top margin to the body text of the PDF; its
        // contents are short, the height is given by the minimal height
        writer.writeStartElement("style:header-style");
        if (hasHeader)
        {
            writer.writeStartElement("style:header-footer-properties");
            writer.writeAttribute("fo:min-height", pt(qMax(page.headerHeight, 1.0)));
            writer.writeAttribute("fo:margin-left", "0pt");
            writer.writeAttribute("fo:margin-right", "0pt");
            writer.writeAttribute("fo:margin-bottom", "0pt");
            writer.writeAttribute("style:dynamic-spacing", "false");
            writer.writeEndElement();
        }
        writer.writeEndElement();

        writer.writeStartElement("style:footer-style");
        if (hasFooter)
        {
            writer.writeStartElement("style:header-footer-properties");
            writer.writeAttribute("fo:min-height", pt(qMax(page.footerHeight, 1.0)));
            writer.writeAttribute("fo:margin-left", "0pt");
            writer.writeAttribute("fo:margin-right", "0pt");
            writer.writeAttribute("fo:margin-top", "0pt");
            writer.writeAttribute("style:dynamic-spacing", "false");
            writer.writeEndElement();
        }
        writer.writeEndElement();
        writer.writeEndElement(); // style:page-layout

        styles.write(writer);
        writer.writeEndElement();
    });

    QByteArray fonts = writeFragment([&](QXmlStreamWriter& writer) { writeFontFaceDecls(writer, document); });

    QByteArray result = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    result += QByteArray("<office:document-styles") + NS_DECLARATIONS + " office:version=\"1.3\">";
    result += fonts;
    result += namedStyles;
    result += automaticStyles;
    result += masterStyles;
    result += "</office:document-styles>";
    return result;
}

QByteArray OdtWriter::createMetaXml(const OdtDocument& document)
{
    QByteArray meta = writeFragment([&](QXmlStreamWriter& writer)
    {
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        writer.writeStartElement("office:meta");
        writer.writeTextElement("meta:generator", "PDF Fire");
        if (!document.title.isEmpty())
        {
            writer.writeTextElement("dc:title", document.title);
        }
        writer.writeTextElement("meta:creation-date", now);
        writer.writeTextElement("dc:date", now);
        writer.writeEndElement();
    });

    QByteArray result = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    result += QByteArray("<office:document-meta") + NS_DECLARATIONS + " office:version=\"1.3\">";
    result += meta;
    result += "</office:document-meta>";
    return result;
}

QByteArray OdtWriter::createManifestXml(const OdtDocument& document)
{
    QByteArray entries = writeFragment([&](QXmlStreamWriter& writer)
    {
        auto addEntry = [&writer](const QString& path, const QString& mediaType, bool root)
        {
            writer.writeStartElement("manifest:file-entry");
            writer.writeAttribute("manifest:full-path", path);
            if (root)
            {
                writer.writeAttribute("manifest:version", "1.3");
            }
            writer.writeAttribute("manifest:media-type", mediaType);
            writer.writeEndElement();
        };

        addEntry("/", MIME_TYPE, true);
        addEntry("content.xml", "text/xml", false);
        addEntry("styles.xml", "text/xml", false);
        addEntry("meta.xml", "text/xml", false);
        for (const OdtImage& image : document.images)
        {
            addEntry(image.fileName, image.mimeType, false);
        }
    });

    QByteArray result = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    result += "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">";
    result += entries;
    result += "</manifest:manifest>";
    return result;
}

QByteArray OdtWriter::createPackage(const OdtDocument& document)
{
    OdtZipWriter zip;

    // "mimetype" first and stored - a reader identifies the file by it at a fixed offset
    zip.addFile("mimetype", QByteArray(MIME_TYPE), OdtZipWriter::Method::Stored);
    zip.addFile("content.xml", createContentXml(document), OdtZipWriter::Method::Deflated);
    zip.addFile("styles.xml", createStylesXml(document), OdtZipWriter::Method::Deflated);
    zip.addFile("meta.xml", createMetaXml(document), OdtZipWriter::Method::Deflated);
    for (const OdtImage& image : document.images)
    {
        // PNG and JPEG are compressed already
        zip.addFile(image.fileName, image.data, OdtZipWriter::Method::Stored);
    }
    zip.addFile("META-INF/manifest.xml", createManifestXml(document), OdtZipWriter::Method::Deflated);
    return zip.finish();
}

}   // namespace pdfplugin
