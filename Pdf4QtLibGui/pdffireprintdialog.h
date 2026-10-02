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

#ifndef PDFFIREPRINTDIALOG_H
#define PDFFIREPRINTDIALOG_H

#include "pdfglobal.h"
#include "pdfdocument.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPrinter;
class QPrintPreviewWidget;
class QSpinBox;

namespace pdf
{
class PDFDrawWidgetProxy;
class PDFProgress;
}

namespace pdfviewer
{

/// PDF Fire: printing with a preview. The options are on the left, the preview of the
/// printed pages (exactly as they will be printed) is on the right and follows them.
class PDFFirePrintDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Scaling
    {
        FitToPaper,         ///< Every page is scaled to fill the paper
        ShrinkLargePages,   ///< Pages larger than the paper are shrunk, the others are printed in their size
        ActualSize,         ///< Pages are printed in their real size
    };

    struct Options
    {
        std::vector<pdf::PDFInteger> pages;
        Scaling scaling = Scaling::FitToPaper;
        bool autoRotate = true;
        bool printAnnotations = true;
    };

    explicit PDFFirePrintDialog(const pdf::PDFDocument* document,
                                pdf::PDFDrawWidgetProxy* proxy,
                                QPrinter* printer,
                                std::vector<pdf::PDFInteger> currentPages,
                                QWidget* parent);

    /// Returns the options chosen in the dialog
    Options getOptions() const;

    /// Prints the pages (to the printer, or to the preview)
    static void printPages(QPrinter* printer,
                           const pdf::PDFDocument* document,
                           pdf::PDFDrawWidgetProxy* proxy,
                           const Options& options,
                           pdf::PDFProgress* progress);

    /// Parses a list of pages like "1-3, 5, 8-" (the page numbers start at 1). Returns
    /// the page indices (from 0), or an empty list, if the text is not valid.
    static std::vector<pdf::PDFInteger> parsePageRanges(const QString& text, pdf::PDFInteger pageCount);

private:
    void updatePreview();
    void updatePrinterLabel();
    void onChangePrinterClicked();

    const pdf::PDFDocument* m_document;
    pdf::PDFDrawWidgetProxy* m_proxy;
    QPrinter* m_printer;
    std::vector<pdf::PDFInteger> m_currentPages;

    QLabel* m_printerLabel;
    QButtonGroup* m_pageRangeGroup;
    QLineEdit* m_pageRangeEdit;
    QLabel* m_pageCountLabel;
    QComboBox* m_scalingCombo;
    QCheckBox* m_autoRotateCheckBox;
    QCheckBox* m_annotationsCheckBox;
    QCheckBox* m_grayscaleCheckBox;
    QSpinBox* m_copiesEdit;
    QPrintPreviewWidget* m_preview;
};

}   // namespace pdfviewer

#endif // PDFFIREPRINTDIALOG_H
