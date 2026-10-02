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

#include "exportplugin.h"
#include "pdffirepermissions.h"
#include "exportodtdialog.h"
#include "pdftoodtconverter.h"

#include "pdfdrawwidget.h"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFontDatabase>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QUrl>

namespace pdfplugin
{

ExportPlugin::ExportPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionExportOdt(nullptr)
{

}

void ExportPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_actionExportOdt = new QAction(QIcon(":/pdfplugins/exportplugin/export-odt.svg"), tr("Export to &Word Processor (ODT)..."), this);
    m_actionExportOdt->setObjectName("exportplugin_ExportOdt");
    m_actionExportOdt->setToolTip(tr("Export the document into an editable word processor document (OpenDocument Text, .odt), "
                                     "which opens in LibreOffice Writer and Microsoft Word."));
    m_actionExportOdt->setStatusTip(m_actionExportOdt->toolTip());

    connect(m_actionExportOdt, &QAction::triggered, this, &ExportPlugin::onExportOdtTriggered);

    updateActions();
}

void ExportPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> ExportPlugin::getActions() const
{
    return { m_actionExportOdt };
}

QString ExportPlugin::getPluginMenuName() const
{
    return tr("E&xport");
}

void ExportPlugin::updateActions()
{
    // PDF Fire: the export copies the text and the images of the document
    m_actionExportOdt->setEnabled(pdf::PDFFirePermissions::canCopyContent(m_document));
}

void ExportPlugin::onExportOdtTriggered()
{
    if (!m_document || !m_widget)
    {
        return;
    }

    const pdf::PDFInteger pageCount = pdf::PDFInteger(m_document->getCatalog()->getPageCount());
    if (pageCount == 0)
    {
        return;
    }

    // Default: the name of the PDF with .odt, next to it
    const QString originalFileName = m_dataExchangeInterface ? m_dataExchangeInterface->getOriginalFileName() : QString();
    QString defaultFileName;
    QString title = m_document->getInfo()->title;
    if (!originalFileName.isEmpty())
    {
        const QFileInfo info(originalFileName);
        defaultFileName = info.absoluteDir().filePath(info.completeBaseName() + ".odt");
        if (title.isEmpty())
        {
            title = info.completeBaseName();
        }
    }
    else
    {
        defaultFileName = QDir::home().filePath(tr("Document") + ".odt");
    }

    std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    const pdf::PDFInteger currentPageIndex = currentPages.empty() ? 0 : currentPages.front();

    ExportOdtDialog dialog(defaultFileName, pageCount, currentPageIndex, m_widget);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    PdfToOdtConverter::Settings settings;
    settings.pages = dialog.getSelectedPages();
    settings.moveHeadersAndFooters = dialog.isMovingHeadersAndFooters();
    settings.title = title;
    settings.installedFontFamilies = QFontDatabase::families();
    const QString fileName = dialog.getFileName();

    // The conversion runs in a background thread on a copy of the document (the objects
    // are shared, not copied), so the window never freezes. The progress dialog is window
    // modal - the document cannot be changed meanwhile.
    PdfToOdtConverter converter(pdf::PDFDocument(*m_document), settings);

    QProgressDialog progressDialog(tr("Exporting..."), tr("Cancel"), 0, converter.getMaximumProgress(), m_widget);
    progressDialog.setObjectName("exportOdtProgressDialog");
    progressDialog.setWindowTitle(tr("Export to Word Processor (ODT)"));
    progressDialog.setWindowModality(Qt::WindowModal);
    progressDialog.setAutoClose(false);
    progressDialog.setAutoReset(false);
    progressDialog.setMinimumDuration(0);
    progressDialog.setValue(0);

    QEventLoop eventLoop;
    QThread* thread = QThread::create([&converter]() { converter.run(); });
    connect(thread, &QThread::finished, &eventLoop, &QEventLoop::quit);
    connect(&progressDialog, &QProgressDialog::canceled, &progressDialog, [&converter]() { converter.cancel(); });

    QTimer timer;
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, &progressDialog, [&converter, &progressDialog]()
    {
        if (converter.isCancelled())
        {
            return;
        }

        const int progress = qMin(converter.getProgress(), converter.getMaximumProgress());
        progressDialog.setValue(progress);
        if (progress >= converter.getMaximumProgress() - 1)
        {
            progressDialog.setLabelText(tr("Creating the document..."));
        }
        else if (converter.getCurrentPageNumber() > 0)
        {
            progressDialog.setLabelText(tr("Reading page %1...").arg(converter.getCurrentPageNumber()));
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

    if (converter.isCancelled())
    {
        return;
    }

    if (!converter.getErrorMessage().isEmpty() || converter.getPackage().isEmpty())
    {
        QMessageBox::critical(m_widget, tr("Export to Word Processor (ODT)"), tr("The document cannot be exported: %1").arg(converter.getErrorMessage()));
        return;
    }

    // QSaveFile - an existing file is replaced only when the new one is written completely
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(converter.getPackage()) != converter.getPackage().size() || !file.commit())
    {
        QMessageBox::critical(m_widget, tr("Export to Word Processor (ODT)"), tr("The file \"%1\" cannot be written: %2").arg(fileName, file.errorString()));
        return;
    }

    if (QMainWindow* mainWindow = m_dataExchangeInterface ? m_dataExchangeInterface->getMainWindow() : nullptr)
    {
        mainWindow->statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(fileName)), 10000);
    }

    QMessageBox messageBox(QMessageBox::Question, tr("Export to Word Processor (ODT)"),
                           tr("The document has been exported to \"%1\" (%n page(s)).\n\nDo you want to open it now?", nullptr, int(settings.pages.size())).arg(QFileInfo(fileName).fileName()),
                           QMessageBox::Yes | QMessageBox::No, m_widget);
    messageBox.setObjectName("exportOdtOpenQuestion");
    messageBox.setDefaultButton(QMessageBox::No);
    if (messageBox.exec() == QMessageBox::Yes)
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
    }
}

}   // namespace pdfplugin
