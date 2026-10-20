// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "redactplugin.h"
#include "pdffirepermissions.h"
#include "createredacteddocumentdialog.h"

#include "pdfdrawwidget.h"
#include "pdfadvancedtools.h"
#include "pdfdocumentbuilder.h"
#include "pdfredact.h"
#include "pdffirecovercheck.h"
#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfselectpagesdialog.h"
#include "pdfcompiler.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QMessageBox>

namespace pdfplugin
{

RedactPlugin::RedactPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionRedactRectangle(nullptr),
    m_actionRedactText(nullptr),
    m_actionRedactTextSelection(nullptr),
    m_actionRedactPage(nullptr),
    m_actionCreateRedactedDocument(nullptr),
    m_actionCheckHiddenText(nullptr)
{

}

void RedactPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_actionRedactRectangle = new QAction(QIcon(":/pdfplugins/redactplugin/redact-rectangle.svg"), tr("Redact &Rectangle"), this);
    m_actionRedactText = new QAction(QIcon(":/pdfplugins/redactplugin/redact-text.svg"), tr("Redact &Text"), this);
    m_actionRedactTextSelection = new QAction(QIcon(":/pdfplugins/redactplugin/redact-text-selection.svg"), tr("Redact Text &Selection"), this);
    m_actionRedactPage = new QAction(QIcon(":/pdfplugins/redactplugin/redact-page.svg"), tr("Redact &Page(s)"), this);
    m_actionCreateRedactedDocument = new QAction(QIcon(":/pdfplugins/redactplugin/redact-create-document.svg"), tr("Create Redacted &Document"), this);

    m_actionRedactRectangle->setObjectName("redactplugin_RedactRectangle");
    m_actionRedactText->setObjectName("redactplugin_RedactText");
    m_actionRedactTextSelection->setObjectName("redactplugin_RedactTextSelection");
    m_actionRedactPage->setObjectName("redactplugin_RedactPage");
    m_actionCreateRedactedDocument->setObjectName("redactplugin_CreateRedactedDocument");

    // PDF Fire: finds black boxes, which only cover the text (the text is still there)
    m_actionCheckHiddenText = new QAction(QIcon(":/pdfplugins/redactplugin/redact-check-hidden.svg"), tr("Check for &Hidden Text..."), this);
    m_actionCheckHiddenText->setObjectName("redactplugin_CheckHiddenText");
    m_actionCheckHiddenText->setIconText(tr("Check Hidden Text"));
    m_actionCheckHiddenText->setToolTip(tr("Check for hidden text under black boxes - a box drawn over text only hides it, the text can still be copied or found. "
                                           "The boxes found are listed, so they can be redacted for real."));

    m_actionRedactRectangle->setCheckable(true);
    m_actionRedactText->setCheckable(true);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    pdf::PDFCreateRedactRectangleTool* redactRectangleTool = new pdf::PDFCreateRedactRectangleTool(widget->getDrawWidgetProxy(), toolManager, m_actionRedactRectangle, this);
    pdf::PDFCreateRedactTextTool* redactTextTool = new pdf::PDFCreateRedactTextTool(widget->getDrawWidgetProxy(), toolManager, m_actionRedactText, this);

    toolManager->addTool(redactRectangleTool);
    toolManager->addTool(redactTextTool);

    connect(m_actionRedactTextSelection, &QAction::triggered, this, &RedactPlugin::onRedactTextSelectionTriggered);
    connect(m_actionRedactPage, &QAction::triggered, this, &RedactPlugin::onRedactPageTriggered);
    connect(m_actionCreateRedactedDocument, &QAction::triggered, this, &RedactPlugin::onCreateRedactedDocumentTriggered);
    connect(m_actionCheckHiddenText, &QAction::triggered, this, &RedactPlugin::onCheckHiddenTextTriggered);

    updateActions();
}

void RedactPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> RedactPlugin::getActions() const
{
    return { m_actionRedactRectangle, m_actionRedactText, m_actionRedactTextSelection, m_actionRedactPage, m_actionCreateRedactedDocument, m_actionCheckHiddenText };
}

QString RedactPlugin::getPluginMenuName() const
{
    return tr("Redac&t");
}

void RedactPlugin::updateActions()
{
    // PDF Fire: redaction changes the content - not on a protected or certified document
    const bool canModify = pdf::PDFFirePermissions::canModifyContent(m_document);
    m_actionRedactTextSelection->setEnabled(canModify);
    m_actionRedactPage->setEnabled(canModify);
    m_actionCreateRedactedDocument->setEnabled(canModify);
    m_actionCheckHiddenText->setEnabled(m_document != nullptr);
}

void RedactPlugin::onRedactTextSelectionTriggered()
{
    pdf::PDFTextSelection selectedText = m_dataExchangeInterface->getSelectedText();

    if (selectedText.isEmpty())
    {
        QMessageBox::information(m_widget, tr("Information"), tr("Select text via 'Advanced Search' tool, and then redact it using this tool. Select rows in 'Result' table to select particular results."));
        return;
    }

    const QColor redactColor = pdf::PDFCreateRedactRectangleTool::getRedactColor();

    pdf::PDFDocumentModifier modifier(m_document);

    for (auto it = selectedText.begin(); it != selectedText.end(); it = selectedText.nextPageRange(it))
    {
        const pdf::PDFTextSelectionColoredItem& item = *it;
        const pdf::PDFInteger pageIndex = item.start.pageIndex;

        pdf::PDFTextLayoutGetter textLayoutGetter = m_widget->getDrawWidgetProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);

        QPolygonF quadrilaterals;
        pdf::PDFTextSelectionPainter textSelectionPainter(&selectedText);
        QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QTransform(), &quadrilaterals);

        if (!path.isEmpty())
        {
            pdf::PDFObjectReference page = m_document->getCatalog()->getPage(pageIndex)->getPageReference();
            modifier.getBuilder()->createAnnotationRedact(page, quadrilaterals, redactColor, redactColor);
            modifier.markAnnotationsChanged();
        }
    }

    if (modifier.finalize())
    {
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void RedactPlugin::onRedactPageTriggered()
{
    pdf::PDFSelectPagesDialog dialog(tr("Redact Pages"), tr("Page Range to be Redacted"), m_document->getCatalog()->getPageCount(), m_widget->getDrawWidget()->getCurrentPages(), m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        std::vector<pdf::PDFInteger> selectedPages = dialog.getSelectedPages();

        if (selectedPages.empty())
        {
            // Jakub Melka: no pages are selected, just return
            return;
        }

        pdf::PDFDocumentModifier modifier(m_document);

        for (pdf::PDFInteger pageIndex : selectedPages)
        {
            const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex - 1);
            pdf::PDFObjectReference pageReference = page->getPageReference();
            pdf::PDFObjectReference annotation = modifier.getBuilder()->createAnnotationRedact(pageReference, page->getMediaBox(), Qt::black, Qt::black);
            modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
        }

        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

void RedactPlugin::onCreateRedactedDocumentTriggered()
{
    CreateRedactedDocumentDialog dialog(getRedactedFileName(), Qt::black, m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        pdf::PDFCMSPointer cms = m_widget->getCMSManager()->getCurrentCMS();
        pdf::PDFRedact redactProcessor(m_document,
                                       m_widget->getDrawWidgetProxy()->getFontCache(),
                                       cms.data(),
                                       m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                       &m_widget->getDrawWidgetProxy()->getMeshQualitySettings(),
                                       dialog.getRedactColor());
        redactProcessor.setLabel(dialog.getLabelText(), dialog.getLabelColor()); // PDF Fire: REDACTED on the boxes

        pdf::PDFRedact::Options options;
        options.setFlag(pdf::PDFRedact::CopyTitle, dialog.isCopyingTitle());
        options.setFlag(pdf::PDFRedact::CopyMetadata, dialog.isCopyingMetadata());
        options.setFlag(pdf::PDFRedact::CopyOutline, dialog.isCopyingOutline());
        options.setFlag(pdf::PDFRedact::KeepTextSearchable, dialog.isKeepingText()); // PDF Fire: text outside the areas stays text

        pdf::PDFDocument redactedDocument = redactProcessor.perform(options);
        pdf::PDFDocumentWriter writer(m_widget->getDrawWidgetProxy()->getProgress());
        pdf::PDFOperationResult result = writer.write(dialog.getFileName(), &redactedDocument, false);
        if (!result)
        {
            QMessageBox::critical(m_widget, tr("Error"), result.getErrorMessage());
        }
        else
        {
            // PDF Fire: the redaction is written into a new file, the opened document is
            // not changed - its marks are still only marks. The user must know both.
            QString message = tr("The redacted copy was saved to:\n%1\n\n"
                                 "The marked content is removed from that copy. The document which is opened now "
                                 "is not changed - it still contains the original content under the marks.").arg(dialog.getFileName());

            // PDF Fire: pages, which could not be filtered, were converted to outlines - the user
            // should know, why their text is not searchable
            const QStringList& messages = redactProcessor.getMessages();
            if (!messages.isEmpty())
            {
                const int shownCount = 10;
                QStringList shownMessages = messages.mid(0, shownCount);
                if (messages.size() > shownCount)
                {
                    shownMessages << tr("... and %1 more pages.").arg(messages.size() - shownCount);
                }
                message += QString("\n\n") + shownMessages.join("\n");
            }

            QMessageBox::information(m_widget, tr("Redactions Applied"), message);
        }
    }
}

QString RedactPlugin::getRedactedFileName() const
{
    QFileInfo fileInfo(m_dataExchangeInterface->getOriginalFileName());

    return fileInfo.path() + "/" + fileInfo.baseName() + "_REDACTED.pdf";
}

void RedactPlugin::onCheckHiddenTextTriggered()
{
    if (!m_document)
    {
        return;
    }

    std::vector<pdf::PDFFireCoverCheck::Finding> findings;
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        pdf::PDFCMSPointer cms = m_widget->getCMSManager()->getCurrentCMS();
        findings = pdf::PDFFireCoverCheck::check(m_document,
                                                 m_widget->getDrawWidgetProxy()->getFontCache(),
                                                 cms.data(),
                                                 m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                                 m_widget->getDrawWidgetProxy()->getMeshQualitySettings());
        QApplication::restoreOverrideCursor();
    }

    if (findings.empty())
    {
        QMessageBox::information(m_widget, tr("Check for Hidden Text"),
                                 tr("No hidden text was found under black boxes.\n\n"
                                    "Boxes drawn over text were checked on every page: none of them hides text, which is still in the document."));
        return;
    }

    QDialog dialog(m_widget);
    dialog.setObjectName("hiddenTextDialog");
    dialog.setWindowTitle(tr("Hidden Text Under Boxes"));
    dialog.resize(pdf::PDFWidgetUtils::scaleDPI(m_widget, QSize(640, 400)));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* label = new QLabel(tr("%n box(es) only cover text: the text is still in the document under them, and anyone can copy it, "
                                  "search for it, or remove the box. Mark them for redaction, then use Apply Redactions to remove the text for real.", nullptr, int(findings.size())), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    QTableWidget* table = new QTableWidget(int(findings.size()), 3, &dialog);
    table->setObjectName("hiddenTextTable");
    table->setHorizontalHeaderLabels({ tr("Page"), tr("Box"), tr("Hidden text") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    for (int row = 0; row < int(findings.size()); ++row)
    {
        const pdf::PDFFireCoverCheck::Finding& finding = findings[row];
        table->setItem(row, 0, new QTableWidgetItem(QString::number(finding.pageIndex + 1)));
        table->setItem(row, 1, new QTableWidgetItem(finding.kind == pdf::PDFFireCoverCheck::CoverKind::Comment ? tr("Box comment") : tr("Drawn box")));
        table->setItem(row, 2, new QTableWidgetItem(finding.coveredText));
    }
    table->resizeColumnsToContents();
    table->selectAll();
    layout->addWidget(table);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* markButton = buttonBox->addButton(tr("Mark Selected for Redaction"), QDialogButtonBox::AcceptRole);
    markButton->setObjectName("hiddenTextMarkButton");
    markButton->setEnabled(pdf::PDFFirePermissions::canModifyContent(m_document));
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(table, &QTableWidget::itemSelectionChanged, &dialog, [&]()
    {
        markButton->setEnabled(pdf::PDFFirePermissions::canModifyContent(m_document) && !table->selectionModel()->selectedRows().isEmpty());
    });
    connect(table, &QTableWidget::cellDoubleClicked, &dialog, [&](int row, int)
    {
        m_widget->getDrawWidgetProxy()->goToPage(findings[row].pageIndex);
    });
    layout->addWidget(buttonBox);

    QLabel* hintLabel = new QLabel(tr("Double-click a row to go to its page."), &dialog);
    layout->insertWidget(2, hintLabel);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    // The boxes are marked - the redaction itself removes the text under them
    const QColor redactColor = pdf::PDFCreateRedactRectangleTool::getRedactColor();
    pdf::PDFDocumentModifier modifier(m_document);
    int markCount = 0;
    for (const QModelIndex& index : table->selectionModel()->selectedRows())
    {
        const pdf::PDFFireCoverCheck::Finding& finding = findings[index.row()];
        const pdf::PDFPage* page = m_document->getCatalog()->getPage(finding.pageIndex);
        if (!page)
        {
            continue;
        }

        const pdf::PDFObjectReference annotation = modifier.getBuilder()->createAnnotationRedact(page->getPageReference(), finding.rect, redactColor, redactColor);
        modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
        ++markCount;
    }

    if (markCount > 0)
    {
        modifier.markAnnotationsChanged();
        if (modifier.finalize())
        {
            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        QMessageBox::information(m_widget, tr("Check for Hidden Text"),
                                 tr("%n box(es) were marked for redaction. Use Apply Redactions to remove the text under them for good.", nullptr, markCount));
    }
}

}
