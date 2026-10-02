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

#ifndef PDFFIREREDACTION_H
#define PDFFIREREDACTION_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QColor>
#include <QPainterPath>

#include <map>
#include <memory>

namespace pdf
{
class PDFCMS;
class PDFDocument;
class PDFDocumentBuilder;
class PDFFontCache;
class PDFPage;
struct PDFMeshQualitySettings;

/// PDF Fire: redaction, which keeps the content outside of the redacted areas as it was
/// (text stays text - searchable and copyable), like Adobe Acrobat does. The content of
/// the page is filtered operator by operator:
///
///  - every glyph, whose box touches a redacted area, is removed from the text showing
///    operators (also invisible text, text in form XObjects, clipping text), the removed
///    glyphs are replaced by a positioning number, so the remaining glyphs stay in place,
///  - pixels of images under the areas are overwritten in the image data (images are copied,
///    the original image object is never changed, so it can be used elsewhere unchanged),
///  - the areas are cut out of filled and stroked paths, clipping paths, which pass through
///    an area, get the area added (their shape inside of the area is lost),
///  - form XObjects are filtered recursively (a copy for every use),
///  - text of the marked content (/ActualText, /Alt, /E) is removed on redacted pages.
///
/// The result is written into a NEW document (the builder), only objects, which are needed
/// for the pages, are copied there (the copy never follows references to pages, annotations,
/// the structure tree, files etc.). Whenever the page contains something, which cannot be
/// filtered safely (Type 3 text or a tiling pattern in an area, vertical text, soft masks, an
/// unreadable font...), the filter fails and the caller must convert the page to outlines
/// (the old, most secure method). Correctness always beats features here.
class PDF4QTLIBCORESHARED_EXPORT PDFFireRedaction
{
public:
    explicit PDFFireRedaction(const PDFDocument* document,
                              const PDFFontCache* fontCache,
                              const PDFCMS* cms,
                              const PDFMeshQualitySettings* meshQualitySettings,
                              PDFDocumentBuilder* builder);
    ~PDFFireRedaction();

    /// Returns the redacted area of the page (union of the regions of all redact annotations),
    /// in the page coordinates (default user space of the page). Overlapping regions are united.
    static QPainterPath getRedactionArea(const PDFDocument* document, const PDFPage* page);

    /// Writes the filtered content and resources of the page into the new page. Area can be empty,
    /// then the content is copied without any change. If the page cannot be filtered safely, false
    /// is returned with a reason, and the page must be converted to outlines by the caller (the
    /// new page is not changed then).
    /// \param pageIndex Index of the page in the source document
    /// \param area Redacted area (page coordinates)
    /// \param newPage New page in the builder
    /// \param fillColor Color, by which the areas are filled (invalid color = no fill)
    /// \param failureReason Reason, why the page cannot be filtered
    bool writePageContent(size_t pageIndex, const QPainterPath& area, PDFObjectReference newPage, QColor fillColor, QString* failureReason);

    /// Copies annotations of the page, which do not touch the area, into the new page. Redact
    /// annotations, popups, file attachments, multimedia are never copied. Form field widgets are
    /// flattened (their appearance is drawn into the page content), links are copied only with
    /// an URI action or a destination in the document.
    /// \param pageIndex Index of the page in the source document
    /// \param area Redacted area (page coordinates)
    /// \param newPage New page in the builder (its content must be already written)
    /// \param pageMapping Mapping of old page references to new page references
    void writeAnnotations(size_t pageIndex,
                          const QPainterPath& area,
                          PDFObjectReference newPage,
                          const std::map<PDFObjectReference, PDFObjectReference>& pageMapping);

    /// Copies the optional content properties (layers) of the document, if they exist,
    /// so marked content of the copied pages keeps its default visibility.
    void copyOptionalContentProperties();

    /// Implementation (public only for the content filter in the source file)
    class Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

}   // namespace pdf

#endif // PDFFIREREDACTION_H
