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

#ifndef PDFFIRECOVERCHECK_H
#define PDFFIRECOVERCHECK_H

#include "pdfdocument.h"

#include <QColor>
#include <QRectF>
#include <QString>

#include <vector>

namespace pdf
{
class PDFCMS;
class PDFFontCache;
class PDFOptionalContentActivity;
struct PDFMeshQualitySettings;

/// PDF Fire: finds "redactions", which only hide the text - a dark box drawn over
/// the text, while the text is still in the page under it (it can be selected,
/// copied, or found by a search). A box counts as a cover, when it is an opaque
/// filled rectangle painted after the text, over at least half of each hidden
/// character, and dark enough against the text to make it unreadable (WCAG contrast
/// below 3). Invisible text (like the text of OCR) under a box is always reported.
/// Opaque filled boxes drawn as comments (a filled square annotation) are checked too.
///
/// Ported from PdfCraft crates/audit/src/cover.rs and lib.rs (MIT OR Apache-2.0).
class PDF4QTLIBCORESHARED_EXPORT PDFFireCoverCheck
{
public:
    enum class CoverKind
    {
        PageContent,    ///< A rectangle drawn by the page
        Comment         ///< A filled box comment (square annotation)
    };

    struct Finding
    {
        PDFInteger pageIndex = 0;
        CoverKind kind = CoverKind::PageContent;
        QRectF rect;            ///< The box, in page coordinates (PDF user space)
        QColor color;           ///< Color of the box
        QString coveredText;    ///< The text, which is still under the box
    };

    /// Checks the pages of the document (empty - all pages)
    static std::vector<Finding> check(const PDFDocument* document,
                                      const PDFFontCache* fontCache,
                                      const PDFCMS* cms,
                                      const PDFOptionalContentActivity* optionalContentActivity,
                                      const PDFMeshQualitySettings& meshQualitySettings,
                                      std::vector<PDFInteger> pages = {});

    /// WCAG 2 contrast ratio of two colors: 1 (the same) to 21 (black on white)
    static PDFReal getContrast(const QColor& first, const QColor& second);
};

}   // namespace pdf

#endif // PDFFIRECOVERCHECK_H
