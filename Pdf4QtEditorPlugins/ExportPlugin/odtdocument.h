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

#ifndef ODTDOCUMENT_H
#define ODTDOCUMENT_H

#include <QByteArray>
#include <QColor>
#include <QString>

#include <tuple>
#include <vector>

namespace pdfplugin
{

/// PDF Fire: the word processor document, which is written as an OpenDocument Text (.odt).
/// It is a plain model on purpose - the layout analysis (PDF -> paragraphs) fills it,
/// the writer turns it into XML, and both can be tested separately.

/// Character formatting of a piece of text
struct OdtTextStyle
{
    QString fontFamily;     ///< Empty = the family of the paragraph style
    double fontSize = 0.0;  ///< In points, 0 = the size of the paragraph style
    bool bold = false;
    bool italic = false;
    QColor color;           ///< Invalid = automatic (black)

    auto key() const { return std::make_tuple(fontFamily, qRound(fontSize * 10.0), bold, italic, color.isValid() ? color.rgb() : 0u, color.isValid()); }
    bool operator==(const OdtTextStyle& other) const { return key() == other.key(); }
    bool operator<(const OdtTextStyle& other) const { return key() < other.key(); }
};

/// A piece of text with the same formatting
struct OdtSpan
{
    enum class Kind
    {
        Text,           ///< Ordinary text ('\t' in the text is written as a tab)
        PageNumber,     ///< Field - number of the current page (headers and footers)
        PageCount       ///< Field - number of pages
    };

    QString text;
    OdtTextStyle style;
    Kind kind = Kind::Text;
};

enum class OdtAlignment
{
    Left,
    Center,
    Right,
    Justify
};

/// An image of the package (stored once, even when it is shown on many places)
struct OdtImage
{
    QString fileName;       ///< Path in the package, e.g. "Pictures/image1.png"
    QString mimeType;       ///< "image/png" or "image/jpeg"
    QByteArray data;
};

/// A paragraph (or a heading, a list item, an image placed between paragraphs)
struct OdtParagraph
{
    std::vector<OdtSpan> spans;

    int headingLevel = 0;               ///< 1..3 for a heading, 0 for an ordinary paragraph
    QChar listBullet;                   ///< Not null for a bulleted list item (the bullet is not in the text)
    OdtAlignment alignment = OdtAlignment::Left;
    double marginLeft = 0.0;            ///< Points, from the left page margin
    double textIndent = 0.0;            ///< Points, first line indentation (negative = hanging)
    double spaceBefore = 0.0;           ///< Points
    bool pageBreakBefore = false;
    double lineHeight = 0.0;            ///< Points, exact distance of the lines as in the PDF (0 = natural)
    std::vector<double> tabStops;       ///< Points from the left margin of the paragraph

    /// Image paragraph: index into OdtDocument::images (-1 = text paragraph)
    int imageIndex = -1;
    double imageWidth = 0.0;            ///< Points
    double imageHeight = 0.0;           ///< Points

    QString getPlainText() const
    {
        QString text;
        for (const OdtSpan& span : spans)
        {
            text += span.text;
        }
        return text;
    }
};

/// An image placed on every page (a logo of the letterhead) - written into the page header,
/// positioned relative to the page, so it is where it was in the PDF
struct OdtPageImage
{
    int imageIndex = -1;
    double x = 0.0;         ///< Points from the left edge of the page
    double y = 0.0;         ///< Points from the top edge of the page
    double width = 0.0;
    double height = 0.0;
};

struct OdtPageLayout
{
    double width = 612.0;           ///< Points (US Letter by default)
    double height = 792.0;
    double marginLeft = 72.0;
    double marginRight = 72.0;
    double marginTop = 72.0;
    double marginBottom = 72.0;
    double headerHeight = 0.0;      ///< Space from the top margin to the body (0 = no header)
    double footerHeight = 0.0;      ///< Space from the body to the bottom margin (0 = no footer)
};

/// Named paragraph style (the body text and the headings), so that the document is
/// structured: Word and Writer show "Heading 1" etc., the navigator and a table of
/// contents work with the exported document
struct OdtNamedStyle
{
    OdtTextStyle text;              ///< Complete character formatting of the style
    bool used = false;
};

struct OdtDocument
{
    QString title;
    OdtPageLayout page;

    OdtNamedStyle bodyStyle;        ///< "Standard"
    OdtNamedStyle headingStyles[3]; ///< "Heading 1" .. "Heading 3"

    std::vector<OdtParagraph> header;
    std::vector<OdtParagraph> footer;
    std::vector<OdtPageImage> headerImages;
    std::vector<OdtParagraph> body;
    std::vector<OdtImage> images;
};

}   // namespace pdfplugin

#endif // ODTDOCUMENT_H
