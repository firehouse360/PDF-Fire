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

#ifndef PDFFIREFLATTEN_H
#define PDFFIREFLATTEN_H

#include "pdfdocument.h"
#include "pdfutils.h"

#include <vector>

namespace pdf
{
class PDFFormManager;

/// PDF Fire: flattening of form fields and comments. The appearance of each field
/// (widget) and comment is drawn into the content of its page, as a form XObject, and
/// the annotation is removed - the page looks the same, but nothing can be changed
/// any more. Flattened fields are removed from the interactive form too.
///
/// What stays as it was:
///   - an annotation, which has no appearance to draw (removing it would lose its value
///     or its text with nothing in its place),
///   - hidden annotations, links, pop-ups (they go with their comment), multimedia,
///   - redaction marks (a flattened mark would look like a redaction, but the text
///     under it would still be there),
///   - signed signature fields (the signature must stay a signature).
///
/// The rules come from PdfCraft crates/edit/src/flatten.rs (MIT OR Apache-2.0).
class PDF4QTLIBCORESHARED_EXPORT PDFFireFlatten
{
public:
    enum Option
    {
        None        = 0x0000,
        FormFields  = 0x0001,   ///< Flatten the widgets of the form fields
        Comments    = 0x0002,   ///< Flatten the comments (markup annotations)
    };
    Q_DECLARE_FLAGS(Options, Option)

    struct Statistics
    {
        int flattenedFields = 0;        ///< Widgets drawn into the page
        int flattenedComments = 0;      ///< Comments drawn into the page
        int keptWithoutAppearance = 0;  ///< Selected annotations left, because they have nothing to draw
        int keptSignatures = 0;         ///< Signed signature fields, which were left
        int keptRedactionMarks = 0;     ///< Redaction marks, which were left
    };

    /// Flattens the form fields and/or the comments of the pages.
    /// \param document Document (it is not changed)
    /// \param pages Indices of the pages (empty - all pages)
    /// \param options What is flattened
    /// \param formManager Form manager of the document, or nullptr. When it is given, the
    ///        appearances of the fields are made again from their values before they are
    ///        drawn (if the form asks for it, or a field has no appearance).
    /// \param[out] result Resulting document
    /// \param[out] statistics What was done (optional)
    static PDFOperationResult flatten(const PDFDocument* document,
                                      std::vector<PDFInteger> pages,
                                      Options options,
                                      const PDFFormManager* formManager,
                                      PDFDocument* result,
                                      Statistics* statistics = nullptr);

    /// Counts the annotations, which would be flattened (fields and comments) on the pages
    static Statistics count(const PDFDocument* document, std::vector<PDFInteger> pages, Options options);

    /// Returns true, if the document contains a signed signature field
    static bool hasSignatures(const PDFDocument* document);
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFFireFlatten::Options)

#endif // PDFFIREFLATTEN_H
