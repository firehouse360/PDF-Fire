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
#include "pdffirepageorganizer.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfsecurityhandler.h"
#include "pdfundoredomanager.h"
#include "pdfviewersettings.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

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

bool PDFProgramController::readDocumentForInsert(const QString& fileName, pdf::PDFDocument* document)
{
    auto queryPassword = [this](bool* ok)
    {
        QString password;
        *ok = false;
        onQueryPasswordRequest(&password, ok);
        return password;
    };

    pdf::PDFDocumentReader reader(nullptr, queryPassword, true, false);
    *document = reader.readFromFile(fileName);

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
            break;

        case pdf::PDFDocumentReader::Result::Failed:
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' cannot be read. %2").arg(fileName, reader.getErrorMessage()));
            return false;

        case pdf::PDFDocumentReader::Result::Cancelled:
            return false;
    }

    const pdf::PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler();
    if (!securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble) &&
        !securityHandler->isAllowed(pdf::PDFSecurityHandler::Permission::Modify))
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' does not allow its pages to be used in another document.").arg(fileName));
        return false;
    }

    return true;
}

void PDFProgramController::insertPagesFromFile(pdf::PDFInteger position)
{
    if (!canModifyPages())
    {
        return;
    }

    const QStringList fileNames = QFileDialog::getOpenFileNames(m_mainWindow, tr("Insert Pages from PDF Files"), m_settings->getDirectory(), tr("PDF document (*.pdf *.pdF *.pDf *.pDF *.Pdf *.PdF *.PDf *.PDF);;All files (*)"));
    if (fileNames.isEmpty())
    {
        return;
    }

    // The files are inserted one after another, in the order they were selected
    pdf::PDFDocument document = *m_pdfDocument;
    pdf::PDFInteger currentPosition = position;

    for (const QString& fileName : fileNames)
    {
        pdf::PDFDocument insertedDocument;
        if (!readDocumentForInsert(fileName, &insertedDocument))
        {
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

bool PDFProgramController::deletePages(const std::vector<pdf::PDFInteger>& pages)
{
    if (!canModifyPages() || pages.empty())
    {
        return false;
    }

    const QString question = pages.size() == 1 ? tr("Delete page %1?").arg(pages.front() + 1)
                                               : tr("Delete %1 pages?").arg(pages.size());
    const QString message = tr("%1\n\nThe pages are removed from the document together with their content. Until the document is closed, this can be undone.").arg(question);
    if (QMessageBox::question(m_mainWindow, tr("Delete Pages"), message, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) != QMessageBox::Yes)
    {
        return false;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::deletePages(m_pdfDocument.data(), pages, &document);
    applyPageOperation(result, std::move(document), *std::min_element(pages.cbegin(), pages.cend()));
    return bool(result);
}

bool PDFProgramController::rotatePages(const std::vector<pdf::PDFInteger>& pages, bool right)
{
    if (!canModifyPages() || pages.empty())
    {
        return false;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::rotatePages(m_pdfDocument.data(), pages, right, &document);
    applyPageOperation(result, std::move(document), *std::min_element(pages.cbegin(), pages.cend()));
    return bool(result);
}

bool PDFProgramController::movePages(const std::vector<pdf::PDFInteger>& pages, bool towardsEnd)
{
    if (!canModifyPages() || pages.empty())
    {
        return false;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFPageOperations::movePages(m_pdfDocument.data(), pages, towardsEnd, &document);

    const pdf::PDFInteger firstPage = *std::min_element(pages.cbegin(), pages.cend());
    applyPageOperation(result, std::move(document), firstPage + (towardsEnd ? 1 : -1));
    return bool(result);
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

// ---- PDF Fire: organizing the pages ----------------------------------------------

namespace
{

/// The filter of the file dialogs for pictures
QString getImageFileFilter()
{
    return PDFProgramController::tr("Pictures (*.png *.jpg *.jpeg *.jpe *.tif *.tiff *.bmp *.gif *.webp *.PNG *.JPG *.JPEG *.TIF *.TIFF);;All files (*)");
}

}   // namespace

void PDFProgramController::showNewDocument(pdf::PDFDocument&& document)
{
    // The same as a new blank document - its own tab, not saved yet
    prepareNewDocumentTab();
    closeDocument();

    m_pdfDocument.reset(new pdf::PDFDocument(std::move(document)));
    m_signatures.clear();
    m_isSignatureLossConfirmed = false;

    pdf::PDFModifiedDocument modifiedDocument(m_pdfDocument.data(), m_optionalContentActivity);
    setDocument(modifiedDocument, m_signatures, false);
    updateTitle();
    Q_EMIT documentTabsChanged();
}

bool PDFProgramController::movePagesTo(const std::vector<pdf::PDFInteger>& pages, pdf::PDFInteger position)
{
    if (!canModifyPages() || pages.empty())
    {
        return false;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::movePagesTo(m_pdfDocument.data(), pages, position, &document);

    // The first moved page is shown at its new place
    const pdf::PDFInteger movedBefore = std::count_if(pages.cbegin(), pages.cend(), [position](pdf::PDFInteger page) { return page < position; });
    applyPageOperation(result, std::move(document), position - movedBefore);
    return bool(result);
}

bool PDFProgramController::copyPagesTo(const std::vector<pdf::PDFInteger>& pages, pdf::PDFInteger position)
{
    if (!canModifyPages() || pages.empty())
    {
        return false;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::copyPages(m_pdfDocument.data(), pages, position, &document);
    applyPageOperation(result, std::move(document), position);
    return bool(result);
}

bool PDFProgramController::duplicatePages(const std::vector<pdf::PDFInteger>& pages)
{
    if (!canModifyPages() || pages.empty())
    {
        return false;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::duplicatePages(m_pdfDocument.data(), pages, &document);
    applyPageOperation(result, std::move(document), *std::max_element(pages.cbegin(), pages.cend()) + 1);
    return bool(result);
}

bool PDFProgramController::reversePages(const std::vector<pdf::PDFInteger>& pages)
{
    if (!canModifyPages())
    {
        return false;
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::reversePages(m_pdfDocument.data(), pages, &document);
    applyPageOperation(result, std::move(document), pages.empty() ? 0 : *std::min_element(pages.cbegin(), pages.cend()));
    return bool(result);
}

void PDFProgramController::copyPagesToClipboard(const std::vector<pdf::PDFInteger>& pages, bool cut)
{
    if (!m_pdfDocument || pages.empty())
    {
        return;
    }

    if (!pdf::PDFFirePermissions::canAssemblePages(m_pdfDocument.data()))
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("This document does not allow its pages to be copied."));
        return;
    }

    PageClipboard clipboard;
    clipboard.document = m_pdfDocument;
    clipboard.isCut = cut;
    clipboard.tab = m_currentTab;
    clipboard.fileName = m_fileInfo.originalFileName;

    const pdf::PDFCatalog* catalog = m_pdfDocument->getCatalog();
    std::vector<pdf::PDFInteger> sortedPages = pages;
    std::sort(sortedPages.begin(), sortedPages.end());
    sortedPages.erase(std::unique(sortedPages.begin(), sortedPages.end()), sortedPages.end());
    for (const pdf::PDFInteger page : sortedPages)
    {
        if (page >= 0 && size_t(page) < catalog->getPageCount())
        {
            clipboard.pages.push_back(catalog->getPage(page)->getPageReference());
        }
    }

    if (clipboard.pages.empty())
    {
        return;
    }

    m_pageClipboard = std::move(clipboard);
    if (m_mainWindowInterface)
    {
        const int count = int(m_pageClipboard.pages.size());
        QString message;
        if (cut)
        {
            message = (count == 1) ? tr("1 page cut. Select a page and choose Paste Pages - the page moves after it.")
                                   : tr("%1 pages cut. Select a page and choose Paste Pages - the pages move after it.").arg(count);
        }
        else
        {
            message = (count == 1) ? tr("1 page copied. Select a page and choose Paste Pages - the copy goes after it.")
                                   : tr("%1 pages copied. Select a page and choose Paste Pages - the copies go after it.").arg(count);
        }
        m_mainWindowInterface->setStatusBarMessage(message, 8000);
    }
}

bool PDFProgramController::canPastePages() const
{
    return m_pageClipboard.document && !m_pageClipboard.pages.empty();
}

bool PDFProgramController::pastePages(pdf::PDFInteger position, std::vector<pdf::PDFInteger>* pastedPages)
{
    if (!canModifyPages() || !canPastePages())
    {
        return false;
    }

    const PageClipboard clipboard = m_pageClipboard;
    const pdf::PDFCatalog* catalog = m_pdfDocument->getCatalog();
    const pdf::PDFInteger pageCount = pdf::PDFInteger(catalog->getPageCount());
    position = qBound<pdf::PDFInteger>(0, position, pageCount);

    // The same document (the same tab, the copied pages are still in it) - the pages are
    // copied or moved inside it, as they are now. Otherwise they are taken from the
    // document as it was, when they were copied.
    std::vector<pdf::PDFInteger> currentPages;
    bool isSameDocument = clipboard.tab == m_currentTab && clipboard.fileName == m_fileInfo.originalFileName;
    for (const pdf::PDFObjectReference& reference : clipboard.pages)
    {
        const size_t pageIndex = catalog->getPageIndexFromPageReference(reference);
        if (!isSameDocument || pageIndex >= size_t(pageCount))
        {
            isSameDocument = false;
            break;
        }
        currentPages.push_back(pdf::PDFInteger(pageIndex));
    }

    pdf::PDFDocument document;
    pdf::PDFOperationResult result(false);
    pdf::PDFInteger firstPastedPage = position;
    const pdf::PDFInteger pastedCount = pdf::PDFInteger(clipboard.pages.size());

    if (isSameDocument && clipboard.isCut)
    {
        result = pdf::PDFFirePageOrganizer::movePagesTo(m_pdfDocument.data(), currentPages, position, &document);
        firstPastedPage = position - std::count_if(currentPages.cbegin(), currentPages.cend(), [position](pdf::PDFInteger page) { return page < position; });
    }
    else if (isSameDocument)
    {
        result = pdf::PDFFirePageOrganizer::copyPages(m_pdfDocument.data(), currentPages, position, &document);
    }
    else
    {
        const pdf::PDFCatalog* sourceCatalog = clipboard.document->getCatalog();
        std::vector<pdf::PDFInteger> sourcePages;
        for (const pdf::PDFObjectReference& reference : clipboard.pages)
        {
            sourcePages.push_back(pdf::PDFInteger(sourceCatalog->getPageIndexFromPageReference(reference)));
        }

        pdf::PDFDocument extractedDocument;
        result = pdf::PDFPageOperations::extractPages(clipboard.document.data(), sourcePages, &extractedDocument);
        if (result)
        {
            result = pdf::PDFPageOperations::insertDocument(m_pdfDocument.data(), &extractedDocument, position, &document);
        }
    }

    applyPageOperation(result, std::move(document), firstPastedPage);
    if (!result)
    {
        return false;
    }

    if (pastedPages)
    {
        pastedPages->clear();
        for (pdf::PDFInteger i = 0; i < pastedCount; ++i)
        {
            pastedPages->push_back(firstPastedPage + i);
        }
    }

    // Cut pages are moved once; pasted into another document they are copies
    if (clipboard.isCut)
    {
        m_pageClipboard = PageClipboard();
        if (!isSameDocument && m_mainWindowInterface)
        {
            m_mainWindowInterface->setStatusBarMessage(tr("The cut pages were copied here - they are still in the document, from which they were cut."), 10000);
        }
    }

    return true;
}

bool PDFProgramController::replacePages(const std::vector<pdf::PDFInteger>& pages)
{
    if (!canModifyPages() || pages.empty())
    {
        return false;
    }

    const QString fileName = QFileDialog::getOpenFileName(m_mainWindow, tr("Replace Pages - the PDF with the New Pages"), m_settings->getDirectory(), tr("PDF document (*.pdf *.pdF *.pDf *.pDF *.Pdf *.PdF *.PDf *.PDF);;All files (*)"));
    if (fileName.isEmpty())
    {
        return false;
    }

    pdf::PDFDocument sourceDocument;
    if (!readDocumentForInsert(fileName, &sourceDocument))
    {
        return false;
    }

    const int pageCount = int(m_pdfDocument->getCatalog()->getPageCount());
    const int sourcePageCount = int(sourceDocument.getCatalog()->getPageCount());
    if (sourcePageCount == 0)
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' has no pages.").arg(fileName));
        return false;
    }

    QDialog dialog(m_mainWindow);
    dialog.setWindowTitle(tr("Replace Pages"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QFormLayout* form = new QFormLayout();
    layout->addLayout(form);

    QSpinBox* firstPageSpinBox = new QSpinBox(&dialog);
    firstPageSpinBox->setObjectName("replaceFirstPageSpinBox");
    firstPageSpinBox->setRange(1, pageCount);
    firstPageSpinBox->setValue(int(*std::min_element(pages.cbegin(), pages.cend())) + 1);
    QSpinBox* lastPageSpinBox = new QSpinBox(&dialog);
    lastPageSpinBox->setObjectName("replaceLastPageSpinBox");
    lastPageSpinBox->setRange(1, pageCount);
    lastPageSpinBox->setValue(int(*std::max_element(pages.cbegin(), pages.cend())) + 1);
    QSpinBox* sourceFirstPageSpinBox = new QSpinBox(&dialog);
    sourceFirstPageSpinBox->setObjectName("replaceSourceFirstPageSpinBox");
    sourceFirstPageSpinBox->setRange(1, sourcePageCount);
    sourceFirstPageSpinBox->setValue(1);

    form->addRow(tr("Replace pages of this document, from:"), firstPageSpinBox);
    form->addRow(tr("to:"), lastPageSpinBox);
    form->addRow(tr("with pages of '%1', from:").arg(QFileInfo(fileName).fileName()), sourceFirstPageSpinBox);

    QLabel* summaryLabel = new QLabel(&dialog);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Replace"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    auto update = [=]()
    {
        const int count = lastPageSpinBox->value() - firstPageSpinBox->value() + 1;
        const int sourceLast = sourceFirstPageSpinBox->value() + count - 1;
        bool isValid = count > 0 && sourceLast <= sourcePageCount;
        if (count <= 0)
        {
            summaryLabel->setText(tr("The last page must not be before the first page."));
        }
        else if (!isValid)
        {
            summaryLabel->setText(tr("'%1' has only %2 page(s) - there are not %3 pages from page %4.").arg(QFileInfo(fileName).fileName()).arg(sourcePageCount).arg(count).arg(sourceFirstPageSpinBox->value()));
        }
        else
        {
            if (count == 1)
            {
                summaryLabel->setText(tr("Page %1 is replaced by page %2 of '%3'. The replaced page is removed (Undo brings it back).")
                                      .arg(firstPageSpinBox->value()).arg(sourceFirstPageSpinBox->value()).arg(QFileInfo(fileName).fileName()));
            }
            else
            {
                summaryLabel->setText(tr("Pages %1-%2 are replaced by pages %3-%4 of '%5'. The replaced pages are removed (Undo brings them back).")
                                      .arg(firstPageSpinBox->value()).arg(lastPageSpinBox->value()).arg(sourceFirstPageSpinBox->value()).arg(sourceLast).arg(QFileInfo(fileName).fileName()));
            }
        }
        buttons->button(QDialogButtonBox::Ok)->setEnabled(isValid);
    };
    connect(firstPageSpinBox, &QSpinBox::valueChanged, &dialog, update);
    connect(lastPageSpinBox, &QSpinBox::valueChanged, &dialog, update);
    connect(sourceFirstPageSpinBox, &QSpinBox::valueChanged, &dialog, update);
    update();

    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }

    std::vector<pdf::PDFInteger> replacedPages;
    std::vector<pdf::PDFInteger> sourcePages;
    for (int page = firstPageSpinBox->value(); page <= lastPageSpinBox->value(); ++page)
    {
        replacedPages.push_back(page - 1);
        sourcePages.push_back(sourceFirstPageSpinBox->value() - 1 + int(sourcePages.size()));
    }

    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::replacePages(m_pdfDocument.data(), replacedPages, &sourceDocument, sourcePages, &document);
    applyPageOperation(result, std::move(document), replacedPages.front());
    return bool(result);
}

void PDFProgramController::splitDocument()
{
    if (!m_pdfDocument)
    {
        return;
    }

    if (!pdf::PDFFirePermissions::canAssemblePages(m_pdfDocument.data()))
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("This document does not allow its pages to be extracted."));
        return;
    }

    const pdf::PDFInteger pageCount = pdf::PDFInteger(m_pdfDocument->getCatalog()->getPageCount());
    const std::vector<pdf::PDFFirePageOrganizer::SplitPart> bookmarkParts = pdf::PDFFirePageOrganizer::getSplitByBookmarks(m_pdfDocument.data());

    // The parts are saved next to the document (or into the last used folder, if the
    // document has no file yet)
    const QFileInfo originalFile(m_fileInfo.originalFileName);
    const QString defaultDirectory = !m_fileInfo.originalFileName.isEmpty() ? originalFile.absolutePath() : m_settings->getDirectory();
    const QString defaultBaseName = !m_fileInfo.originalFileName.isEmpty() ? originalFile.completeBaseName() : tr("Document");

    QDialog dialog(m_mainWindow);
    dialog.setWindowTitle(tr("Split Document"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(tr("The document (%1 pages) is split into several PDF files. The document itself is not changed.").arg(pageCount), &dialog));

    QHBoxLayout* everyLayout = new QHBoxLayout();
    QRadioButton* everyButton = new QRadioButton(tr("Every"), &dialog);
    everyButton->setObjectName("splitEveryButton");
    QSpinBox* everySpinBox = new QSpinBox(&dialog);
    everySpinBox->setObjectName("splitEverySpinBox");
    everySpinBox->setRange(1, int(qMax<pdf::PDFInteger>(1, pageCount)));
    everySpinBox->setValue(1);
    everyLayout->addWidget(everyButton);
    everyLayout->addWidget(everySpinBox);
    everyLayout->addWidget(new QLabel(tr("pages"), &dialog));
    everyLayout->addStretch(1);
    layout->addLayout(everyLayout);

    QRadioButton* bookmarkButton = new QRadioButton(bookmarkParts.size() >= 2 ? tr("At each top-level bookmark (%1 files)").arg(bookmarkParts.size())
                                                                              : tr("At each top-level bookmark (the document has no bookmarks to split at)"), &dialog);
    bookmarkButton->setObjectName("splitBookmarksButton");
    bookmarkButton->setEnabled(bookmarkParts.size() >= 2);
    layout->addWidget(bookmarkButton);
    (bookmarkParts.size() >= 2 ? bookmarkButton : everyButton)->setChecked(true);

    QFormLayout* form = new QFormLayout();
    layout->addLayout(form);
    QHBoxLayout* folderLayout = new QHBoxLayout();
    QLineEdit* folderEdit = new QLineEdit(QDir::toNativeSeparators(defaultDirectory), &dialog);
    folderEdit->setObjectName("splitFolderEdit");
    QPushButton* browseButton = new QPushButton(tr("Browse..."), &dialog);
    folderLayout->addWidget(folderEdit, 1);
    folderLayout->addWidget(browseButton);
    form->addRow(tr("Folder:"), folderLayout);
    QLineEdit* nameEdit = new QLineEdit(defaultBaseName, &dialog);
    nameEdit->setObjectName("splitNameEdit");
    form->addRow(tr("File names start with:"), nameEdit);

    QLabel* previewLabel = new QLabel(&dialog);
    previewLabel->setWordWrap(true);
    previewLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(previewLabel);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Split"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, folderEdit]()
    {
        const QString directory = QFileDialog::getExistingDirectory(&dialog, tr("Folder for the Parts"), folderEdit->text());
        if (!directory.isEmpty())
        {
            folderEdit->setText(QDir::toNativeSeparators(directory));
        }
    });

    auto getParts = [=]()
    {
        return bookmarkButton->isChecked() ? bookmarkParts : pdf::PDFFirePageOrganizer::getSplitEveryNPages(pageCount, everySpinBox->value());
    };
    auto getBaseName = [=]()
    {
        const QString name = nameEdit->text().trimmed();
        return name.isEmpty() ? defaultBaseName : name;
    };

    auto update = [=]()
    {
        const std::vector<pdf::PDFFirePageOrganizer::SplitPart> parts = getParts();
        const int count = int(parts.size());
        QString text;
        if (count > 0)
        {
            const QString first = pdf::PDFFirePageOrganizer::getSplitPartFileName(getBaseName(), 1, count, parts.front().title);
            const QString last = pdf::PDFFirePageOrganizer::getSplitPartFileName(getBaseName(), count, count, parts.back().title);
            text = (count == 1) ? tr("Makes 1 file: %1").arg(first) : tr("Makes %1 files: %2 ... %3").arg(count).arg(first, last);
        }
        previewLabel->setText(text);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(count > 0 && !folderEdit->text().trimmed().isEmpty());
    };
    connect(everyButton, &QRadioButton::toggled, &dialog, update);
    connect(everySpinBox, &QSpinBox::valueChanged, &dialog, update);
    connect(nameEdit, &QLineEdit::textChanged, &dialog, update);
    connect(folderEdit, &QLineEdit::textChanged, &dialog, update);
    update();

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const std::vector<pdf::PDFFirePageOrganizer::SplitPart> parts = getParts();
    const QDir directory(QDir::fromNativeSeparators(folderEdit->text().trimmed()));
    if (!directory.exists())
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("Folder '%1' does not exist.").arg(folderEdit->text()));
        return;
    }

    QStringList fileNames;
    QStringList existingFiles;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const QString fileName = directory.absoluteFilePath(pdf::PDFFirePageOrganizer::getSplitPartFileName(getBaseName(), int(i + 1), int(parts.size()), parts[i].title));
        fileNames << fileName;
        if (QFileInfo::exists(fileName))
        {
            existingFiles << QFileInfo(fileName).fileName();
        }
    }

    // The opened document is never overwritten by its part
    if (!m_fileInfo.originalFileName.isEmpty() && fileNames.contains(QFileInfo(m_fileInfo.originalFileName).absoluteFilePath()))
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("A part would overwrite the opened document. Choose another name or folder."));
        return;
    }

    if (!existingFiles.isEmpty())
    {
        const QString list = existingFiles.mid(0, 8).join(QChar('\n')) + (existingFiles.size() > 8 ? QStringLiteral("\n...") : QString());
        if (QMessageBox::question(m_mainWindow, tr("Split Document"), tr("These files already exist:\n\n%1\n\nReplace them?").arg(list), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        {
            return;
        }
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString errorMessage;
    for (size_t i = 0; i < parts.size() && errorMessage.isEmpty(); ++i)
    {
        pdf::PDFDocument partDocument;
        pdf::PDFOperationResult result = pdf::PDFPageOperations::extractPages(m_pdfDocument.data(), parts[i].pages, &partDocument);
        if (result)
        {
            pdf::PDFDocumentWriter writer(nullptr);
            result = writer.write(fileNames[int(i)], &partDocument, true);
        }
        if (!result)
        {
            errorMessage = tr("File '%1' cannot be written. %2").arg(fileNames[int(i)], result.getErrorMessage());
        }
    }
    QApplication::restoreOverrideCursor();

    if (!errorMessage.isEmpty())
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), errorMessage);
    }
    else if (m_mainWindowInterface)
    {
        m_mainWindowInterface->setStatusBarMessage(tr("The document was split into %1 files in '%2'.").arg(parts.size()).arg(QDir::toNativeSeparators(directory.absolutePath())), 10000);
    }
}

bool PDFProgramController::insertFiles(const QStringList& fileNames, pdf::PDFInteger position)
{
    if (!canModifyPages() || fileNames.isEmpty())
    {
        return false;
    }

    const pdf::PDFCatalog* catalog = m_pdfDocument->getCatalog();
    const pdf::PDFInteger pageCount = pdf::PDFInteger(catalog->getPageCount());
    position = qBound<pdf::PDFInteger>(0, position, pageCount);

    // Pictures are fitted into pages of the size of the page next to them
    const pdf::PDFInteger neighbourPage = qBound<pdf::PDFInteger>(0, position - 1, pageCount - 1);
    const QSizeF pageSize = catalog->getPage(neighbourPage)->getMediaBox().size();

    pdf::PDFDocument document = *m_pdfDocument;
    pdf::PDFInteger currentPosition = position;
    std::vector<pdf::PDFFirePageOrganizer::ImageSource> images;

    auto insert = [&](const pdf::PDFDocument& insertedDocument, const QString& fileName)
    {
        pdf::PDFDocument mergedDocument;
        const pdf::PDFOperationResult result = pdf::PDFPageOperations::insertDocument(&document, &insertedDocument, currentPosition, &mergedDocument);
        if (!result)
        {
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' cannot be inserted. %2").arg(fileName, result.getErrorMessage()));
            return false;
        }
        currentPosition += pdf::PDFInteger(insertedDocument.getCatalog()->getPageCount());
        document = std::move(mergedDocument);
        return true;
    };

    // Consecutive pictures make one inserted document
    auto insertImages = [&]()
    {
        if (images.empty())
        {
            return true;
        }

        pdf::PDFDocument imagesDocument;
        const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::createDocumentFromImages(images, pdf::PDFFirePageOrganizer::ImagePageSize::Custom, &imagesDocument, pageSize);
        images.clear();
        if (!result)
        {
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), result.getErrorMessage());
            return false;
        }
        return insert(imagesDocument, tr("pictures"));
    };

    for (const QString& fileName : fileNames)
    {
        if (pdf::PDFFirePageOrganizer::isSupportedImageFile(fileName))
        {
            const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::readImageFile(fileName, &images);
            if (!result)
            {
                QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), result.getErrorMessage());
                return false;
            }
            continue;
        }

        if (QFileInfo(fileName).suffix().compare("pdf", Qt::CaseInsensitive) != 0)
        {
            QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), tr("File '%1' is not a PDF document or a picture.").arg(QFileInfo(fileName).fileName()));
            return false;
        }

        pdf::PDFDocument insertedDocument;
        if (!insertImages() || !readDocumentForInsert(fileName, &insertedDocument) || !insert(insertedDocument, fileName))
        {
            return false;
        }
    }

    if (!insertImages())
    {
        return false;
    }

    applyPageOperation(true, std::move(document), position);
    return true;
}

bool PDFProgramController::insertImagesFromFile(pdf::PDFInteger position)
{
    if (!canModifyPages())
    {
        return false;
    }

    const QStringList fileNames = QFileDialog::getOpenFileNames(m_mainWindow, tr("Insert Pictures as Pages"), m_settings->getDirectory(), getImageFileFilter());
    return !fileNames.isEmpty() && insertFiles(fileNames, position);
}

void PDFProgramController::createDocumentFromImages()
{
    if (!canClose())
    {
        return;
    }

    const QStringList fileNames = QFileDialog::getOpenFileNames(m_mainWindow, tr("Create PDF from Pictures"), m_settings->getDirectory(), getImageFileFilter());
    if (fileNames.isEmpty())
    {
        return;
    }

    const QStringList sizes = { tr("The size of each picture"), tr("Letter, the picture fitted into the page"), tr("A4, the picture fitted into the page") };
    bool ok = false;
    const QString size = QInputDialog::getItem(m_mainWindow, tr("Create PDF from Pictures"), tr("One picture per page. Size of the pages:"), sizes, 0, false, &ok);
    if (!ok)
    {
        return;
    }

    const pdf::PDFFirePageOrganizer::ImagePageSize pageSize = (size == sizes[1]) ? pdf::PDFFirePageOrganizer::ImagePageSize::Letter
                                                                                 : (size == sizes[2]) ? pdf::PDFFirePageOrganizer::ImagePageSize::A4
                                                                                                      : pdf::PDFFirePageOrganizer::ImagePageSize::Image;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    pdf::PDFDocument document;
    const pdf::PDFOperationResult result = pdf::PDFFirePageOrganizer::createDocumentFromImageFiles(fileNames, pageSize, &document);
    QApplication::restoreOverrideCursor();

    if (!result)
    {
        QMessageBox::critical(m_mainWindow, QApplication::applicationDisplayName(), result.getErrorMessage());
        return;
    }

    showNewDocument(std::move(document));
}

}   // namespace pdfviewer
