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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "scannerplugin.h"

#include "scandialog.h"
#include "scannedpdfbuilder.h"
#include <QTimer>
#include <QPointer>
#include <QMenu>
#include <QMainWindow>

#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"

#include <QAction>
#include <QDialog>
#include <QIcon>

namespace pdfplugin
{

ScannerPlugin::ScannerPlugin() :
    pdf::PDFPlugin(nullptr)
{

}

void ScannerPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_scanAction = new QAction(QIcon(":/pdfplugins/scannerplugin/scan.svg"), tr("&Scan Pages..."), this);
    m_scanAction->setObjectName("scannerplugin_ScanPages");

    connect(m_scanAction, &QAction::triggered, this, &ScannerPlugin::onScanTriggered);
}

std::vector<QAction*> ScannerPlugin::getActions() const
{
    return { m_scanAction };
}

QString ScannerPlugin::getPluginMenuName() const
{
    return tr("&Scanner");
}

void ScannerPlugin::onScanTriggered()
{
    // PDF Fire: the text recognition is done by the OCR plugin, if it is installed
    QAction* recognizeTextAction = nullptr;
    if (QMainWindow* mainWindow = m_dataExchangeInterface ? m_dataExchangeInterface->getMainWindow() : nullptr)
    {
        for (QMenu* menu : mainWindow->findChildren<QMenu*>())
        {
            for (QAction* action : menu->actions())
            {
                if (action->objectName() == QLatin1String("ocrplugin_RecognizeText"))
                {
                    recognizeTextAction = action;
                }
            }
        }
    }

    ScanDialog dialog(m_widget);
    dialog.setOcrAvailable(recognizeTextAction != nullptr);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const bool isOcrRequested = dialog.isOcrRequested();
    const pdf::PDFInteger firstNewPage = m_document ? pdf::PDFInteger(m_document->getCatalog()->getPageCount()) : 0;

    std::vector<ScannedPage> pages = dialog.takePages();
    if (pages.empty())
    {
        return;
    }

    if (m_document)
    {
        pdf::PDFDocumentModifier modifier(m_document);
        ScannedPdfBuilder::appendPages(modifier.getBuilder(), pages);
        modifier.markReset();

        if (modifier.finalize())
        {
            // PDF Fire: the import of the scanned pages can be undone
            pdf::PDFModifiedDocument::ModificationFlags flags = modifier.getFlags() | pdf::PDFModifiedDocument::PreserveView | pdf::PDFModifiedDocument::PreserveUndoRedo;
            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, flags));
        }
    }
    else
    {
        pdf::PDFDocumentBuilder builder;
        ScannedPdfBuilder::appendPages(&builder, pages);

        pdf::PDFDocumentPointer document(new pdf::PDFDocument(builder.build()));
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(document, nullptr, pdf::PDFModifiedDocument::Reset));
    }

    // The new pages are recognized (in the background, with a progress) after they are shown
    if (isOcrRequested && recognizeTextAction)
    {
        QVariantList newPages;
        for (size_t i = 0; i < pages.size(); ++i)
        {
            newPages << QVariant::fromValue<qlonglong>(firstNewPage + qlonglong(i));
        }
        QPointer<QAction> action(recognizeTextAction);
        QTimer::singleShot(0, this, [action, newPages]()
        {
            if (action)
            {
                action->setProperty("pdffire_automaticPages", newPages);
                action->trigger();
            }
        });
    }
}

}   // namespace pdfplugin
