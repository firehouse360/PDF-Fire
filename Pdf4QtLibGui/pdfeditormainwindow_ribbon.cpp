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

// PDF Fire: the shell of the editor window - the ribbon, which replaces the menu
// bar and the toolbars, the controls of the status bar and the default view of
// an opened document. The window itself is built by pdfeditormainwindow.cpp the
// same way as before (menus, toolbars, actions), this file then rearranges it.
// So the changes of the original file stay minimal.

#include "pdfeditormainwindow.h"
#include "ui_pdfeditormainwindow.h"
#include "pdffireribbon.h"
#include "pdffirewelcome.h"
#include "pdffireemaildialog.h"
#include "pdfrecentfilemanager.h"
#include "pdfsidebarwidget.h"
#include "pdfadvancedfindwidget.h"
#include "pdfwidgetutils.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QDesktopServices>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenuBar>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include "pdfdbgheap.h"

namespace pdfviewer
{

namespace
{

QString plainText(QString text)
{
    text.remove(QChar('&'));
    return text;
}

/// Text of a button of the ribbon: the words are capitalized, as it is usual
/// for the labels of buttons ("Select table" -> "Select Table").
QString buttonText(const QString& text)
{
    static const QStringList lowerCaseWords = { "a", "an", "and", "by", "for", "of", "on", "or", "the", "to", "with" };

    QStringList words = text.split(QChar(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < words.size(); ++i)
    {
        QString& word = words[i];
        if (i > 0 && lowerCaseWords.contains(word.toLower()))
        {
            word = word.toLower();
        }
        else if (word.front().isLower())
        {
            word[0] = word.front().toUpper();
        }
    }

    return words.join(QChar(' '));
}

/// Icon from the resources, in the colors of the current theme
QIcon themedIcon(const QWidget* widget, const QString& resource)
{
    QIcon icon(resource);
    if (pdf::PDFWidgetUtils::isDarkTheme())
    {
        icon = pdf::PDFWidgetUtils::convertIconForDarkTheme(icon, pdf::PDFWidgetUtils::scaleDPI(widget, QSize(28, 28)), widget->devicePixelRatioF());
    }
    return icon;
}

/// Collects the actions of the menu, and of its submenus
void collectActions(const QMenu* menu, QList<QAction*>& actions)
{
    for (QAction* action : menu->actions())
    {
        if (action->isSeparator())
        {
            continue;
        }

        if (const QMenu* submenu = action->menu())
        {
            collectActions(submenu, actions);
        }
        else
        {
            actions.push_back(action);
        }
    }
}

/// Fills the group with the actions of the menu. A menu with many
/// actions gets buttons without a text, so the group stays compact.
/// Finds the action of the menu by its object name
QAction* findMenuAction(const QMenu* menu, const QString& objectName)
{
    if (menu)
    {
        for (QAction* action : menu->actions())
        {
            if (action->objectName() == objectName)
            {
                return action;
            }
        }
    }

    return nullptr;
}

void fillGroupFromMenu(PDFFireRibbonGroup* group, const QMenu* menu, int maximalCountWithText = 9, bool isFirstLarge = false, const QStringList& excludedActions = QStringList())
{
    if (!menu)
    {
        return;
    }

    if (isFirstLarge)
    {
        for (QAction* action : menu->actions())
        {
            if (!action->isSeparator() && !action->menu())
            {
                group->addLargeAction(action);
                break;
            }
        }
    }

    int count = 0;
    for (const QAction* action : menu->actions())
    {
        if (!action->isSeparator())
        {
            ++count;
        }
    }

    const bool showText = count <= maximalCountWithText;
    for (QAction* action : menu->actions())
    {
        if (action->isSeparator())
        {
            if (!showText)
            {
                group->breakColumn();
            }
            continue;
        }

        if (excludedActions.contains(action->objectName()))
        {
            continue;
        }

        if (QMenu* submenu = action->menu())
        {
            group->addSmallMenu(action->icon(), plainText(action->text()), submenu);
        }
        else if (isFirstLarge)
        {
            // The first action is already added as the large button
            isFirstLarge = false;
        }
        else
        {
            group->addSmallAction(action, showText);
        }
    }
}

/// Adds a large button opening the menu of a plugin. The icon of the
/// first action of the menu is used as the icon of the button.
void addPluginMenuButton(PDFFireRibbonGroup* group, const QString& text, QMenu* menu)
{
    if (!menu)
    {
        return;
    }

    QIcon icon;
    for (const QAction* action : menu->actions())
    {
        if (!action->icon().isNull())
        {
            icon = action->icon();
            break;
        }
    }

    group->addLargeMenu(icon, text, menu);
}

}   // namespace

QMenu* PDFEditorMainWindow::findPluginMenu(const QString& name) const
{
    for (const QMenu* parentMenu : { ui->menuEdit, ui->menuTools })
    {
        for (QAction* action : parentMenu->actions())
        {
            QMenu* menu = action->menu();
            if (menu && plainText(menu->title()).compare(name, Qt::CaseInsensitive) == 0)
            {
                return menu;
            }
        }
    }

    return nullptr;
}

void PDFEditorMainWindow::setupRibbon()
{
    m_ribbon = new PDFFireRibbon(this);

    // Settings saved by a version with the old shell (large buttons with texts in
    // the side panel) are converted once to the slim icon rail. The user can choose
    // another size in the options later, it is then kept.
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
        constexpr int SHELL_VERSION = 1;
        if (settings.value("PDFFire/shellVersion", 0).toInt() < SHELL_VERSION)
        {
            PDFViewerSettings::Settings viewerSettings = m_programController->getSettings()->getSettings();
            viewerSettings.m_sidebarButtonIconSize = PDFViewerSettings::SidebarButtonIconSizeSmall;
            m_programController->getSettings()->setSettings(viewerSettings);
            settings.setValue("PDFFire/shellVersion", SHELL_VERSION);
        }
    }

    QAction* advancedFindAction = m_advancedFindDockWidget->toggleViewAction();
    QAction* sidebarAction = m_sidebarDockWidget->toggleViewAction();
    sidebarAction->setText(tr("Side Panel"));
    sidebarAction->setIcon(themedIcon(this, ":/resources/sidebar-outline.svg"));

    // Actions without an icon would be bare texts among the buttons
    if (ui->actionDeleteAnnotation->icon().isNull())
    {
        ui->actionDeleteAnnotation->setIcon(themedIcon(this, ":/resources/close.svg"));
    }

    // The options are reachable from the File button too
    ui->menuFile->insertAction(ui->actionQuit, ui->actionOptions);
    ui->menuFile->insertSeparator(ui->actionQuit);
    m_ribbon->setFileMenu(tr("File"), ui->menuFile);

    // ---- Actions added by PDF Fire ---------------------------------------------
    auto createAction = [this](const char* objectName, const QString& text, const QString& icon, const QString& toolTip)
    {
        QAction* action = new QAction(themedIcon(this, icon), text, this);
        action->setObjectName(objectName);
        action->setToolTip(toolTip);
        addAction(action);
        return action;
    };

    QAction* newDocumentAction = createAction("actionNewDocument", tr("New"), ":/resources/page.svg", tr("Create a new document with one blank page"));
    newDocumentAction->setShortcut(QKeySequence::New);
    connect(newDocumentAction, &QAction::triggered, m_programController, &PDFProgramController::newDocument);
    ui->menuFile->insertAction(ui->actionOpen, newDocumentAction);

    QAction* insertBlankPageAction = createAction("actionInsertBlankPage", tr("Blank Page"), ":/resources/page.svg", tr("Insert a blank page after the current page"));
    QAction* insertPagesFromFileAction = createAction("actionInsertPagesFromFile", tr("Insert PDF"), ":/resources/open.svg", tr("Merge: insert the pages of other PDF files after the current page"));
    QAction* deletePagesAction = createAction("actionDeletePages", tr("Delete"), ":/resources/close.svg", tr("Delete the current page, or the pages selected in the thumbnails"));
    QAction* rotatePagesLeftAction = createAction("actionRotatePagesLeft", tr("Rotate Left"), ":/resources/rotate-left.svg", tr("Rotate the pages counterclockwise (saved with the document)"));
    QAction* rotatePagesRightAction = createAction("actionRotatePagesRight", tr("Rotate Right"), ":/resources/rotate-right.svg", tr("Rotate the pages clockwise (saved with the document)"));
    QAction* movePagesUpAction = createAction("actionMovePagesUp", tr("Move Earlier"), ":/resources/previous-page.svg", tr("Move the pages one position towards the start of the document"));
    QAction* movePagesDownAction = createAction("actionMovePagesDown", tr("Move Later"), ":/resources/next-page.svg", tr("Move the pages one position towards the end of the document"));
    QAction* extractPagesAction = createAction("actionExtractPages", tr("Extract"), ":/resources/save-as.svg", tr("Save the current page, or the pages selected in the thumbnails, as a new PDF file"));

    connect(insertBlankPageAction, &QAction::triggered, this, [this]() { const auto pages = getTargetPages(); if (!pages.empty()) { m_programController->insertBlankPage(pages.back() + 1); } });
    connect(insertPagesFromFileAction, &QAction::triggered, this, [this]() { const auto pages = getTargetPages(); if (!pages.empty()) { m_programController->insertPagesFromFile(pages.back() + 1); } });
    connect(deletePagesAction, &QAction::triggered, this, [this]() { m_programController->deletePages(getTargetPages()); });
    connect(rotatePagesLeftAction, &QAction::triggered, this, [this]() { m_programController->rotatePages(getTargetPages(), false); });
    connect(rotatePagesRightAction, &QAction::triggered, this, [this]() { m_programController->rotatePages(getTargetPages(), true); });
    connect(movePagesUpAction, &QAction::triggered, this, [this]() { m_programController->movePages(getTargetPages(), false); });
    connect(movePagesDownAction, &QAction::triggered, this, [this]() { m_programController->movePages(getTargetPages(), true); });
    connect(extractPagesAction, &QAction::triggered, this, [this]() { m_programController->extractPages(getTargetPages()); });

    m_pageActions = { insertBlankPageAction, insertPagesFromFileAction, deletePagesAction, rotatePagesLeftAction, rotatePagesRightAction,
                      movePagesUpAction, movePagesDownAction, extractPagesAction };

    // The same operations are offered by the context menu of the thumbnails, where several pages can be selected
    QAction* thumbnailSeparator1 = new QAction(this);
    thumbnailSeparator1->setSeparator(true);
    QAction* thumbnailSeparator2 = new QAction(this);
    thumbnailSeparator2->setSeparator(true);
    m_sidebarWidget->setThumbnailActions({ insertBlankPageAction, insertPagesFromFileAction, thumbnailSeparator1,
                                           rotatePagesLeftAction, rotatePagesRightAction, movePagesUpAction, movePagesDownAction, thumbnailSeparator2,
                                           extractPagesAction, deletePagesAction });

    const QMenu* editorMenu = findPluginMenu("Editor");
    const QMenu* redactMenu = findPluginMenu("Redact");
    const QMenu* signatureMenu = findPluginMenu("Signature");

    // ---- Home -----------------------------------------------------------------
    PDFFireRibbonPage* homePage = m_ribbon->addPage(tr("Home"));

    PDFFireRibbonGroup* fileGroup = homePage->addGroup(tr("File"));
    fileGroup->addLargeAction(newDocumentAction);
    fileGroup->addLargeAction(ui->actionOpen);
    fileGroup->addLargeAction(ui->actionSave);
    fileGroup->addSmallAction(ui->actionSave_As);
    fileGroup->addSmallAction(ui->actionPrint);
    fileGroup->addSmallAction(ui->actionSend_by_E_Mail);

    // PDF Fire: export to an editable document (ExportPlugin), next to Save As
    if (QAction* exportOdtAction = findMenuAction(findPluginMenu("Export"), "exportplugin_ExportOdt"))
    {
        exportOdtAction->setIconText(tr("Export ODT"));
        exportOdtAction->setToolTip(tr("Save the document as an editable text document (OpenDocument .odt) - it opens in LibreOffice Writer and in Microsoft Word"));
        fileGroup->addSmallAction(exportOdtAction);
    }

    PDFFireRibbonGroup* undoGroup = homePage->addGroup(tr("Undo"));
    undoGroup->addSmallAction(ui->actionUndo);
    undoGroup->addSmallAction(ui->actionRedo);

    PDFFireRibbonGroup* selectGroup = homePage->addGroup(tr("Select"));
    selectGroup->addLargeAction(ui->actionSelectText);
    selectGroup->addSmallAction(ui->actionSelectTable);
    selectGroup->addSmallAction(ui->actionCopyText);
    selectGroup->addSmallAction(ui->actionSelectTextAll);

    PDFFireRibbonGroup* findGroup = homePage->addGroup(tr("Find"));
    findGroup->addLargeAction(ui->actionFind);
    findGroup->addSmallAction(ui->actionFindNext);
    findGroup->addSmallAction(ui->actionFindPrevious);
    findGroup->addSmallAction(advancedFindAction);

    PDFFireRibbonGroup* zoomGroup = homePage->addGroup(tr("Zoom"));
    zoomGroup->addLargeAction(ui->actionFitWidth);
    zoomGroup->addLargeAction(ui->actionFitPage);
    zoomGroup->addSmallAction(ui->actionZoom_In);
    zoomGroup->addSmallAction(ui->actionZoom_Out);
    zoomGroup->addSmallAction(ui->actionFitHeight);

    PDFFireRibbonGroup* navigateGroup = homePage->addGroup(tr("Go To"));
    navigateGroup->addSmallAction(ui->actionGoToPreviousPage);
    navigateGroup->addSmallAction(ui->actionGoToNextPage);
    navigateGroup->breakColumn();
    navigateGroup->addSmallAction(ui->actionGoToDocumentStart);
    navigateGroup->addSmallAction(ui->actionGoToDocumentEnd);

    PDFFireRibbonGroup* homeToolsGroup = homePage->addGroup(tr("Tools"));
    homeToolsGroup->addSmallAction(ui->actionMagnifier);
    homeToolsGroup->addSmallAction(ui->actionScreenshot);
    homeToolsGroup->addSmallAction(ui->actionExtractImage);

    // ---- Comment --------------------------------------------------------------
    PDFFireRibbonPage* commentPage = m_ribbon->addPage(tr("Comment"));

    PDFFireRibbonGroup* markupGroup = commentPage->addGroup(tr("Text Markup"));
    markupGroup->addLargeAction(ui->actionCreateTextHighlight);
    markupGroup->addSmallAction(ui->actionCreateTextUnderline);
    markupGroup->addSmallAction(ui->actionCreateTextStrikeout);
    markupGroup->addSmallAction(ui->actionCreateTextSquiggly);

    PDFFireRibbonGroup* notesGroup = commentPage->addGroup(tr("Notes"));
    notesGroup->addLargeMenu(themedIcon(this, ":/resources/annot-sticky-note.svg"), tr("Sticky Note"), ui->menuSticky_Note);
    notesGroup->addLargeAction(ui->actionInlineText);
    notesGroup->addLargeMenu(themedIcon(this, ":/resources/wallet.svg"), tr("Stamp"), ui->menuStamp);

    PDFFireRibbonGroup* drawGroup = commentPage->addGroup(tr("Draw"));
    drawGroup->addSmallAction(ui->actionCreateStraightLine);
    drawGroup->addSmallAction(ui->actionCreateArrow);
    drawGroup->addSmallAction(ui->actionCreatePolyline);
    drawGroup->addSmallAction(ui->actionCreateFreehandCurve);
    drawGroup->addSmallAction(ui->actionCreateRectangle);
    drawGroup->addSmallAction(ui->actionCreateEllipse);
    drawGroup->addSmallAction(ui->actionCreatePolygon);

    PDFFireRibbonGroup* linkGroup = commentPage->addGroup(tr("Links"));
    linkGroup->addLargeAction(ui->actionCreateHyperlink);
    linkGroup->addLargeMenu(themedIcon(this, ":/resources/hyperlink.svg"), tr("Link to Page"), ui->menuHyperlinkToThisPDF);

    PDFFireRibbonGroup* manageCommentsGroup = commentPage->addGroup(tr("Manage"));
    manageCommentsGroup->addLargeAction(ui->actionDeleteAnnotation);

    // ---- Review ---------------------------------------------------------------
    PDFFireRibbonPage* reviewPage = m_ribbon->addPage(tr("Review"));

    // The accept and reject marks are tools of the page content editing. Here they
    // switch the editing on by themselves, so a reviewer can just click and place them.
    PDFFireRibbonGroup* marksGroup = reviewPage->addGroup(tr("Marks"));
    QAction* editorActivateAction = findMenuAction(editorMenu, "editortool_activateAction");
    for (const auto& [objectName, text] : { std::pair<const char*, QString>{ "editortool_createAcceptMarkAction", tr("Accept Mark") },
                                            std::pair<const char*, QString>{ "editortool_createRejectMarkAction", tr("Reject Mark") } })
    {
        QAction* toolAction = findMenuAction(editorMenu, objectName);
        if (!toolAction || !editorActivateAction)
        {
            continue;
        }

        QAction* markAction = new QAction(toolAction->icon(), text, this);
        markAction->setToolTip(tr("%1: click on the page to place it. The marks are written to the page by Edit > Apply.").arg(text));
        markAction->setEnabled(editorActivateAction->isEnabled());
        connect(editorActivateAction, &QAction::changed, markAction, [markAction, editorActivateAction]() { markAction->setEnabled(editorActivateAction->isEnabled()); });
        connect(markAction, &QAction::triggered, this, [editorActivateAction, toolAction]()
        {
            if (!editorActivateAction->isChecked())
            {
                editorActivateAction->trigger();
            }
            QTimer::singleShot(0, toolAction, &QAction::trigger);
        });
        marksGroup->addLargeAction(markAction);
    }

    PDFFireRibbonGroup* reviewNotesGroup = reviewPage->addGroup(tr("Notes and Stamps"));
    reviewNotesGroup->addLargeMenu(themedIcon(this, ":/resources/annot-sticky-note.svg"), tr("Sticky Note"), ui->menuSticky_Note);
    reviewNotesGroup->addLargeMenu(themedIcon(this, ":/resources/wallet.svg"), tr("Stamp"), ui->menuStamp);

    // PDF Fire: the marks a reviewer draws to point something out (Acrobat:
    // Comment > Drawing tools) - the same tools as on the Comment tab
    PDFFireRibbonGroup* reviewDrawGroup = reviewPage->addGroup(tr("Point It Out"));
    reviewDrawGroup->addLargeAction(ui->actionCreateEllipse);
    reviewDrawGroup->addLargeAction(ui->actionCreateArrow);
    reviewDrawGroup->addSmallAction(ui->actionCreateRectangle);
    reviewDrawGroup->addSmallAction(ui->actionCreateStraightLine);
    reviewDrawGroup->addSmallAction(ui->actionCreateFreehandCurve);

    PDFFireRibbonGroup* reviewCommentsGroup = reviewPage->addGroup(tr("Comments"));
    QAction* showCommentsAction = createAction("actionShowComments", tr("Comment List"), ":/resources/sidebar-annotations.svg", tr("Show the comments of the document in the side panel"));
    connect(showCommentsAction, &QAction::triggered, this, [this]()
    {
        if (m_sidebarWidget->isEmpty(PDFSidebarWidget::Notes))
        {
            statusBar()->showMessage(tr("This document has no comments yet."), 5000);
            return;
        }
        m_sidebarDockWidget->show();
        m_sidebarWidget->setCollapsed(false);
        m_sidebarWidget->selectPage(PDFSidebarWidget::Notes);
    });
    reviewCommentsGroup->addLargeAction(showCommentsAction);
    reviewCommentsGroup->addLargeAction(ui->actionDeleteAnnotation);

    // ---- Edit -----------------------------------------------------------------
    PDFFireRibbonPage* editPage = m_ribbon->addPage(tr("Edit"));
    if (editorMenu)
    {
        // "Create Rectangle" -> "Rectangle", the group is about creating
        for (QAction* action : editorMenu->actions())
        {
            QString text = plainText(action->text());
            if (text.startsWith("Create "))
            {
                action->setIconText(text.mid(7));
            }
        }

        if (QAction* clearAction = findMenuAction(editorMenu, "editortool_clearAction"))
        {
            // It removes everything from the edited pages, texts included
            clearAction->setIconText(tr("Clear Page"));
            clearAction->setToolTip(tr("Remove everything from the edited pages - texts, images and graphics"));
        }

        fillGroupFromMenu(editPage->addGroup(tr("Page Content")), editorMenu, 100, true,
                          { "editortool_createAcceptMarkAction", "editortool_createRejectMarkAction" });
    }

    // ---- Pages ----------------------------------------------------------------
    PDFFireRibbonPage* pagesPage = m_ribbon->addPage(tr("Pages"));

    PDFFireRibbonGroup* insertPagesGroup = pagesPage->addGroup(tr("Insert"));
    insertPagesGroup->addLargeAction(insertBlankPageAction);
    insertPagesGroup->addLargeAction(insertPagesFromFileAction);

    PDFFireRibbonGroup* organizePagesGroup = pagesPage->addGroup(tr("Organize"));
    organizePagesGroup->addLargeAction(deletePagesAction);
    organizePagesGroup->addSmallAction(rotatePagesLeftAction);
    organizePagesGroup->addSmallAction(rotatePagesRightAction);
    organizePagesGroup->addSmallAction(extractPagesAction);
    organizePagesGroup->addSmallAction(movePagesUpAction);
    organizePagesGroup->addSmallAction(movePagesDownAction);

    PDFFireRibbonGroup* pageGroup = pagesPage->addGroup(tr("Page"));
    pageGroup->addLargeAction(ui->actionPageGeometry);
    pageGroup->addLargeAction(ui->actionInsertPageNumbers);

    PDFFireRibbonGroup* bookmarkGroup = pagesPage->addGroup(tr("Bookmarks"));
    bookmarkGroup->addLargeAction(ui->actionBookmarkPage);
    bookmarkGroup->addSmallAction(ui->actionGotoPreviousBookmark);
    bookmarkGroup->addSmallAction(ui->actionGotoNextBookmark);
    bookmarkGroup->addSmallMenu(themedIcon(this, ":/resources/settings.svg"), tr("Settings"), ui->menuBookmarkSettings);

    if (QAction* outlineNewItemAction = m_sidebarWidget->getOutlineNewItemAction())
    {
        bookmarkGroup->addSmallAction(outlineNewItemAction); // PDF Fire: in the bookmarks group (the tab is full)
    }

    // PDF Fire: watermarks, headers and footers and Bates numbers (PageMarksPlugin)
    if (const QMenu* pageMarksMenu = findPluginMenu("Page Marks"))
    {
        PDFFireRibbonGroup* pageMarksGroup = pagesPage->addGroup(tr("Page Marks"));
        for (const auto& [objectName, text, isLarge] : { std::tuple<const char*, QString, bool>{ "pagemarksplugin_AddWatermark", tr("Watermark"), true },
                                                         std::tuple<const char*, QString, bool>{ "pagemarksplugin_AddHeaderFooter", tr("Header && Footer"), true },
                                                         std::tuple<const char*, QString, bool>{ "pagemarksplugin_BatesNumbering", tr("Bates Numbering"), false },
                                                         std::tuple<const char*, QString, bool>{ "pagemarksplugin_AddBackground", tr("Background"), false },   // PDF Fire: a page of another PDF underneath
                                                         std::tuple<const char*, QString, bool>{ "pagemarksplugin_RemoveMarks", tr("Remove Marks"), false } })
        {
            if (QAction* action = findMenuAction(pageMarksMenu, objectName))
            {
                action->setIconText(text);
                isLarge ? pageMarksGroup->addLargeAction(action) : pageMarksGroup->addSmallAction(action);
            }
        }
    }

    // ---- Protect --------------------------------------------------------------
    PDFFireRibbonPage* protectPage = m_ribbon->addPage(tr("Protect"));

    PDFFireRibbonGroup* securityGroup = protectPage->addGroup(tr("Security"));
    securityGroup->addLargeAction(ui->actionEncryption);
    securityGroup->addLargeAction(ui->actionSanitize);
    securityGroup->addSmallAction(ui->actionRemoveExternalLinks);

    // Redaction has two steps, and the group says so: the content is first marked,
    // and it is really removed only when the redactions are applied (into a new file).
    PDFFireRibbonGroup* redactMarkGroup = protectPage->addGroup(tr("Redact - 1. Mark"));
    for (const auto& [objectName, text, toolTip] : { std::tuple<const char*, QString, QString>{ "redactplugin_RedactText", tr("Mark Text"), tr("Drag over a text to mark it for redaction") },
                                                     std::tuple<const char*, QString, QString>{ "redactplugin_RedactRectangle", tr("Mark Area"), tr("Draw a rectangle to mark an area for redaction") },
                                                     std::tuple<const char*, QString, QString>{ "redactplugin_RedactPage", tr("Mark Pages"), tr("Mark whole pages for redaction") } })
    {
        if (QAction* action = findMenuAction(redactMenu, objectName))
        {
            action->setIconText(text);
            action->setToolTip(tr("%1. The content is only marked - it is removed by Apply Redactions.").arg(toolTip));
            redactMarkGroup->addSmallAction(action);
        }
    }

    // Marking of the found text works with the results of Advanced Find. If nothing is
    // selected there yet, the panel is opened instead of a message about it.
    if (QAction* redactFoundTextAction = findMenuAction(redactMenu, "redactplugin_RedactTextSelection"))
    {
        QAction* markFoundTextAction = createAction("actionMarkFoundText", tr("Mark Found Text"), ":/resources/find-advanced.svg",
                                                    tr("Search for a text (also a pattern, like all phone numbers), select the results, and mark them for redaction"));
        markFoundTextAction->setIcon(redactFoundTextAction->icon());
        connect(redactFoundTextAction, &QAction::changed, markFoundTextAction, [markFoundTextAction, redactFoundTextAction]() { markFoundTextAction->setEnabled(redactFoundTextAction->isEnabled()); });
        markFoundTextAction->setEnabled(redactFoundTextAction->isEnabled());
        connect(markFoundTextAction, &QAction::triggered, this, [this, redactFoundTextAction]()
        {
            if (!getSelectedText().isEmpty())
            {
                redactFoundTextAction->trigger();
                return;
            }

            m_advancedFindDockWidget->show();
            m_advancedFindDockWidget->raise();
            m_advancedFindWidget->setFocus();
            statusBar()->showMessage(tr("Search for the text, select the results to be redacted in the list, then click Mark Found Text again."), 15000);
        });
        redactMarkGroup->addSmallAction(markFoundTextAction);
    }

    PDFFireRibbonGroup* redactApplyGroup = protectPage->addGroup(tr("Redact - 2. Apply"));
    if (QAction* action = findMenuAction(redactMenu, "redactplugin_CreateRedactedDocument"))
    {
        action->setIconText(tr("Apply Redactions"));
        action->setToolTip(tr("Remove the marked content for good, and save the result as a new redacted PDF file"));
        redactApplyGroup->addLargeAction(action);
    }

    // ---- Sign -----------------------------------------------------------------
    PDFFireRibbonPage* signPage = m_ribbon->addPage(tr("Sign"));

    PDFFireRibbonGroup* digitalSignatureGroup = signPage->addGroup(tr("Digital Signature"));
    if (QAction* action = findMenuAction(signatureMenu, "signaturetool_signDigitallyAction"))
    {
        action->setIconText(tr("Sign with Certificate"));
        action->setToolTip(tr("Sign the document with a certificate - the signature proves who signed it, and that it was not changed afterwards. The signed document is saved as a new file."));
        digitalSignatureGroup->addLargeAction(action);
    }
    if (QAction* action = findMenuAction(signatureMenu, "signaturetool_certificatesAction"))
    {
        action->setIconText(tr("My Certificates"));
        action->setToolTip(tr("Create or import the certificates used for signing"));
        digitalSignatureGroup->addLargeAction(action);
    }
    if (QAction* action = findMenuAction(signatureMenu, "signaturetool_certifyAction"))
    {
        action->setIconText(tr("Certify"));
        action->setToolTip(tr("Sign as the author and lock the document - no changes allowed (or only form filling and signing). Any other change breaks the certification, in Adobe Acrobat and PDF Fire."));
        digitalSignatureGroup->addLargeAction(action);
    }
    if (QAction* action = findMenuAction(signatureMenu, "signaturetool_departmentAuthorityAction"))
    {
        action->setIconText(tr("Department"));
        action->setToolTip(tr("The certificate authority of the department: issue and revoke the signing certificates of the members, and trust the department's signatures"));
        digitalSignatureGroup->addLargeAction(action);
    }
    ui->actionCertificateManager->setIconText(tr("Trusted Certificates"));
    ui->actionCertificateManager->setToolTip(tr("Certificates which are trusted, when the signatures of a document are verified"));
    digitalSignatureGroup->addLargeAction(ui->actionCertificateManager);

    PDFFireRibbonGroup* drawnSignatureGroup = signPage->addGroup(tr("Handwritten Signature"));
    if (QAction* action = findMenuAction(signatureMenu, "signaturetool_activateAction"))
    {
        action->setIconText(tr("Draw Signature"));
        action->setToolTip(tr("Draw or type a signature onto the page. Then either place it on the page as it is, or use it as the look of a signature with a certificate."));
        drawnSignatureGroup->addLargeAction(action);
    }
    if (QAction* action = findMenuAction(signatureMenu, "signaturetool_signElectronicallyAction"))
    {
        action->setIconText(tr("Place on Page"));
        action->setToolTip(tr("Write the drawn signature into the page. It is a picture of a signature, without a certificate."));
        drawnSignatureGroup->addLargeAction(action);
    }

    PDFFireRibbonGroup* verifySignatureGroup = signPage->addGroup(tr("Verify"));
    QAction* showSignaturesAction = createAction("actionShowSignatures", tr("Signatures"), ":/resources/sidebar-signature.svg", tr("Show the signatures of the document, and whether they are valid"));
    connect(showSignaturesAction, &QAction::triggered, this, [this]()
    {
        if (m_sidebarWidget->isEmpty(PDFSidebarWidget::Signatures))
        {
            statusBar()->showMessage(tr("This document is not signed."), 5000);
            return;
        }
        m_sidebarDockWidget->show();
        m_sidebarWidget->setCollapsed(false);
        m_sidebarWidget->selectPage(PDFSidebarWidget::Signatures);
    });
    verifySignatureGroup->addLargeAction(showSignaturesAction);

    // ---- Forms ----------------------------------------------------------------
    // The form maker (FormsPlugin): fields are placed on the page, edited, or found
    // on the blanks of the document automatically. Filling needs no tab - a form is
    // filled by clicking its fields.
    if (const QMenu* formsMenu = findPluginMenu("Forms"))
    {
        PDFFireRibbonPage* formsPage = m_ribbon->addPage(tr("Forms"));

        PDFFireRibbonGroup* formsEditGroup = formsPage->addGroup(tr("Mode"));
        if (QAction* action = findMenuAction(formsMenu, "formsplugin_FillForm"))
        {
            formsEditGroup->addLargeAction(action);
        }
        if (QAction* action = findMenuAction(formsMenu, "formsplugin_EditFields"))
        {
            formsEditGroup->addLargeAction(action);
        }

        PDFFireRibbonGroup* addFieldGroup = formsPage->addGroup(tr("Add Field"));
        if (QAction* action = findMenuAction(formsMenu, "formsplugin_AddText"))
        {
            addFieldGroup->addLargeAction(action);
        }
        for (const char* objectName : { "formsplugin_AddTextBox", "formsplugin_AddDate", "formsplugin_AddCheckBox",
                                        "formsplugin_AddRadio", "formsplugin_AddDropdown", "formsplugin_AddList" })
        {
            if (QAction* action = findMenuAction(formsMenu, objectName))
            {
                addFieldGroup->addSmallAction(action);
            }
        }
        if (QAction* action = findMenuAction(formsMenu, "formsplugin_AddSignature"))
        {
            addFieldGroup->addLargeAction(action);
        }

        PDFFireRibbonGroup* fieldGroup = formsPage->addGroup(tr("Selected Field"));
        if (QAction* action = findMenuAction(formsMenu, "formsplugin_Properties"))
        {
            fieldGroup->addLargeAction(action);
        }
        for (const char* objectName : { "formsplugin_Duplicate", "formsplugin_CopyToAllPages", "formsplugin_Delete" })
        {
            if (QAction* action = findMenuAction(formsMenu, objectName))
            {
                fieldGroup->addSmallAction(action);
            }
        }

        PDFFireRibbonGroup* automaticGroup = formsPage->addGroup(tr("Automatic"));
        if (QAction* action = findMenuAction(formsMenu, "formsplugin_DetectFields"))
        {
            automaticGroup->addLargeAction(action);
        }
        if (QAction* action = findMenuAction(formsMenu, "formsplugin_TabOrder"))
        {
            automaticGroup->addLargeAction(action);
        }
    }

    // ---- Tools ----------------------------------------------------------------
    PDFFireRibbonPage* toolsPage = m_ribbon->addPage(tr("Tools"));

    PDFFireRibbonGroup* documentGroup = toolsPage->addGroup(tr("Document"));
    documentGroup->addLargeAction(ui->actionOptimize);
    documentGroup->addLargeAction(ui->actionOptimizeImages);
    documentGroup->addSmallAction(ui->actionCreateBitonalDocument);
    documentGroup->addSmallAction(ui->actionRender_to_Images);
    documentGroup->addSmallAction(ui->actionProperties);
    if (QAction* exportOdtAction = findMenuAction(findPluginMenu("Export"), "exportplugin_ExportOdt"))
    {
        documentGroup->addSmallAction(exportOdtAction);
    }

    // PDF Fire: scanning and the text recognition together, as "Scan & OCR" of Acrobat
    PDFFireRibbonGroup* scanGroup = toolsPage->addGroup(tr("Scan & OCR"));
    if (QAction* action = findMenuAction(findPluginMenu("Scanner"), "scannerplugin_ScanPages"))
    {
        action->setIconText(tr("Scan Pages"));
        scanGroup->addLargeAction(action);
    }
    if (QAction* action = findMenuAction(findPluginMenu("OCR"), "ocrplugin_RecognizeText"))
    {
        action->setIconText(tr("Recognize Text"));
        action->setToolTip(tr("Make scanned pages searchable: the text in the images is recognized (OCR) and added invisibly, so it can be searched, selected and copied. The pages look the same."));
        scanGroup->addLargeAction(action);
    }
    if (scanGroup->isEmpty())
    {
        scanGroup->hide();
    }

    fillGroupFromMenu(toolsPage->addGroup(tr("Measure")), findPluginMenu("Dimensions"));

    PDFFireRibbonGroup* printProductionGroup = toolsPage->addGroup(tr("Print Production"));
    addPluginMenuButton(printProductionGroup, tr("Soft Proofing"), findPluginMenu("Soft Proofing"));
    addPluginMenuButton(printProductionGroup, tr("Output Preview"), findPluginMenu("Output Preview"));

    PDFFireRibbonGroup* moreToolsGroup = toolsPage->addGroup(tr("More"));
    addPluginMenuButton(moreToolsGroup, tr("Audio Book"), findPluginMenu("Audio Book"));
    addPluginMenuButton(moreToolsGroup, tr("Inspect"), findPluginMenu("Object Inspector"));

    // ---- View -----------------------------------------------------------------
    PDFFireRibbonPage* viewPage = m_ribbon->addPage(tr("View"));

    PDFFireRibbonGroup* showGroup = viewPage->addGroup(tr("Show"));
    showGroup->addLargeAction(sidebarAction);
    showGroup->addLargeAction(ui->actionFullscreenMode);

    PDFFireRibbonGroup* layoutGroup = viewPage->addGroup(tr("Page Layout"));
    layoutGroup->addSmallAction(ui->actionPageLayoutSinglePage);
    layoutGroup->addSmallAction(ui->actionPageLayoutContinuous);
    layoutGroup->addSmallAction(ui->actionFirstPageOnRightSide);
    layoutGroup->addSmallAction(ui->actionPageLayoutTwoPages);
    layoutGroup->addSmallAction(ui->actionPageLayoutTwoColumns);

    PDFFireRibbonGroup* rotateGroup = viewPage->addGroup(tr("Rotate View"));
    rotateGroup->addSmallAction(ui->actionRotateLeft);
    rotateGroup->addSmallAction(ui->actionRotateRight);

    PDFFireRibbonGroup* colorGroup = viewPage->addGroup(tr("Colors"));
    colorGroup->addSmallAction(ui->actionColorInvert);
    colorGroup->addSmallAction(ui->actionColorGrayscale);
    colorGroup->addSmallAction(ui->actionColorHighContrast);
    colorGroup->addSmallAction(ui->actionColorBitonal);
    colorGroup->addSmallAction(ui->actionColorCustom);

    PDFFireRibbonGroup* renderingGroup = viewPage->addGroup(tr("Rendering"));
    renderingGroup->addSmallMenu(themedIcon(this, ":/resources/rendering.svg"), tr("Options"), ui->menuRendering_Options);
    renderingGroup->addSmallAction(ui->actionRendering_Errors);

    // ---- Help -----------------------------------------------------------------
    PDFFireRibbonPage* helpPage = m_ribbon->addPage(tr("Help"));

    PDFFireRibbonGroup* settingsGroup = helpPage->addGroup(tr("Settings"));
    settingsGroup->addLargeAction(ui->actionOptions);
    settingsGroup->addSmallAction(ui->actionResetToFactorySettings);
    QAction* mailServerAction = createAction("actionMailServer", tr("Mail Server"), ":/resources/send-mail.svg", tr("The mail server used to send documents by e-mail"));
    connect(mailServerAction, &QAction::triggered, this, [this]() { PDFFireMailServerDialog dialog(this); dialog.exec(); });
    settingsGroup->addSmallAction(mailServerAction);
    settingsGroup->addSmallMenu(themedIcon(this, ":/resources/engine.svg"), tr("Developer"), ui->menuDeveloper);

    PDFFireRibbonGroup* aboutGroup = helpPage->addGroup(tr("About"));
    aboutGroup->addLargeAction(ui->actionAbout);

    // PDF Fire: PDF Fire is free - a supporter can help it, once or monthly, on the web page
    // of Firehouse 360 (the payment is never entered in the program)
    QAction* supportAction = createAction("actionSupportPdfFire", tr("Support PDF Fire"), ":/resources/support-heart.svg",
                                          tr("PDF Fire is free. If it helps you, you can support it - once or monthly, cancel anytime (opens the web page in the browser)."));
    connect(supportAction, &QAction::triggered, this, []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://firehouse360.com/support/pdf-fire"))); });
    aboutGroup->addLargeAction(supportAction);

    // The source and the sponsorship links of the engine lead to its author's pages, not
    // to PDF Fire - they are not offered (the engine is credited in the About dialog)
    ui->actionGetSource->setVisible(false);
    ui->actionBecomeASponsor->setVisible(false);

    const std::initializer_list<PDFFireRibbonPage*> ribbonPages = { homePage, commentPage, reviewPage, editPage, pagesPage, protectPage, signPage, toolsPage, viewPage, helpPage };
    for (PDFFireRibbonPage* page : ribbonPages)
    {
        page->removeEmptyGroups();
    }

    // Short texts for the buttons. The icon text of an action is used by the buttons
    // only, the menus and the search of the actions keep the full text.
    ui->actionGoToPreviousPage->setIconText(tr("Previous Page"));
    ui->actionGoToNextPage->setIconText(tr("Next Page"));
    ui->actionGoToDocumentStart->setIconText(tr("First Page"));
    ui->actionGoToDocumentEnd->setIconText(tr("Last Page"));
    ui->actionSend_by_E_Mail->setIconText(tr("E-Mail"));
    ui->actionCreateTextHighlight->setIconText(tr("Highlight"));
    ui->actionCreateTextUnderline->setIconText(tr("Underline"));
    ui->actionCreateTextStrikeout->setIconText(tr("Strikeout"));
    ui->actionCreateTextSquiggly->setIconText(tr("Squiggly"));
    ui->actionCreateStraightLine->setIconText(tr("Line"));
    ui->actionCreatePolyline->setIconText(tr("Polyline"));
    ui->actionCreateFreehandCurve->setIconText(tr("Freehand"));
    ui->actionCreateRectangle->setIconText(tr("Rectangle"));
    ui->actionCreateEllipse->setIconText(tr("Circle"));
    ui->actionCreateEllipse->setToolTip(tr("Circle or oval - drag around what you want to point out"));
    ui->actionCreateArrow->setIconText(tr("Arrow"));
    ui->actionCreatePolygon->setIconText(tr("Polygon"));
    ui->actionCreateHyperlink->setIconText(tr("Web Link"));
    ui->actionInlineText->setIconText(tr("Text Box"));
    ui->actionDeleteAnnotation->setIconText(tr("Delete"));
    ui->actionInsertPageNumbers->setIconText(tr("Page Numbers"));
    ui->actionPageGeometry->setIconText(tr("Page Size"));
    ui->actionCreateBitonalDocument->setIconText(tr("Black and White"));
    ui->actionRender_to_Images->setIconText(tr("Export Images"));
    ui->actionOptimizeImages->setIconText(tr("Compress Images"));
    ui->actionRemoveExternalLinks->setIconText(tr("Remove Links"));
    ui->actionResetToFactorySettings->setIconText(tr("Reset Settings"));
    ui->actionFullscreenMode->setIconText(tr("Full Screen"));
    ui->actionColorInvert->setIconText(tr("Invert Colors"));
    ui->actionColorGrayscale->setIconText(tr("Grayscale"));
    ui->actionColorHighContrast->setIconText(tr("High Contrast"));
    ui->actionColorBitonal->setIconText(tr("Monochrome"));
    ui->actionColorCustom->setIconText(tr("Custom Colors"));
    ui->actionFirstPageOnRightSide->setIconText(tr("Cover Page"));
    ui->actionGotoPreviousBookmark->setIconText(tr("Previous Bookmark"));
    ui->actionGotoNextBookmark->setIconText(tr("Next Bookmark"));

    for (QToolButton* button : m_ribbon->findChildren<QToolButton*>())
    {
        if (QAction* action = button->defaultAction())
        {
            action->setIconText(buttonText(action->iconText()));
        }
    }

    for (PDFFireRibbonPage* page : ribbonPages)
    {
        page->updateMinimumWidth();
    }

    // The quick search of the actions moves from the corner of the menu bar
    if (QWidget* actionSearchBox = menuBar()->cornerWidget())
    {
        menuBar()->setCornerWidget(nullptr);
        actionSearchBox->setObjectName("ribbonSearchBox");
        actionSearchBox->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 240));
        m_ribbon->setCornerWidget(actionSearchBox);
        actionSearchBox->show();
    }

    // The menu bar is hidden, but it is not removed - the menus are used by the
    // ribbon, and the plugins add their menus to them. The shortcuts of the actions
    // of a hidden menu bar do not work, so the actions are added to the window too.
    QList<QAction*> menuActions;
    for (QAction* menuAction : menuBar()->actions())
    {
        if (const QMenu* menu = menuAction->menu())
        {
            collectActions(menu, menuActions);
        }
    }
    addActions(menuActions);
    addAction(advancedFindAction);
    addAction(sidebarAction);
    menuBar()->hide();

    // The toolbars are replaced by the ribbon. The ribbon itself is placed into
    // a fixed toolbar, because the menu widget of the window cannot be replaced
    // without destroying the menu bar.
    for (QToolBar* toolBar : findChildren<QToolBar*>())
    {
        removeToolBar(toolBar);
        toolBar->toggleViewAction()->setVisible(false);
    }

    QToolBar* ribbonToolBar = new QToolBar(tr("Ribbon"), this);
    ribbonToolBar->setObjectName("ribbonToolBar");
    ribbonToolBar->setMovable(false);
    ribbonToolBar->setFloatable(false);
    ribbonToolBar->setContextMenuPolicy(Qt::PreventContextMenu);
    ribbonToolBar->toggleViewAction()->setVisible(false);
    ribbonToolBar->setStyleSheet("QToolBar#ribbonToolBar { border: none; padding: 0px; margin: 0px; spacing: 0px; }");
    ribbonToolBar->layout()->setContentsMargins(0, 0, 0, 0);
    ribbonToolBar->layout()->setSpacing(0);
    m_ribbon->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ribbonToolBar->addWidget(m_ribbon);
    addToolBar(Qt::TopToolBarArea, ribbonToolBar);
    setContextMenuPolicy(Qt::NoContextMenu);

    setupStatusBarControls();

    // The side panel has no title bar, it is shown and hidden from the ribbon
    m_sidebarDockWidget->setTitleBarWidget(new QWidget(m_sidebarDockWidget));
    // (closable, so its toggle action stays enabled - there is no close button without the title bar)
    m_sidebarDockWidget->setFeatures(QDockWidget::DockWidgetClosable);

    // A click on the button of the displayed page collapses the side panel to the column
    // of its buttons, a click on any button expands it to the width it had before.
    connect(m_sidebarWidget, &PDFSidebarWidget::collapsedChanged, this, [this](bool collapsed)
    {
        if (collapsed)
        {
            m_sidebarExpandedWidth = m_sidebarDockWidget->width();
            m_sidebarDockWidget->setFixedWidth(m_sidebarWidget->minimumSizeHint().width());
        }
        else
        {
            m_sidebarDockWidget->setMinimumWidth(0);
            m_sidebarDockWidget->setMaximumWidth(QWIDGETSIZE_MAX);
            resizeDocks({ m_sidebarDockWidget }, { qMax(m_sidebarExpandedWidth, pdf::PDFWidgetUtils::scaleDPI_x(this, 260)) }, Qt::Horizontal);
        }
    });

    updatePageActions();

    // The window starts without a document, so there is nothing to show in the side
    // panel (the restored state of the window can have it visible).
    m_sidebarDockWidget->hide();

    // The welcome page covers the empty document area
    std::vector<QAction*> recentFileActions;
    for (QAction* action : m_programController->getRecentFileManager()->getActions())
    {
        recentFileActions.push_back(action);
    }
    m_welcomeWidget = new PDFFireWelcomeWidget(ui->actionOpen, qMove(recentFileActions), ui->actionClearRecentFileHistory, centralWidget());

    // The recent documents in the File menu are hidden, if the user does not want them shown
    connect(ui->menuFile, &QMenu::aboutToShow, this, [this]()
    {
        const bool isShowing = PDFFireWelcomeWidget::isShowingRecentDocuments();
        for (QAction* action : m_programController->getRecentFileManager()->getActions())
        {
            if (!action->data().toString().isEmpty())
            {
                action->setVisible(isShowing);
            }
        }
    });
    updateWelcomePage(m_programController->getDocument() != nullptr);

    // An icon for the full screen mode, and the developer tools among the settings
    if (ui->actionFullscreenMode->icon().isNull())
    {
        ui->actionFullscreenMode->setIcon(themedIcon(this, ":/resources/ui.svg"));
    }
}

void PDFEditorMainWindow::setupStatusBarControls()
{
    auto createButton = [this](QAction* action)
    {
        QToolButton* button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(16, 16)));
        return button;
    };

    // The page number and the zoom were placed in the main toolbar, which is not
    // displayed anymore. They are taken out of it, and moved to the status bar.
    for (QAction* action : ui->mainToolBar->actions())
    {
        if (QWidgetAction* widgetAction = qobject_cast<QWidgetAction*>(action))
        {
            QWidget* widget = widgetAction->defaultWidget();
            if (widget == m_pageNumberSpinBox || widget == m_pageNumberLabel || widget == m_pageZoomSpinBox)
            {
                ui->mainToolBar->removeAction(action);
            }
        }
    }

    m_pageNumberSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageNumberSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 52));
    m_pageZoomSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageZoomSpinBox->setDecimals(0);
    m_pageZoomSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 60));
    m_pageZoomSpinBox->setAlignment(Qt::AlignCenter);

    QWidget* spacer = new QWidget(this);
    spacer->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 16));

    statusBar()->addPermanentWidget(createButton(ui->actionGoToPreviousPage));
    statusBar()->addPermanentWidget(m_pageNumberSpinBox);
    statusBar()->addPermanentWidget(m_pageNumberLabel);
    statusBar()->addPermanentWidget(createButton(ui->actionGoToNextPage));
    statusBar()->addPermanentWidget(spacer);
    statusBar()->addPermanentWidget(createButton(ui->actionFitWidth));
    statusBar()->addPermanentWidget(createButton(ui->actionFitPage));
    statusBar()->addPermanentWidget(createButton(ui->actionZoom_Out));
    statusBar()->addPermanentWidget(m_pageZoomSpinBox);
    statusBar()->addPermanentWidget(createButton(ui->actionZoom_In));

    m_pageNumberSpinBox->show();
    m_pageNumberLabel->show();
    m_pageZoomSpinBox->show();
}

std::vector<pdf::PDFInteger> PDFEditorMainWindow::getTargetPages() const
{
    // The pages selected in the thumbnails, if they are displayed; otherwise the current page
    std::vector<pdf::PDFInteger> pages = m_sidebarWidget->isCollapsed() ? std::vector<pdf::PDFInteger>() : m_sidebarWidget->getSelectedThumbnailPages();

    if (pages.empty())
    {
        const pdf::PDFInteger currentPage = m_programController->getCurrentPageIndex();
        if (currentPage >= 0)
        {
            pages.push_back(currentPage);
        }
    }

    return pages;
}

void PDFEditorMainWindow::updatePageActions()
{
    const bool canModifyPages = m_programController->canModifyPages();
    for (QAction* action : m_pageActions)
    {
        action->setEnabled(canModifyPages);
    }
}

void PDFEditorMainWindow::updateWelcomePage(bool hasDocument)
{
    updatePageActions();

    if (!m_welcomeWidget)
    {
        return;
    }

    if (!hasDocument)
    {
        m_welcomeWidget->refresh();
        m_welcomeWidget->raise();
    }
    m_welcomeWidget->setVisible(!hasDocument);
}

void PDFEditorMainWindow::applyDefaultDocumentView()
{
    // A newly opened document gets the zoom chosen in the settings (fit width,
    // when nothing is chosen). The zoom is set when the window is laid out with
    // the document, not immediately.
    QTimer::singleShot(0, this, [this]()
    {
        if (!m_programController->getDocument())
        {
            return;
        }

        const QString openZoom = m_programController->getSettings()->getSettings().m_openZoom;
        bool isPercent = false;
        const int percent = openZoom.toInt(&isPercent);
        if (openZoom == QStringLiteral("fitPage"))
        {
            ui->actionFitPage->trigger();
        }
        else if (openZoom == QStringLiteral("fitHeight"))
        {
            ui->actionFitHeight->trigger();
        }
        else if (isPercent && percent >= 10 && percent <= 1000)
        {
            m_programController->getPdfWidget()->getDrawWidgetProxy()->zoom(percent / 100.0);
        }
        else
        {
            ui->actionFitWidth->trigger();
        }
    });
}

}   // namespace pdfviewer
