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

#include "pdffireprintdialog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfrenderer.h"
#include "pdfannotation.h"
#include "pdftextlayout.h"
#include "pdfcms.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"
#include "pdfwidgetutils.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrintPreviewWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <optional>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFFirePrintDialog::PDFFirePrintDialog(const pdf::PDFDocument* document,
                                       pdf::PDFDrawWidgetProxy* proxy,
                                       QPrinter* printer,
                                       std::vector<pdf::PDFInteger> currentPages,
                                       QWidget* parent) :
    QDialog(parent),
    m_document(document),
    m_proxy(proxy),
    m_printer(printer),
    m_currentPages(qMove(currentPages))
{
    setWindowTitle(tr("Print"));

    const pdf::PDFInteger pageCount = pdf::PDFInteger(document->getCatalog()->getPageCount());

    // ---- Options ---------------------------------------------------------------
    QWidget* optionsWidget = new QWidget(this);
    QVBoxLayout* optionsLayout = new QVBoxLayout(optionsWidget);
    optionsLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* printerGroup = new QGroupBox(tr("Printer"), optionsWidget);
    QHBoxLayout* printerLayout = new QHBoxLayout(printerGroup);
    m_printerLabel = new QLabel(printerGroup);
    m_printerLabel->setWordWrap(true);
    QPushButton* changePrinterButton = new QPushButton(tr("Printer Settings..."), printerGroup);
    printerLayout->addWidget(m_printerLabel, 1);
    printerLayout->addWidget(changePrinterButton);
    connect(changePrinterButton, &QPushButton::clicked, this, &PDFFirePrintDialog::onChangePrinterClicked);
    optionsLayout->addWidget(printerGroup);

    QGroupBox* pagesGroup = new QGroupBox(tr("Pages"), optionsWidget);
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesGroup);
    QRadioButton* allPagesButton = new QRadioButton(tr("All %1 pages").arg(pageCount), pagesGroup);
    QRadioButton* currentPageButton = new QRadioButton(m_currentPages.empty() ? tr("Current page") : tr("Current page (%1)").arg(m_currentPages.front() + 1), pagesGroup);
    QRadioButton* rangeButton = new QRadioButton(tr("Pages:"), pagesGroup);
    m_pageRangeEdit = new QLineEdit(pagesGroup);
    m_pageRangeEdit->setPlaceholderText(tr("for example 1-3, 5, 8-"));
    QHBoxLayout* rangeLayout = new QHBoxLayout();
    rangeLayout->addWidget(rangeButton);
    rangeLayout->addWidget(m_pageRangeEdit, 1);
    m_pageCountLabel = new QLabel(pagesGroup);
    pagesLayout->addWidget(allPagesButton);
    pagesLayout->addWidget(currentPageButton);
    pagesLayout->addLayout(rangeLayout);
    pagesLayout->addWidget(m_pageCountLabel);
    m_pageRangeGroup = new QButtonGroup(this);
    m_pageRangeGroup->addButton(allPagesButton, 0);
    m_pageRangeGroup->addButton(currentPageButton, 1);
    m_pageRangeGroup->addButton(rangeButton, 2);
    allPagesButton->setChecked(true);
    currentPageButton->setEnabled(!m_currentPages.empty());
    optionsLayout->addWidget(pagesGroup);

    QGroupBox* layoutGroup = new QGroupBox(tr("Page Sizing"), optionsWidget);
    QFormLayout* layoutLayout = new QFormLayout(layoutGroup);
    m_scalingCombo = new QComboBox(layoutGroup);
    m_scalingCombo->addItem(tr("Fit to paper"), int(Scaling::FitToPaper));
    m_scalingCombo->addItem(tr("Shrink oversized pages"), int(Scaling::ShrinkLargePages));
    m_scalingCombo->addItem(tr("Actual size"), int(Scaling::ActualSize));
    m_autoRotateCheckBox = new QCheckBox(tr("Rotate pages to fit the paper"), layoutGroup);
    m_autoRotateCheckBox->setChecked(true);
    layoutLayout->addRow(tr("Size:"), m_scalingCombo);
    layoutLayout->addRow(m_autoRotateCheckBox);
    optionsLayout->addWidget(layoutGroup);

    QGroupBox* outputGroup = new QGroupBox(tr("Output"), optionsWidget);
    QFormLayout* outputLayout = new QFormLayout(outputGroup);
    m_copiesEdit = new QSpinBox(outputGroup);
    m_copiesEdit->setRange(1, 999);
    m_copiesEdit->setValue(qMax(1, printer->copyCount()));
    m_annotationsCheckBox = new QCheckBox(tr("Print comments, stamps and form fields"), outputGroup);
    m_annotationsCheckBox->setChecked(true);
    m_grayscaleCheckBox = new QCheckBox(tr("Black and white (grayscale)"), outputGroup);
    m_grayscaleCheckBox->setChecked(printer->colorMode() == QPrinter::GrayScale);
    outputLayout->addRow(tr("Copies:"), m_copiesEdit);
    outputLayout->addRow(m_annotationsCheckBox);
    outputLayout->addRow(m_grayscaleCheckBox);
    optionsLayout->addWidget(outputGroup);
    optionsLayout->addStretch(1);

    optionsWidget->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 340));

    // ---- Preview ---------------------------------------------------------------
    m_preview = new QPrintPreviewWidget(printer, this);
    m_preview->setViewMode(QPrintPreviewWidget::SinglePageView);
    m_preview->setZoomMode(QPrintPreviewWidget::FitInView);
    connect(m_preview, &QPrintPreviewWidget::paintRequested, this, [this](QPrinter* previewPrinter)
    {
        printPages(previewPrinter, m_document, m_proxy, getOptions(), nullptr);
    });

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->addWidget(optionsWidget);
    contentLayout->addWidget(m_preview, 1);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* printButton = buttonBox->addButton(tr("Print"), QDialogButtonBox::AcceptRole);
    printButton->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(contentLayout, 1);
    layout->addWidget(buttonBox);

    // Every change of the options is shown in the preview at once
    connect(m_pageRangeGroup, &QButtonGroup::idClicked, this, &PDFFirePrintDialog::updatePreview);
    connect(m_pageRangeEdit, &QLineEdit::textChanged, this, [this, rangeButton]() { rangeButton->setChecked(true); updatePreview(); });
    connect(m_scalingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFFirePrintDialog::updatePreview);
    connect(m_autoRotateCheckBox, &QCheckBox::toggled, this, &PDFFirePrintDialog::updatePreview);
    connect(m_annotationsCheckBox, &QCheckBox::toggled, this, &PDFFirePrintDialog::updatePreview);
    connect(m_grayscaleCheckBox, &QCheckBox::toggled, this, [this](bool grayscale)
    {
        m_printer->setColorMode(grayscale ? QPrinter::GrayScale : QPrinter::Color);
        updatePreview();
    });
    connect(m_copiesEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int copies) { m_printer->setCopyCount(copies); });

    updatePrinterLabel();
    updatePreview();
    resize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(1000, 720)));
}

void PDFFirePrintDialog::updatePrinterLabel()
{
    const QString printerName = m_printer->printerName().isEmpty() ? tr("(no printer)") : m_printer->printerName();
    const QString paper = m_printer->pageLayout().pageSize().name();
    m_printerLabel->setText(QString("%1\n%2, %3").arg(printerName, paper,
                                                       m_printer->pageLayout().orientation() == QPageLayout::Portrait ? tr("portrait") : tr("landscape")));
}

void PDFFirePrintDialog::onChangePrinterClicked()
{
    // The system dialog chooses the printer, the paper and its settings. The pages are
    // chosen by this dialog, so the page range of the system dialog is not offered.
    QPrintDialog printDialog(m_printer, this);
    printDialog.setOptions(QAbstractPrintDialog::PrintShowPageSize);
    if (printDialog.exec() == QDialog::Accepted)
    {
        m_copiesEdit->setValue(qMax(1, m_printer->copyCount()));
        m_grayscaleCheckBox->setChecked(m_printer->colorMode() == QPrinter::GrayScale);
        updatePrinterLabel();
        updatePreview();
    }
}

void PDFFirePrintDialog::updatePreview()
{
    const Options options = getOptions();
    m_pageCountLabel->setText(options.pages.empty() ? tr("No pages to print - check the page numbers.") : tr("%n page(s) will be printed.", nullptr, int(options.pages.size())));
    m_preview->updatePreview();
}

PDFFirePrintDialog::Options PDFFirePrintDialog::getOptions() const
{
    Options options;
    const pdf::PDFInteger pageCount = pdf::PDFInteger(m_document->getCatalog()->getPageCount());

    switch (m_pageRangeGroup->checkedId())
    {
        case 1:
            if (!m_currentPages.empty())
            {
                options.pages = { m_currentPages.front() };
            }
            break;

        case 2:
            options.pages = parsePageRanges(m_pageRangeEdit->text(), pageCount);
            break;

        default:
            options.pages.resize(pageCount);
            std::iota(options.pages.begin(), options.pages.end(), 0);
            break;
    }

    options.scaling = static_cast<Scaling>(m_scalingCombo->currentData().toInt());
    options.autoRotate = m_autoRotateCheckBox->isChecked();
    options.printAnnotations = m_annotationsCheckBox->isChecked();
    return options;
}

std::vector<pdf::PDFInteger> PDFFirePrintDialog::parsePageRanges(const QString& text, pdf::PDFInteger pageCount)
{
    std::vector<pdf::PDFInteger> pages;

    const QStringList parts = QString(text).remove(QChar(' ')).split(QChar(','), Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        bool isFirstValid = false;
        bool isLastValid = false;
        pdf::PDFInteger first = 0;
        pdf::PDFInteger last = 0;

        if (part.contains(QChar('-')))
        {
            const QString firstText = part.section(QChar('-'), 0, 0);
            const QString lastText = part.section(QChar('-'), 1);
            first = firstText.isEmpty() ? 1 : firstText.toLongLong(&isFirstValid);
            last = lastText.isEmpty() ? pageCount : lastText.toLongLong(&isLastValid);
            isFirstValid = firstText.isEmpty() || isFirstValid;
            isLastValid = lastText.isEmpty() || isLastValid;
        }
        else
        {
            first = last = part.toLongLong(&isFirstValid);
            isLastValid = isFirstValid;
        }

        if (!isFirstValid || !isLastValid || first < 1 || last > pageCount || first > last)
        {
            return { };
        }

        for (pdf::PDFInteger page = first; page <= last; ++page)
        {
            pages.push_back(page - 1);
        }
    }

    return pages;
}

void PDFFirePrintDialog::printPages(QPrinter* printer,
                                    const pdf::PDFDocument* document,
                                    pdf::PDFDrawWidgetProxy* proxy,
                                    const Options& options,
                                    pdf::PDFProgress* progress)
{
    if (options.pages.empty())
    {
        return;
    }

    if (progress)
    {
        pdf::ProgressStartupInfo info;
        info.showDialog = true;
        info.text = tr("Printing document");
        progress->start(options.pages.size(), qMove(info));
    }

    printer->setFullPage(true);
    QPainter painter(printer);

    pdf::PDFRenderer::Features features = proxy->getFeatures();
    features.setFlag(pdf::PDFRenderer::DisplayAnnotations, options.printAnnotations);

    pdf::PDFOptionalContentActivity optionalContentActivity(document, pdf::OCUsage::Print, nullptr);
    pdf::PDFCMSPointer cms = proxy->getCMSManager()->getCurrentCMS();
    pdf::PDFRenderer renderer(document, proxy->getFontCache(), cms.data(), &optionalContentActivity, features, proxy->getMeshQualitySettings());

    // PDF Fire: the renderer draws the content of the page only. The comments, stamps
    // and form fields (with the values filled in) are annotations, drawn by an annotation
    // manager for printing - without it, none of them were printed (2026-09-30).
    std::optional<pdf::PDFAnnotationManager> annotationManager;
    if (options.printAnnotations)
    {
        annotationManager.emplace(proxy->getFontCache(), proxy->getCMSManager(), &optionalContentActivity, proxy->getMeshQualitySettings(),
                                  features, pdf::PDFAnnotationManager::Target::Print, nullptr);
        annotationManager->setDocument(pdf::PDFModifiedDocument(const_cast<pdf::PDFDocument*>(document), &optionalContentActivity));
    }

    const QRectF paperRect = printer->pageLayout().fullRectPixels(printer->resolution());
    const qreal pixelsPerPoint = printer->resolution() / 72.0;

    for (size_t i = 0; i < options.pages.size(); ++i)
    {
        const pdf::PDFInteger pageIndex = options.pages[i];
        const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        // A landscape page on a portrait paper (or the opposite) is turned, if it is allowed
        QSizeF pageSize = page->getRotatedMediaBox().size();
        pdf::PageRotation extraRotation = pdf::PageRotation::None;
        const bool isPageLandscape = pageSize.width() > pageSize.height();
        const bool isPaperLandscape = paperRect.width() > paperRect.height();
        if (options.autoRotate && isPageLandscape != isPaperLandscape && qAbs(pageSize.width() - pageSize.height()) > 1.0)
        {
            extraRotation = pdf::PageRotation::Rotate90;
            pageSize.transpose();
        }

        const qreal fitScale = qMin(paperRect.width() / pageSize.width(), paperRect.height() / pageSize.height());
        qreal scale = fitScale;
        switch (options.scaling)
        {
            case Scaling::ActualSize:
                scale = pixelsPerPoint;
                break;

            case Scaling::ShrinkLargePages:
                scale = qMin(pixelsPerPoint, fitScale);
                break;

            case Scaling::FitToPaper:
                break;
        }

        QRectF targetRect(QPointF(0, 0), pageSize * scale);
        targetRect.moveCenter(paperRect.center());

        const QTransform matrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(page, targetRect, extraRotation);
        renderer.render(&painter, matrix, pageIndex);

        if (annotationManager)
        {
            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            annotationManager->drawPage(&painter, pageIndex, nullptr, textLayoutGetter, matrix, pdf::PDFColorConvertor(), errors);
        }

        if (progress)
        {
            progress->step();
        }

        if (i + 1 < options.pages.size() && !printer->newPage())
        {
            break;
        }
    }

    painter.end();

    if (progress)
    {
        progress->finish();
    }
}

}   // namespace pdfviewer
