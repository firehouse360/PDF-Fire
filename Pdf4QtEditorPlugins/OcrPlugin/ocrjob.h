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

#ifndef OCRJOB_H
#define OCRJOB_H

#include "ocrtextlayer.h"

#include "pdfdocument.h"

#include <QMutex>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

namespace pdfplugin
{

/// PDF Fire: the recognition of the pages of a document, which runs in a background thread.
/// It works on its own copy of the document and with its own font cache, so the window
/// can go on painting the document meanwhile. It does not change the document - it
/// returns the recognized text of each page, and the text layers are added to the
/// document by the caller, in the main thread, as one undoable change.
class OcrJob
{
public:
    struct Settings
    {
        std::vector<pdf::PDFInteger> pages;     ///< Zero-based page indices
        QString language = "eng";
        int dpi = 300;
        bool skipPagesWithText = true;
    };

    struct PageResult
    {
        pdf::PDFInteger pageIndex = -1;
        OcrPageText text;
        QTransform imageToPage;
    };

    explicit OcrJob(pdf::PDFDocument document, Settings settings);

    /// Runs the job (in the calling thread)
    void run();

    /// Requests the job to stop as soon as possible (thread safe)
    void cancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled; }

    /// Progress in 1/100 of a page, 0 ... page count * 100 (thread safe)
    int getProgress() const { return m_progress; }
    int getMaximumProgress() const { return int(m_settings.pages.size()) * 100; }

    /// Number (one-based) of the page being processed, and its order in the job (thread safe)
    int getCurrentPageNumber() const { return m_currentPageNumber; }
    int getCurrentPageOrder() const { return m_currentPageOrder; }

    // Results - read them after the job has finished
    const std::vector<PageResult>& getResults() const { return m_results; }
    int getSkippedPageCount() const { return m_skippedPageCount; }
    int getPagesWithoutTextCount() const { return m_pagesWithoutTextCount; }
    const QString& getErrorMessage() const { return m_errorMessage; }

    /// Returns true, if the page has any text already (scanned pages have none)
    static bool hasText(const pdf::PDFDocument* document, const pdf::PDFPage* page);

    /// Renders the page to an image of the given resolution (the whole media box, rotated
    /// as it is displayed) and returns the transformation from the image to the page space
    static QImage renderPage(const pdf::PDFDocument* document, pdf::PDFInteger pageIndex, int dpi, QTransform* imageToPage);

private:
    pdf::PDFDocument m_document;
    Settings m_settings;

    std::atomic<bool> m_cancelled = false;
    std::atomic<int> m_progress = 0;
    std::atomic<int> m_currentPageNumber = 0;
    std::atomic<int> m_currentPageOrder = 0;

    std::vector<PageResult> m_results;
    int m_skippedPageCount = 0;
    int m_pagesWithoutTextCount = 0;
    QString m_errorMessage;
};

}   // namespace pdfplugin

#endif // OCRJOB_H
