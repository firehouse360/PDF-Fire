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

#ifndef ODTLAYOUT_H
#define ODTLAYOUT_H

#include "odtdocument.h"

#include <QColor>
#include <QRectF>
#include <QString>

#include <vector>

namespace pdfplugin
{

/// PDF Fire: the layout analysis of the "Export to Word Processor (ODT)". It takes the
/// positioned characters of the PDF pages (already in lines and in reading order) and
/// rebuilds what a word processor needs: paragraphs, headings, list items, the page
/// header and footer, spacing and indentation. It does not touch the PDF at all, so
/// it is tested on synthetic positioned text.
///
/// Coordinates are in points, the origin is the top left corner of the page, y grows down.

/// Formatting of a character, as found in the PDF
struct OdtCharacterStyle
{
    QString fontFamily;
    bool bold = false;
    bool italic = false;
    QColor color;           ///< Invalid = black

    bool operator==(const OdtCharacterStyle&) const = default;
};

struct OdtLayoutCharacter
{
    QChar character;
    double x = 0.0;         ///< Left edge (origin of the glyph)
    double right = 0.0;     ///< x + advance
    double baseline = 0.0;
    double fontSize = 0.0;
    int style = 0;          ///< Index into the character style table
};

struct OdtLayoutLine
{
    std::vector<OdtLayoutCharacter> characters;     ///< Sorted by x
    int block = 0;          ///< Text block (column part) of the layout analysis
    bool rotated = false;   ///< Not horizontal text - its geometry is not comparable

    double left() const { return characters.empty() ? 0.0 : characters.front().x; }
    double right() const;
    double baseline() const;
    double fontSize() const;
};

struct OdtLayoutImage
{
    int imageIndex = -1;    ///< Index into the image table
    QRectF rect;            ///< Position on the page
};

struct OdtLayoutPage
{
    double width = 612.0;
    double height = 792.0;
    int pageNumber = 1;     ///< Number of the page in the PDF (1-based), for page number fields
    std::vector<OdtLayoutLine> lines;   ///< In reading order
    std::vector<OdtLayoutImage> images;
    bool hasInvisibleText = false;      ///< OCR text layer of a scan
};

class OdtLayoutBuilder
{
public:
    struct Settings
    {
        bool moveHeadersAndFooters = true;
        int documentPageCount = 0;      ///< Pages of the PDF (for "page X of Y" fields)
        QString title;
    };

    /// Builds the word processor document. The images are moved into the document.
    static OdtDocument build(std::vector<OdtLayoutPage> pages,
                             const std::vector<OdtCharacterStyle>& characterStyles,
                             std::vector<OdtImage> images,
                             const Settings& settings);

    /// Text of a line, with spaces (and tabs for wide gaps) where the characters are apart,
    /// and the style (and font size) of each resulting character. Public for the tests.
    struct LineText
    {
        QString text;
        std::vector<int> styles;
        std::vector<double> sizes;
        std::vector<double> tabPositions;   ///< x of the text after each tab (for the tab stops)
    };
    static LineText getLineText(const OdtLayoutLine& line);

    /// Merges lines lying on the same baseline, which the layout analysis put into
    /// different blocks: a bullet and its text, cells of a line like "Date: ___ | Place: ___".
    static void mergeLinesOnBaseline(std::vector<OdtLayoutLine>& lines);

    /// Returns true, if the text starts with a bullet (a list item), the bullet is returned
    static bool startsWithBullet(const QString& text, QChar* bullet);
};

}   // namespace pdfplugin

#endif // ODTLAYOUT_H
