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

#ifndef OCRTEXTLAYER_H
#define OCRTEXTLAYER_H

#include "pdfdocumentbuilder.h"
#include "pdfpage.h"

#include <QLineF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>

#include <vector>

namespace pdfplugin
{

/// One recognized word. The box is in the pixels of the recognized image
/// (origin at the top left corner, y goes down), as the OCR engine reports it.
struct OcrWord
{
    QString text;
    QRectF box;
    float confidence = 0.0f;
};

/// One recognized line of words, in reading order. The baseline is in image
/// pixels too; when it is unknown (null line), the bottom of the box is used.
struct OcrLine
{
    QRectF box;
    QLineF baseline;
    std::vector<OcrWord> words;
};

/// The text recognized on one page image, lines in reading order
struct OcrPageText
{
    QSizeF imageSize;
    std::vector<OcrLine> lines;

    bool isEmpty() const;
    QString toPlainText() const;
};

/// PDF Fire: writes an INVISIBLE text layer over a page (the "Recognize Text" of Acrobat).
/// The page looks exactly the same, but its text can be searched, selected and copied.
///
/// Every word is written in text render mode 3 (neither filled nor stroked) with a
/// glyph-less font: a Type0 font with Identity-H encoding over a CIDFontType2 font, whose
/// character codes ARE the UTF-16 code units of the text (Identity-H), all drawn with the one
/// empty glyph of the font (CIDToGIDMap stream; /Identity when no font is embedded), and whose
/// ToUnicode CMap maps every code to itself. So any character of any language can be
/// written, and the text extraction gets the exact characters back. All glyphs have
/// the same width (/DW 500); the horizontal scaling (Tz) of every word stretches it over
/// its box, so selections and search hits land on the word in the image.
///
/// The layer is a new content stream appended to the page. The existing content is wrapped
/// in q ... Q, so an unbalanced graphics state in it cannot move the layer, and the layer
/// itself is wrapped in q ... Q, so it cannot disturb anything drawn after it.
///
/// This class does not need the OCR engine at all - it only writes the words it is given.
class OcrTextLayer
{
public:
    /// Base name of the font resource on the page (a number is added when it is taken)
    static constexpr const char* FONT_RESOURCE_NAME = "PDFFireOCR";

    /// Creates the glyph-less font objects and returns the reference of the Type0 font.
    /// One font can be shared by all pages of the document.
    /// \param builder Document builder
    /// \param fontProgram TrueType font program to embed (Tesseract's pdf.ttf); when it is
    ///        empty, the font is not embedded (it is never drawn anyway)
    static pdf::PDFObjectReference createFont(pdf::PDFDocumentBuilder* builder, const QByteArray& fontProgram);

    /// Creates the content stream of the text layer (uncompressed). Words are mapped
    /// from the image pixels into the page space by \p imageToPage.
    /// \param text Recognized text
    /// \param imageToPage Transformation from image pixels to the page (user) space
    /// \param fontResourceName Name of the font in the page resources
    static QByteArray createContentStream(const OcrPageText& text, const QTransform& imageToPage, const QByteArray& fontResourceName);

    /// Reads the media box and the rotation of the page (inherited from the page tree,
    /// when the page does not have them itself)
    static bool readPageGeometry(const pdf::PDFObjectStorage* storage,
                                 pdf::PDFObjectReference page,
                                 QRectF* mediaBox,
                                 pdf::PageRotation* rotation);

    /// Transformation from the pixels of an image of the whole page, as it is displayed
    /// (the media box, rotated by /Rotate - this is how PDF4QT renders a page), to the page space
    static QTransform createImageToPageTransform(const QRectF& mediaBox, pdf::PageRotation rotation, const QSizeF& imageSize);

    /// Adds the invisible text layer to the page.
    /// \param builder Document builder
    /// \param page Page reference
    /// \param text Recognized text (word boxes in the pixels of the page image)
    /// \param imageToPage Transformation from the image pixels to the page space
    /// \param font In/out: the font to use; when it is invalid, the font is created (from
    ///        \p fontProgram) and returned here, so the next page can share it
    /// \param fontProgram Font program, used only when the font is created
    /// \returns true, if the layer has been added (false when there is no text, or the page is invalid)
    static bool addTextLayer(pdf::PDFDocumentBuilder* builder,
                             pdf::PDFObjectReference page,
                             const OcrPageText& text,
                             const QTransform& imageToPage,
                             pdf::PDFObjectReference* font,
                             const QByteArray& fontProgram);

    /// The same, the image is an image of the whole page as it is displayed (see createImageToPageTransform)
    static bool addTextLayer(pdf::PDFDocumentBuilder* builder,
                             pdf::PDFObjectReference page,
                             const OcrPageText& text,
                             pdf::PDFObjectReference* font,
                             const QByteArray& fontProgram);

private:
    static QByteArray createToUnicodeCMap();
};

}   // namespace pdfplugin

#endif // OCRTEXTLAYER_H
