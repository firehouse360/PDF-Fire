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

#include "editorplugin.h"
#include "pdffirepermissions.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontenteditorstylesettings.h"
#include "pdfdocumentbuilder.h"
#include "pdfcertificatemanagerdialog.h"
#include "pdfdocumentwriter.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfstreamfilters.h"
#include "pdfoptimizer.h"
#include "pdffireeditedtext.h"
#include "pdffireinlinetexteditor.h"
#include "pdffiretextelementupdater.h"
#include <QApplication>
#include <QStatusBar>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QRegularExpression>

#include <QAction>
#include <QToolButton>
#include <QMainWindow>
#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QCoreApplication>

#include <cmath>
#include <limits>

namespace pdfplugin
{

EditorPlugin::EditorPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actions({ }),
    m_tools({ }),
    m_editorWidget(nullptr),
    m_scene(nullptr),
    m_sceneSelectionChangeEnabled(true),
    m_isSaving(false),
    m_isUndoRedoInProgress(false)
{
    m_scene.setIsPageContentDrawSuppressed(true);

    // PDF Fire: the preview of the text edited in place is updated shortly after the
    // last keystroke (a fast typist does not wait for each one)
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(40);
    connect(&m_previewTimer, &QTimer::timeout, this, &EditorPlugin::updateInlinePreview);
}

void EditorPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* activateAction = new QAction(QIcon(":/pdfplugins/editorplugin/activate.svg"), tr("&Edit page content"), this);
    QAction* editTextAction = new QAction(QIcon(":/pdfplugins/editorplugin/edit-text.svg"), tr("Edit Te&xt"), this);
    QAction* createTextAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-text.svg"), tr("Create &Text Label"), this);
    QAction* createFreehandCurveAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-freehand-curve.svg"), tr("Create &Freehand Curve"), this);
    QAction* createAcceptMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-yes-mark.svg"), tr("Create &Accept Mark"), this);
    QAction* createRejectMarkAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-no-mark.svg"), tr("Create &Reject Mark"), this);
    QAction* createRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rectangle.svg"), tr("Create R&ectangle"), this);
    QAction* createRoundedRectangleAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-rounded-rectangle.svg"), tr("&Create Rounded Rectangle"), this);
    QAction* createHorizontalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-horizontal-line.svg"), tr("Create &Horizontal Line"), this);
    QAction* createVerticalLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-vertical-line.svg"), tr("Create &Vertical Line"), this);
    QAction* createLineAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-line.svg"), tr("Create L&ine"), this);
    QAction* createDotAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-dot.svg"), tr("Create &Dot"), this);
    QAction* createSvgImageAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-svg-image.svg"), tr("Create &SVG Image"), this);
    QAction* createMultipleElementsAction = new QAction(QIcon(":/pdfplugins/editorplugin/create-multiple.svg"), tr("Create &Multiple Elements"), this);
    QAction* undoAction = new QAction(QIcon(":/resources/undo.svg"), tr("&Undo"), this);
    QAction* redoAction = new QAction(QIcon(":/resources/redo.svg"), tr("&Redo"), this);
    QAction* clearAction = new QAction(QIcon(":/pdfplugins/editorplugin/clear.svg"), tr("Clear A&ll Graphics"), this);

    activateAction->setObjectName("editortool_activateAction");
    editTextAction->setObjectName("editortool_editTextAction");
    createTextAction->setObjectName("editortool_createTextAction");
    createFreehandCurveAction->setObjectName("editortool_createFreehandCurveAction");
    createAcceptMarkAction->setObjectName("editortool_createAcceptMarkAction");
    createRejectMarkAction->setObjectName("editortool_createRejectMarkAction");
    createRectangleAction->setObjectName("editortool_createRectangleAction");
    createRoundedRectangleAction->setObjectName("editortool_createRoundedRectangleAction");
    createHorizontalLineAction->setObjectName("editortool_createHorizontalLineAction");
    createVerticalLineAction->setObjectName("editortool_createVerticalLineAction");
    createLineAction->setObjectName("editortool_createLineAction");
    createDotAction->setObjectName("editortool_createDotAction");
    createSvgImageAction->setObjectName("editortool_createSvgImageAction");
    createMultipleElementsAction->setObjectName("editortool_createMultipleElementsAction");
    undoAction->setObjectName("editortool_undoAction");
    redoAction->setObjectName("editortool_redoAction");
    clearAction->setObjectName("editortool_clearAction");

    activateAction->setCheckable(true);
    editTextAction->setCheckable(true);
    editTextAction->setToolTip(tr("Edit Text\n\n"
                                  "Click a text on the page to change it in place, in the font of the document. "
                                  "The line or the paragraph under the mouse is outlined. "
                                  "Enter (a line) or Ctrl+Enter (a paragraph) or a click outside keeps the change, Esc cancels."));
    createTextAction->setCheckable(true);
    createFreehandCurveAction->setCheckable(true);
    createAcceptMarkAction->setCheckable(true);
    createRejectMarkAction->setCheckable(true);
    createRectangleAction->setCheckable(true);
    createRoundedRectangleAction->setCheckable(true);
    createHorizontalLineAction->setCheckable(true);
    createVerticalLineAction->setCheckable(true);
    createLineAction->setCheckable(true);
    createDotAction->setCheckable(true);
    createSvgImageAction->setCheckable(true);
    createMultipleElementsAction->setCheckable(true);
    createMultipleElementsAction->setToolTip(tr("Create Multiple Elements\n\n"
                                                "The creation tool stays active after an element has been created "
                                                "and the size of the last created element is reused, so a single click "
                                                "creates the next element of the same size. "
                                                "Hold Shift to define a different size."));

    m_actions[Activate] = activateAction;
    m_actions[EditText] = editTextAction;
    m_actions[Text] = createTextAction;
    m_actions[FreehandCurve] = createFreehandCurveAction;
    m_actions[AcceptMark] = createAcceptMarkAction;
    m_actions[RejectMark] = createRejectMarkAction;
    m_actions[Rectangle] = createRectangleAction;
    m_actions[RoundedRectangle] = createRoundedRectangleAction;
    m_actions[HorizontalLine] = createHorizontalLineAction;
    m_actions[VerticalLine] = createVerticalLineAction;
    m_actions[Line] = createLineAction;
    m_actions[Dot] = createDotAction;
    m_actions[SvgImage] = createSvgImageAction;
    m_actions[CreateMultipleElements] = createMultipleElementsAction;
    m_actions[Undo] = undoAction;
    m_actions[Redo] = redoAction;
    m_actions[Clear] = clearAction;

    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);

    // Jakub Melka: the tool actions are enabled only when the page content editing
    // is active, so single letter shortcuts do not collide with the rest of the
    // application. While a text label is being edited, the printable characters are
    // consumed by the text editor, so the shortcuts do not interfere with typing.
    activateAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    createTextAction->setShortcut(QKeySequence("T"));
    createFreehandCurveAction->setShortcut(QKeySequence("F"));
    createAcceptMarkAction->setShortcut(QKeySequence("A"));
    createRejectMarkAction->setShortcut(QKeySequence("X"));
    createRectangleAction->setShortcut(QKeySequence("R"));
    createRoundedRectangleAction->setShortcut(QKeySequence("Shift+R"));
    createHorizontalLineAction->setShortcut(QKeySequence("H"));
    createVerticalLineAction->setShortcut(QKeySequence("V"));
    createLineAction->setShortcut(QKeySequence("L"));
    createDotAction->setShortcut(QKeySequence("D"));
    createSvgImageAction->setShortcut(QKeySequence("I"));
    createMultipleElementsAction->setShortcut(QKeySequence("M"));

    QFile acceptMarkFile(":/pdfplugins/editorplugin/accept-mark.svg");
    QByteArray acceptMarkContent;
    if (acceptMarkFile.open(QFile::ReadOnly))
    {
        acceptMarkContent = acceptMarkFile.readAll();
        acceptMarkFile.close();
    }

    QFile rejectMarkFile(":/pdfplugins/editorplugin/reject-mark.svg");
    QByteArray rejectMarkContent;
    if (rejectMarkFile.open(QFile::ReadOnly))
    {
        rejectMarkContent = rejectMarkFile.readAll();
        rejectMarkFile.close();
    }

    m_tools[TextTool] = new pdf::PDFCreatePCElementTextTool(widget->getDrawWidgetProxy(), &m_scene, createTextAction, this);
    m_tools[FreehandCurveTool] = new pdf::PDFCreatePCElementFreehandCurveTool(widget->getDrawWidgetProxy(), &m_scene, createFreehandCurveAction, this);
    m_tools[AcceptMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createAcceptMarkAction, acceptMarkContent, false, this);
    m_tools[RejectMarkTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createRejectMarkAction, rejectMarkContent, false, this);
    m_tools[RectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRectangleAction, false, this);
    m_tools[RoundedRectangleTool] = new pdf::PDFCreatePCElementRectangleTool(widget->getDrawWidgetProxy(), &m_scene, createRoundedRectangleAction, true, this);
    m_tools[HorizontalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createHorizontalLineAction, true, false, this);
    m_tools[VerticalLineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createVerticalLineAction, false, true, this);
    m_tools[LineTool] = new pdf::PDFCreatePCElementLineTool(widget->getDrawWidgetProxy(), &m_scene, createLineAction, false, false, this);
    m_tools[DotTool] = new pdf::PDFCreatePCElementDotTool(widget->getDrawWidgetProxy(), &m_scene, createDotAction, this);
    m_tools[ImageTool] = new pdf::PDFCreatePCElementImageTool(widget->getDrawWidgetProxy(), &m_scene, createSvgImageAction, QByteArray(), true, this);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        toolManager->addTool(tool);
        connect(tool, &pdf::PDFWidgetTool::toolActivityChanged, this, &EditorPlugin::onToolActivityChanged);
    }

    m_widget->addInputInterface(&m_scene);
    m_widget->getDrawWidgetProxy()->registerDrawInterface(&m_scene);
    m_scene.setWidget(m_widget);
    connect(&m_scene, &pdf::PDFPageContentScene::sceneChanged, this, &EditorPlugin::onSceneChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::selectionChanged, this, &EditorPlugin::onSceneSelectionChanged);
    connect(&m_scene, &pdf::PDFPageContentScene::editElementRequest, this, &EditorPlugin::onSceneEditElement);
    connect(&m_scene, &pdf::PDFPageContentScene::textElementHovered, this, &EditorPlugin::onTextElementHovered);
    connect(editTextAction, &QAction::triggered, this, &EditorPlugin::onEditTextTriggered);
    // PDF Fire: this removes everything from the edited pages, including their original
    // content - applying the changes then writes empty pages. So it is confirmed first.
    connect(clearAction, &QAction::triggered, this, [this]()
    {
        const QString message = tr("All graphics will be removed from the edited pages, including the original content of the pages. "
                                   "If the changes are then applied, the pages become empty.\n\nRemove all graphics?");
        if (QMessageBox::warning(m_widget, tr("Clear All Graphics"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        {
            m_scene.clear();
        }
    });
    connect(undoAction, &QAction::triggered, this, &EditorPlugin::onUndoTriggered);
    connect(redoAction, &QAction::triggered, this, &EditorPlugin::onRedoTriggered);
    connect(activateAction, &QAction::triggered, this, &EditorPlugin::onSetActive);
    connect(createMultipleElementsAction, &QAction::triggered, this, &EditorPlugin::onCreateMultipleElementsTriggered);
    connect(createMultipleElementsAction, &QAction::triggered, this, &EditorPlugin::writeSettings);
    connect(m_widget->getDrawWidgetProxy(), &pdf::PDFDrawWidgetProxy::drawSpaceChanged, this, &EditorPlugin::onDrawSpaceChanged);
    connect(m_widget, &pdf::PDFWidget::sceneActivityChanged, this, &EditorPlugin::onSceneActivityChanged);

    readSettings();
    updateActions();
}

void EditorPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        clearUndoRedo();
        setActive(false);
        updateActions();
    }
}

std::vector<QAction*> EditorPlugin::getActions() const
{
    // Jakub Melka: all the actions are offered, so the page content editing and its
    // tools can be found in the menu of the application and not only in the toolbox,
    // which is displayed when the editing is already active. A null action creates
    // a separator both in the menu and in the toolbar of the plugin.
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);
    result.push_back(m_actions[EditText]);
    result.push_back(nullptr);

    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        result.push_back(m_actions[actionId]);
    }

    result.push_back(nullptr);
    result.push_back(m_actions[CreateMultipleElements]);
    result.push_back(nullptr);
    result.push_back(m_actions[Undo]);
    result.push_back(m_actions[Redo]);
    result.push_back(m_actions[Clear]);

    return result;
}

QString EditorPlugin::getPluginMenuName() const
{
    return tr("Ed&itor");
}

pdf::PDFPlugin::PluginMenuLocation EditorPlugin::getPluginMenuLocation() const
{
    return PluginMenuLocation::Edit;
}

std::vector<QAction*> EditorPlugin::getToolbarActions() const
{
    // Jakub Melka: only the activation of the page content editing is placed on the
    // toolbar - the tools would not fit on it. They are offered in the menu of the
    // plugin and in the toolbox, which is displayed while the editing is active.
    std::vector<QAction*> result;

    result.push_back(m_actions[Activate]);

    return result;
}

bool EditorPlugin::updatePageContent(pdf::PDFInteger pageIndex,
                                     const std::vector<const pdf::PDFPageContentElement*>& elements,
                                     pdf::PDFDocumentBuilder* builder)
{
    pdf::PDFColorConvertor convertor;
    const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
    const pdf::PDFEditedPageContent& editedPageContent = m_editedPageContent.at(pageIndex);

    QRectF mediaBox = page->getMediaBox();
    QRectF mediaBoxMM = page->getMediaBoxMM();

    pdf::PDFPageContentEditorContentStreamBuilder contentStreamBuilder(m_document);
    contentStreamBuilder.setFontDictionary(editedPageContent.getFontDictionary());
    contentStreamBuilder.setXObjectDictionary(editedPageContent.getXObjectDictionary());
    contentStreamBuilder.setGraphicStateDictionary(editedPageContent.getGraphicStateDictionary());
    contentStreamBuilder.setShadingDictionary(editedPageContent.getShadingDictionary());

    for (const pdf::PDFPageContentElement* element : elements)
    {
        const pdf::PDFPageContentElementEdited* editedElement = element->asElementEdited();
        const pdf::PDFPageContentElementRectangle* elementRectangle = element->asElementRectangle();
        const pdf::PDFPageContentElementLine* elementLine = element->asElementLine();
        const pdf::PDFPageContentElementDot* elementDot = element->asElementDot();
        const pdf::PDFPageContentElementFreehandCurve* elementFreehandCurve = element->asElementFreehandCurve();
        const pdf::PDFPageContentImageElement* elementImage = element->asElementImage();
        const pdf::PDFPageContentElementTextBox* elementTextBox = element->asElementTextBox();

        if (editedElement)
        {
            contentStreamBuilder.writeEditedElement(editedElement->getElement());
        }

        if (elementRectangle)
        {
            QRectF rect = elementRectangle->getRectangle();

            QPainterPath path;
            if (elementRectangle->isRounded())
            {
                qreal radius = qMin(rect.width(), rect.height()) * 0.25;
                path.addRoundedRect(rect, radius, radius, Qt::AbsoluteSize);
            }
            else
            {
                path.addRect(rect);
            }

            const bool stroke = elementRectangle->getPen().style() != Qt::NoPen;
            const bool fill = elementRectangle->getBrush().style() != Qt::NoBrush;
            contentStreamBuilder.writeStyledPath(path, elementRectangle->getPen(), elementRectangle->getBrush(), stroke, fill);
        }

        if (elementLine)
        {
            QLineF line = elementLine->getLine();
            QPainterPath path;
            path.moveTo(line.p1());
            path.lineTo(line.p2());

            contentStreamBuilder.writeStyledPath(path, elementLine->getPen(), elementLine->getBrush(), true, false);
        }

        if (elementDot)
        {
            QPen pen = elementDot->getPen();
            const qreal radius = pen.widthF() * 0.5;

            QPainterPath path;
            path.addEllipse(elementDot->getPoint(), radius, radius);

            contentStreamBuilder.writeStyledPath(path, Qt::NoPen, QBrush(pen.color()), false, true);
        }

        if (elementFreehandCurve)
        {
            QPainterPath path = elementFreehandCurve->getCurve();
            contentStreamBuilder.writeStyledPath(path, elementFreehandCurve->getPen(), elementFreehandCurve->getBrush(), true, false);
        }

        if (elementImage)
        {
            QImage image = elementImage->getImage();
            if (!image.isNull())
            {
                contentStreamBuilder.writeImage(image, elementImage->getRectangle());
            }
            else
            {
                // It is probably an SVG image
                pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
                QPainter painter(&paintDevice);

                QList<pdf::PDFRenderError> errors;
                pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
                elementImage->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
            }
        }

        if (elementTextBox)
        {
            pdf::PDFContentEditorPaintDevice paintDevice(&contentStreamBuilder, mediaBox, mediaBoxMM);
            QPainter painter(&paintDevice);

            QList<pdf::PDFRenderError> errors;
            pdf::PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
            elementTextBox->drawPage(&painter, &m_scene, pageIndex, nullptr, textLayoutGetter, QTransform(), convertor, errors);
        }
    }

    QStringList errors = contentStreamBuilder.getErrors();
    contentStreamBuilder.clearErrors();

    if (!confirmContentStreamErrors(errors, pageIndex))
    {
        return false;
    }

    pdf::PDFFireTextElementUpdater::writePageContent(builder, m_document, page, contentStreamBuilder);

    return true;
}

bool EditorPlugin::hasUnwrittenChanges() const
{
    // While the page content editing is active, the edited content is held
    // by the scene only - it is written into the document when the editing
    // is finished.
    return m_scene.isActive();
}

bool EditorPlugin::writeUnwrittenChanges()
{
    pdf::PDFTemporaryValueChange guard(&m_isSaving, true);
    return writePageContentToDocument();
}

bool EditorPlugin::save()
{
    pdf::PDFTemporaryValueChange guard(&m_isSaving, true);

    auto answer = QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Confirm Changes"), tr("The changes to the page content will be written to the document. Do you want to continue?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);

    if (answer == QMessageBox::Cancel)
    {
        return false;
    }

    if (answer == QMessageBox::Yes)
    {
        return writePageContentToDocument();
    }

    return true;
}

bool EditorPlugin::writePageContentToDocument()
{
    // PDF Fire: the text being edited in place is a part of the changes
    if (!finishInlineEdit())
    {
        return false;
    }

    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    std::set<pdf::PDFInteger> pageIndices;
    for (const auto& item : m_editedPageContent)
    {
        pageIndices.insert(item.first);
    }

    std::map<pdf::PDFInteger, std::vector<const pdf::PDFPageContentElement*>> elementsByPage = m_scene.getElementsByPage();
    for (pdf::PDFInteger pageIndex : pageIndices)
    {
        if (m_editedPageContent.count(pageIndex) == 0)
        {
            continue;
        }

        std::vector<const pdf::PDFPageContentElement*> elements;
        auto it = elementsByPage.find(pageIndex);
        if (it != elementsByPage.cend())
        {
            elements = std::move(it->second);
        }

        if (!updatePageContent(pageIndex, elements, builder))
        {
            return false;
        }

        modifier.markReset();
    }

    pdf::PDFTemporaryValueChange restoreGuard(&m_isUndoRedoInProgress, true);
    clearUndoRedo();
    m_scene.clear();
    m_editedPageContent.clear();

    if (modifier.finalize())
    {
        pdf::PDFDocument document = *modifier.getDocument();
        pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::DereferenceSimpleObjects |
                                    pdf::PDFOptimizer::RemoveNullObjects |
                                    pdf::PDFOptimizer::RemoveUnusedObjects |
                                    pdf::PDFOptimizer::MergeIdenticalObjects |
                                    pdf::PDFOptimizer::ShrinkObjectStorage, nullptr);
        optimizer.setDocument(&document);
        optimizer.optimize();
        document = optimizer.takeOptimizedDocument();

        const pdf::PDFModifiedDocument::ModificationFlags flags = modifier.getFlags() | pdf::PDFModifiedDocument::PreserveUndoRedo;
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), nullptr, flags));
    }

    return true;
}

void EditorPlugin::onSceneActivityChanged()
{
    updateActions();
}

void EditorPlugin::onSceneChanged(bool graphicsOnly)
{
    if (!graphicsOnly && !m_isUndoRedoInProgress && !m_isSaving)
    {
        createUndoStep();
    }

    if (!graphicsOnly)
    {
        updateActions();
    }

    if (m_editorWidget)
    {
        m_editorWidget->updateItemsInListWidget();
    }

    updateGraphics();
}

void EditorPlugin::onSceneSelectionChanged()
{
    if (m_editorWidget && m_sceneSelectionChangeEnabled)
    {
        m_editorWidget->setSelection(m_scene.getSelectedElementIds());
    }
}

void EditorPlugin::onWidgetSelectionChanged()
{
    Q_ASSERT(m_editorWidget);

    pdf::PDFTemporaryValueChange guard(&m_sceneSelectionChangeEnabled, false);
    m_scene.setSelectedElementIds(m_editorWidget->getSelection());
}

pdf::PDFWidgetTool* EditorPlugin::getActiveTool()
{
    for (pdf::PDFWidgetTool* currentTool : m_tools)
    {
        if (currentTool->isActive())
        {
            return currentTool;
        }
    }

    return nullptr;
}

void EditorPlugin::onToolActivityChanged()
{
    // PDF Fire: a tool of the page content editing switches the Edit Text mode off
    if (m_scene.isTextEditMode() && getActiveTool())
    {
        // The page content editing goes on with the tool
        setTextEditMode(false);
        m_isSceneActivatedByEditText = false;
    }

    if (m_editorWidget)
    {
        pdf::PDFWidgetTool* activeTool = getActiveTool();

        const pdf::PDFPageContentElement* element = nullptr;
        pdf::PDFCreatePCElementTool* tool = qobject_cast<pdf::PDFCreatePCElementTool*>(activeTool);
        if (tool)
        {
            element = tool->getElement();
        }

        m_editorWidget->loadStyleFromElement(element);
    }
}

void EditorPlugin::onSceneEditElement(const std::set<pdf::PDFInteger>& elements)
{
    if (elements.empty())
    {
        return;
    }

    // PDF Fire: the text edited in place is applied first (it can change the elements).
    // If it can't be applied, its editor stays open and nothing else is edited.
    if (!finishInlineEdit())
    {
        return;
    }

    pdf::PDFPageContentElement* element = nullptr;
    for (pdf::PDFInteger id : elements)
    {
        element = m_scene.getElementById(id);
        if (element)
        {
            break;
        }
    }

    if (!element)
    {
        return;
    }

    // PDF Fire: a text is edited in place, with a caret in the text; Shift opens the
    // dialog (font size, transformation, the markup)
    if (!QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) && startInlineTextEdit(element))
    {
        return;
    }

    std::unique_ptr<pdf::PDFPageContentElement> clonedElement(element->clone());
    if (pdf::PDFPageContentEditorStyleSettings::showEditElementStyleDialog(m_dataExchangeInterface->getMainWindow(), clonedElement.get()))
    {
        if (clonedElement->asElementEdited())
        {
            pdf::PDFPageContentElementEdited* editedElement = dynamic_cast<pdf::PDFPageContentElementEdited*>(clonedElement.get());
            if (editedElement->getElement()->asText())
            {
                if (!updateTextElement(editedElement))
                {
                    return;
                }
            }
        }

        m_scene.replaceElement(clonedElement.release());
        updateGraphics();
    }
}

QFont EditorPlugin::getInlineEditorFont(const pdf::PDFFontPointer& pdfFont)
{
    // The editor shows the text in a font of the system with the name of the font of
    // the text. The program of the font embedded in the document is not used: it is
    // usually a subset with a private character map, and Qt shows garbage with it.
    // (The editor only shows the text - it is written back in the original font.)
    QFont font(QStringLiteral("Sans Serif"));
    if (pdfFont)
    {
        const pdf::FontDescriptor* descriptor = pdfFont->getFontDescriptor();
        QString fontName = QString::fromLatin1(descriptor->fontName);
        if (fontName.size() > 7 && fontName[6] == QChar('+'))
        {
            fontName = fontName.mid(7);
        }

        const bool isBold = fontName.contains(QLatin1String("Bold"), Qt::CaseInsensitive) || descriptor->fontWeight >= 600;
        const bool isItalic = fontName.contains(QLatin1String("Italic"), Qt::CaseInsensitive) || fontName.contains(QLatin1String("Oblique"), Qt::CaseInsensitive) || descriptor->italicAngle != 0.0;
        QString familyName = fontName.section(QChar('-'), 0, 0).section(QChar(','), 0, 0);
        familyName.replace(QRegularExpression(QStringLiteral("(MT|PS|Std|PSMT)$")), QString());

        QStringList candidates = { familyName, QString::fromLatin1(descriptor->fontFamily) };
        // Common fonts of Windows documents and their metric-compatible free twins
        static const std::pair<const char*, const char*> twins[] = {
            { "Calibri", "Carlito" }, { "Cambria", "Caladea" }, { "Arial", "Liberation Sans" },
            { "TimesNewRoman", "Liberation Serif" }, { "Times New Roman", "Liberation Serif" },
            { "CourierNew", "Liberation Mono" }, { "Courier New", "Liberation Mono" }, { "Helvetica", "Liberation Sans" }
        };
        for (const auto& [windowsName, twin] : twins)
        {
            if (familyName.compare(QLatin1String(windowsName), Qt::CaseInsensitive) == 0)
            {
                candidates << QLatin1String(twin);
            }
        }

        bool isFontFound = false;
        for (const QString& candidate : candidates)
        {
            if (candidate.isEmpty())
            {
                continue;
            }
            QFont systemFont(candidate);
            if (QFontInfo(systemFont).family().compare(candidate, Qt::CaseInsensitive) == 0)
            {
                font = systemFont;
                isFontFound = true;
                break;
            }
        }

        if (!isFontFound)
        {
            const bool isSerif = descriptor->flags & 2;
            const bool isFixedPitch = descriptor->flags & 1;
            font = QFont(isFixedPitch ? QStringLiteral("Monospace") : (isSerif ? QStringLiteral("Serif") : QStringLiteral("Sans Serif")));
        }

        font.setBold(isBold);
        font.setItalic(isItalic);
    }

    return font;
}

bool EditorPlugin::startInlineTextEdit(pdf::PDFPageContentElement* element)
{
    // PDF Fire: a line of a paragraph is edited with the whole paragraph (reflow)
    if (startInlineParagraphEdit(element))
    {
        return true;
    }

    InlineLine line;
    if (!element || !getJoinedInlineLine(element, &line))
    {
        // Not a single line of horizontal text - a line with several lines (T*) is
        // still edited alone, if it is horizontal
        const pdf::PDFPageContentElementEdited* editedElement = element ? element->asElementEdited() : nullptr;
        const pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
        if (!textElement)
        {
            return false;
        }

        line.id = element->getElementId();
        line.pageIndex = element->getPageIndex();
        line.rect = element->getBoundingBox().normalized();
        line.text = pdf::PDFEditedTextMarkup(textElement->getItemsAsText()).getPlainText();
        line.font = textElement->getState().getTextFont();
    }

    // The text of the line (the texts of the line joined, if it is written as several)
    const pdf::PDFPageContentElement* lineElement = line.joinedElement ? line.joinedElement.get() : element;
    const pdf::PDFPageContentElementEdited* editedElement = lineElement->asElementEdited();
    const pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
    if (!textElement)
    {
        return false;
    }

    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    const pdf::PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const pdf::PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(element->getPageIndex());
    if (!pageSnapshot)
    {
        return false;
    }

    // Only a text written horizontally on the screen is edited in place
    const QTransform& pageToDevice = pageSnapshot->pageToDeviceMatrix;
    const QTransform textTransform = textElement->getState().getTextMatrix() * textElement->getTransform();
    const QTransform textToDevice = textTransform * pageToDevice;
    if (qAbs(textToDevice.m12()) > 0.01 * qAbs(textToDevice.m11()) || textToDevice.m11() <= 0.0 || textToDevice.m22() >= 0.0)
    {
        return false;
    }

    const pdf::PDFEditedTextMarkup markup(textElement->getItemsAsText());
    const QString plainText = markup.getPlainText();

    // The font of the text: the font program embedded in the document, if it can be
    // used, otherwise a similar font of the system
    pdf::PDFFontPointer pdfFont = textElement->getState().getTextFont();
    for (const pdf::PDFEditedPageContentElementText::FontResource& fontResource : textElement->getFontResources())
    {
        if (pdfFont)
        {
            break;
        }
        pdfFont = fontResource.font;
    }

    QFont font = getInlineEditorFont(pdfFont);

    // PDF Fire: the text itself is shown by the page (the real glyphs), the editor only
    // places the caret - its font has the size of the font of the text, so the caret is
    // as high as the line of the text
    const qreal fontSize = markup.getFirstFontSize() > 0.0 ? markup.getFirstFontSize() : textElement->getState().getTextFontSize();
    const qreal textScale = std::hypot(textTransform.m21(), textTransform.m22());
    const qreal deviceScale = std::hypot(pageToDevice.m21(), pageToDevice.m22());
    font.setPixelSize(qMax(4, qRound(fontSize * (textScale > 0.0 ? textScale : 1.0) * deviceScale)));

    QWidget* drawWidget = m_widget->getDrawWidget()->getWidget();
    pdf::PDFFireInlineTextEditor* editor = new pdf::PDFFireInlineTextEditor(drawWidget);
    m_inlineEditor = editor;

    const QRect deviceRect = pageToDevice.mapRect(lineElement->getBoundingBox()).toAlignedRect();

    InlineEditSession session;
    session.isParagraph = false;
    session.paragraph.lines = { line };
    session.paragraph.bodyLeft = line.rect.left();
    session.paragraph.maxRight = line.rect.right();
    session.paragraph.pitch = line.rect.height();
    session.originalLines = QStringList{ plainText };
    session.editorFont = font;
    session.deviceTextRect = deviceRect;

    // The text of a PDF is often set tighter or looser than the natural spacing of the
    // font (Word writes the kerning as small moves). The letter spacing of the editor is
    // adjusted, so the line is as wide as on the page and the caret follows the glyphs.
    // It is adjusted again with each change of the text (updateInlinePreview).
    const qreal naturalWidth = QFontMetricsF(font).horizontalAdvance(plainText);
    if (naturalWidth > 0.0 && plainText.size() > 1)
    {
        const qreal difference = deviceRect.width() - naturalWidth;
        if (qAbs(difference) < naturalWidth * 0.3)
        {
            font.setLetterSpacing(QFont::AbsoluteSpacing, difference / plainText.size());
        }
    }

    editor->setPreviewMode(true);
    editor->start(plainText, font, deviceRect, drawWidget->mapFromGlobal(QCursor::pos()));

    connect(editor, &pdf::PDFFireInlineTextEditor::committed, this, [this](const QString& text) { onInlineEditCommitted(QStringList{ text }); });
    connect(editor, &pdf::PDFFireInlineTextEditor::cancelled, this, &EditorPlugin::endInlineEdit);
    connect(editor, &QPlainTextEdit::textChanged, &m_previewTimer, qOverload<>(&QTimer::start));

    // Zooming or scrolling moves the text - the editing finishes then
    connect(proxy, &pdf::PDFDrawWidgetProxy::drawSpaceChanged, editor, &pdf::PDFFireInlineTextEditor::finish);

    beginInlineEdit(std::move(session), editor);

    if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
    {
        mainWindow->statusBar()->showMessage(m_scene.isTextEditMode() ? tr("Type to change the text. Enter or a click outside keeps the change, Esc cancels. "
                                                                           "Shift + click opens the text properties (font size, position).")
                                                                        : tr("Type to change the text. Enter or a click outside keeps the change, Esc cancels. "
                                                                           "Shift + double-click opens the text properties (font size, position)."), 15000);
    }

    return true;
}

bool EditorPlugin::getInlineLine(const pdf::PDFPageContentElement* element, InlineLine* line) const
{
    const pdf::PDFPageContentElementEdited* editedElement = element ? element->asElementEdited() : nullptr;
    const pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
    if (!textElement)
    {
        return false;
    }

    // Only horizontal text, upright in the page, of one line
    const QTransform textTransform = textElement->getState().getTextMatrix() * textElement->getTransform();
    if (qAbs(textTransform.m12()) > 0.01 * qAbs(textTransform.m11()) || qAbs(textTransform.m21()) > 0.01 * qAbs(textTransform.m22()) ||
        textTransform.m11() <= 0.0 || textTransform.m22() <= 0.0)
    {
        return false;
    }

    const pdf::PDFEditedTextMarkup markup(textElement->getItemsAsText());
    const QString text = markup.getPlainText();
    if (text.contains(QChar('\n')) || text.trimmed().isEmpty())
    {
        return false;
    }

    pdf::PDFFontPointer font = textElement->getState().getTextFont();
    for (const pdf::PDFEditedPageContentElementText::FontResource& fontResource : textElement->getFontResources())
    {
        if (font)
        {
            break;
        }
        font = fontResource.font;
    }

    line->id = element->getElementId();
    line->pageIndex = element->getPageIndex();
    line->rect = element->getBoundingBox().normalized();
    line->text = text;
    line->fontName = font ? font->getFontDescriptor()->fontName : QByteArray();
    line->font = font;
    return !line->rect.isEmpty();
}

std::vector<pdf::PDFInteger> EditorPlugin::InlineLine::getElementIds() const
{
    std::vector<pdf::PDFInteger> ids = { id };
    ids.insert(ids.end(), fragmentIds.cbegin(), fragmentIds.cend());
    return ids;
}

bool EditorPlugin::InlineLine::containsElement(pdf::PDFInteger elementId) const
{
    return id == elementId || std::find(fragmentIds.cbegin(), fragmentIds.cend(), elementId) != fragmentIds.cend();
}

std::vector<EditorPlugin::InlineLine> EditorPlugin::getPageLines(pdf::PDFInteger pageIndex) const
{
    std::vector<InlineLine> fragments;
    const auto elementsByPage = m_scene.getElementsByPage();
    auto it = elementsByPage.find(pageIndex);
    if (it == elementsByPage.cend())
    {
        return fragments;
    }
    for (const pdf::PDFPageContentElement* pageElement : it->second)
    {
        InlineLine line;
        if (getInlineLine(pageElement, &line))
        {
            fragments.push_back(line);
        }
    }

    // The texts of one line: their boxes overlap vertically (the box of a smaller text,
    // for example of a hyphen, is inside the box of the bigger one), each one starting
    // at most a word gap after the text before it (PdfCraft joins texts of other text
    // objects within 1.5 font size, Pen::continues in crates/edit/src/text.rs, MIT OR
    // Apache-2.0). The boxes are the ink of the glyphs - the exact baseline is checked,
    // when the texts are joined (joinLineFragments).
    std::stable_sort(fragments.begin(), fragments.end(), [](const InlineLine& a, const InlineLine& b) { return a.rect.left() < b.rect.left(); });
    std::vector<std::vector<size_t>> rows;
    for (size_t i = 0; i < fragments.size(); ++i)
    {
        const QRectF& rect = fragments[i].rect;
        std::vector<size_t>* bestRow = nullptr;
        qreal bestGap = std::numeric_limits<qreal>::infinity();
        for (std::vector<size_t>& row : rows)
        {
            // The height of the row is the height of its highest text
            QRectF rowRect;
            for (size_t index : row)
            {
                rowRect = rowRect.united(fragments[index].rect);
            }
            const QRectF& last = fragments[row.back()].rect;
            const QRectF& smaller = (rect.height() < rowRect.height()) ? rect : rowRect;
            const QRectF& bigger = (rect.height() < rowRect.height()) ? rowRect : rect;
            const qreal height = bigger.height();
            const qreal overlap = qMin(rect.bottom(), rowRect.bottom()) - qMax(rect.top(), rowRect.top());
            const bool isSmallerInside = smaller.center().y() > bigger.top() && smaller.center().y() < bigger.bottom();
            const qreal gap = rect.left() - last.right();
            if (height <= 0.0 || !isSmallerInside || overlap < 0.5 * smaller.height() || gap < -0.3 * height || gap > 1.5 * height)
            {
                continue;
            }
            if (qAbs(gap) < bestGap)
            {
                bestGap = qAbs(gap);
                bestRow = &row;
            }
        }

        if (bestRow)
        {
            bestRow->push_back(i);
        }
        else
        {
            rows.push_back({ i });
        }
    }

    auto getText = [this, &fragments](size_t index) -> const pdf::PDFEditedPageContentElementText*
    {
        const pdf::PDFPageContentElement* fragmentElement = m_scene.getElementById(fragments[index].id);
        const pdf::PDFPageContentElementEdited* editedElement = fragmentElement ? fragmentElement->asElementEdited() : nullptr;
        return editedElement ? editedElement->getElement()->asText() : nullptr;
    };

    // The texts of a row are joined, while they can be (a text on another baseline,
    // for example a superscript, starts a line of its own)
    std::vector<InlineLine> lines;
    auto addLine = [&](const std::vector<size_t>& chain, std::unique_ptr<pdf::PDFEditedPageContentElementText> joinedText)
    {
        InlineLine line = fragments[chain.front()];
        if (chain.size() > 1 && joinedText)
        {
            std::unique_ptr<pdf::PDFPageContentElement> joinedElement(m_scene.getElementById(line.id)->clone());
            pdf::PDFPageContentElementEdited* joinedEditedElement = dynamic_cast<pdf::PDFPageContentElementEdited*>(joinedElement.get());
            if (joinedEditedElement && joinedEditedElement->getElement()->asText())
            {
                *joinedEditedElement->getElement()->asText() = *joinedText;
                for (size_t i = 1; i < chain.size(); ++i)
                {
                    line.fragmentIds.push_back(fragments[chain[i]].id);
                    line.rect = line.rect.united(fragments[chain[i]].rect);
                }
                line.text = pdf::PDFEditedTextMarkup(joinedText->getItemsAsText()).getPlainText();
                line.joinedElement.reset(joinedElement.release());
                lines.push_back(std::move(line));
                return;
            }
        }

        for (size_t index : chain)
        {
            lines.push_back(fragments[index]);
        }
    };

    for (const std::vector<size_t>& row : rows)
    {
        std::vector<size_t> chain = { row.front() };
        std::unique_ptr<pdf::PDFEditedPageContentElementText> joinedText;
        for (size_t i = 1; i < row.size(); ++i)
        {
            std::vector<const pdf::PDFEditedPageContentElementText*> texts;
            for (size_t index : chain)
            {
                texts.push_back(getText(index));
            }
            texts.push_back(getText(row[i]));

            std::unique_ptr<pdf::PDFEditedPageContentElementText> candidate = pdf::PDFFireTextElementUpdater::joinLineFragments(texts);
            if (candidate && !pdf::PDFEditedTextMarkup(candidate->getItemsAsText()).getPlainText().contains(QChar('\n')))
            {
                chain.push_back(row[i]);
                joinedText = std::move(candidate);
            }
            else
            {
                addLine(chain, std::move(joinedText));
                chain = { row[i] };
                joinedText.reset();
            }
        }
        addLine(chain, std::move(joinedText));
    }

    return lines;
}

bool EditorPlugin::getJoinedInlineLine(const pdf::PDFPageContentElement* element, InlineLine* line) const
{
    if (!element)
    {
        return false;
    }

    for (InlineLine& pageLine : getPageLines(element->getPageIndex()))
    {
        if (pageLine.containsElement(element->getElementId()))
        {
            *line = std::move(pageLine);
            return true;
        }
    }

    return false;
}

bool EditorPlugin::findParagraph(const pdf::PDFPageContentElement* element, Paragraph* paragraph) const
{
    if (!element)
    {
        return false;
    }

    // The lines of the page, which can be a part of the paragraph
    const std::vector<InlineLine> pageLines = getPageLines(element->getPageIndex());
    auto clickedIt = std::find_if(pageLines.cbegin(), pageLines.cend(), [element](const InlineLine& line) { return line.containsElement(element->getElementId()); });
    if (clickedIt == pageLines.cend())
    {
        return false;
    }
    const InlineLine clicked = *clickedIt;

    std::vector<InlineLine> lines;
    for (const InlineLine& line : pageLines)
    {
        if (line.fontName == clicked.fontName && qAbs(line.rect.height() - clicked.rect.height()) <= clicked.rect.height() * 0.3)
        {
            lines.push_back(line);
        }
    }

    const qreal height = clicked.rect.height();
    auto findNeighbour = [&](const InlineLine& from, bool below, qreal pitch) -> const InlineLine*
    {
        const InlineLine* best = nullptr;
        qreal bestDistance = std::numeric_limits<qreal>::infinity();
        for (const InlineLine& line : lines)
        {
            // The y axis of a page goes up - a line below has a smaller y
            const qreal distance = below ? from.rect.center().y() - line.rect.center().y() : line.rect.center().y() - from.rect.center().y();
            const bool isOverlapping = line.rect.left() < from.rect.right() && line.rect.right() > from.rect.left();
            if (line.id == from.id || !isOverlapping || distance < 0.8 * height || distance > 2.2 * height)
            {
                continue;
            }
            if (pitch > 0.0 && qAbs(distance - pitch) > 0.15 * pitch)
            {
                continue;
            }
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &line;
            }
        }
        return best;
    };

    // The column: the lines above and below with the same left margin (the first line
    // of a paragraph can be indented), at the same distance
    std::vector<InlineLine> column = { clicked };
    qreal pitch = 0.0;
    qreal bodyLeft = clicked.rect.left();
    bool isBodyLeftKnown = false;

    while (const InlineLine* next = findNeighbour(column.back(), true, pitch))
    {
        const qreal left = next->rect.left();
        if (isBodyLeftKnown ? qAbs(left - bodyLeft) > 3.0 : (left > bodyLeft + 3.0 || bodyLeft - left > 40.0))
        {
            break;
        }
        if (!isBodyLeftKnown)
        {
            bodyLeft = left;
            isBodyLeftKnown = true;
        }
        if (pitch == 0.0)
        {
            pitch = column.back().rect.center().y() - next->rect.center().y();
        }
        column.push_back(*next);
    }

    while (const InlineLine* previous = findNeighbour(column.front(), false, pitch))
    {
        const qreal left = previous->rect.left();
        if (pitch == 0.0)
        {
            pitch = previous->rect.center().y() - column.front().rect.center().y();
        }
        if (qAbs(left - bodyLeft) <= 3.0 || !isBodyLeftKnown && qAbs(left - column.front().rect.left()) <= 3.0)
        {
            column.insert(column.begin(), *previous);
            continue;
        }
        if (left > bodyLeft + 3.0 && left <= bodyLeft + 40.0)
        {
            // An indented first line of a paragraph
            column.insert(column.begin(), *previous);
        }
        break;
    }

    if (column.size() < 2 || pitch <= 0.0)
    {
        // A line alone - edited as a line
        return false;
    }

    if (!isBodyLeftKnown)
    {
        bodyLeft = std::min_element(column.cbegin(), column.cend(), [](const auto& a, const auto& b) { return a.rect.left() < b.rect.left(); })->rect.left();
    }

    qreal maxRight = 0.0;
    for (const InlineLine& line : column)
    {
        maxRight = qMax(maxRight, line.rect.right());
    }

    // The paragraphs of the column: a paragraph ends with a short line, or before an
    // indented line
    const qreal width = maxRight - bodyLeft;
    const qreal shortLineLimit = maxRight - qMax(15.0, width * 0.12);
    size_t start = 0;
    for (size_t i = 0; i < column.size(); ++i)
    {
        const bool isLast = (i + 1 == column.size());
        const bool isEnd = isLast || column[i].rect.right() < shortLineLimit || column[i + 1].rect.left() > bodyLeft + 3.0;
        if (!isEnd)
        {
            continue;
        }

        const bool containsClicked = std::any_of(column.cbegin() + start, column.cbegin() + i + 1, [&clicked](const InlineLine& line) { return line.id == clicked.id; });
        if (containsClicked)
        {
            paragraph->lines.assign(column.cbegin() + start, column.cbegin() + i + 1);
            break;
        }
        start = i + 1;
    }

    paragraph->bodyLeft = bodyLeft;
    paragraph->maxRight = maxRight;
    paragraph->pitch = pitch;
    return !paragraph->lines.empty();
}

bool EditorPlugin::startInlineParagraphEdit(pdf::PDFPageContentElement* element)
{
    Paragraph paragraph;
    if (!findParagraph(element, &paragraph))
    {
        return false;
    }

    pdf::PDFDrawWidgetProxy* proxy = m_widget->getDrawWidgetProxy();
    const pdf::PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const pdf::PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(element->getPageIndex());
    if (!pageSnapshot)
    {
        return false;
    }

    const QTransform& pageToDevice = pageSnapshot->pageToDeviceMatrix;
    if (qAbs(pageToDevice.m12()) > 0.001 || qAbs(pageToDevice.m21()) > 0.001)
    {
        // A rotated page - the lines are edited one by one
        return false;
    }

    // The text: the lines joined by spaces (a word broken by a hyphen stays together)
    QString text;
    for (size_t i = 0; i < paragraph.lines.size(); ++i)
    {
        const QString& lineText = paragraph.lines[i].text;
        if (i > 0)
        {
            const bool isHyphenated = text.endsWith(QChar('-')) && !lineText.isEmpty() && lineText.front().isLower();
            if (!isHyphenated)
            {
                text += QChar(' ');
            }
        }
        text += lineText.trimmed();
    }

    // The font and its size from the first line (as for one line), the letter spacing
    // from the full lines (the last line of a paragraph is usually short)
    const InlineLine& firstLine = paragraph.lines.front();
    QFont font = getInlineEditorFont(firstLine.font);
    const qreal deviceScale = std::hypot(pageToDevice.m11(), pageToDevice.m12());
    const QRectF firstDeviceRect = pageToDevice.mapRect(firstLine.rect);
    font.setPixelSize(qMax(4, qRound(firstDeviceRect.height() * 0.9)));
    const qreal inkHeight = QFontMetricsF(font).tightBoundingRect(firstLine.text).height();
    if (inkHeight > 0.0)
    {
        font.setPixelSize(qMax(4, qRound(font.pixelSize() * qBound(0.5, firstDeviceRect.height() / inkHeight, 2.0))));
    }

    qreal naturalWidth = 0.0;
    qreal pageWidth = 0.0;
    int characterCount = 0;
    for (size_t i = 0; i + 1 < paragraph.lines.size() || (paragraph.lines.size() == 1 && i == 0); ++i)
    {
        naturalWidth += QFontMetricsF(font).horizontalAdvance(paragraph.lines[i].text);
        pageWidth += paragraph.lines[i].rect.width() * deviceScale;
        characterCount += int(paragraph.lines[i].text.size());
    }
    if (naturalWidth > 0.0 && characterCount > 1 && qAbs(pageWidth - naturalWidth) < naturalWidth * 0.3)
    {
        font.setLetterSpacing(QFont::AbsoluteSpacing, (pageWidth - naturalWidth) / characterCount);
    }

    // The editor covers the paragraph; its lines are as wide as the column
    const QRectF paragraphRect(QPointF(paragraph.bodyLeft, paragraph.lines.back().rect.bottom()), QPointF(paragraph.maxRight, firstLine.rect.top()));
    QRect deviceRect = pageToDevice.mapRect(paragraphRect.normalized()).toAlignedRect();
    deviceRect.setTop(qRound(firstDeviceRect.top() - (paragraph.pitch * deviceScale - firstDeviceRect.height()) * 0.5));
    const qreal lineHeight = paragraph.pitch * deviceScale;
    const qreal firstLineIndent = (firstLine.rect.left() - paragraph.bodyLeft) * deviceScale;

    QWidget* drawWidget = m_widget->getDrawWidget()->getWidget();
    pdf::PDFFireInlineParagraphEditor* editor = new pdf::PDFFireInlineParagraphEditor(drawWidget);
    m_inlineParagraphEditor = editor;

    InlineEditSession session;
    session.isParagraph = true;
    session.paragraph = paragraph;
    for (const InlineLine& line : paragraph.lines)
    {
        session.originalLines << line.text.trimmed();
    }
    session.editorFont = font;
    session.deviceTextRect = deviceRect;

    editor->setPreviewMode(true);
    editor->start(text, font, deviceRect, lineHeight, firstLineIndent, drawWidget->mapFromGlobal(QCursor::pos()));

    connect(editor, &pdf::PDFFireInlineParagraphEditor::committed, this, &EditorPlugin::onInlineEditCommitted);
    connect(editor, &pdf::PDFFireInlineParagraphEditor::cancelled, this, &EditorPlugin::endInlineEdit);
    connect(editor, &QTextEdit::textChanged, &m_previewTimer, qOverload<>(&QTimer::start));
    connect(proxy, &pdf::PDFDrawWidgetProxy::drawSpaceChanged, editor, &pdf::PDFFireInlineParagraphEditor::finish);

    beginInlineEdit(std::move(session), editor);

    if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
    {
        mainWindow->statusBar()->showMessage(m_scene.isTextEditMode() ? tr("Editing the paragraph: the lines are rewrapped as you type. Enter starts a new line; a click outside "
                                                                           "or Ctrl+Enter keeps the change, Esc cancels. Shift + click edits one line in the dialog.")
                                                                        : tr("Editing the paragraph: the lines are rewrapped as you type. Enter starts a new line; a click outside "
                                                                           "or Ctrl+Enter keeps the change, Esc cancels. Shift + double-click edits one line in the dialog."), 20000);
    }
    return true;
}

void EditorPlugin::beginInlineEdit(InlineEditSession session, QWidget* editor)
{
    Q_UNUSED(editor);

    m_inlineSession = std::move(session);
    m_inlineSession.isActive = true;
    m_changedLines.clear();

    // The text under the editor is the preview from the beginning - the original glyphs
    updateInlinePreview();
}

QStringList EditorPlugin::getInlineEditorLines() const
{
    if (m_inlineSession.isParagraph)
    {
        return m_inlineParagraphEditor ? m_inlineParagraphEditor->getLines() : QStringList();
    }

    return m_inlineEditor ? QStringList{ m_inlineEditor->toPlainText() } : QStringList();
}

const EditorPlugin::ChangedLine* EditorPlugin::getChangedLine(const InlineLine& sourceLine, const QString& text, QPointF offset)
{
    const ChangedLineKey key(sourceLine.id, offset.x(), offset.y(), text);
    auto it = m_changedLines.find(key);
    if (it != m_changedLines.cend())
    {
        return &it->second;
    }

    if (m_changedLines.size() > 256)
    {
        // The lines of the previous keystrokes are not needed any more
        m_changedLines.clear();
    }

    // PDF Fire: a line written as several texts is written as one text (its first element)
    ChangedLine changedLine;
    const pdf::PDFPageContentElement* source = sourceLine.joinedElement ? sourceLine.joinedElement.get() : m_scene.getElementById(sourceLine.id);
    std::unique_ptr<pdf::PDFPageContentElement> element(source ? source->clone() : nullptr);
    pdf::PDFPageContentElementEdited* editedElement = dynamic_cast<pdf::PDFPageContentElementEdited*>(element.get());
    pdf::PDFEditedPageContentElementText* textElement = editedElement ? editedElement->getElement()->asText() : nullptr;
    if (textElement && m_editedPageContent.count(element->getPageIndex()))
    {
        if (!offset.isNull())
        {
            // A new line is a copy of a line of the paragraph, moved down
            textElement->setTransform(textElement->getTransform() * QTransform::fromTranslate(offset.x(), offset.y()));
        }

        if (qEnvironmentVariableIsSet("PDFFIRE_DEBUG_TEXT"))
        {
            // Development aid: the markup before and after the edit
            qInfo().noquote() << "PDFFIRE_DEBUG_TEXT before:" << textElement->getItemsAsText();
            qInfo().noquote() << "PDFFIRE_DEBUG_TEXT after: " << pdf::PDFEditedTextMarkup(textElement->getItemsAsText()).getMarkupWithPlainText(text);
        }

        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        if (pdf::PDFFireTextElementUpdater::setPlainText(m_document, element->getPageIndex(), m_editedPageContent.at(element->getPageIndex()),
                                                         textElement, text, cms.data(), &changedLine.errors))
        {
            changedLine.element.reset(element.release());
        }
    }

    return &m_changedLines.emplace(key, std::move(changedLine)).first->second;
}

EditorPlugin::TextEditResult EditorPlugin::createInlineEdit(const QStringList& newLines)
{
    TextEditResult result;
    const Paragraph& paragraph = m_inlineSession.paragraph;
    const QStringList& originalLines = m_inlineSession.originalLines;
    if (paragraph.lines.empty())
    {
        result.isFailed = true;
        return result;
    }

    auto addChangedLine = [&](const InlineLine& sourceLine, const QString& text, QPointF offset, bool isAdded)
    {
        const ChangedLine* changedLine = getChangedLine(sourceLine, text, offset);
        if (!changedLine->element)
        {
            result.isFailed = true;
            return;
        }

        (isAdded ? result.addedElements : result.replacedElements).push_back(changedLine->element);
        result.errors << changedLine->errors;

        // The other texts of a joined line are in the changed text now
        if (!isAdded)
        {
            result.removedElementIds.insert(result.removedElementIds.end(), sourceLine.fragmentIds.cbegin(), sourceLine.fragmentIds.cend());
        }
    };
    auto removeLine = [&](const InlineLine& line)
    {
        const std::vector<pdf::PDFInteger> ids = line.getElementIds();
        result.removedElementIds.insert(result.removedElementIds.end(), ids.cbegin(), ids.cend());
    };

    const size_t oldCount = paragraph.lines.size();
    const size_t newCount = size_t(newLines.size());
    const InlineLine& lastLine = paragraph.lines.back();
    for (size_t i = 0; i < qMax(oldCount, newCount); ++i)
    {
        if (i < oldCount && i < newCount)
        {
            if (newLines[i] == originalLines[i])
            {
                continue;
            }
            if (newLines[i].trimmed().isEmpty())
            {
                // An emptied line is removed (an empty text can't be written)
                removeLine(paragraph.lines[i]);
                continue;
            }
            addChangedLine(paragraph.lines[i], newLines[i], QPointF(), false);
        }
        else if (i < oldCount)
        {
            removeLine(paragraph.lines[i]);
        }
        else if (!newLines[i].trimmed().isEmpty())
        {
            // A new line below the last line; it starts at the left margin of the paragraph
            const qreal dx = paragraph.bodyLeft - lastLine.rect.left();
            const qreal dy = -paragraph.pitch * qreal(i - (oldCount - 1));
            addChangedLine(lastLine, newLines[i], QPointF(dx, dy), true);
        }
    }

    result.errors.removeDuplicates();
    return result;
}

bool EditorPlugin::applyTextEditResult(const TextEditResult& result)
{
    if (result.isFailed)
    {
        return false;
    }

    if (!result.errors.isEmpty() && !confirmContentStreamErrors(result.errors, m_inlineSession.paragraph.lines.front().pageIndex))
    {
        return false;
    }

    std::vector<pdf::PDFPageContentElement*> replacedElements;
    std::vector<pdf::PDFPageContentElement*> addedElements;
    for (const auto& element : result.replacedElements)
    {
        replacedElements.push_back(element->clone());
    }
    for (const auto& element : result.addedElements)
    {
        addedElements.push_back(element->clone());
    }

    // All the lines are changed as one step of undo
    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        m_scene.clearTransientPreview();
        if (!replacedElements.empty())
        {
            m_scene.replaceElements(replacedElements);
        }
        if (!addedElements.empty())
        {
            m_scene.addElements(addedElements);
        }
        if (!result.removedElementIds.empty())
        {
            m_scene.removeElementsById(result.removedElementIds);
        }
    }
    createUndoStep();
    updateActions();
    updateGraphics();
    return true;
}

void EditorPlugin::updateInlinePreview()
{
    m_previewTimer.stop();
    if (!m_inlineSession.isActive)
    {
        return;
    }

    const TextEditResult result = createInlineEdit(getInlineEditorLines());
    const Paragraph& paragraph = m_inlineSession.paragraph;

    if (result.isFailed)
    {
        // The text can't be written (so it can't be shown with the glyphs of the
        // document) - the editor shows it in a font of the system, over the original
        m_scene.clearTransientPreview();
        if (m_inlineEditor)
        {
            m_inlineEditor->setPreviewMode(false);
        }
        if (m_inlineParagraphEditor)
        {
            m_inlineParagraphEditor->setPreviewMode(false);
        }
        updateTextOutlines();
        updateGraphics();
        return;
    }

    // The changed and the removed texts are hidden, the changed ones are shown as
    // they will be written instead (the unchanged lines stay, as they are)
    std::set<pdf::PDFInteger> hiddenElementIds(result.removedElementIds.cbegin(), result.removedElementIds.cend());
    std::vector<std::unique_ptr<pdf::PDFPageContentElement>> previewElements;
    for (const auto& element : result.replacedElements)
    {
        hiddenElementIds.insert(element->getElementId());
        previewElements.emplace_back(element->clone());
    }
    for (const auto& element : result.addedElements)
    {
        previewElements.emplace_back(element->clone());
    }

    // The caret of the editor of one line follows the real glyphs: the letter spacing
    // of the editor is set, so the text in the editor is as wide as the written text
    // (the ink of both is compared, so a space at the end does not count)
    if (!m_inlineSession.isParagraph && m_inlineEditor && !previewElements.empty())
    {
        const pdf::PDFWidgetSnapshot snapshot = m_widget->getDrawWidgetProxy()->getSnapshot();
        const pdf::PDFWidgetSnapshot::SnapshotItem* pageSnapshot = snapshot.getPageSnapshot(paragraph.lines.front().pageIndex);
        const QString text = m_inlineEditor->toPlainText();
        const QFontMetricsF metrics(m_inlineSession.editorFont);
        const qreal naturalWidth = metrics.tightBoundingRect(text).width();
        const int gapCount = int(text.trimmed().size()) - 1;
        if (pageSnapshot && naturalWidth > 0.0 && gapCount > 0)
        {
            const qreal previewWidth = pageSnapshot->pageToDeviceMatrix.mapRect(previewElements.front()->getBoundingBox()).width();
            const qreal spacing = (previewWidth - naturalWidth) / gapCount;
            if (qAbs(spacing) * gapCount < naturalWidth * 0.3)
            {
                QFont font = m_inlineSession.editorFont;
                font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
                if (font != m_inlineEditor->font())
                {
                    m_inlineEditor->setFont(font);
                }
            }
        }
    }

    if (m_inlineEditor)
    {
        m_inlineEditor->setPreviewMode(true);
    }
    if (m_inlineParagraphEditor)
    {
        m_inlineParagraphEditor->setPreviewMode(true);
    }
    m_scene.setTransientPreview(std::move(hiddenElementIds), std::move(previewElements));
    updateTextOutlines();
    updateGraphics();
}

void EditorPlugin::onInlineEditCommitted(const QStringList& lines)
{
    if (!m_inlineSession.isActive || m_isCommittingInlineEdit)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isCommittingInlineEdit, true);
    m_previewTimer.stop();

    bool isApplied = true;
    if (lines != m_inlineSession.originalLines)
    {
        // Development aid: the failure of an edit (the typed text must not be lost)
        const bool isFailureSimulated = qEnvironmentVariableIsSet("PDFFIRE_DEBUG_FAIL_TEXT_EDIT");
        isApplied = !isFailureSimulated && applyTextEditResult(createInlineEdit(lines));
    }

    if (!isApplied)
    {
        // PDF Fire: the typed text is never lost - the editor stays open with it
        updateInlinePreview();
        QMessageBox::warning(m_widget, tr("Edit Text"), tr("The text could not be changed. Your text is kept in the editor - "
                                                           "change it and try again, or press Esc to discard it."));
        if (m_inlineEditor)
        {
            m_inlineEditor->resume();
        }
        if (m_inlineParagraphEditor)
        {
            m_inlineParagraphEditor->resume();
        }
        return;
    }

    endInlineEdit();
}

void EditorPlugin::endInlineEdit()
{
    m_previewTimer.stop();
    m_inlineSession = InlineEditSession();
    m_changedLines.clear();
    m_scene.clearTransientPreview();

    for (QWidget* editor : { static_cast<QWidget*>(m_inlineEditor.data()), static_cast<QWidget*>(m_inlineParagraphEditor.data()) })
    {
        if (editor)
        {
            editor->disconnect(this);
            editor->hide();
            editor->deleteLater();
        }
    }
    m_inlineEditor = nullptr;
    m_inlineParagraphEditor = nullptr;

    updateTextOutlines();
    updateGraphics();
}

bool EditorPlugin::finishInlineEdit()
{
    if (m_inlineEditor)
    {
        m_inlineEditor->finish();
    }
    if (m_inlineParagraphEditor)
    {
        m_inlineParagraphEditor->finish();
    }
    return !m_inlineSession.isActive;
}

void EditorPlugin::onEditTextTriggered(bool checked)
{
    if (checked && !m_scene.isActive())
    {
        // The Edit Text tool works on the content of the page - the page content
        // editing is switched on silently
        setActive(true);
        if (!m_scene.isActive())
        {
            m_actions[EditText]->setChecked(false);
            return;
        }
        m_isSceneActivatedByEditText = true;
    }

    if (!checked && m_isSceneActivatedByEditText && m_scene.isActive())
    {
        // The page content editing was switched on by the Edit Text tool, so it ends
        // with it (the changes are written to the document, as when the page content
        // editing is switched off)
        setTextEditMode(false);
        if (m_inlineSession.isActive)
        {
            // The text being edited can't be applied - the editing goes on
            setTextEditMode(true);
            return;
        }

        onSetActive(false);
        if (m_scene.isActive())
        {
            // Cancelled by the user
            setTextEditMode(true);
        }
        return;
    }

    setTextEditMode(checked);
}

void EditorPlugin::setTextEditMode(bool enabled)
{
    if (!enabled)
    {
        finishInlineEdit();
    }
    else if (m_widget->getToolManager()->getActiveTool())
    {
        // A click on the page edits the text, the tools are switched off
        m_widget->getToolManager()->setActiveTool(nullptr);
    }

    m_scene.setTextEditMode(enabled);
    m_actions[EditText]->setChecked(enabled);
    m_hoveredTextElementId = pdf::PDFPageContentScene::INVALID_ELEMENT_ID;
    updateTextOutlines();
    updateGraphics();

    if (enabled)
    {
        if (QMainWindow* mainWindow = m_dataExchangeInterface->getMainWindow())
        {
            mainWindow->statusBar()->showMessage(tr("Edit Text: click a text on the page to change it. The changes are written to the document when it is saved."), 15000);
        }
    }
}

void EditorPlugin::onTextElementHovered(pdf::PDFInteger elementId)
{
    m_hoveredTextElementId = elementId;
    updateTextOutlines();
    updateGraphics();
}

void EditorPlugin::updateTextOutlines()
{
    std::vector<pdf::PDFPageContentScene::TextOutline> outlines;

    if (m_inlineSession.isActive)
    {
        // The frame of the edited text: the original lines and the preview
        const Paragraph& paragraph = m_inlineSession.paragraph;
        QRectF rect;
        for (const InlineLine& line : paragraph.lines)
        {
            rect = rect.united(line.rect);
        }
        rect.setLeft(qMin(rect.left(), paragraph.bodyLeft));
        rect.setRight(qMax(rect.right(), paragraph.maxRight));
        for (const auto& item : m_changedLines)
        {
            const ChangedLine& changedLine = item.second;
            if (changedLine.element && getInlineEditorLines().contains(std::get<3>(item.first)))
            {
                rect = rect.united(changedLine.element->getBoundingBox().normalized());
            }
        }
        outlines.push_back({ paragraph.lines.front().pageIndex, rect, true });
    }

    if (m_scene.isTextEditMode() && m_hoveredTextElementId != pdf::PDFPageContentScene::INVALID_ELEMENT_ID)
    {
        const bool isEdited = m_inlineSession.isActive &&
                              std::any_of(m_inlineSession.paragraph.lines.cbegin(), m_inlineSession.paragraph.lines.cend(),
                                          [this](const InlineLine& line) { return line.containsElement(m_hoveredTextElementId); });
        const pdf::PDFPageContentElement* element = m_scene.getElementById(m_hoveredTextElementId);
        if (element && !isEdited)
        {
            outlines.push_back({ element->getPageIndex(), getEditableTextRect(element), false });
        }
    }

    m_scene.setTextOutlines(std::move(outlines));
}

QRectF EditorPlugin::getEditableTextRect(const pdf::PDFPageContentElement* element) const
{
    Paragraph paragraph;
    if (findParagraph(element, &paragraph))
    {
        QRectF rect;
        for (const InlineLine& line : paragraph.lines)
        {
            rect = rect.united(line.rect);
        }
        rect.setLeft(qMin(rect.left(), paragraph.bodyLeft));
        rect.setRight(qMax(rect.right(), paragraph.maxRight));
        return rect;
    }

    InlineLine line;
    if (getJoinedInlineLine(element, &line))
    {
        return line.rect;
    }

    return element->getBoundingBox().normalized();
}

void EditorPlugin::onSceneEditSingleElement(pdf::PDFInteger elementId)
{
    onSceneEditElement({ elementId });
}

void EditorPlugin::onPenChanged(const QPen& pen)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setPen(pen);
    }
}

void EditorPlugin::onBrushChanged(const QBrush& brush)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setBrush(brush);
    }
}

void EditorPlugin::onFontChanged(const QFont& font)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setFont(font);
    }
}

void EditorPlugin::onAlignmentChanged(Qt::Alignment alignment)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setAlignment(alignment);
    }
}

void EditorPlugin::onTextAngleChanged(pdf::PDFReal angle)
{
    if (pdf::PDFCreatePCElementTool* activeTool = qobject_cast<pdf::PDFCreatePCElementTool*>(getActiveTool()))
    {
        activeTool->setTextAngle(angle);
    }
}

void EditorPlugin::setActive(bool active)
{
    if (m_scene.isActive() != active)
    {
        // Abort active tool, if we are deactivating the plugin
        if (!active)
        {
            // PDF Fire: the text edited in place and the Edit Text mode end with the editing
            endInlineEdit();
            m_scene.setTextEditMode(false);
            m_actions[EditText]->setChecked(false);
            m_isSceneActivatedByEditText = false;

            if (pdf::PDFWidgetTool* tool = m_widget->getToolManager()->getActiveTool())
            {
                auto it = std::find(m_tools.cbegin(), m_tools.cend(), tool);
                if (it != m_tools.cend())
                {
                    m_widget->getToolManager()->setActiveTool(nullptr);
                }
            }
        }

        m_scene.setActive(active);
        if (!active)
        {
            clearUndoRedo();
            m_scene.clear();
            m_editedPageContent.clear();
        }
        else
        {
            clearUndoRedo();
            updateDockWidget();
            pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
            updateEditedPages();
            initializeUndoRedo();
        }

        m_actions[Activate]->setChecked(active);
        updateActions();

        // If editor is not active, remove the widget
        if (m_editorWidget && !active)
        {
            delete m_editorWidget;
            m_editorWidget = nullptr;
        }
    }
}

void EditorPlugin::onSetActive(bool active)
{
    if (m_scene.isActive() && !active && !save())
    {
        updateActions();
        m_actions[Activate]->setChecked(true);
        return;
    }

    setActive(active);
}

void EditorPlugin::onCreateMultipleElementsTriggered(bool enabled)
{
    for (pdf::PDFWidgetTool* tool : m_tools)
    {
        if (pdf::PDFCreatePCElementTool* createTool = qobject_cast<pdf::PDFCreatePCElementTool*>(tool))
        {
            createTool->setMultipleElementCreationEnabled(enabled);
        }
    }
}

void EditorPlugin::readSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.beginGroup("EditorPlugin");
    const bool createMultipleElements = settings.value("CreateMultipleElements", false).toBool();
    settings.endGroup();

    m_actions[CreateMultipleElements]->setChecked(createMultipleElements);
    onCreateMultipleElementsTriggered(createMultipleElements);
}

void EditorPlugin::writeSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.beginGroup("EditorPlugin");
    settings.setValue("CreateMultipleElements", m_actions[CreateMultipleElements]->isChecked());
    settings.endGroup();
}

void EditorPlugin::updateActions()
{
    m_actions[Activate]->setEnabled(m_document);

    if (!m_scene.isActive() || !m_document)
    {
        // Inactive scene - disable all except activate action and the settings
        // of the creation tools, which can be changed at any time
        for (QAction* action : m_actions)
        {
            if (action == m_actions[Activate] || action == m_actions[EditText])
            {
                // PDF Fire: a protected or certified document can't be edited
                const bool canModify = pdf::PDFFirePermissions::canModifyContent(m_document);
                action->setEnabled(m_widget && !m_widget->isAnySceneActive(&m_scene) && canModify);
                action->setStatusTip(m_document && !canModify ? pdf::PDFFirePermissions::getRestrictionReason(m_document) : QString());
                continue;
            }

            if (action == m_actions[CreateMultipleElements])
            {
                continue;
            }

            action->setEnabled(false);
        }

        return;
    }

    const bool isSceneNonempty = !m_scene.isEmpty();
    const bool canUndo = !m_isSaving && !m_isUndoRedoInProgress && m_undoStates.size() > 1;
    const bool canRedo = !m_isSaving && !m_isUndoRedoInProgress && !m_redoStates.empty();

    // Tool actions
    for (auto actionId : { Text, FreehandCurve, AcceptMark, RejectMark,
                           Rectangle, RoundedRectangle, HorizontalLine,
                           VerticalLine, Line, Dot, SvgImage })
    {
        m_actions[actionId]->setEnabled(true);
    }

    m_actions[Undo]->setEnabled(canUndo);
    m_actions[Redo]->setEnabled(canRedo);
    m_actions[EditText]->setEnabled(true);

    // Clear action
    QAction* clearAction = m_actions[Clear];
    clearAction->setEnabled(isSceneNonempty);
}

void EditorPlugin::clearUndoRedo()
{
    m_undoStates.clear();
    m_redoStates.clear();
}

void EditorPlugin::initializeUndoRedo()
{
    clearUndoRedo();
    m_undoStates.emplace_back(m_scene.captureState());
}

void EditorPlugin::createUndoStep()
{
    m_undoStates.emplace_back(m_scene.captureState());
    m_redoStates.clear();

    while (m_undoStates.size() > MAX_UNDO_STEPS)
    {
        m_undoStates.erase(m_undoStates.begin());
    }
}

pdf::PDFPageContentScene::SceneState EditorPlugin::copySceneState(const pdf::PDFPageContentScene::SceneState& state) const
{
    pdf::PDFPageContentScene::SceneState copy;
    copy.selectedElementIds = state.selectedElementIds;
    copy.elements.reserve(state.elements.size());

    for (const auto& element : state.elements)
    {
        copy.elements.emplace_back(element->clone());
    }

    return copy;
}

void EditorPlugin::onUndoTriggered()
{
    if (m_undoStates.size() < 2)
    {
        return;
    }

    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        m_redoStates.emplace_back(std::move(m_undoStates.back()));
        m_undoStates.pop_back();
        while (m_redoStates.size() > MAX_REDO_STEPS)
        {
            m_redoStates.erase(m_redoStates.begin());
        }

        m_scene.restoreState(copySceneState(m_undoStates.back()));
    }

    updateActions();
}

void EditorPlugin::onRedoTriggered()
{
    if (m_redoStates.empty())
    {
        return;
    }

    {
        pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
        pdf::PDFPageContentScene::SceneState state = std::move(m_redoStates.back());
        m_redoStates.pop_back();
        m_scene.restoreState(copySceneState(state));
        m_undoStates.emplace_back(std::move(state));
        while (m_undoStates.size() > MAX_UNDO_STEPS)
        {
            m_undoStates.erase(m_undoStates.begin());
        }
    }

    updateActions();
}

void EditorPlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

void EditorPlugin::updateDockWidget()
{
    if (m_editorWidget)
    {
        return;
    }

    m_editorWidget = new pdf::PDFPageContentEditorWidget(m_dataExchangeInterface->getMainWindow());
    m_editorWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, m_editorWidget, Qt::Vertical);
    m_editorWidget->setFloating(false);
    m_editorWidget->setWindowTitle(tr("Properties"));
    m_editorWidget->setScene(&m_scene);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::operationTriggered, &m_scene, &pdf::PDFPageContentScene::performOperation);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::itemSelectionChangedByUser, this, &EditorPlugin::onWidgetSelectionChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::editElementRequest, this, &EditorPlugin::onSceneEditSingleElement);

    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignTop))->setIcon(QIcon(":/resources/pce-align-top.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterVertically))->setIcon(QIcon(":/resources/pce-align-v-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignBottom))->setIcon(QIcon(":/resources/pce-align-bottom.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignLeft))->setIcon(QIcon(":/resources/pce-align-left.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignCenterHorizontally))->setIcon(QIcon(":/resources/pce-align-h-center.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::AlignRight))->setIcon(QIcon(":/resources/pce-align-right.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameHeight))->setIcon(QIcon(":/resources/pce-same-height.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameWidth))->setIcon(QIcon(":/resources/pce-same-width.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::SetSameSize))->setIcon(QIcon(":/resources/pce-same-size.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorizontally))->setIcon(QIcon(":/resources/pce-center-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterVertically))->setIcon(QIcon(":/resources/pce-center-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::CenterHorAndVert))->setIcon(QIcon(":/resources/pce-center-vh.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutVertically))->setIcon(QIcon(":/resources/pce-layout-v.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutHorizontally))->setIcon(QIcon(":/resources/pce-layout-h.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutForm))->setIcon(QIcon(":/resources/pce-layout-form.svg"));
    m_editorWidget->getToolButtonForOperation(static_cast<int>(pdf::PDFPageContentElementManipulator::Operation::LayoutGrid))->setIcon(QIcon(":/resources/pce-layout-grid.svg"));

    // PDF Fire: the tools are in the ribbon (tab Edit), so they are not repeated here
    // as buttons - this panel keeps the properties of the tools and of the selection.

    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::penChanged, this, &EditorPlugin::onPenChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::brushChanged, this, &EditorPlugin::onBrushChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::fontChanged, this, &EditorPlugin::onFontChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::alignmentChanged, this, &EditorPlugin::onAlignmentChanged);
    connect(m_editorWidget, &pdf::PDFPageContentEditorWidget::textAngleChanged, this, &EditorPlugin::onTextAngleChanged);
}

void EditorPlugin::updateEditedPages()
{
    if (!m_scene.isActive() || m_isSaving)
    {
        // Editor is not active or we are saving the document
        return;
    }

    std::vector<pdf::PDFInteger> currentPages = m_widget->getDrawWidget()->getCurrentPages();
    for (pdf::PDFInteger pageIndex : currentPages)
    {
        if (m_editedPageContent.count(pageIndex))
        {
            continue;
        }

        const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex);
        auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
        pdf::PDFPageContentEditorProcessor processor(page,
                                                     m_document,
                                                     m_widget->getDrawWidgetProxy()->getFontCache(),
                                                     cms.data(),
                                                     m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                                     QTransform(),
                                                     pdf::PDFMeshQualitySettings());

        QList<pdf::PDFRenderError> errors = processor.processContents();
        Q_UNUSED(errors);

        m_editedPageContent[pageIndex] = processor.takeEditedPageContent();

        size_t elementCount = m_editedPageContent[pageIndex].getElementCount();
        for (size_t i = 0; i < elementCount; ++i)
        {
            pdf::PDFEditedPageContentElement* element = m_editedPageContent[pageIndex].getElement(i);
            pdf::PDFPageContentElementEdited* editedElement = new pdf::PDFPageContentElementEdited(element);
            editedElement->setPageIndex(pageIndex);
            m_scene.addElement(editedElement);
        }
    }
}

bool EditorPlugin::updateTextElement(pdf::PDFPageContentElementEdited* element)
{
    QStringList errors;
    if (!refreshTextElement(element, &errors))
    {
        return false;
    }

    return confirmContentStreamErrors(errors, element->getPageIndex());
}

bool EditorPlugin::refreshTextElement(pdf::PDFPageContentElementEdited* element, QStringList* errors)
{
    pdf::PDFEditedPageContentElementText* textElement = element ? element->getElement()->asText() : nullptr;
    if (!textElement || !m_editedPageContent.count(element->getPageIndex()))
    {
        return false;
    }

    // PDF Fire: the element is written into a temporary document and read back - the
    // same function makes the preview of the text edited in place
    auto cms = m_widget->getDrawWidgetProxy()->getCMSManager()->getCurrentCMS();
    return pdf::PDFFireTextElementUpdater::refresh(m_document, element->getPageIndex(), m_editedPageContent.at(element->getPageIndex()), textElement, cms.data(), errors);
}

bool EditorPlugin::confirmContentStreamErrors(QStringList errors, pdf::PDFInteger pageIndex)
{
    if (errors.empty())
    {
        return true;
    }

    const int errorCount = errors.size();
    if (errors.size() > 3)
    {
        errors.resize(3);
    }

    QString message = tr("Errors (%2) occured while creating content stream on page %3.<br>%1").arg(errors.join("<br>")).arg(errorCount).arg(pageIndex + 1);
    return QMessageBox::question(m_dataExchangeInterface->getMainWindow(), tr("Error"), message, QMessageBox::Abort, QMessageBox::Ignore) != QMessageBox::Abort;
}

void EditorPlugin::onDrawSpaceChanged()
{
    const size_t elementCount = m_scene.getElementIds().size();
    pdf::PDFTemporaryValueChange guard(&m_isUndoRedoInProgress, true);
    updateEditedPages();

    if (m_scene.getElementIds().size() != elementCount)
    {
        initializeUndoRedo();
        updateActions();
    }
}

}
