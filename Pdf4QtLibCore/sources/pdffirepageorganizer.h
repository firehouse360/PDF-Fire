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

#ifndef PDFFIREPAGEORGANIZER_H
#define PDFFIREPAGEORGANIZER_H

#include "pdfdocument.h"
#include "pdfutils.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

/// PDF Fire: organizing the pages of a document - moving pages to another place,
/// copies of pages, reversing the order, replacing pages by pages of another
/// document, splitting a document, and documents made from pictures. As with
/// PDFPageOperations, the source document is never changed, a new one is made.
class PDF4QTLIBCORESHARED_EXPORT PDFFirePageOrganizer
{
public:
    /// Moves the pages to another place in the document. The pages keep their order.
    /// \param document Document
    /// \param pages Indices of the moved pages
    /// \param position The pages are put in front of the page with this index (in the
    ///        numbering before the move); the page count puts them at the end
    /// \param[out] result Resulting document
    static PDFOperationResult movePagesTo(const PDFDocument* document, std::vector<PDFInteger> pages, PDFInteger position, PDFDocument* result);

    /// Inserts copies of the pages. A copy has the content and the annotations of its page;
    /// the form fields on it are linked to the same fields (they show the same values).
    /// \param document Document
    /// \param pages Indices of the copied pages
    /// \param position The copies are put in front of the page with this index (in the
    ///        numbering before the copies are inserted); the page count puts them at the end
    /// \param[out] result Resulting document
    static PDFOperationResult copyPages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFInteger position, PDFDocument* result);

    /// Duplicates the pages: the copies follow the last of the pages
    static PDFOperationResult duplicatePages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result);

    /// Reverses the order of the pages. The pages exchange their places among themselves,
    /// the other pages stay where they are. Empty \p pages reverses the whole document.
    static PDFOperationResult reversePages(const PDFDocument* document, std::vector<PDFInteger> pages, PDFDocument* result);

    /// Replaces pages by pages of another document, one for one: pages[i] (sorted) is
    /// replaced by sourcePages[i]. The replaced pages are removed (as by deletePages).
    /// \param document Document
    /// \param pages Indices of the replaced pages
    /// \param sourceDocument Document, from which the new pages are taken
    /// \param sourcePages Indices of the pages of the source document, the same count as \p pages
    /// \param[out] result Resulting document
    static PDFOperationResult replacePages(const PDFDocument* document, std::vector<PDFInteger> pages,
                                           const PDFDocument* sourceDocument, std::vector<PDFInteger> sourcePages, PDFDocument* result);

    /// One part of a split document
    struct SplitPart
    {
        QString title;                      ///< Title of the bookmark (for a split by bookmarks)
        std::vector<PDFInteger> pages;      ///< Pages of the part (indices in the document)
    };

    /// Splits pages into parts of \p pagesPerPart pages (the last part can be shorter)
    static std::vector<SplitPart> getSplitEveryNPages(PDFInteger pageCount, PDFInteger pagesPerPart);

    /// Splits the document by its top-level bookmarks: each part starts at the page of a
    /// bookmark and ends before the page of the next one. Pages before the first bookmark
    /// make a part of their own. Bookmarks without a page in this document are skipped.
    static std::vector<SplitPart> getSplitByBookmarks(const PDFDocument* document);

    /// A file name for a part of a split document, without a directory: "<base> - <n>.pdf",
    /// or "<base> - <n> <title>.pdf" when the part has a title. Characters, which can't be
    /// in a file name, are left out of the title.
    static QString getSplitPartFileName(const QString& baseName, int partNumber, int partCount, const QString& title);

    /// How large the pages of a document made from pictures are
    enum class ImagePageSize
    {
        Image,      ///< The page has the size of the picture (its resolution, 72 dpi if it has none)
        Letter,     ///< US Letter, the picture is fitted into the page (the page turns with the picture)
        A4,         ///< A4, the picture is fitted into the page (the page turns with the picture)
        Custom      ///< The given size, the picture is fitted into the page (the page turns with the picture)
    };

    /// One picture placed on a page
    struct ImageSource
    {
        QImage image;               ///< The picture (always set)
        QByteArray jpegData;        ///< The original JPEG file, which is stored as it is (can be empty)
        QSizeF sizeInPoints;        ///< The size of the picture at its resolution
    };

    /// Reads the pictures from a file (a TIFF file can contain several). Returns an error
    /// message, when the file is not a picture.
    static PDFOperationResult readImageFile(const QString& fileName, std::vector<ImageSource>* images);

    /// Makes a document from pictures, one picture per page
    /// \param images Pictures
    /// \param pageSize How large the pages are
    /// \param[out] result Resulting document
    /// \param customPageSize Size of the pages (in points) for ImagePageSize::Custom
    static PDFOperationResult createDocumentFromImages(const std::vector<ImageSource>& images, ImagePageSize pageSize, PDFDocument* result, QSizeF customPageSize = QSizeF());

    /// Makes a document from picture files, one picture per page
    static PDFOperationResult createDocumentFromImageFiles(const QStringList& fileNames, ImagePageSize pageSize, PDFDocument* result);

    /// Is the file a picture, which can be made into a page (by its suffix)
    static bool isSupportedImageFile(const QString& fileName);

private:
    /// Puts the pages to the new order: newOrder[i] is the index of the page, which is
    /// on the place i. It must contain every page once.
    static PDFOperationResult arrangePages(const PDFDocument* document, const std::vector<PDFInteger>& newOrder, PDFDocument* result);

    /// Sorts the indices, removes the duplicates, and checks them against the page count
    static bool normalizePages(PDFInteger pageCount, std::vector<PDFInteger>& pages);
};

}   // namespace pdf

#endif // PDFFIREPAGEORGANIZER_H
