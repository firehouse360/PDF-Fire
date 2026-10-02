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

#ifndef PAGEMARKS_H
#define PAGEMARKS_H

#include "pdfdocument.h"

#include <QColor>
#include <QDate>
#include <QString>

#include <array>
#include <optional>
#include <vector>

namespace pdf
{
class PDFDocumentBuilder;
}

namespace pdfplugin
{

/// PDF Fire: watermarks, headers and footers and Bates numbers, written into the
/// page content as Acrobat writes them - so they are real text (searchable,
/// selectable, printed), and Acrobat (and we) can remove them again later.
///
/// Every mark is a form XObject, drawn by its own content stream of the page:
///     /Artifact <</Type /Pagination /Subtype /Watermark>> BDC q /PFMark0 Do Q EMC
/// The form carries the private data /PieceInfo /PDFFire_PageMarks (Acrobat's
/// own marks carry /PieceInfo /ADBE_CompoundType), and only a pagination artifact
/// drawing such a form is removed - a header, which a word processor wrote as a
/// tagged artifact of the page text, stays. The marks are drawn in the space of the
/// page as it is shown (the form matrix turns them by /Rotate and moves them to the
/// crop box), so they are upright on a rotated page too.
///
/// The functions do not need any user interface, they are tested on their own.
class PageMarks
{
public:
    /// The standard 14 fonts of the text (no embedding, WinAnsi encoding)
    enum class Font
    {
        Helvetica,
        HelveticaBold,
        HelveticaOblique,
        HelveticaBoldOblique,
        TimesRoman,
        TimesBold,
        TimesItalic,
        TimesBoldItalic,
        Courier,
        CourierBold,
        CourierOblique,
        CourierBoldOblique
    };

    /// The kinds of the marks (flags - removal takes a combination)
    enum Kind
    {
        Watermark = 0x01,
        HeaderFooter = 0x02,
        Bates = 0x04,
        Background = 0x08,      ///< PDF Fire: a page of another PDF drawn underneath the content
        AllKinds = Watermark | HeaderFooter | Bates | Background
    };

    /// The six boxes of the headers and footers
    enum Box
    {
        HeaderLeft,
        HeaderCenter,
        HeaderRight,
        FooterLeft,
        FooterCenter,
        FooterRight,
        BoxCount
    };

    struct TextStyle
    {
        Font font = Font::Helvetica;
        double fontSize = 12.0;
        QColor color = Qt::black;
    };

    struct WatermarkSettings
    {
        bool useImage = false;

        /// Text of the watermark (lines separated by '\n'), its style, and whether
        /// the font size is chosen so the text fills the page
        QString text;
        TextStyle style;
        bool fitToPage = false;

        /// The image file (PNG, JPEG...) and its scale: of its own size (at its
        /// resolution), or of the largest size fitting the page
        QByteArray imageData;
        double imageScale = 1.0;
        bool imageScaleRelativeToPage = false;

        double opacity = 0.5;               ///< 0..1
        bool diagonal = false;              ///< From the lower left corner to the upper right one (overrides the rotation)
        double rotation = 0.0;              ///< Degrees, counterclockwise as the page is shown
        Qt::Alignment alignment = Qt::AlignCenter;
        double offsetX = 0.0;               ///< Points, + moves to the right (as the page is shown)
        double offsetY = 0.0;               ///< Points, + moves up (as the page is shown)
        bool behind = false;                ///< Behind the page content, or in front of it
        QString pageRange;                  ///< "1-3, 5", "all" (or empty), "odd", "even"
    };

    struct HeaderFooterSettings
    {
        /// Texts of the boxes, with the tokens <<page>>, <<pages>>, <<date>>,
        /// <<date:format>>, <<filename>> and <<bates>>
        std::array<QString, BoxCount> texts;
        TextStyle style;

        double marginTop = 36.0;
        double marginBottom = 36.0;
        double marginLeft = 72.0;
        double marginRight = 72.0;

        QString pageRange;
        bool skipFirstPage = false;
        int startPageNumber = 1;            ///< The number of the first page, which gets the header

        QString dateFormat = QStringLiteral("M/d/yyyy");
        QDate date = QDate::currentDate();
        QString fileName;

        /// Bates numbering: <<bates>> is prefix + zero padded number + suffix, the
        /// number grows by one from page to page. The marks are of the kind Bates
        /// (removed separately from the headers and footers).
        bool bates = false;
        QString batesPrefix;
        QString batesSuffix;
        qint64 batesStart = 1;
        int batesDigits = 6;
    };

    struct BatesSettings
    {
        QString prefix;
        QString suffix;
        qint64 start = 1;
        int digits = 6;
        Box position = FooterRight;
        TextStyle style;
        double marginTop = 36.0;
        double marginBottom = 36.0;
        double marginLeft = 36.0;
        double marginRight = 36.0;
        QString pageRange;
    };

    /// PDF Fire: a background - a page of another PDF file (a letterhead, the
    /// background of a form) drawn underneath the page content, as Acrobat's
    /// Edit PDF > Background > Add, From file. The page is imported once as a form
    /// XObject (its content and resources are copied into the document) and drawn
    /// by a form of the mark, which is the first content stream of the page:
    ///     /Artifact <</Type /Background /BBox [...]>> BDC q /PFMark0 Do Q EMC
    struct BackgroundSettings
    {
        pdf::PDFInteger sourcePageIndex = 0;   ///< The page of the source document (from 0)
        bool fitToPage = true;              ///< Scaled to the largest size fitting the page, or of its own size
        double scale = 1.0;                 ///< Of the fitting size, or of the own size
        double opacity = 1.0;               ///< 0..1 (the page is drawn as one transparency group)
        Qt::Alignment alignment = Qt::AlignCenter;
        double offsetX = 0.0;               ///< Points, + moves to the right (as the page is shown)
        double offsetY = 0.0;               ///< Points, + moves up (as the page is shown)
        QString pageRange;                  ///< "1-3, 5", "all" (or empty), "odd", "even"
    };

    /// Page geometry as the page is shown: the size, and the matrix from the shown
    /// space (origin in the lower left corner of the shown crop box) to the space
    /// of the page
    struct PageGeometry
    {
        double width = 0.0;
        double height = 0.0;
        std::array<double, 6> matrix = { 1, 0, 0, 1, 0, 0 };
    };

    /// Pages of the range text (indices from 0). Empty text and "all" are all pages,
    /// "odd" and "even" are the odd and even pages, otherwise "1-3, 5, 8-".
    static std::vector<pdf::PDFInteger> parsePageRange(const QString& text, size_t pageCount, QString* errorMessage);

    /// Adds the watermark to the pages of the range (onlyPage: to that one page,
    /// as a part of the range - used for the preview)
    static bool addWatermark(pdf::PDFDocumentBuilder* builder, const pdf::PDFDocument* document, const WatermarkSettings& settings,
                             QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage = std::nullopt);

    /// Adds the headers and footers to the pages of the range
    static bool addHeaderFooter(pdf::PDFDocumentBuilder* builder, const pdf::PDFDocument* document, const HeaderFooterSettings& settings,
                                QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage = std::nullopt);

    /// Adds the Bates numbers (a header or footer of the kind Bates)
    static bool addBatesNumbers(pdf::PDFDocumentBuilder* builder, const pdf::PDFDocument* document, const BatesSettings& settings,
                                QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage = std::nullopt);

    /// PDF Fire: adds the page of the source document as the background of the pages
    /// of the range (underneath the existing content; rotated pages and crop boxes
    /// not starting at 0,0 are handled - the background is upright as the page is shown)
    static bool addBackground(pdf::PDFDocumentBuilder* builder, const pdf::PDFDocument* document, const pdf::PDFDocument* sourceDocument,
                              const BackgroundSettings& settings, QString* errorMessage, std::optional<pdf::PDFInteger> onlyPage = std::nullopt);

    /// Number of the marks of the kinds (combination of Kind) in the document
    static int countMarks(const pdf::PDFDocument* document, int kinds);

    /// Number of the pages having a mark of the kinds
    static int countMarkedPages(const pdf::PDFDocument* document, int kinds);

    /// Removes the marks of the kinds from all pages; the other content is kept
    /// exactly as it is. Returns the number of the removed marks.
    static int removeMarks(pdf::PDFDocumentBuilder* builder, const pdf::PDFDocument* document, int kinds);

    /// Replaces the tokens of a header text
    static QString expandTokens(const QString& text, int pageNumber, int pageCount, const QDate& date, const QString& dateFormat,
                                const QString& fileName, const QString& bates);

    /// Bates number: prefix + number padded by zeros to the digits + suffix
    static QString formatBates(const QString& prefix, qint64 number, int digits, const QString& suffix);

    /// The text in WinAnsi encoding; a character, which WinAnsi does not have, is '?'
    static QByteArray encodeWinAnsi(const QString& text);

    /// Width of the WinAnsi encoded text in the font, at the size
    static double getTextWidth(Font font, const QByteArray& encoded, double fontSize);

    static QByteArray getFontBaseName(Font font);
    static PageGeometry getPageGeometry(const pdf::PDFPage* page);

private:
    static std::vector<int> countMarksOnPages(const pdf::PDFDocument* document, int kinds);
};

}   // namespace pdfplugin

#endif // PAGEMARKS_H
