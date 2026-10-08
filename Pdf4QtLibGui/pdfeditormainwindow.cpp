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

#include "pdfeditormainwindow.h"
#include <QVBoxLayout>
#include <QShortcut>
#include <QTabBar>
#include "ui_pdfeditormainwindow.h"

#include "pdfaboutdialog.h"
#include "pdfsidebarwidget.h"
#include "pdftexttospeech.h"
#include "pdfadvancedfindwidget.h"
#include "pdfviewersettingsdialog.h"
#include "pdfdocumentpropertiesdialog.h"
#include "pdfrendertoimagesdialog.h"

#include "pdfdocumentreader.h"
#include "pdfvisitor.h"
#include "pdfstreamfilters.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfrenderingerrorswidget.h"
#include "pdffont.h"
#include "pdfitemmodels.h"
#include "pdfutils.h"
#include "pdfsendmail.h"
#include "pdfexecutionpolicy.h"
#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfsignaturehandler.h"
#include "pdfadvancedtools.h"
#include "pdfwidgetutils.h"
#include "pdfactioncombobox.h"
#include "pdffirepermissions.h"

#include "pdfannotationstyle.h"

#include <QPainter>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QApplication>
#include <QStandardPaths>
#include <QDockWidget>
#include <QTreeView>
#include <QLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QSpinBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLockFile>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QtConcurrent/QtConcurrent>
#include <QToolButton>
#include <QActionGroup>

#include "pdfdbgheap.h"

#ifdef Q_OS_WIN
#include "Windows.h"
#endif

namespace pdfviewer
{

PDFEditorMainWindow::PDFEditorMainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::PDFEditorMainWindow),
    m_actionManager(new PDFActionManager(this)),
    m_programController(new PDFProgramController(this)),
    m_sidebarWidget(nullptr),
    m_sidebarDockWidget(nullptr),
    m_advancedFindWidget(nullptr),
    m_advancedFindDockWidget(nullptr),
    m_pageNumberSpinBox(nullptr),
    m_pageNumberLabel(nullptr),
    m_pageZoomSpinBox(nullptr),
    m_isLoadingUI(false),
    m_progress(new pdf::PDFProgress(this)),
    m_progressTaskbarIndicator(new PDFWinTaskBarProgress(this)),
    m_progressBarOnStatusBar(nullptr),
    m_progressBarLeftLabelOnStatusBar(nullptr),
    m_isChangingProgressStep(false)
{
    ui->setupUi(this);

    setAcceptDrops(true);

    // Initialize toolbar icon size
    adjustToolbar(ui->mainToolBar);
    ui->mainToolBar->setWindowTitle(tr("Standard"));

    // Initialize status bar
    m_progressBarOnStatusBar = new QProgressBar(this);
    m_progressBarOnStatusBar->setHidden(true);
    m_progressBarLeftLabelOnStatusBar = new QLabel(this);
    m_progressBarLeftLabelOnStatusBar->setHidden(true);
    statusBar()->addPermanentWidget(m_progressBarLeftLabelOnStatusBar);
    statusBar()->addPermanentWidget(m_progressBarOnStatusBar);

    // Initialize actions
    m_actionManager->setAction(PDFActionManager::Open, ui->actionOpen);
    m_actionManager->setAction(PDFActionManager::Close, ui->actionClose);
    m_actionManager->setAction(PDFActionManager::AutomaticDocumentRefresh, ui->actionAutomaticDocumentRefresh);
    m_actionManager->setAction(PDFActionManager::Quit, ui->actionQuit);
    m_actionManager->setAction(PDFActionManager::ZoomIn, ui->actionZoom_In);
    m_actionManager->setAction(PDFActionManager::ZoomOut, ui->actionZoom_Out);
    m_actionManager->setAction(PDFActionManager::Find, ui->actionFind);
    m_actionManager->setAction(PDFActionManager::FindPrevious, ui->actionFindPrevious);
    m_actionManager->setAction(PDFActionManager::FindNext, ui->actionFindNext);
    m_actionManager->setAction(PDFActionManager::SelectTextAll, ui->actionSelectTextAll);
    m_actionManager->setAction(PDFActionManager::DeselectText, ui->actionDeselectText);
    m_actionManager->setAction(PDFActionManager::CopyText, ui->actionCopyText);
    m_actionManager->setAction(PDFActionManager::RotateRight, ui->actionRotateRight);
    m_actionManager->setAction(PDFActionManager::RotateLeft, ui->actionRotateLeft);
    m_actionManager->setAction(PDFActionManager::Print, ui->actionPrint);
    m_actionManager->setAction(PDFActionManager::Undo, ui->actionUndo);
    m_actionManager->setAction(PDFActionManager::Redo, ui->actionRedo);
    m_actionManager->setAction(PDFActionManager::Save, ui->actionSave);
    m_actionManager->setAction(PDFActionManager::SaveAs, ui->actionSave_As);
    m_actionManager->setAction(PDFActionManager::GoToDocumentStart, ui->actionGoToDocumentStart);
    m_actionManager->setAction(PDFActionManager::GoToDocumentEnd, ui->actionGoToDocumentEnd);
    m_actionManager->setAction(PDFActionManager::GoToNextPage, ui->actionGoToNextPage);
    m_actionManager->setAction(PDFActionManager::GoToPreviousPage, ui->actionGoToPreviousPage);
    m_actionManager->setAction(PDFActionManager::GoToNextLine, ui->actionGoToNextLine);
    m_actionManager->setAction(PDFActionManager::GoToPreviousLine, ui->actionGoToPreviousLine);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteComment, ui->actionStickyNoteComment);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteHelp, ui->actionStickyNoteHelp);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteInsert, ui->actionStickyNoteInsert);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteKey, ui->actionStickyNoteKey);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteNewParagraph, ui->actionStickyNoteNewParagraph);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteNote, ui->actionStickyNoteNote);
    m_actionManager->setAction(PDFActionManager::CreateStickyNoteParagraph, ui->actionStickyNoteParagraph);
    m_actionManager->setAction(PDFActionManager::CreateTextHighlight, ui->actionCreateTextHighlight);
    m_actionManager->setAction(PDFActionManager::CreateTextUnderline, ui->actionCreateTextUnderline);
    m_actionManager->setAction(PDFActionManager::CreateTextStrikeout, ui->actionCreateTextStrikeout);
    m_actionManager->setAction(PDFActionManager::CreateTextSquiggly, ui->actionCreateTextSquiggly);
    m_actionManager->setAction(PDFActionManager::CreateHyperlink, ui->actionCreateHyperlink);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFit, ui->actionCreateHyperlinkToThisPDFFit);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitH, ui->actionCreateHyperlinkToThisPDFFitH);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitV, ui->actionCreateHyperlinkToThisPDFFitV);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitR, ui->actionCreateHyperlinkToThisPDFFitR);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitB, ui->actionCreateHyperlinkToThisPDFFitB);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitBH, ui->actionCreateHyperlinkToThisPDFFitBH);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFFitBV, ui->actionCreateHyperlinkToThisPDFFitBV);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFXYZ, ui->actionCreateHyperlinkToThisPDFXYZ);
    m_actionManager->setAction(PDFActionManager::CreateHyperlinkToThisPDFXYZInheritZoom, ui->actionCreateHyperlinkToThisPDFXYZInheritZoom);
    m_actionManager->setAction(PDFActionManager::InsertPageNumbers, ui->actionInsertPageNumbers);
    m_actionManager->setAction(PDFActionManager::CreateInlineText, ui->actionInlineText);
    m_actionManager->setAction(PDFActionManager::CreateStraightLine, ui->actionCreateStraightLine);
    m_actionManager->setAction(PDFActionManager::CreatePolyline, ui->actionCreatePolyline);
    m_actionManager->setAction(PDFActionManager::CreateRectangle, ui->actionCreateRectangle);
    m_actionManager->setAction(PDFActionManager::CreatePolygon, ui->actionCreatePolygon);
    m_actionManager->setAction(PDFActionManager::CreateEllipse, ui->actionCreateEllipse);
    m_actionManager->setAction(PDFActionManager::CreateArrow, ui->actionCreateArrow);
    m_actionManager->setAction(PDFActionManager::AddText, ui->actionAddText);
    m_actionManager->setAction(PDFActionManager::UnlockPermissions, ui->actionUnlockPermissions);
    m_actionManager->setAction(PDFActionManager::CreateFreehandCurve, ui->actionCreateFreehandCurve);
    m_actionManager->setAction(PDFActionManager::DeleteAnnotation, ui->actionDeleteAnnotation);
    m_actionManager->setAction(PDFActionManager::RenderOptionAntialiasing, ui->actionRenderOptionAntialiasing);
    m_actionManager->setAction(PDFActionManager::RenderOptionTextAntialiasing, ui->actionRenderOptionTextAntialiasing);
    m_actionManager->setAction(PDFActionManager::RenderOptionSmoothPictures, ui->actionRenderOptionSmoothPictures);
    m_actionManager->setAction(PDFActionManager::RenderOptionIgnoreOptionalContentSettings, ui->actionRenderOptionIgnoreOptionalContentSettings);
    m_actionManager->setAction(PDFActionManager::RenderOptionDisplayRenderTimes, ui->actionRenderOptionDisplayRenderTimes);
    m_actionManager->setAction(PDFActionManager::RenderOptionDisplayAnnotations, ui->actionRenderOptionDisplayAnnotations);
    m_actionManager->setAction(PDFActionManager::RenderOptionInvertColors, ui->actionColorInvert);
    m_actionManager->setAction(PDFActionManager::RenderOptionGrayscale, ui->actionColorGrayscale);
    m_actionManager->setAction(PDFActionManager::RenderOptionHighContrast, ui->actionColorHighContrast);
    m_actionManager->setAction(PDFActionManager::RenderOptionBitonal, ui->actionColorBitonal);
    m_actionManager->setAction(PDFActionManager::RenderOptionCustomColors, ui->actionColorCustom);
    m_actionManager->setAction(PDFActionManager::RenderOptionShowTextBlocks, ui->actionShow_Text_Blocks);
    m_actionManager->setAction(PDFActionManager::RenderOptionShowTextLines, ui->actionShow_Text_Lines);
    m_actionManager->setAction(PDFActionManager::Properties, ui->actionProperties);
    m_actionManager->setAction(PDFActionManager::Options, ui->actionOptions);
    m_actionManager->setAction(PDFActionManager::ResetToFactorySettings, ui->actionResetToFactorySettings);
    m_actionManager->setAction(PDFActionManager::ClearRecentFileHistory, ui->actionClearRecentFileHistory);
    m_actionManager->setAction(PDFActionManager::CertificateManager, ui->actionCertificateManager);
    m_actionManager->setAction(PDFActionManager::GetSource, ui->actionGetSource);
    m_actionManager->setAction(PDFActionManager::BecomeSponsor, ui->actionBecomeASponsor);
    m_actionManager->setAction(PDFActionManager::About, ui->actionAbout);
    m_actionManager->setAction(PDFActionManager::SendByMail, ui->actionSend_by_E_Mail);
    m_actionManager->setAction(PDFActionManager::RenderToImages, ui->actionRender_to_Images);
    m_actionManager->setAction(PDFActionManager::Optimize, ui->actionOptimize);
    m_actionManager->setAction(PDFActionManager::OptimizeImages, ui->actionOptimizeImages);
    m_actionManager->setAction(PDFActionManager::Sanitize, ui->actionSanitize);
    m_actionManager->setAction(PDFActionManager::RemoveExternalLinks, ui->actionRemoveExternalLinks);
    m_actionManager->setAction(PDFActionManager::PageGeometry, ui->actionPageGeometry);
    m_actionManager->setAction(PDFActionManager::CreateBitonalDocument, ui->actionCreateBitonalDocument);
    m_actionManager->setAction(PDFActionManager::Encryption, ui->actionEncryption);
    m_actionManager->setAction(PDFActionManager::FitPage, ui->actionFitPage);
    m_actionManager->setAction(PDFActionManager::FitWidth, ui->actionFitWidth);
    m_actionManager->setAction(PDFActionManager::FitHeight, ui->actionFitHeight);
    m_actionManager->setAction(PDFActionManager::ShowRenderingErrors, ui->actionRendering_Errors);
    m_actionManager->setAction(PDFActionManager::PageLayoutSinglePage, ui->actionPageLayoutSinglePage);
    m_actionManager->setAction(PDFActionManager::PageLayoutContinuous, ui->actionPageLayoutContinuous);
    m_actionManager->setAction(PDFActionManager::PageLayoutTwoPages, ui->actionPageLayoutTwoPages);
    m_actionManager->setAction(PDFActionManager::PageLayoutTwoColumns, ui->actionPageLayoutTwoColumns);
    m_actionManager->setAction(PDFActionManager::PageLayoutFirstPageOnRightSide, ui->actionFirstPageOnRightSide);
    m_actionManager->setAction(PDFActionManager::FullscreenMode, ui->actionFullscreenMode);
    m_actionManager->setAction(PDFActionManager::ToolSelectText, ui->actionSelectText);
    m_actionManager->setAction(PDFActionManager::ToolSelectTable, ui->actionSelectTable);
    m_actionManager->setAction(PDFActionManager::ToolMagnifier, ui->actionMagnifier);
    m_actionManager->setAction(PDFActionManager::ToolScreenshot, ui->actionScreenshot);
    m_actionManager->setAction(PDFActionManager::ToolExtractImage, ui->actionExtractImage);
    m_actionManager->setAction(PDFActionManager::BookmarkPage, ui->actionBookmarkPage);
    m_actionManager->setAction(PDFActionManager::BookmarkGoToNext, ui->actionGotoNextBookmark);
    m_actionManager->setAction(PDFActionManager::BookmarkGoToPrevious, ui->actionGotoPreviousBookmark);
    m_actionManager->setAction(PDFActionManager::BookmarkExport, ui->actionBookmarkExport);
    m_actionManager->setAction(PDFActionManager::BookmarkImport, ui->actionBookmarkImport);
    m_actionManager->setAction(PDFActionManager::BookmarkGenerateAutomatically, ui->actionBookmarkAutoGenerate);
    // PDF Fire: the large buttons of the ribbon use bigger icons than the toolbars did
    m_actionManager->initActions(pdf::PDFWidgetUtils::scaleDPI(this, QSize(28, 28)), true);

    for (QAction* action : m_programController->getRecentFileManager()->getActions())
    {
        ui->menuFile->insertAction(ui->actionClearRecentFileHistory, action);
    }
    m_programController->getRecentFileManager()->setClearRecentFileHistoryAction(ui->actionClearRecentFileHistory);
    ui->menuFile->insertSeparator(ui->actionQuit);

    connect(ui->actionQuit, &QAction::triggered, this, &PDFEditorMainWindow::onActionQuitTriggered);

    m_pageNumberSpinBox = new QSpinBox(this);
    m_pageNumberSpinBox->setObjectName("pageNumberSpinBox");
    m_pageNumberLabel = new QLabel(this);
    m_pageNumberLabel->setObjectName("pageNumberLabel");
    m_pageNumberSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageNumberSpinBox, 80));
    m_pageNumberSpinBox->setAlignment(Qt::AlignCenter);
    connect(m_pageNumberSpinBox, &QSpinBox::editingFinished, this, &PDFEditorMainWindow::onPageNumberSpinboxEditingFinished);

    for (QAction* action : m_actionManager->getActionGroup(PDFActionManager::CreateStampGroup)->actions())
    {
        ui->menuStamp->addAction(action);
    }

    // Page control
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(ui->actionGoToDocumentStart);
    ui->mainToolBar->addAction(ui->actionGoToPreviousPage);
    ui->mainToolBar->addWidget(m_pageNumberSpinBox);
    ui->mainToolBar->addWidget(m_pageNumberLabel);
    ui->mainToolBar->addAction(ui->actionGoToNextPage);
    ui->mainToolBar->addAction(ui->actionGoToDocumentEnd);

    // Zoom
    ui->mainToolBar->addSeparator();
    ui->mainToolBar->addAction(ui->actionZoom_In);
    ui->mainToolBar->addAction(ui->actionZoom_Out);

    m_pageZoomSpinBox = new QDoubleSpinBox(this);
    m_pageZoomSpinBox->setObjectName("pageZoomSpinBox");
    m_pageZoomSpinBox->setMinimum(pdf::PDFDrawWidgetProxy::getMinZoom() * 100);
    m_pageZoomSpinBox->setMaximum(pdf::PDFDrawWidgetProxy::getMaxZoom() * 100);
    m_pageZoomSpinBox->setDecimals(2);
    m_pageZoomSpinBox->setSuffix(tr("%"));
    m_pageZoomSpinBox->setFixedWidth(pdf::PDFWidgetUtils::scaleDPI_x(m_pageNumberSpinBox, 80));
    m_pageZoomSpinBox->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    connect(m_pageZoomSpinBox, &QDoubleSpinBox::editingFinished, this, &PDFEditorMainWindow::onPageZoomSpinboxEditingFinished);
    ui->mainToolBar->addWidget(m_pageZoomSpinBox);

    // Fit page, width, height
    ui->mainToolBar->addAction(ui->actionFitPage);
    ui->mainToolBar->addAction(ui->actionFitWidth);
    ui->mainToolBar->addAction(ui->actionFitHeight);
    ui->mainToolBar->addSeparator();

    // Tools
    ui->mainToolBar->addAction(ui->actionSelectText);
    ui->mainToolBar->addAction(ui->actionSelectTable);
    ui->mainToolBar->addAction(ui->actionCreateTextHighlight);
    ui->mainToolBar->addAction(ui->actionCreateTextUnderline);
    ui->mainToolBar->addAction(ui->actionCreateTextStrikeout);
    ui->mainToolBar->addAction(ui->actionCreateTextSquiggly);
    ui->mainToolBar->addAction(ui->actionMagnifier);
    ui->mainToolBar->addAction(ui->actionScreenshot);
    ui->mainToolBar->addAction(ui->actionExtractImage);
    ui->mainToolBar->addSeparator();

    // Special tools
    QToolButton* insertStickyNoteButton = m_actionManager->createToolButtonForActionGroup(PDFActionManager::CreateStickyNoteGroup, ui->mainToolBar);
    ui->mainToolBar->addWidget(insertStickyNoteButton);
    ui->mainToolBar->addSeparator();

    m_programController->initialize(PDFProgramController::AllFeatures, this, this, m_actionManager, m_progress);
    // PDF Fire: the tabs of the open documents above the pages
    QWidget* documentArea = new QWidget(this);
    QVBoxLayout* documentAreaLayout = new QVBoxLayout(documentArea);
    documentAreaLayout->setContentsMargins(0, 0, 0, 0);
    documentAreaLayout->setSpacing(0);
    m_documentTabBar = new QTabBar(documentArea);
    m_documentTabBar->setObjectName("documentTabBar");
    m_documentTabBar->setTabsClosable(true);
    m_documentTabBar->setDocumentMode(true);
    m_documentTabBar->setExpanding(false);
    m_documentTabBar->setElideMode(Qt::ElideMiddle);
    m_documentTabBar->setUsesScrollButtons(true);
    m_documentTabBar->setVisible(false);
    documentAreaLayout->addWidget(m_documentTabBar);

    // PDF Fire: a document protected by its author says so above the pages - otherwise
    // the greyed-out tools are a riddle (Acrobat shows "SECURED" and a similar bar)
    m_protectionBar = new QFrame(documentArea);
    m_protectionBar->setObjectName("protectionBar");
    m_protectionBar->setStyleSheet("QFrame#protectionBar { background: palette(alternate-base); border-left: 4px solid #E8590C; border-bottom: 1px solid palette(mid); }");
    QHBoxLayout* protectionBarLayout = new QHBoxLayout(m_protectionBar);
    protectionBarLayout->setContentsMargins(10, 4, 4, 4);
    m_protectionBarLabel = new QLabel(m_protectionBar);
    m_protectionBarLabel->setObjectName("protectionBarLabel");
    m_protectionBarLabel->setWordWrap(true);
    m_protectionBarLabel->setTextFormat(Qt::PlainText);
    protectionBarLayout->addWidget(m_protectionBarLabel, 1);
    QPushButton* protectionInfoButton = new QPushButton(tr("Document Info"), m_protectionBar);
    protectionInfoButton->setObjectName("protectionBarInfoButton");
    connect(protectionInfoButton, &QPushButton::clicked, ui->actionProperties, &QAction::trigger);
    protectionBarLayout->addWidget(protectionInfoButton);
    m_protectionBarUnlockButton = new QPushButton(tr("Enter Password..."), m_protectionBar);
    m_protectionBarUnlockButton->setObjectName("protectionBarUnlockButton");
    m_protectionBarUnlockButton->setToolTip(ui->actionUnlockPermissions->toolTip());
    connect(m_protectionBarUnlockButton, &QPushButton::clicked, ui->actionUnlockPermissions, &QAction::trigger);
    protectionBarLayout->addWidget(m_protectionBarUnlockButton);
    QToolButton* protectionCloseButton = new QToolButton(m_protectionBar);
    protectionCloseButton->setText(QString::fromUtf8("\xC3\x97"));
    protectionCloseButton->setAutoRaise(true);
    protectionCloseButton->setToolTip(tr("Hide this message"));
    connect(protectionCloseButton, &QToolButton::clicked, this, [this]()
    {
        m_isProtectionBarDismissed = true;
        m_protectionBar->hide();
    });
    protectionBarLayout->addWidget(protectionCloseButton);
    m_protectionBar->hide();
    documentAreaLayout->addWidget(m_protectionBar);

    documentAreaLayout->addWidget(m_programController->getPdfWidget(), 1);
    setCentralWidget(documentArea);
    setFocusProxy(m_programController->getPdfWidget());

    connect(m_programController, &PDFProgramController::documentTabsChanged, this, &PDFEditorMainWindow::updateDocumentTabs);
    connect(m_documentTabBar, &QTabBar::currentChanged, this, [this](int index)
    {
        if (!m_isUpdatingDocumentTabs && index >= 0)
        {
            m_programController->switchToDocumentTab(index);
        }
    });
    connect(m_documentTabBar, &QTabBar::tabCloseRequested, this, [this](int index) { m_programController->closeDocumentTab(index); });

    // Ctrl+Tab and Ctrl+Shift+Tab go through the tabs
    QShortcut* nextTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(nextTabShortcut, &QShortcut::activated, this, [this]()
    {
        const int count = m_documentTabBar->count();
        if (count > 1)
        {
            m_programController->switchToDocumentTab((m_programController->getCurrentDocumentTab() + 1) % count);
        }
    });
    QShortcut* previousTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), this);
    connect(previousTabShortcut, &QShortcut::activated, this, [this]()
    {
        const int count = m_documentTabBar->count();
        if (count > 1)
        {
            m_programController->switchToDocumentTab((m_programController->getCurrentDocumentTab() + count - 1) % count);
        }
    });

    m_sidebarWidget = new PDFSidebarWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), m_programController->getTextToSpeech(), m_programController->getCertificateStore(), m_programController->getBookmarkManager(), m_programController->getSettings(), true, this);
    m_sidebarDockWidget = new QDockWidget(tr("&Sidebar"), this);
    m_sidebarDockWidget->setObjectName("SidebarDockWidget");
    m_sidebarDockWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDockWidget->setWidget(m_sidebarWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDockWidget);
    m_sidebarDockWidget->hide();
    connect(m_sidebarWidget, &PDFSidebarWidget::actionTriggered, m_programController, &PDFProgramController::onActionTriggered);
    connect(m_sidebarWidget, &PDFSidebarWidget::documentModified, m_programController, &PDFProgramController::onDocumentModified);
    connect(m_sidebarWidget, &PDFSidebarWidget::sidebarVisibilityRequested, this, &PDFEditorMainWindow::onSidebarVisibilityRequested);
    for (QAction* action : m_sidebarWidget->getOutlineActions())
    {
        m_actionManager->addAdditionalAction(action);
    }

    // Outline items are created directly from the document being read, so the action
    // must be reachable from the menu (and by its shortcut) without opening the sidebar
    if (QAction* outlineNewItemAction = m_sidebarWidget->getOutlineNewItemAction())
    {
        ui->menuInsert->addSeparator();
        ui->menuInsert->addAction(outlineNewItemAction);
    }

    m_advancedFindWidget = new PDFAdvancedFindWidget(m_programController->getPdfWidget()->getDrawWidgetProxy(), this);
    // PDF Fire: the find panel is at the right side - at the bottom it took half of the
    // window over the page (on a laptop screen), and its close button was not visible
    m_advancedFindDockWidget = new QDockWidget(tr("Advanced Find"), this);
    m_advancedFindDockWidget->setObjectName("AdvancedFind");
    m_advancedFindDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
    m_advancedFindDockWidget->setWidget(m_advancedFindWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_advancedFindDockWidget);
    setDockTitleBar(m_advancedFindDockWidget);
    m_advancedFindDockWidget->hide();

    // PDF Fire: the options of the active tool, at the right side (see PDFAnnotationStyleWidget)
    m_toolOptionsDockWidget = new QDockWidget(tr("Tool Options"), this);
    m_toolOptionsDockWidget->setObjectName("ToolOptions");
    m_toolOptionsDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
    QWidget* toolOptionsPanel = new QWidget(m_toolOptionsDockWidget);
    QVBoxLayout* toolOptionsLayout = new QVBoxLayout(toolOptionsPanel);
    toolOptionsLayout->addStretch(1);
    m_toolOptionsDockWidget->setWidget(toolOptionsPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_toolOptionsDockWidget);
    setDockTitleBar(m_toolOptionsDockWidget);
    m_toolOptionsDockWidget->hide();
    pdf::PDFAnnotationStyleWidget::setOptionsPanel(toolOptionsPanel);
    QAction* toggleAdvancedFindAction = m_advancedFindDockWidget->toggleViewAction();
    toggleAdvancedFindAction->setObjectName("actionAdvancedFind");
    toggleAdvancedFindAction->setText(tr("Ad&vanced Find..."));
    toggleAdvancedFindAction->setShortcut(QKeySequence("Ctrl+Shift+F"));
    toggleAdvancedFindAction->setIcon(QIcon(":/resources/find-advanced.svg"));
    ui->menuEdit->insertAction(nullptr, toggleAdvancedFindAction);
    m_actionManager->addAdditionalAction(m_advancedFindDockWidget->toggleViewAction());

    ui->menuView->addSeparator();
    ui->menuView->addAction(m_sidebarDockWidget->toggleViewAction());
    m_sidebarDockWidget->toggleViewAction()->setObjectName("actionSidebar");
    m_actionManager->addAdditionalAction(m_sidebarDockWidget->toggleViewAction());

    connect(m_progress, &pdf::PDFProgress::progressStarted, this, &PDFEditorMainWindow::onProgressStarted);
    connect(m_progress, &pdf::PDFProgress::progressStep, this, &PDFEditorMainWindow::onProgressStep);
    connect(m_progress, &pdf::PDFProgress::progressFinished, this, &PDFEditorMainWindow::onProgressFinished);

    PDFActionComboBox* actionComboBox = new PDFActionComboBox(this);
    menuBar()->setCornerWidget(actionComboBox);

    m_programController->finishInitialization();
    updateDeveloperMenu();

    // PDF Fire: a saved window layout can have the find panel at the bottom (where it was
    // before) - it is moved to the right side; the tool options appear only with a tool
    addDockWidget(Qt::RightDockWidgetArea, m_advancedFindDockWidget);
    if (m_toolOptionsDockWidget)
    {
        m_toolOptionsDockWidget->hide();
    }

    // PDF Fire: the sidebar belongs to a document - with no document open, it is not
    // restored from the saved layout (2026-10-03: a layout saved with the sidebar open,
    // after a document was closed while it was read aloud, froze a Windows PC at start)
    if (m_sidebarDockWidget && !m_programController->getDocument())
    {
        m_sidebarDockWidget->hide();
    }

    if (pdf::PDFToolManager* toolManager = m_programController->getToolManager())
    {
        connect(toolManager, &pdf::PDFToolManager::messageDisplayRequest, statusBar(), &QStatusBar::showMessage);
    }

    m_actionManager->styleActions();
    m_programController->initActionComboBox(actionComboBox);

    // PDF Fire: the ribbon replaces the menu bar and the toolbars built above
    setupRibbon();

#ifndef NDEBUG
    pdf::PDFWidgetUtils::checkMenuAccessibility(this);
#endif
}

PDFEditorMainWindow::~PDFEditorMainWindow()
{
    // PDF Fire: the side panel and the find panel use objects owned by the program
    // controller (the bookmark manager, the draw widget proxy...), so they must be
    // destroyed before it. Otherwise they are destroyed with the window, after the
    // controller, and an event delivered in between reads the freed objects (it does
    // happen - a widget is re-styled when it is detached from its parent).
    delete m_sidebarDockWidget;
    m_sidebarDockWidget = nullptr;
    m_sidebarWidget = nullptr;

    delete m_advancedFindDockWidget;
    m_advancedFindDockWidget = nullptr;
    m_advancedFindWidget = nullptr;

    delete m_programController;
    m_programController = nullptr;

    delete m_actionManager;
    m_actionManager = nullptr;

    delete ui;
}

void PDFEditorMainWindow::setDockTitleBar(QDockWidget* dockWidget)
{
    QWidget* titleBar = new QWidget(dockWidget);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    const int margin = pdf::PDFWidgetUtils::scaleDPI_x(titleBar, 6);
    titleLayout->setContentsMargins(margin, margin / 2, margin / 2, margin / 2);

    QLabel* titleLabel = new QLabel(dockWidget->windowTitle(), titleBar);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch(1);

    QToolButton* closeButton = new QToolButton(titleBar);
    closeButton->setObjectName(dockWidget->objectName() + "CloseButton");
    closeButton->setText(QString::fromUtf8("✕"));
    closeButton->setToolTip(tr("Close"));
    closeButton->setAutoRaise(true);
    QFont closeFont = closeButton->font();
    closeFont.setPointSizeF(closeFont.pointSizeF() * 1.2);
    closeButton->setFont(closeFont);
    connect(closeButton, &QToolButton::clicked, dockWidget, &QDockWidget::close);
    titleLayout->addWidget(closeButton);

    dockWidget->setTitleBarWidget(titleBar);
}

void PDFEditorMainWindow::onSidebarVisibilityRequested()
{
    if (!m_sidebarDockWidget->isVisible())
    {
        m_sidebarDockWidget->show();
    }

    m_sidebarDockWidget->raise();
}

void PDFEditorMainWindow::onActionQuitTriggered()
{
    close();
}

void PDFEditorMainWindow::onPageNumberSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageNumberSpinBox->hasFocus())
    {
        m_programController->getPdfWidget()->setFocus();
    }

    m_programController->getPdfWidget()->getDrawWidgetProxy()->goToPage(m_pageNumberSpinBox->value() - 1);
}

void PDFEditorMainWindow::onPageZoomSpinboxEditingFinished()
{
    if (m_isLoadingUI)
    {
        return;
    }

    if (m_pageZoomSpinBox->hasFocus())
    {
        m_programController->getPdfWidget()->setFocus();
    }

    m_programController->getPdfWidget()->getDrawWidgetProxy()->zoom(m_pageZoomSpinBox->value() / 100.0);
}

void PDFEditorMainWindow::onProgressStarted(pdf::ProgressStartupInfo info)
{
    m_progressBarLeftLabelOnStatusBar->setText(info.text);
    m_progressBarLeftLabelOnStatusBar->setVisible(!info.text.isEmpty());

    m_progressBarOnStatusBar->setRange(0, 100);
    m_progressBarOnStatusBar->reset();
    m_progressBarOnStatusBar->show();

    m_progressTaskbarIndicator->setRange(0, 100);
    m_progressTaskbarIndicator->reset();
    m_progressTaskbarIndicator->show();

    m_programController->setIsBusy(true);
    m_programController->updateActionsAvailability();
}

void PDFEditorMainWindow::onProgressStep(int percentage)
{
    if (m_isChangingProgressStep)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isChangingProgressStep, true);
    m_progressBarOnStatusBar->setValue(percentage);
    m_progressTaskbarIndicator->setValue(percentage);
}

void PDFEditorMainWindow::onProgressFinished()
{
    m_progressBarLeftLabelOnStatusBar->hide();
    m_progressBarOnStatusBar->hide();
    m_progressTaskbarIndicator->hide();

    m_programController->setIsBusy(false);
    m_programController->updateActionsAvailability();
}

void PDFEditorMainWindow::updateDeveloperMenu()
{
    bool isDeveloperMode = m_programController->getSettings()->getSettings().m_allowDeveloperMode;
    ui->menuDeveloper->menuAction()->setVisible(isDeveloperMode);
}

void PDFEditorMainWindow::updateUI(bool fullUpdate)
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingUI, true);

    if (fullUpdate)
    {
        if (pdf::PDFDocument* document = m_programController->getDocument())
        {
            size_t pageCount = document->getCatalog()->getPageCount();
            m_pageNumberSpinBox->setMinimum(1);
            m_pageNumberSpinBox->setMaximum(static_cast<int>(pageCount));
            m_pageNumberSpinBox->setEnabled(true);
            m_pageNumberLabel->setText(tr(" / %1").arg(pageCount));
        }
        else
        {
            m_pageNumberSpinBox->setEnabled(false);
            m_pageNumberLabel->setText(QString());
        }
    }
    else
    {
        std::vector<pdf::PDFInteger> currentPages = m_programController->getPdfWidget()->getDrawWidget()->getCurrentPages();
        if (!currentPages.empty())
        {
            m_pageNumberSpinBox->setValue(currentPages.front() + 1);

            // Prefetch pages, if it is enabled
            if (m_programController->getSettings()->isPagePrefetchingEnabled())
            {
                m_programController->getPdfWidget()->getDrawWidgetProxy()->prefetchPages(currentPages.back());
            }
        }

        m_sidebarWidget->setCurrentPages(currentPages);
    }

    m_pageZoomSpinBox->setValue(m_programController->getPdfWidget()->getDrawWidgetProxy()->getZoom() * 100);
}

QMenu* PDFEditorMainWindow::addToolMenu(QString name, pdf::PDFPlugin::PluginMenuLocation location)
{
    QMenu* parentMenu = (location == pdf::PDFPlugin::PluginMenuLocation::Edit) ? ui->menuEdit : ui->menuTools;
    return parentMenu->addMenu(name);
}

void PDFEditorMainWindow::setStatusBarMessage(QString message, int time)
{
    statusBar()->showMessage(message, time);
}

void PDFEditorMainWindow::setDocument(const pdf::PDFModifiedDocument& document)
{
    if (m_sidebarWidget)
    {
        m_sidebarWidget->setDocument(document, *m_programController->getSignatures());
    }

    if (m_advancedFindWidget)
    {
        m_advancedFindWidget->setDocument(document);
    }

    if (m_sidebarWidget)
    {
        if (!document || m_sidebarWidget->isEmpty())
        {
            m_sidebarDockWidget->hide();
        }
        else if (document.hasReset() && !document.hasPreserveUndoRedo())
        {
            const bool showSidebar = m_programController->getSettings()->getSettings().m_showSidebarOnDocumentOpen;
            m_sidebarDockWidget->setVisible(showSidebar);
        }
    }

    if (m_ribbon)
    {
        updateWelcomePage(bool(document));
    }

    if (document && document.hasReset() && !document.hasPreserveUndoRedo())
    {
        if (m_ribbon)
        {
            applyDefaultDocumentView();
        }
    }

    if (!document && m_advancedFindDockWidget)
    {
        m_advancedFindDockWidget->hide();
    }

    if (document && document.hasReset() && !document.hasPreserveUndoRedo())
    {
        // Another document - its protection is shown again
        m_isProtectionBarDismissed = false;
    }
    updateProtectionBar();
}

void PDFEditorMainWindow::updateProtectionBar()
{
    if (!m_protectionBar)
    {
        return;
    }

    const pdf::PDFDocument* document = m_programController->getDocument();
    const pdf::PDFFirePermissions::ProtectionSummary protection = pdf::PDFFirePermissions::getProtectionSummary(document);
    if (!document || !protection.isRestricted || m_isProtectionBarDismissed)
    {
        m_protectionBar->hide();
        return;
    }

    QString text = QString::fromUtf8("\xF0\x9F\x94\x92  ") + protection.headline;
    if (!protection.allowed.isEmpty())
    {
        text += QChar(' ') + tr("Allowed: %1.").arg(protection.allowed.join(QStringLiteral(", ")).toLower());
    }
    m_protectionBarLabel->setText(text);
    m_protectionBarUnlockButton->setVisible(protection.canUnlock);
    m_protectionBar->show();
}

void PDFEditorMainWindow::updateDocumentTabs()
{
    if (!m_documentTabBar)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isUpdatingDocumentTabs, true);
    const std::vector<PDFProgramController::DocumentTabInfo> tabs = m_programController->getDocumentTabs();

    while (m_documentTabBar->count() > int(tabs.size()))
    {
        m_documentTabBar->removeTab(m_documentTabBar->count() - 1);
    }
    while (m_documentTabBar->count() < int(tabs.size()))
    {
        m_documentTabBar->addTab(QString());
    }

    for (int i = 0; i < int(tabs.size()); ++i)
    {
        // A modified document has a dot after its name, as in other editors
        m_documentTabBar->setTabText(i, tabs[i].isModified ? tabs[i].title + QStringLiteral(" \u25CF") : tabs[i].title);
        m_documentTabBar->setTabToolTip(i, tabs[i].toolTip);
    }

    m_documentTabBar->setCurrentIndex(m_programController->getCurrentDocumentTab());
    m_documentTabBar->setVisible(!tabs.empty() && m_programController->getDocument() != nullptr);
}

void PDFEditorMainWindow::adjustToolbar(QToolBar* toolbar)
{
    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    toolbar->setIconSize(iconSize);
}

pdf::PDFTextSelection PDFEditorMainWindow::getSelectedText() const
{
    if (!m_advancedFindWidget)
    {
        return pdf::PDFTextSelection();
    }

    return m_advancedFindWidget->getSelectedText();
}

void PDFEditorMainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_programController->canClose())
    {
        // Jakub Melka: Do not allow to close the application, if document
        // reading is running.
        event->ignore();
    }
    else
    {
        // PDF Fire: every open document (tab) is asked about
        if (!m_programController->askForSaveAllDocumentsBeforeClose())
        {
            // User cancelled close operation
            event->ignore();
            return;
        }

        // PDF Fire: reading aloud is stopped first, so the state saved is the quiet one
        if (PDFTextToSpeech* textToSpeech = m_programController->getTextToSpeech())
        {
            textToSpeech->stop();
        }

        if (!m_programController->isFactorySettingsBeingRestored())
        {
            m_programController->writeSettings();
        }

        m_programController->closeDocument();
        event->accept();
    }
}

void PDFEditorMainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    m_progressTaskbarIndicator->setWindow(windowHandle());
}

void PDFEditorMainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFEditorMainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->setDropAction(Qt::LinkAction);
        event->accept();
    }
}

void PDFEditorMainWindow::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1)
        {
            m_programController->openDocument(urls.front().toLocalFile());
            event->acceptProposedAction();
        }
    }
}

}   // namespace pdfviewer
