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

#include "ocrplugin.h"
#include "pdffirepermissions.h"
#include "ocrdialog.h"
#include "ocrengine.h"
#include "ocrjob.h"

#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"

#include <QAction>
#include <QEventLoop>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QThread>
#include <QTimer>

namespace pdfplugin
{

OcrPlugin::OcrPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionRecognizeText(nullptr)
{

}

void OcrPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_actionRecognizeText = new QAction(QIcon(":/pdfplugins/ocrplugin/ocr-recognize-text.svg"), tr("Recognize &Text (OCR)"), this);
    m_actionRecognizeText->setObjectName("ocrplugin_RecognizeText");
    m_actionRecognizeText->setToolTip(tr("Recognize the text of scanned pages, so it can be searched, selected and copied. The pages look the same."));
    m_actionRecognizeText->setStatusTip(m_actionRecognizeText->toolTip());

    connect(m_actionRecognizeText, &QAction::triggered, this, &OcrPlugin::onRecognizeTextTriggered);

    updateActions();
}

void OcrPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> OcrPlugin::getActions() const
{
    return { m_actionRecognizeText };
}

QString OcrPlugin::getPluginMenuName() const
{
    return tr("&OCR");
}

void OcrPlugin::updateActions()
{
    // PDF Fire: the recognized text is added to the pages - it changes the content
    m_actionRecognizeText->setEnabled(pdf::PDFFirePermissions::canModifyContent(m_document));
}

void OcrPlugin::onRecognizeTextTriggered()
{
    if (!m_document || !m_widget)
    {
        return;
    }

    const QStringList languages = OcrEngine::getAvailableLanguages();
    if (languages.isEmpty())
    {
        QMessageBox::warning(m_widget, tr("Recognize Text"), tr("No text recognition languages are installed. Install the Tesseract language data, "
                                                                 "for example the package tesseract-ocr-eng, and try again."));
        return;
    }

    const pdf::PDFInteger pageCount = pdf::PDFInteger(m_document->getCatalog()->getPageCount());
    if (pageCount == 0)
    {
        return;
    }

    std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    const pdf::PDFInteger currentPageIndex = currentPages.empty() ? 0 : currentPages.front();

    OcrDialog dialog(pageCount, currentPageIndex, languages, m_widget);

    // PDF Fire: the scanner asks for the recognition of the pages it has just added
    // ("Make the scanned pages searchable") - the pages are given by the property of
    // the action, and the settings used last time are used without asking
    std::vector<pdf::PDFInteger> automaticPages;
    QAction* action = qobject_cast<QAction*>(sender());
    if (action && action->property("pdffire_automaticPages").isValid())
    {
        for (const QVariant& page : action->property("pdffire_automaticPages").toList())
        {
            if (page.toLongLong() >= 0 && page.toLongLong() < pageCount)
            {
                automaticPages.push_back(page.toLongLong());
            }
        }
        action->setProperty("pdffire_automaticPages", QVariant());
    }

    if (automaticPages.empty() && dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    OcrJob::Settings settings;
    settings.pages = automaticPages.empty() ? dialog.getSelectedPages() : automaticPages;
    settings.language = dialog.getLanguage();
    settings.dpi = dialog.getResolution();
    settings.skipPagesWithText = dialog.isSkippingPagesWithText();

    // PDF Fire: the recognition runs in a background thread on a copy of the document
    // (the objects are shared, not copied), so the window never freezes. The progress
    // dialog is window modal - the document cannot be changed meanwhile.
    const pdf::PDFDocument* startDocument = m_document;
    OcrJob job(pdf::PDFDocument(*m_document), settings);

    QProgressDialog progressDialog(tr("Recognizing text..."), tr("Cancel"), 0, job.getMaximumProgress(), m_widget);
    progressDialog.setObjectName("ocrProgressDialog");
    progressDialog.setWindowTitle(tr("Recognize Text"));
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setAutoClose(false);
    progressDialog.setAutoReset(false);
    progressDialog.setMinimumDuration(0);
    progressDialog.setValue(0);

    QEventLoop eventLoop;
    QThread* thread = QThread::create([&job]() { job.run(); });
    connect(thread, &QThread::finished, &eventLoop, &QEventLoop::quit);
    connect(&progressDialog, &QProgressDialog::canceled, &progressDialog, [&job]() { job.cancel(); });

    QTimer timer;
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, &progressDialog, [&job, &progressDialog, pageCount = settings.pages.size()]()
    {
        if (job.isCancelled())
        {
            return;
        }

        progressDialog.setValue(qMin(job.getProgress(), job.getMaximumProgress()));
        if (job.getCurrentPageNumber() > 0)
        {
            progressDialog.setLabelText(tr("Recognizing text of page %1 (%2 of %3)...").arg(job.getCurrentPageNumber()).arg(job.getCurrentPageOrder()).arg(pageCount));
        }
    });

    timer.start();
    progressDialog.show();
    thread->start();
    eventLoop.exec();
    thread->wait();
    delete thread;
    timer.stop();
    progressDialog.hide();

    if (job.isCancelled())
    {
        // Nothing is changed, when the recognition is cancelled
        return;
    }

    if (!job.getErrorMessage().isEmpty())
    {
        QMessageBox::critical(m_widget, tr("Recognize Text"), job.getErrorMessage());
        return;
    }

    if (m_document != startDocument)
    {
        // Safety net - the document has been changed meanwhile, the results do not belong to it
        return;
    }

    const std::vector<OcrJob::PageResult>& results = job.getResults();
    if (results.empty())
    {
        QString message;
        if (job.getSkippedPageCount() > 0 && job.getPagesWithoutTextCount() == 0)
        {
            message = tr("All selected pages already have text - nothing to recognize. To recognize them anyway, turn off \"Skip pages which already have text\".");
        }
        else
        {
            message = tr("No text has been found on the selected pages.");
        }
        QMessageBox::information(m_widget, tr("Recognize Text"), message);
        return;
    }

    // All pages are changed at once - it is one step of undo
    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFObjectReference font;
    const QByteArray fontProgram = OcrEngine::getGlyphLessFontProgram();
    int changedPageCount = 0;
    for (const OcrJob::PageResult& result : results)
    {
        const pdf::PDFPage* page = m_document->getCatalog()->getPage(result.pageIndex);
        if (page && OcrTextLayer::addTextLayer(modifier.getBuilder(), page->getPageReference(), result.text, result.imageToPage, &font, fontProgram))
        {
            ++changedPageCount;
        }
    }

    if (changedPageCount == 0)
    {
        return;
    }

    modifier.markReset();
    if (modifier.finalize())
    {
        pdf::PDFModifiedDocument::ModificationFlags flags = modifier.getFlags();
        flags.setFlag(pdf::PDFModifiedDocument::PreserveUndoRedo);
        flags.setFlag(pdf::PDFModifiedDocument::PreserveView);
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, flags));
    }

    QString message = tr("Text recognized on %n page(s).", nullptr, changedPageCount);
    if (job.getSkippedPageCount() > 0)
    {
        message += QChar(' ') + tr("%n page(s) already had text and were skipped.", nullptr, job.getSkippedPageCount());
    }

    if (QMainWindow* mainWindow = m_dataExchangeInterface ? m_dataExchangeInterface->getMainWindow() : nullptr)
    {
        mainWindow->statusBar()->showMessage(message, 10000);
    }
}

}   // namespace pdfplugin
