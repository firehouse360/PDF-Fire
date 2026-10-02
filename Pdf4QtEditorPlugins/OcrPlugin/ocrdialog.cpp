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

#include "ocrdialog.h"

#include "pdfutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pdfplugin
{

OcrDialog::OcrDialog(pdf::PDFInteger pageCount, pdf::PDFInteger currentPageIndex, const QStringList& languages, QWidget* parent) :
    BaseClass(parent),
    m_pageCount(pageCount),
    m_currentPageIndex(currentPageIndex)
{
    setWindowTitle(tr("Recognize Text (OCR)"));
    setObjectName("ocrDialog");

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* introduction = new QLabel(tr("Recognizes the text of scanned pages and adds it to the pages as an invisible layer. "
                                         "The pages look exactly the same, but their text can be searched, selected and copied."), this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    // Pages
    QGroupBox* pagesGroup = new QGroupBox(tr("Pages"), this);
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesGroup);
    m_allPagesButton = new QRadioButton(tr("&All pages"), pagesGroup);
    m_currentPageButton = new QRadioButton(tr("&Current page (%1)").arg(currentPageIndex + 1), pagesGroup);
    m_rangeButton = new QRadioButton(tr("Pa&ges:"), pagesGroup);
    m_rangeEdit = new QLineEdit(pagesGroup);
    m_rangeEdit->setObjectName("ocrPageRangeEdit");
    m_rangeEdit->setPlaceholderText(tr("e.g. 1-3, 5"));
    m_allPagesButton->setObjectName("ocrAllPagesButton");
    m_currentPageButton->setObjectName("ocrCurrentPageButton");
    m_rangeButton->setObjectName("ocrPageRangeButton");
    m_allPagesButton->setChecked(true);

    QHBoxLayout* rangeLayout = new QHBoxLayout();
    rangeLayout->addWidget(m_rangeButton);
    rangeLayout->addWidget(m_rangeEdit, 1);
    pagesLayout->addWidget(m_allPagesButton);
    pagesLayout->addWidget(m_currentPageButton);
    pagesLayout->addLayout(rangeLayout);
    layout->addWidget(pagesGroup);

    // Typing a range chooses the range
    connect(m_rangeEdit, &QLineEdit::textChanged, this, [this](const QString& text)
    {
        if (!text.trimmed().isEmpty())
        {
            m_rangeButton->setChecked(true);
        }
    });

    // Settings
    QSettings settings;
    settings.beginGroup("OcrPlugin");
    const QString lastLanguage = settings.value("Language", "eng").toString();
    const int lastResolution = settings.value("Resolution", 300).toInt();
    const bool lastSkip = settings.value("SkipPagesWithText", true).toBool();
    settings.endGroup();

    QFormLayout* formLayout = new QFormLayout();
    m_languageCombo = new QComboBox(this);
    m_languageCombo->setObjectName("ocrLanguageCombo");
    m_languageCombo->addItems(languages);
    int languageIndex = languages.indexOf(lastLanguage);
    if (languageIndex < 0)
    {
        languageIndex = qMax(0, languages.indexOf("eng"));
    }
    m_languageCombo->setCurrentIndex(languageIndex);
    m_languageCombo->setToolTip(tr("Language of the text. More languages can be installed with the system packages tesseract-ocr-<language>."));
    formLayout->addRow(tr("&Language:"), m_languageCombo);

    m_resolutionSpin = new QSpinBox(this);
    m_resolutionSpin->setObjectName("ocrResolutionSpin");
    m_resolutionSpin->setRange(150, 600);
    m_resolutionSpin->setSingleStep(50);
    m_resolutionSpin->setSuffix(tr(" dpi"));
    m_resolutionSpin->setValue(qBound(150, lastResolution, 600));
    m_resolutionSpin->setToolTip(tr("Resolution, in which the pages are recognized. 300 dpi suits most documents, small print recognizes better with more."));
    formLayout->addRow(tr("&Resolution:"), m_resolutionSpin);
    layout->addLayout(formLayout);

    m_skipPagesWithTextCheck = new QCheckBox(tr("&Skip pages which already have text"), this);
    m_skipPagesWithTextCheck->setObjectName("ocrSkipPagesWithTextCheck");
    m_skipPagesWithTextCheck->setChecked(lastSkip);
    m_skipPagesWithTextCheck->setToolTip(tr("Pages with text (not scanned, or recognized before) are left as they are."));
    layout->addWidget(m_skipPagesWithTextCheck);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("&Recognize Text"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &OcrDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &OcrDialog::reject);
    layout->addWidget(buttonBox);

    setMinimumWidth(420);
}

void OcrDialog::accept()
{
    m_selectedPages.clear();

    if (m_allPagesButton->isChecked())
    {
        for (pdf::PDFInteger pageIndex = 0; pageIndex < m_pageCount; ++pageIndex)
        {
            m_selectedPages.push_back(pageIndex);
        }
    }
    else if (m_currentPageButton->isChecked())
    {
        if (m_currentPageIndex >= 0 && m_currentPageIndex < m_pageCount)
        {
            m_selectedPages.push_back(m_currentPageIndex);
        }
    }
    else
    {
        QString errorMessage;
        const pdf::PDFClosedIntervalSet pages = pdf::PDFClosedIntervalSet::parse(1, m_pageCount, m_rangeEdit->text(), &errorMessage);
        if (!errorMessage.isEmpty())
        {
            QMessageBox::critical(this, tr("Error"), errorMessage);
            return;
        }

        for (pdf::PDFInteger pageNumber : pages.unfold())
        {
            if (pageNumber >= 1 && pageNumber <= m_pageCount)
            {
                m_selectedPages.push_back(pageNumber - 1);
            }
        }
    }

    if (m_selectedPages.empty())
    {
        QMessageBox::critical(this, tr("Error"), tr("No pages have been selected."));
        return;
    }

    if (m_languageCombo->currentText().isEmpty())
    {
        QMessageBox::critical(this, tr("Error"), tr("No language has been selected."));
        return;
    }

    QSettings settings;
    settings.beginGroup("OcrPlugin");
    settings.setValue("Language", getLanguage());
    settings.setValue("Resolution", getResolution());
    settings.setValue("SkipPagesWithText", isSkippingPagesWithText());
    settings.endGroup();

    BaseClass::accept();
}

QString OcrDialog::getLanguage() const
{
    return m_languageCombo->currentText();
}

bool OcrDialog::isSkippingPagesWithText() const
{
    return m_skipPagesWithTextCheck->isChecked();
}

int OcrDialog::getResolution() const
{
    return m_resolutionSpin->value();
}

}   // namespace pdfplugin
