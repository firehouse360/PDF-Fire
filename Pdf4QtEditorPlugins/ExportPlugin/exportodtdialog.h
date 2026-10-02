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

#ifndef EXPORTODTDIALOG_H
#define EXPORTODTDIALOG_H

#include "pdfglobal.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace pdfplugin
{

/// PDF Fire: options of "Export to Word Processor (ODT)" - the file, the pages and
/// what to do with repeated headers and footers
class ExportOdtDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportOdtDialog(const QString& fileName, pdf::PDFInteger pageCount, pdf::PDFInteger currentPageIndex, QWidget* parent);

    QString getFileName() const;
    std::vector<pdf::PDFInteger> getSelectedPages() const;
    bool isMovingHeadersAndFooters() const;

    virtual void accept() override;

private:
    void browse();
    void updateControls();

    pdf::PDFInteger m_pageCount;
    QLineEdit* m_fileEdit;
    QRadioButton* m_allPagesButton;
    QRadioButton* m_currentPageButton;
    QRadioButton* m_rangeButton;
    QSpinBox* m_fromSpinBox;
    QSpinBox* m_toSpinBox;
    QCheckBox* m_headersCheckBox;
    pdf::PDFInteger m_currentPageIndex;
};

}   // namespace pdfplugin

#endif // EXPORTODTDIALOG_H
