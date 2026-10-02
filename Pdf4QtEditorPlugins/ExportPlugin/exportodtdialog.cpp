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

#include "exportodtdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pdfplugin
{

ExportOdtDialog::ExportOdtDialog(const QString& fileName, pdf::PDFInteger pageCount, pdf::PDFInteger currentPageIndex, QWidget* parent) :
    QDialog(parent),
    m_pageCount(pageCount),
    m_currentPageIndex(currentPageIndex)
{
    setWindowTitle(tr("Export to Word Processor (ODT)"));
    setObjectName("exportOdtDialog");

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* introduction = new QLabel(tr("Creates an editable document (OpenDocument Text, .odt) with the text, headings, paragraphs and images "
                                         "of the PDF. It opens in LibreOffice Writer and in Microsoft Word."), this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    // File
    QHBoxLayout* fileLayout = new QHBoxLayout();
    m_fileEdit = new QLineEdit(fileName, this);
    m_fileEdit->setObjectName("exportOdtFileEdit");
    m_fileEdit->setMinimumWidth(360);
    QPushButton* browseButton = new QPushButton(tr("Browse..."), this);
    browseButton->setObjectName("exportOdtBrowseButton");
    fileLayout->addWidget(new QLabel(tr("Save as:"), this));
    fileLayout->addWidget(m_fileEdit, 1);
    fileLayout->addWidget(browseButton);
    layout->addLayout(fileLayout);
    connect(browseButton, &QPushButton::clicked, this, &ExportOdtDialog::browse);

    // Pages
    QGroupBox* pagesBox = new QGroupBox(tr("Pages"), this);
    QGridLayout* pagesLayout = new QGridLayout(pagesBox);
    m_allPagesButton = new QRadioButton(tr("All pages (%1)").arg(pageCount), pagesBox);
    m_currentPageButton = new QRadioButton(tr("Current page (%1)").arg(currentPageIndex + 1), pagesBox);
    m_rangeButton = new QRadioButton(tr("Pages from"), pagesBox);
    m_allPagesButton->setChecked(true);

    m_fromSpinBox = new QSpinBox(pagesBox);
    m_fromSpinBox->setObjectName("exportOdtFromSpinBox");
    m_fromSpinBox->setRange(1, int(qMax<pdf::PDFInteger>(pageCount, 1)));
    m_fromSpinBox->setValue(1);
    m_toSpinBox = new QSpinBox(pagesBox);
    m_toSpinBox->setObjectName("exportOdtToSpinBox");
    m_toSpinBox->setRange(1, int(qMax<pdf::PDFInteger>(pageCount, 1)));
    m_toSpinBox->setValue(int(qMax<pdf::PDFInteger>(pageCount, 1)));

    pagesLayout->addWidget(m_allPagesButton, 0, 0, 1, 4);
    pagesLayout->addWidget(m_currentPageButton, 1, 0, 1, 4);
    pagesLayout->addWidget(m_rangeButton, 2, 0);
    pagesLayout->addWidget(m_fromSpinBox, 2, 1);
    pagesLayout->addWidget(new QLabel(tr("to"), pagesBox), 2, 2);
    pagesLayout->addWidget(m_toSpinBox, 2, 3);
    pagesLayout->setColumnStretch(4, 1);
    layout->addWidget(pagesBox);

    // Repeated headers and footers
    m_headersCheckBox = new QCheckBox(tr("Put the text repeated at the top and bottom of every page into the page header and footer"), this);
    m_headersCheckBox->setObjectName("exportOdtHeadersCheckBox");
    m_headersCheckBox->setToolTip(tr("Letterheads, page numbers and running titles become the header and footer of the document, "
                                     "so they do not break the text in the middle of the pages."));
    m_headersCheckBox->setChecked(true);
    layout->addWidget(m_headersCheckBox);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* exportButton = buttonBox->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    exportButton->setDefault(true);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ExportOdtDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ExportOdtDialog::reject);

    connect(m_rangeButton, &QRadioButton::toggled, this, &ExportOdtDialog::updateControls);
    connect(m_fromSpinBox, &QSpinBox::valueChanged, this, [this](int value) { if (m_toSpinBox->value() < value) { m_toSpinBox->setValue(value); } });
    connect(m_toSpinBox, &QSpinBox::valueChanged, this, [this](int value) { if (m_fromSpinBox->value() > value) { m_fromSpinBox->setValue(value); } });
    updateControls();
}

QString ExportOdtDialog::getFileName() const
{
    QString fileName = m_fileEdit->text().trimmed();
    if (!fileName.isEmpty() && QFileInfo(fileName).suffix().compare("odt", Qt::CaseInsensitive) != 0)
    {
        fileName += ".odt";
    }
    return fileName;
}

std::vector<pdf::PDFInteger> ExportOdtDialog::getSelectedPages() const
{
    std::vector<pdf::PDFInteger> pages;
    if (m_currentPageButton->isChecked())
    {
        pages.push_back(m_currentPageIndex);
    }
    else if (m_rangeButton->isChecked())
    {
        for (int page = m_fromSpinBox->value(); page <= m_toSpinBox->value(); ++page)
        {
            pages.push_back(page - 1);
        }
    }
    else
    {
        for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
        {
            pages.push_back(page);
        }
    }
    return pages;
}

bool ExportOdtDialog::isMovingHeadersAndFooters() const
{
    return m_headersCheckBox->isChecked();
}

void ExportOdtDialog::accept()
{
    const QString fileName = getFileName();
    if (fileName.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("Choose the file to save the document to."));
        return;
    }

    const QFileInfo info(fileName);
    if (!info.absoluteDir().exists())
    {
        QMessageBox::warning(this, windowTitle(), tr("The folder \"%1\" does not exist.").arg(info.absolutePath()));
        return;
    }

    if (info.exists() && QMessageBox::question(this, windowTitle(), tr("The file \"%1\" already exists. Do you want to replace it?").arg(info.fileName()),
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    QDialog::accept();
}

void ExportOdtDialog::browse()
{
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export to Word Processor (ODT)"), m_fileEdit->text(),
                                                          tr("OpenDocument Text (*.odt)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (!fileName.isEmpty())
    {
        m_fileEdit->setText(fileName);
    }
}

void ExportOdtDialog::updateControls()
{
    m_fromSpinBox->setEnabled(m_rangeButton->isChecked());
    m_toSpinBox->setEnabled(m_rangeButton->isChecked());
}

}   // namespace pdfplugin
