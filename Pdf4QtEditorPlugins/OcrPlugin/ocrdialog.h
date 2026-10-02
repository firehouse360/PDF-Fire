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

#ifndef OCRDIALOG_H
#define OCRDIALOG_H

#include "pdfglobal.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace pdfplugin
{

/// PDF Fire: options of the text recognition - pages, language, resolution
class OcrDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    /// \param pageCount Page count of the document
    /// \param currentPageIndex Zero-based index of the current page
    /// \param languages Installed languages
    explicit OcrDialog(pdf::PDFInteger pageCount, pdf::PDFInteger currentPageIndex, const QStringList& languages, QWidget* parent);

    virtual void accept() override;

    /// Zero-based indices of the selected pages (valid after the dialog is accepted)
    const std::vector<pdf::PDFInteger>& getSelectedPages() const { return m_selectedPages; }

    QString getLanguage() const;
    bool isSkippingPagesWithText() const;
    int getResolution() const;

private:
    pdf::PDFInteger m_pageCount;
    pdf::PDFInteger m_currentPageIndex;
    std::vector<pdf::PDFInteger> m_selectedPages;

    QRadioButton* m_allPagesButton;
    QRadioButton* m_currentPageButton;
    QRadioButton* m_rangeButton;
    QLineEdit* m_rangeEdit;
    QComboBox* m_languageCombo;
    QCheckBox* m_skipPagesWithTextCheck;
    QSpinBox* m_resolutionSpin;
};

}   // namespace pdfplugin

#endif // OCRDIALOG_H
