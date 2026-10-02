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

// PDF Fire: operations with the pages of the opened document, and new documents.
// The operations themselves are in the class pdf::PDFPageOperations of the core
// library, the functions here ask the user, and apply the result to the window.

#include "pdfprogramcontroller.h"
#include "pdffirepermissions.h"
#include "pdffirepageoperations.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfsecurityhandler.h"
#include "pdfundoredomanager.h"
#include "pdfviewersettings.h"

#include <QApplication>
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>

#include "pdfdbgheap.h"

namespace pdfviewer
{

bool PDFProgramController::canModifyPages() const
{
    if (!m_pdfDocument || m_isBusy)
    {
        return false;
    }

    // PDF Fire: the security settings and the certification of the document
    return pdf::PDFFirePermissions::canAssemblePages(m_pdfDocument.data());
}

pdf::PDFInteger PDFProgramController::getCurrentPageIndex() const
{
    if (!m_pdfDocument || !m_pdfWidget)
    {
        return -1;
    }

    const std::vector<pdf::PDFInteger> currentPages = m_pdfWidget->getDrawWidget()->getCurrentPages();
    return currentPages.empty() ? 0 : currentPages.front();
}

void PDFProgramController::applyPageOperation(const pdf::PDFOperationResult& result, pdf::PDFDocument&& document, pdf::PDFInteger pageToShow)
{
    if (!result)
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), result.getErrorMessage());
        return;
    }

    // The layout of the pages must be calculated again (Reset), but this is still the
    // same document for the user - the undo history and the view are kept.
    const pdf::PDFModifiedDocument::ModificationFlags flags(pdf::PDFModifiedDocument::Reset |
                                                           pdf::PDFModifiedDocument::PreserveUndoRedo |
                                                           pdf::PDFModifiedDocument::PreserveView);

    pdf::PDFDocumentPointer pointer(new pdf::PDFDocument(std::move(document)));
    onDocumentModified(pdf::PDFModifiedDocument(std::move(pointer), m_optionalContentActivity, flags));

    if (m_pdfDocument)
    {
        const pdf::PDFInteger pageCount = pdf::PDFInteger(m_pdfDocument->getCatalog()->getPageCount());
        m_pdfWidget->getDrawWidgetProxy()->goToPage(qBound<pdf::PDFInteger>(0, pageToShow, pageCount - 1));
    }
}

void PDFProgramController::newDocument()
{
    if (!canClose())
    {
        return;
    }

    // PDF Fire: the new document gets its own tab (the shown one stays open)
    prepareNewDocumentTab();
    closeDocument();

    // US Letter, the size can be changed by Pages > Page Size
    m_pdfDocument.reset(new pdf::PDFDocument(pdf::PDFPageOperations::createBlankDocument(QSizeF(612.0, 792.0))));
    m_signatures.clear();
    m_isSignatureLossConfirmed = false;

    // The document has no file yet, so it is not saved
    pdf::PDFModifiedDocument document(m_pdfDocument.data(), m_optionalContentActivity);
    setDocument(document, m_signatures, false);
    updateTitle();
    Q_EMIT documentTabsChanged();
}

void PDFProgramController::insertBlankPage(pdf::PDFInteger position)
{
    if (!canModifyPages())
    {
        return;
    }

    // The new page has the size of the page it is inserted next to
    const pdf::PDFInteger pageCount = pdf::PDFInteger(m_pdfDocument->getCatalog()->getPageCount());
    const pdf::PDFInteger neighbourPage = qBound<pdf::PDFInteger>(0, position - 1, pageCount - 1);
    const QRectF mediaBox = m_pdfDocument->getCatalog()->getPage(neighbourPage)->getMediaBox();

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::insertBlankPage(m_pdfDocument.data(), position, mediaBox, &document);
    applyPageOperation(result, std::move(document), position);
}

void PDFProgramController::insertPagesFromFile(pdf::PDFInteger position)
{
    if (!canModifyPages())
    {
        return;
    }

    const QStringList fileNames = QFileDialog::getOpenFileNames(m_mainWindow, tr("Insert Pages from PDF Files"), m_settings->getDirectory(), tr("PDF document (*.pdf)"));
    if (fileNames.isEmpty())
    {
        return;
    }

    auto queryPassword = [this](bool* ok)
    {
        QString password;
        *ok = false;
        onQueryPasswordRequest(&password, ok);
        return password;
    };

    // The files are inserted one after another, in the order they were selected
    pdf::PDFDocument document = *m_pdfDocument;
    pdf::PDFInteger currentPosition = position;

    for (const QString& fileName : fileNames)
    {
        pdf::PDFDocumentReader reader(nullptr, queryPassword, true, false);
        const pdf::PDFDocument insertedDocument = reader.readFromFile(fileName);

        switch (reader.getReadingResult())
        {
            case pdf::PDFDocumentReader::Result::OK:
                break;

            case pdf::PDFDocumentReader::Result::Failed:
                QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' cannot be read. %2").arg(fileName, reader.getErrorMessage()));
                return;

            case pdf::PDFDocumentReader::Result::Cancelled:
                return;
        }

        const pdf::PDFSecurityHandler* securityHandler = insertedDocument.getStorage().getSecurityHandler();
        if (!securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble) &&
            !securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Modify))
        {
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' does not allow its pages to be used in another document.").arg(fileName));
            return;
        }

        pdf::PDFDocument mergedDocument;
        const pdf::PDFOperationResult result = pdf::PDFPageOperations::insertDocument(&document, &insertedDocument, currentPosition, &mergedDocument);
        if (!result)
        {
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' cannot be inserted. %2").arg(fileName, result.getErrorMessage()));
            return;
        }

        currentPosition += pdf::PDFInteger(insertedDocument.getCatalog()->getPageCount());
        document = std::move(mergedDocument);
    }

    applyPageOperation(true, std::move(document), position);
}

void PDFProgramController::deletePages(const std::vector<pdf::PDFInteger>& pages)
{
    if (!canModifyPages() || pages.empty())
    {
        return;
    }

    const QString question = pages.size() == 1 ? tr("Delete page %1?").arg(pages.front() + 1)
                                               : tr("Delete %1 pages?").arg(pages.size());
    const QString message = tr("%1\n\nThe pages are removed from the document together with their content. Until the document is closed, this can be undone.").arg(question);
    if (QMessageBox::question(m_mainWindow, tr("Delete Pages"), message, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) != QMessageBox::Yes)
    {
        return;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::deletePages(m_pdfDocument.data(), pages, &document);
    applyPageOperation(result, std::move(document), *std::min_element(pages.cbegin(), pages.cend()));
}

void PDFProgramController::rotatePages(const std::vector<pdf::PDFInteger>& pages, bool right)
{
    if (!canModifyPages() || pages.empty())
    {
        return;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::rotatePages(m_pdfDocument.data(), pages, right, &document);
    applyPageOperation(result, std::move(document), *std::min_element(pages.cbegin(), pages.cend()));
}

void PDFProgramController::movePages(const std::vector<pdf::PDFInteger>& pages, bool towardsEnd)
{
    if (!canModifyPages() || pages.empty())
    {
        return;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::movePages(m_pdfDocument.data(), pages, towardsEnd, &document);

    const pdf::PDFInteger firstPage = *std::min_element(pages.cbegin(), pages.cend());
    applyPageOperation(result, std::move(document), firstPage + (towardsEnd ? 1 : -1));
}

void PDFProgramController::extractPages(const std::vector<pdf::PDFInteger>& pages)
{
    if (!m_pdfDocument || pages.empty())
    {
        return;
    }

    if (!pdf::PDFFirePermissions::canAssemblePages(m_pdfDocument.data()))
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("This document does not allow its pages to be extracted."));
        return;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::extractPages(m_pdfDocument.data(), pages, &document);
    if (!result)
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), result.getErrorMessage());
        return;
    }

    const QString fileName = QFileDialog::getSaveFileName(m_mainWindow, tr("Save Extracted Pages"), m_settings->getDirectory(), tr("Portable Document (*.pdf);;All files (*.*)"));
    if (fileName.isEmpty())
    {
        return;
    }

    pdf::PDFDocumentWriter writer(nullptr);
    const pdf::PDFOperationResult writeResult = writer.write(fileName, &document, true);
    if (!writeResult)
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), writeResult.getErrorMessage());
    }
    else if (m_mainWindowInterface)
    {
        m_mainWindowInterface->setStatusBarMessage(tr("%1 page(s) saved to '%2'.").arg(pages.size()).arg(fileName), 8000);
    }
}

}   // namespace pdfviewer
