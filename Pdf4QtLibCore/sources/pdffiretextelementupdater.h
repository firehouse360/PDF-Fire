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

#ifndef PDFFIRETEXTELEMENTUPDATER_H
#define PDFFIRETEXTELEMENTUPDATER_H

#include "pdfglobal.h"

#include <QStringList>

#include <memory>
#include <vector>

namespace pdf
{
class PDFCMS;
class PDFPage;
class PDFDocument;
class PDFDocumentBuilder;
class PDFEditedPageContent;
class PDFEditedPageContentElementText;
class PDFPageContentEditorContentStreamBuilder;

/// PDF Fire: writes an edited text element of the page content editor into a
/// temporary copy of the document and reads it back, so the element gets the
/// glyphs, positions and fonts exactly as they are written into the document.
///
/// The same function is used for the live preview of an in-place edit (each
/// keystroke) and for the edit itself, so what the user sees while typing is
/// what is written.
class PDF4QTLIBCORESHARED_EXPORT PDFFireTextElementUpdater
{
public:
    /// Sets the content stream (written by the content stream builder) and the
    /// resources of the page into the document being built. The resources of the
    /// page, which are not written by the content stream builder (color spaces,
    /// patterns, properties), are kept.
    /// \param builder Builder of the modified document
    /// \param document Document, from which the page is
    /// \param page Page
    /// \param contentStreamBuilder Content stream builder with the content of the page
    static void writePageContent(PDFDocumentBuilder* builder,
                                 const PDFDocument* document,
                                 const PDFPage* page,
                                 PDFPageContentEditorContentStreamBuilder& contentStreamBuilder);

    /// Refreshes the text element from its items as text (the markup): the element
    /// is written alone onto the page of a temporary document and processed again.
    /// The state, the glyphs and the fonts of the element are replaced by the read
    /// back ones. Font objects created only in the temporary document (a fallback
    /// font) are not used by the element - they are created again, when the element
    /// is written into the document.
    /// \param document Edited document
    /// \param pageIndex Index of the page of the element
    /// \param pageContent Edited content of the page (its resources are used)
    /// \param element Element to be refreshed
    /// \param cms Color management system
    /// \param errors Errors of writing of the content stream (can be nullptr)
    /// \returns False, if the element cannot be written or read back (it is not changed then)
    static bool refresh(const PDFDocument* document,
                        PDFInteger pageIndex,
                        const PDFEditedPageContent& pageContent,
                        PDFEditedPageContentElementText* element,
                        const PDFCMS* cms,
                        QStringList* errors);

    /// Changes the text of the element to the plain text (the untouched characters
    /// keep their positions and kerning, see PDFEditedTextMarkup) and refreshes it.
    /// \returns False, if the element cannot be written or read back
    static bool setPlainText(const PDFDocument* document,
                             PDFInteger pageIndex,
                             const PDFEditedPageContent& pageContent,
                             PDFEditedPageContentElementText* element,
                             const QString& plainText,
                             const PDFCMS* cms,
                             QStringList* errors);

    /// Joins the texts of a line, which are written as separate text objects (Word
    /// writes a new text object, where the font changes - for example a non-breaking
    /// hyphen in another font), into one text element, so the line is edited as one
    /// text. The texts after the first one follow the text before them (their position
    /// is a move after it), so they move, when the text before them changes. The texts
    /// must be in the order of the line, on the same baseline, with the same
    /// transformation, clipping, colours and transparency.
    /// \param fragments Texts of the line, from the left
    /// \returns The joined text (the markup is set, the glyphs and the fonts are the ones
    ///          of the fragments), or nullptr, if the texts can't be joined
    static std::unique_ptr<PDFEditedPageContentElementText> joinLineFragments(const std::vector<const PDFEditedPageContentElementText*>& fragments);
};

}   // namespace pdf

#endif // PDFFIRETEXTELEMENTUPDATER_H
