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

#ifndef PDFTOODTCONVERTER_H
#define PDFTOODTCONVERTER_H

#include "odtdocument.h"

#include "pdfdocument.h"
#include "pdfglobal.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <atomic>
#include <vector>

namespace pdfplugin
{

/// PDF Fire: converts PDF pages into an OpenDocument Text (.odt). It is meant to run in a
/// background thread: it works on its own copy of the document (the objects are shared,
/// not copied), reports the progress and can be cancelled between pages.
///
/// The text is taken by PDF4QT's own text extraction (characters with positions, the
/// layout analysis into lines and blocks in reading order) - only the font and the colour
/// of each character are captured in addition. The paragraphs are built by OdtLayoutBuilder.
class PdfToOdtConverter
{
public:
    struct Settings
    {
        std::vector<pdf::PDFInteger> pages;     ///< Page indices (0-based) to export
        bool moveHeadersAndFooters = true;      ///< Repeated header/footer -> page header/footer
        QString title;                          ///< Title of the document (metadata)

        /// Font families installed on this computer (empty = no substitution). A font of
        /// the PDF which is not installed (e.g. "CMR10" of TeX) is replaced by a similar
        /// installed one (serif, sans, monospace) - otherwise the word processor takes some
        /// fallback, often much wider, and the text no longer fits the pages.
        QStringList installedFontFamilies;
    };

    explicit PdfToOdtConverter(pdf::PDFDocument document, Settings settings);

    /// Converts the pages and creates the package (call from a worker thread)
    void run();

    void cancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled; }

    int getProgress() const { return m_progress; }
    int getMaximumProgress() const { return int(m_settings.pages.size()) + 1; }
    int getCurrentPageNumber() const { return m_currentPageNumber; }

    /// The .odt file (empty, when cancelled or failed)
    const QByteArray& getPackage() const { return m_package; }
    const OdtDocument& getDocument() const { return m_document; }
    const QString& getErrorMessage() const { return m_errorMessage; }

    /// Font family and style from a PDF font name ("ABCDEF+Carlito-BoldItalic" ->
    /// "Carlito", bold, italic). Public for the tests.
    static void parseFontName(QByteArray fontName, QString* family, bool* bold, bool* italic);

    /// Returns the family to use for \p family: itself, when installed, otherwise a similar
    /// installed family. Public for the tests.
    static QString getSubstituteFamily(const QString& family, const QByteArray& fontName, bool serif, bool monospace, const QStringList& installedFamilies);

private:
    pdf::PDFDocument m_pdfDocument;
    Settings m_settings;
    std::atomic_bool m_cancelled = false;
    std::atomic_int m_progress = 0;
    std::atomic_int m_currentPageNumber = 0;
    QByteArray m_package;
    OdtDocument m_document;
    QString m_errorMessage;
};

}   // namespace pdfplugin

#endif // PDFTOODTCONVERTER_H
