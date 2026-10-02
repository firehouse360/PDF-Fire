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

#include "pagemarksplugin.h"
#include "pdffirepermissions.h"
#include "pagemarks.h"
#include "pagemarksdialogs.h"

#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>

namespace pdfplugin
{

PageMarksPlugin::PageMarksPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionAddWatermark(nullptr),
    m_actionAddHeaderFooter(nullptr),
    m_actionBatesNumbering(nullptr),
    m_actionAddBackground(nullptr),
    m_actionRemoveMarks(nullptr)
{

}

void PageMarksPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    auto createAction = [this](const char* objectName, const QString& text, const char* icon, const QString& toolTip)
    {
        QAction* action = new QAction(QIcon(QString(":/pdfplugins/pagemarksplugin/%1").arg(QString::fromLatin1(icon))), text, this);
        action->setObjectName(QString::fromLatin1(objectName));
        action->setToolTip(toolTip);
        return action;
    };

    m_actionAddWatermark = createAction("pagemarksplugin_AddWatermark", tr("Add Watermark..."), "marks-watermark.svg",
                                        tr("Put a text (CONFIDENTIAL, DRAFT) or an image across the pages - behind or in front of the content, with a transparency. The text stays searchable."));
    m_actionAddHeaderFooter = createAction("pagemarksplugin_AddHeaderFooter", tr("Add Header && Footer..."), "marks-header-footer.svg",
                                           tr("Write a text at the top and the bottom of the pages: page numbers (Page 1 of 9), the date, the file name"));
    m_actionBatesNumbering = createAction("pagemarksplugin_BatesNumbering", tr("Bates Numbering..."), "marks-bates.svg",
                                          tr("Number every page with a unique number (prefix + zero padded number + suffix), as legal documents are numbered"));
    // PDF Fire: a page of another PDF (a letterhead, a form background) underneath the content
    m_actionAddBackground = createAction("pagemarksplugin_AddBackground", tr("Add Background..."), "marks-background.svg",
                                         tr("Put a page of another PDF file (a letterhead, the background of a form) underneath the content of the pages"));
    m_actionRemoveMarks = createAction("pagemarksplugin_RemoveMarks", tr("Remove Watermarks && Headers..."), "marks-remove.svg",
                                       tr("Remove the watermarks, headers and footers and Bates numbers (added by PDF Fire or by Adobe Acrobat) and the backgrounds "
                                          "(added by PDF Fire), without changing the rest of the pages"));

    connect(m_actionAddWatermark, &QAction::triggered, this, &PageMarksPlugin::onAddWatermark);
    connect(m_actionAddHeaderFooter, &QAction::triggered, this, &PageMarksPlugin::onAddHeaderFooter);
    connect(m_actionBatesNumbering, &QAction::triggered, this, &PageMarksPlugin::onBatesNumbering);
    connect(m_actionAddBackground, &QAction::triggered, this, &PageMarksPlugin::onAddBackground);
    connect(m_actionRemoveMarks, &QAction::triggered, this, &PageMarksPlugin::onRemoveMarks);

    updateActions();
}

void PageMarksPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);
    updateActions();
}

std::vector<QAction*> PageMarksPlugin::getActions() const
{
    return { m_actionAddWatermark, m_actionAddHeaderFooter, m_actionBatesNumbering, m_actionAddBackground, nullptr, m_actionRemoveMarks };
}

QString PageMarksPlugin::getPluginMenuName() const
{
    return tr("Page Mar&ks");
}

pdf::PDFPlugin::PluginMenuLocation PageMarksPlugin::getPluginMenuLocation() const
{
    return PluginMenuLocation::Edit;
}

void PageMarksPlugin::updateActions()
{
    // PDF Fire: the marks change the content - not on a protected or certified document
    const bool hasPages = m_document && m_document->getCatalog()->getPageCount() > 0 && pdf::PDFFirePermissions::canModifyContent(m_document);
    for (QAction* action : { m_actionAddWatermark, m_actionAddHeaderFooter, m_actionBatesNumbering, m_actionAddBackground, m_actionRemoveMarks })
    {
        if (action)
        {
            action->setEnabled(hasPages);
        }
    }
}

QWidget* PageMarksPlugin::getDialogParent() const
{
    if (m_dataExchangeInterface && m_dataExchangeInterface->getMainWindow())
    {
        return m_dataExchangeInterface->getMainWindow();
    }
    return m_widget;
}

PageMarksPlugin::Existing PageMarksPlugin::askAboutExisting(int kind, const QString& question)
{
    if (PageMarks::countMarks(m_document, kind) == 0)
    {
        return Existing::Add;
    }

    QMessageBox messageBox(QMessageBox::Question, tr("Page Marks"), question, QMessageBox::NoButton, getDialogParent());
    QPushButton* replaceButton = messageBox.addButton(tr("Replace"), QMessageBox::AcceptRole);
    QPushButton* addButton = messageBox.addButton(tr("Add"), QMessageBox::AcceptRole);
    messageBox.addButton(QMessageBox::Cancel);
    messageBox.setDefaultButton(replaceButton);
    messageBox.exec();

    if (messageBox.clickedButton() == replaceButton)
    {
        return Existing::Replace;
    }
    if (messageBox.clickedButton() == addButton)
    {
        return Existing::Add;
    }
    return Existing::Cancel;
}

void PageMarksPlugin::modify(const std::function<bool(pdf::PDFDocumentBuilder*, QString*)>& change)
{
    if (!m_document)
    {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    pdf::PDFDocumentModifier modifier(m_document);
    QString errorMessage;
    const bool changed = change(modifier.getBuilder(), &errorMessage);
    QApplication::restoreOverrideCursor();

    if (!changed)
    {
        if (!errorMessage.isEmpty())
        {
            QMessageBox::critical(getDialogParent(), tr("Page Marks"), errorMessage);
        }
        return;
    }

    // One step of Undo; the view stays where it is
    modifier.markReset();
    if (modifier.finalize())
    {
        pdf::PDFModifiedDocument::ModificationFlags flags = modifier.getFlags();
        flags.setFlag(pdf::PDFModifiedDocument::PreserveUndoRedo);
        flags.setFlag(pdf::PDFModifiedDocument::PreserveView);
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, flags));
    }
}

void PageMarksPlugin::onAddWatermark()
{
    if (!m_document)
    {
        return;
    }

    WatermarkDialog dialog(m_document, getDialogParent());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const Existing existing = askAboutExisting(PageMarks::Watermark, tr("The document has a watermark already. Replace it by the new one, or add the new one to it?"));
    if (existing == Existing::Cancel)
    {
        return;
    }

    const PageMarks::WatermarkSettings settings = dialog.getSettings();
    const pdf::PDFDocument* document = m_document;
    modify([&](pdf::PDFDocumentBuilder* builder, QString* errorMessage)
    {
        if (existing == Existing::Replace)
        {
            PageMarks::removeMarks(builder, document, PageMarks::Watermark);
        }
        return PageMarks::addWatermark(builder, document, settings, errorMessage);
    });
}

void PageMarksPlugin::onAddHeaderFooter()
{
    if (!m_document)
    {
        return;
    }

    const QString fileName = m_dataExchangeInterface ? QFileInfo(m_dataExchangeInterface->getOriginalFileName()).fileName() : QString();
    HeaderFooterDialog dialog(m_document, fileName, getDialogParent());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const Existing existing = askAboutExisting(PageMarks::HeaderFooter, tr("The document has a header or footer already. Replace it by the new one, or add the new one to it?"));
    if (existing == Existing::Cancel)
    {
        return;
    }

    const PageMarks::HeaderFooterSettings settings = dialog.getSettings();
    const pdf::PDFDocument* document = m_document;
    modify([&](pdf::PDFDocumentBuilder* builder, QString* errorMessage)
    {
        if (existing == Existing::Replace)
        {
            PageMarks::removeMarks(builder, document, PageMarks::HeaderFooter);
        }
        return PageMarks::addHeaderFooter(builder, document, settings, errorMessage);
    });
}

void PageMarksPlugin::onBatesNumbering()
{
    if (!m_document)
    {
        return;
    }

    BatesDialog dialog(m_document, getDialogParent());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const Existing existing = askAboutExisting(PageMarks::Bates, tr("The document has Bates numbers already. Replace them by the new ones, or add the new ones?"));
    if (existing == Existing::Cancel)
    {
        return;
    }

    const PageMarks::BatesSettings settings = dialog.getSettings();
    const pdf::PDFDocument* document = m_document;
    modify([&](pdf::PDFDocumentBuilder* builder, QString* errorMessage)
    {
        if (existing == Existing::Replace)
        {
            PageMarks::removeMarks(builder, document, PageMarks::Bates);
        }
        return PageMarks::addBatesNumbers(builder, document, settings, errorMessage);
    });
}

void PageMarksPlugin::onAddBackground()
{
    if (!m_document)
    {
        return;
    }

    const std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    BackgroundDialog dialog(m_document, currentPages.empty() ? 0 : currentPages.front(), getDialogParent());
    if (dialog.exec() != QDialog::Accepted || !dialog.getSourceDocument())
    {
        return;
    }

    const Existing existing = askAboutExisting(PageMarks::Background, tr("The document has a background already. Replace it by the new one, or add the new one to it?"));
    if (existing == Existing::Cancel)
    {
        return;
    }

    const PageMarks::BackgroundSettings settings = dialog.getSettings();
    const pdf::PDFDocument* document = m_document;
    const pdf::PDFDocument* sourceDocument = dialog.getSourceDocument();
    modify([&](pdf::PDFDocumentBuilder* builder, QString* errorMessage)
    {
        if (existing == Existing::Replace)
        {
            PageMarks::removeMarks(builder, document, PageMarks::Background);
        }
        return PageMarks::addBackground(builder, document, sourceDocument, settings, errorMessage);
    });
}

void PageMarksPlugin::onRemoveMarks()
{
    if (!m_document)
    {
        return;
    }

    if (PageMarks::countMarks(m_document, PageMarks::AllKinds) == 0)
    {
        QMessageBox::information(getDialogParent(), tr("Remove Watermarks, Headers & Footers"),
                                 tr("The document has no watermarks, headers and footers, Bates numbers or backgrounds, which could be removed."));
        return;
    }

    RemoveMarksDialog dialog(m_document, getDialogParent());
    if (dialog.exec() != QDialog::Accepted || dialog.getKinds() == 0)
    {
        return;
    }

    const int kinds = dialog.getKinds();
    const pdf::PDFDocument* document = m_document;
    modify([&](pdf::PDFDocumentBuilder* builder, QString*)
    {
        return PageMarks::removeMarks(builder, document, kinds) > 0;
    });
}

}   // namespace pdfplugin
