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

#ifndef PDFFIREPAGEOPERATIONS_H
#define PDFFIREPAGEOPERATIONS_H

#include "pdfdocument.h"
#include "pdfutils.h"

#include <QRectF>
#include <QSizeF>

#include <vector>

namespace pdf
{

/// PDF Fire: operations with the pages of a document - inserting, deleting, rotating,
/// moving pages, and inserting the pages of another document. Every operation leaves
/// the source document untouched and produces a new one. The numbers of the objects
/// of the document are kept (nothing is renumbered), the page tree is flattened.
class PDF4QTLIBCORESHARED_EXPORT PDFPageOperations
{
public:
    /// Creates a document with a single blank page
    /// \param pageSize Size of the page, in points
    static PDFDocument createBlankDocument(QSizeF pageSize);

    /// Inserts a blank page
    /// \param document Document
    /// \param position Index, which the new page gets (0 = first page, page count = last page)
    /// \param mediaBox Media box of the new page, in points
    /// \param[out] result Resulting document
    static PDFOperationResult insertBlankPage(const PDFDocument* document, PDFInteger position, QRectF mediaBox, PDFDocument* result);

    /// Deletes pages. The deleted pages are really removed from the document, together
    /// with their content, which is not used by another page - they cannot be recovered
    /// from the saved file. At least one page must be left in the document.
    /// \param document Document
    /// \param pages Indices of the pages to be deleted
    /// \param[out] result Resulting document
    static PDFOperationResult deletePages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result);

    /// Keeps only the given pages, all the other pages are deleted (see deletePages)
    static PDFOperationResult extractPages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result);

    /// Rotates pages by 90 degrees (the rotation is written into the document)
    /// \param document Document
    /// \param pages Indices of the pages to be rotated
    /// \param right Rotate clockwise (otherwise counterclockwise)
    /// \param[out] result Resulting document
    static PDFOperationResult rotatePages(const PDFDocument* document, std::vector<PDFInteger> pages, bool right, PDFDocument* result);

    /// Moves pages by one position towards the start, or towards the end of the document
    /// \param document Document
    /// \param pages Indices of the pages to be moved
    /// \param towardsEnd Move towards the end of the document (otherwise towards the start)
    /// \param[out] result Resulting document
    static PDFOperationResult movePages(const PDFDocument* document, std::vector<PDFInteger> pages, bool towardsEnd, PDFDocument* result);

    /// Inserts all pages of another document
    /// \param document Document
    /// \param insertedDocument Document, whose pages are inserted
    /// \param position Index, which the first inserted page gets
    /// \param[out] result Resulting document
    static PDFOperationResult insertDocument(const PDFDocument* document, const PDFDocument* insertedDocument, PDFInteger position, PDFDocument* result);

private:
    /// Sorts the indices, removes the duplicates, and checks them against the page count
    static bool normalizePages(const PDFDocument* document, std::vector<PDFInteger>& pages);
};

}   // namespace pdf

#endif // PDFFIREPAGEOPERATIONS_H
